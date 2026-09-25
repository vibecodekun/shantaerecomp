"""Replay the platform leak and sweep L1-L5 using the game's debug flight.

Requires local saved-state fixtures; never writes user saves or settings.
The grid fixture is WayForward's menu after entering the title-screen code.
The sweep exercises object activation/pools, not puzzle or boss completion.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from view_probe import Debug, ROOT
from view_state_repro import step


def word(data, at):
    return int.from_bytes(data[at:at + 2], "little")


SLOTS = [0xD000 + i * 126 if i < 93 else 0xA000 + (i - 93) * 126 for i in range(157)]
NODES = [0xDD40 + i * 19 for i in range(12)] + [0xE000 + i * 19 for i in range(157)]


def snapshot(c):
    info = c.command("shantae_view_info")
    for _ in range(info["count"]):
        c.read()
    low = bytes.fromhex(c.peek(0xC000, 4096))
    hram = bytes.fromhex(c.peek(0xFF80, 127))
    raw = bytes.fromhex(c.peek(0xD000, 4096, wram_bank=3) +
                        c.peek(0xE000, 93 * 126 - 4096, wram_bank=3) +
                        c.peek(0xA000, 64 * 126, wram_bank=3))
    slots = {addr: raw[i * 126:(i + 1) * 126] for i, addr in enumerate(SLOTS)}
    raw_nodes = bytes.fromhex(c.peek(0xDD40, 12 * 19, wram_bank=7) +
                              c.peek(0xE000, 157 * 19, wram_bank=7))
    nodes = {addr: raw_nodes[i * 19:(i + 1) * 19] for i, addr in enumerate(NODES)}

    def chain(pool, head, link, previous=None):
        seen, prev = set(), 0
        while head:
            assert head in pool and head not in seen, ("invalid list", hex(head))
            data = pool[head]
            if previous is not None:
                assert word(data, previous) == prev, ("node backlink", hex(head))
            seen.add(head)
            prev, head = head, word(data, link)
        return seen

    free = chain(slots, word(hram, 0x33), 0x7C)
    assert free == {a for a, s in slots.items() if s[0] == 255}, "slot/free-list disagreement"
    used = chain(nodes, word(hram, 0x6C), 0x10, 0x0E)
    spare = chain(nodes, word(hram, 0x6A), 0x10)
    assert not (used & spare) and len(used | spare) == len(nodes), "lost or double-listed collision node"
    assert len(used) == low[0xA03], "collision count disagreement"
    parts = []
    for addr, s in slots.items():
        pc = word(s, 2)
        if s[0] != 255 and s[4] == 9 and not s[0x24] and not word(s, 0x27) and (
                0x46EA <= pc <= 0x4710 or 0x47D5 <= pc <= 0x480C):
            parts.append(addr)
    assert len(parts) <= 8, ("cloned platform group", parts)
    return dict(view=info, free=len(free), nodes=len(used), parts=parts,
                player=slots[word(low, 0xA13)].hex(), player_addr=word(low, 0xA13),
                map=low[0x9F8:0x9FB].hex(), debug=low[0xC04], scene=low[0xBFD])


def checked(c):
    # A VBlank can interrupt an allocator between linked-list writes. A stable
    # violation persists after the interrupted game routine finishes.
    for attempt in range(4):
        try:
            return snapshot(c)
        except AssertionError:
            if attempt == 3:
                raise
            step(c, 4)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("state", type=Path)
    ap.add_argument("debug_grid", type=Path)
    ap.add_argument("--out", type=Path, default=ROOT / "logs/platform-regression")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--cycles", type=int, default=100)
    ap.add_argument("--port", type=int, default=14423)
    args = ap.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    results = []
    with tempfile.TemporaryDirectory(prefix="shantae-platform-check-") as tmp, (args.out / "run.log").open("w") as log:
        Path(tmp, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(tmp, "shantae.ini").write_text(
            f"expanded_view=1\nview_width={args.width}\nview_height={args.height}\nroom_zoom=1\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=tmp, GBRECOMP_NO_LAUNCHER="1",
                   GBRECOMP_DEBUG_PORT=str(args.port))
        proc = subprocess.Popen([str(ROOT / "generated/build/shantae.exe"), "--benchmark"],
                                cwd=tmp, env=env, stdout=log, stderr=log,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + 20
            while True:
                try:
                    c = Debug(args.port)
                    break
                except OSError:
                    if proc.poll() is not None or time.monotonic() > deadline:
                        raise
                    time.sleep(.02)
            c.command("pause")
            c.command("window", width=1920, height=1080, scaling_mode=0)

            def load(path):
                c.command("set_input", buttons="-")
                c.command("load_state", path=path.resolve().as_posix())
                step(c, 4)

            def press(buttons, count):
                c.command("set_input", buttons=buttons)
                step(c, count)

            def capture(label):
                e = dict(label=label, **checked(c))
                results.append(e)
                (args.out / "results.json").write_text(json.dumps(results, indent=2))
                return e

            load(args.state)
            capture("repro-start")
            for cycle in range(args.cycles):
                for direction in ("L", "R"):
                    press(direction, 65)
                    capture(f"repro-{cycle}-{direction}")
            c.command("screenshot", path=str(args.out / "repro-end.png"))
            print(f"PASS: {args.cycles} left/right cycles", flush=True)

            for label, scene in (("L1", 1), ("L2", 7), ("L3", 10), ("L4", 58), ("L5", 60)):
                load(args.debug_grid)
                # Supply the same selection request as menu handler 0A:4DFC;
                # the game's debug scene dispatcher (0A:4919) loads the map.
                c.command("poke", addr="0xcbfc", hex=f"01{scene:02x}")
                press("-", 360)
                assert checked(c)["scene"] == scene
                # L1 opens with dialogue; allow every page to finish and use B
                # to advance it. Flight is enabled only by the game's Select+A.
                for attempt in range(8):
                    press("TA", 4)
                    press("-", 12)
                    e = checked(c)
                    player = bytes.fromhex(e["player"])
                    if player[0x19] == 6 and word(player, 0x1B) == 0x71D4:
                        break
                    for _ in range(60):
                        press("B", 2)
                        press("-", 6)
                    press("-", 60)
                else:
                    raise AssertionError(f"{label}: could not enter the game's debug flight")
                assert e["debug"] == 1
                e = capture(f"{label}-flight")
                map_directory = e["map"]
                bounds = e["view"]["room"]
                assert bounds[2] - bounds[0] > 160, (label, "still in a cutscene", bounds)
                # Overlapping activation areas across this map, then the same
                # points in reverse. Flight stays on: never drop out of bounds.
                xs = list(range(bounds[0] + 80, bounds[2] - 80, 640))
                ys = list(range(bounds[1] + 72, bounds[3] - 72, 480))
                points = [(x, y) for row, y in enumerate(ys) for x in (xs if row % 2 == 0 else xs[::-1])]
                for i, (x, y) in enumerate(points + points[::-1]):
                    p = e["player_addr"]
                    c.command("poke", addr=hex(p + 0x34), hex=x.to_bytes(2, "little").hex(), wram_bank=3)
                    c.command("poke", addr=hex(p + 0x37), hex=y.to_bytes(2, "little").hex(), wram_bank=3)
                    step(c, 60)
                    e = capture(f"{label}-{i}-{x}-{y}")
                    assert e["map"] == map_directory, (label, "unexpected map transition")
                c.command("screenshot", path=str(args.out / f"{label}-end.png"))
                print(f"PASS: {label}, {len(points) * 2} debug-flight positions", flush=True)
            fallback = c.command("interp_fallbacks")
            (args.out / "fallbacks.json").write_text(json.dumps(fallback, indent=2))
            assert fallback["total_fallbacks"] == 0 and fallback["total_instructions"] == 0, fallback
            c.command("quit")
            proc.wait(timeout=10)
            assert proc.returncode == 0, proc.returncode
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=10)


if __name__ == "__main__":
    main()
