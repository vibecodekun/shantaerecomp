"""Spawner budgets with the expanded view, against the original.

Some spawners share a count of what they have made and make no more at its
limit (expanded_background.inc, "Budgets"). Each route is flown with the
game's debug flight (Select+A, 4 pixels a frame) from a scene of WayForward's
debug grid, in the original and in the expanded view (1920x1080, room zoom
Fill, a 1920x1080 window):
- scene 0D, map 40:9F: the twelve swamp creatures (0A:4204) along the bottom,
  flying right. Each surfaces once Shantae is about 112 pixels past it; all
  twelve must surface in both, within 40 pixels of the original's lead.
- scene 09, map 67:85: hovering by the three spawners (0A:41F8) at
  419-516,1390-1446. The original's creatures all come from them; the view's
  must mostly too (at least three within 120 pixels of Shantae), with never
  more than six counted (C000).
- scene 36 (the same map's room above, 0A:4220) and scene 02 (map 40:58,
  0A:41AC and 0A:41B0 sharing C080), flying along their rows and stopping by
  each spawner: the view must make at least two thirds of the original's
  number, with never more than four counted. These spawners wait a random
  60-150 frames between tries, and the game draws its random numbers with
  the time each frame leaves over, which the view's larger table changes, so
  the counts differ from run to run (the view made 6 of the original's 14 on
  map 40:58 before the fix, 10-13 after).
The grid is made from a cold boot with the title code, so no saved state is
needed. Never writes user saves or settings.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from view_probe import Debug, ROOT
from view_state_repro import step

SLOTS = [0xD000 + i * 126 if i < 93 else 0xA000 + (i - 93) * 126 for i in range(157)]
ROM = (ROOT / "roms/shantae.gbc").read_bytes()


def word(data, at):
    return int.from_bytes(data[at:at + 2], "little")


def rom16(bank, addr):
    off = addr if addr < 0x4000 else bank * 0x4000 + addr - 0x4000
    return word(ROM, off)


class Game:
    def __init__(self, tmp, expanded, port):
        Path(tmp, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(tmp, "shantae.ini").write_text(
            f"expanded_view={int(expanded)}\nview_width=1920\nview_height=1080\nroom_zoom=1\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=str(tmp), GBRECOMP_NO_LAUNCHER="1", GBRECOMP_DEBUG_PORT=str(port))
        self.log = open(Path(tmp, "run.log"), "w")
        self.proc = subprocess.Popen([str(ROOT / "generated/build/shantae.exe"), "--benchmark"], cwd=tmp, env=env,
                                     stdout=self.log, stderr=subprocess.STDOUT,
                                     creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 20
        while True:
            try:
                self.c = Debug(port)
                break
            except OSError:
                if self.proc.poll() is not None or time.monotonic() > deadline:
                    raise
                time.sleep(0.02)
        self.c.command("pause")
        if expanded:
            self.c.command("window", width=1920, height=1080, scaling_mode=0)

    def close(self):
        try:
            self.c.command("quit")
            self.proc.wait(timeout=10)
        finally:
            if self.proc.poll() is None:
                self.proc.terminate()
                self.proc.wait(timeout=10)
            self.log.close()

    def press(self, buttons, frames):
        self.c.command("set_input", buttons=buttons)
        step(self.c, frames)

    def byte(self, addr):
        return int(self.c.peek(addr, 1), 16)

    def table(self):
        raw = bytes.fromhex(self.c.peek(0xD000, 4096, wram_bank=3) + self.c.peek(0xE000, 93 * 126 - 4096, wram_bank=3)
                            + self.c.peek(0xA000, 64 * 126, wram_bank=3))
        return {a: raw[i * 126:(i + 1) * 126] for i, a in enumerate(SLOTS)}

    def player(self):
        s = self.table()[word(bytes.fromhex(self.c.peek(0xCA13, 2)), 0)]
        return word(s, 0x34), word(s, 0x37), s

    def debug_grid(self, path):
        """From a cold boot: the title code at "Press Start", Start Debug Game, File Select."""
        self.press("-", 300)
        for buttons, count in (("L", 2), ("R", 8), ("L", 6), ("R", 2), ("L", 7), ("R", 6), ("L", 8)):
            for _ in range(count):
                self.press(buttons, 4)
                self.press("-", 4)
        self.press("-", 30)
        self.press("S", 4); self.press("-", 90)      # the developer menu
        self.press("D", 4); self.press("-", 10)
        self.press("A", 4); self.press("-", 240)     # Start Debug Game: File Select
        self.press("A", 4); self.press("-", 120)     # New 1: the scene grid
        assert self.byte(0xCC04) == 1, "debug mode is off"
        self.c.command("save_state", path=Path(path).resolve().as_posix())

    def scene(self, grid, scene):
        self.c.command("set_input", buttons="-")
        self.c.command("load_state", path=Path(grid).resolve().as_posix())
        step(self.c, 2)
        self.c.command("poke", addr="0xcbfc", hex=f"01{scene:02x}")
        self.press("-", 360)
        for _ in range(8):   # Select+A: the debug flight (dialogue: hold B, then press B)
            self.press("TA", 4)
            self.press("-", 12)
            s = self.player()[2]
            if s[0x19] == 6 and word(s, 0x1B) == 0x71D4:
                return
            self.press("B", 60)
            self.press("-", 4)
            self.press("B", 4)
            self.press("-", 30)
        raise AssertionError(f"scene {scene:02X}: no debug flight")


def chasers(g, grid):
    """Shantae's lead over each swamp creature's record when it surfaced."""
    g.scene(grid, 0x0D)
    surfaced = {}
    g.c.command("set_input", buttons="R")
    for _ in range(240):
        step(g.c, 4)
        px = g.player()[0]
        for s in g.table().values():
            if s[0] != 0xFF and s[4] == 0x21 and s[0x24] and 0x5C24 <= word(s, 2) < 0x5C44:
                surfaced.setdefault(rom16(s[0x24], word(s, 0x25) + 2), px - rom16(s[0x24], word(s, 0x25) + 2))
    return surfaced


def rises(g, grid, scene, counter, route, near=None):
    """Rises of the count along the route, its most, and rises with a new object within `near` of Shantae."""
    g.scene(grid, scene)
    live = {a for a, s in g.table().items() if s[0] != 0xFF}
    prev = g.byte(counter)
    count = most = close = 0
    for part in route.split(","):
        buttons, frames = part.split(":")
        g.c.command("set_input", buttons=buttons)
        for _ in range(int(frames)):
            step(g.c, 1)
            value = g.byte(counter)
            most = max(most, value)
            if value > prev:
                count += 1
                if near:
                    px, py, _ = g.player()
                    new = [s for a, s in g.table().items() if s[0] != 0xFF and a not in live]
                    close += any(abs(word(s, 0x34) - px) < near and abs(word(s, 0x37) - py) < near for s in new)
            if value != prev or near:
                live = {a for a, s in g.table().items() if s[0] != 0xFF}
            prev = value
    return count, most, close


ROUTE_36 = "R:123,-:200,R:48,-:200,R:28,-:200,R:195,-:200,R:158,-:200,R:28,-:200,R:21,-:200,R:93,-:200,R:35,-:200,R:17,-:200,R:84,-:200"
ROUTE_02 = "R:340,-:200,R:70,-:200,R:70,-:200,R:310,-:200"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=14387)
    args = ap.parse_args()
    results = {}
    with tempfile.TemporaryDirectory(prefix="shantae-budgets-", ignore_cleanup_errors=True) as tmp:
        grid = Path(tmp, "debug-grid.state")
        for expanded in (False, True):
            run = Path(tmp, "view" if expanded else "original")
            run.mkdir()
            g = Game(run, expanded, args.port + expanded)
            try:
                if not grid.exists():
                    g.debug_grid(grid)
                results[expanded] = dict(
                    chasers=chasers(g, grid),
                    cluster=rises(g, grid, 0x09, 0xC000, "D:115,R:50,-:900", near=120),
                    room_a=rises(g, grid, 0x36, 0xC000, ROUTE_36),
                    c080=rises(g, grid, 0x02, 0xC080, ROUTE_02))
                fallback = g.c.command("interp_fallbacks")
                assert fallback["total_fallbacks"] == 0, fallback
            finally:
                g.close()
    original, view = results[False], results[True]
    print("swamp creatures (spot: Shantae's lead, original / view):")
    for x in sorted(original["chasers"]):
        print(f"  {x:5d}: {original['chasers'][x]:4d} / {view['chasers'].get(x, '-')}")
    assert len(original["chasers"]) == 12 and set(view["chasers"]) == set(original["chasers"]), "a creature never surfaced"
    for x, lead in original["chasers"].items():
        assert abs(view["chasers"][x] - lead) <= 40, (x, lead, view["chasers"][x])
    for key, label in (("cluster", "0A:41F8 cluster"), ("room_a", "0A:4220"), ("c080", "0A:41AC/41B0")):
        print(f"{label}: made {original[key][0]} / {view[key][0]}, most counted {original[key][1]} / {view[key][1]}"
              + (f", by Shantae {original[key][2]} / {view[key][2]}" if key == "cluster" else ""))
    assert view["cluster"][2] >= 3 and view["cluster"][1] <= 6, view["cluster"]
    for key in ("room_a", "c080"):
        assert 3 * view[key][0] >= 2 * original[key][0] and view[key][1] <= 4, (key, original[key], view[key])
    print("PASS: spawner budgets")


if __name__ == "__main__":
    main()
