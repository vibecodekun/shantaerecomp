"""Sky's crow at the desert labyrinth with the expanded view, from a saved state.

The door of the labyrinth in map 74:AB (object 0A:4238, record AD:70F7, box
880-911 x 880-919) makes the crow (script 15:5A37, at 890,890, 12x16, no
record of its own) as it spawns while CA9C is 1, and waits for CA9C to reach
4, which only the crow's dialogue sets. The state (default
logs/states/crow-door.state, the user's state1 on 2026-09-27 at 1920x1080)
stands beside the door with the crow already lost; loaded with the view on,
the door makes it again (`crows` in shantae_view_info).

- The door: walk into the crow, press B, go through the dialogue (hold B, then
  B), and CA9C must reach 4; walk onto the open door and hold Up, and the
  labyrinth (map 78:BE) must load.
- Approaches: with the debug flight (the `shantae_flight` debug command, no
  debug mode), fly right until the door has been released for 40 frames, then
  back at a walking pace (4 pixels every third frame) and end the flight by
  the door; the crow must be there. Each phase leaves a frame later. At
  1920x1080 before the fix, 3 of 6 lost it. --native flies them in the
  original view, where the crow stays.

Requires the local saved state; never writes user saves or settings.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from view_probe import Debug, ROOT
from view_state_repro import step

DOOR = (0xAD, 0x70F7)
CROW_AT = (890, 890)


def word(data, at):
    return int.from_bytes(data[at:at + 2], "little")


class Game:
    def __init__(self, tmp, expanded, width, height, port):
        Path(tmp, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(tmp, "shantae.ini").write_text(
            f"expanded_view={int(expanded)}\nview_width={width}\nview_height={height}\nroom_zoom=1\n")
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

    def load(self, state):
        self.c.command("set_input", buttons="-")
        self.c.command("load_state", path=Path(state).resolve().as_posix())
        step(self.c, 2)

    def press(self, buttons, frames):
        self.c.command("set_input", buttons=buttons)
        step(self.c, frames)

    def byte(self, addr):
        return int(self.c.peek(addr, 1), 16)

    def scene(self):
        """(door alive, crow alive, player x) from the object table."""
        r = self.c.command("shantae_slots")
        player = word(bytes.fromhex(self.c.peek(0xCA13, 2)), 0)
        index = player - 0xD000
        door = any((s[2], s[3]) == DOOR for s in r["live"])
        crow = any(s[2] == 0 and (s[4], s[5]) == CROW_AT for s in r["live"])
        x = next((s[4] for s in r["live"] if index >= 0 and index % 126 == 0 and s[0] == index // 126), None)
        return door, crow, x

    def map(self):
        return self.c.peek(0xC9F9, 2)


def enter(g, state, expanded):
    g.load(state)
    if expanded:
        info = g.c.command("shantae_view_info")
        for _ in range(info["count"]):
            g.c.read()
        assert info["crows"] == 1, ("the door did not make the crow again", info["crows"])
    g.press("-", 4)
    assert g.scene()[1], "no crow on the door"
    assert g.byte(0xCA9C) == 1, g.byte(0xCA9C)
    # Into the crow (Shantae's box is x+20 to x+30), then B.
    g.press("L", 48); g.press("-", 10); g.press("B", 2)
    for _ in range(40):
        if g.byte(0xCA9C) == 4:
            break
        g.press("B", 40); g.press("-", 4); g.press("B", 4); g.press("-", 20)
    assert g.byte(0xCA9C) == 4, ("the dialogue did not open the door", g.byte(0xCA9C))
    g.press("-", 240)
    # Onto the doorway; the door's helper (06:7143) wants Up held 10 frames.
    g.press("R", 24); g.press("-", 10)
    g.press("U", 40); g.press("-", 240)
    assert g.map() == "78be", ("the labyrinth did not load", g.map())
    print("PASS: the crow's dialogue opened the door, and Up entered the labyrinth (map 78:BE)")


def approach(g, state, phase, pace):
    g.load(state)
    g.c.command("shantae_flight")
    g.press("-", 2)
    gone = frames = 0
    while gone < 40:
        assert frames < 2000, "the door was never released"
        g.press("R", 4)
        frames += 4
        gone = gone + 4 if not g.scene()[0] else 0
    g.press("R", phase)
    spawns, last = 0, False
    for f in range(0, 3000, pace):
        g.press("L", 1)
        g.press("-", pace - 1)
        door, crow, x = g.scene()
        spawns += door and not last
        last = door
        if x is not None and x <= 920:
            break
    g.press("T", 4); g.press("-", 60)
    door, crow, _ = g.scene()
    return door, crow, spawns


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("state", type=Path, nargs="?", default=ROOT / "logs/states/crow-door.state")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--native", action="store_true", help="the original view (approaches only)")
    ap.add_argument("--phases", type=int, default=6)
    ap.add_argument("--pace", type=int, default=3, help="frames per 4-pixel step on the way back")
    ap.add_argument("--port", type=int, default=14388)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="shantae-crow-", ignore_cleanup_errors=True) as tmp:
        g = Game(tmp, not args.native, args.width, args.height, args.port)
        try:
            if not args.native:
                enter(g, args.state, True)
            kept = 0
            for phase in range(args.phases):
                door, crow, spawns = approach(g, args.state, phase, args.pace)
                kept += crow
                print(f"approach {phase}: door {door}, crow {crow}, door spawned {spawns} time(s)")
            fallback = g.c.command("interp_fallbacks")
            assert fallback["total_fallbacks"] == 0, fallback
            assert kept == args.phases, f"the crow was lost in {args.phases - kept} of {args.phases} approaches"
            print(f"PASS: the crow was on the door after all {args.phases} approaches")
        finally:
            g.close()


if __name__ == "__main__":
    main()
