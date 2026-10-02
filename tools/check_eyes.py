"""The labyrinth's eye puzzle with the expanded view, from a saved state.

The state (default logs/states/eye-puzzle.state, the user's state1 on
2026-10-01 at 1920x1080) stands by the jar of puzzle 2 at 6504,5288 in map
5E:8F (records in bank 92: the jar 6C54, its socket 6C76 at 6595,5274, the
statue 6C65), with puzzles 1 and 5 awake in the same view and the room's
socket noted fourth in a list of two.

- Phases: the eye waits for its frame phase (up to eight frames) before it
  has its frames and its own collision box. Whipping the jar on each of eight
  successive frames, the eye (script 14:5E16) must settle on the table where
  the jar was every time, and seven of them must have needed the box
  (`eye_boxes` in shantae_view_info). Before the fix it went 8 pixels left for
  every frame it waited, up to 56, into the wall, and fell out of the room.
- The puzzle: a whip breaks the jar and the eye settles on the table; a
  standing whip from the wall on the left
  knocks it across the room, where it comes to rest by the statue; a crouching
  whip beside it sends it up through the socket, which takes it (state bytes
  1 and 5 after the socket's flag); the statue opens and drops the key, and
  walking into it takes it (CA87, the third labyrinth's keys; "You found a
  key!"). --native plays it in
  the original view with the socket noted by hand where the original would
  have it; the eye's path is the same in both.
- Reach: with the eye out, the debug flight (the `shantae_flight` debug
  command) 300 pixels left and back finds it still on the table.
- Two puzzles: flying down until puzzle 1's first jar (67E9, at 5704,4744) is
  released and back up until it spawns again, it must be full although
  puzzle 2's eye 1 is out; flying left until that eye is released, it must go
  back into its own jar (colour 5 in D1BA) and leave puzzle 1's (D1B5, D1B6)
  alone.
- A lost eye: logs/states/eye-lost.state was saved by the build before the
  fix after the eye had fallen out of the room: the jar empty, the eye back
  in it by its state, no eye about. Loaded, the jar must be full again
  (`jars` in shantae_view_info) and the puzzle play through as above. Skipped
  when the file is missing.

Requires the local saved states; never writes user saves or settings.
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
JAR, SOCKET, STATUE, OTHER_JAR = 0x6C54, 0x6C76, 0x6C65, 0x67E9   # records in bank 92
TABLE_X, WALL_X = 6504, 6480
KEYS = 0xCA87


def word(data, at):
    return int.from_bytes(data[at:at + 2], "little")


class Game:
    def __init__(self, tmp, exe, expanded, width, height, port):
        Path(tmp, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(tmp, "shantae.ini").write_text(
            f"expanded_view={int(expanded)}\nview_width={width}\nview_height={height}\nroom_zoom=1\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=str(tmp), GBRECOMP_NO_LAUNCHER="1", GBRECOMP_DEBUG_PORT=str(port))
        self.log = open(Path(tmp, "run.log"), "w")
        self.proc = subprocess.Popen([str(exe), "--benchmark"], cwd=tmp, env=env, stdout=self.log,
                                     stderr=subprocess.STDOUT,
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
        self.expanded = expanded
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
        step(self.c, 10)
        if not self.expanded:
            # The other puzzles' sockets are released by now; the room's goes
            # where the original would have noted it.
            socket = SLOTS[self.record(SOCKET)[0]]
            self.c.command("poke", addr="0xc03a", hex=socket.to_bytes(2, "little").hex() + "000000000000")
            self.c.command("poke", addr=hex(socket + 0x63), hex="3ac0", wram_bank=3)

    def press(self, buttons, frames):
        self.c.command("set_input", buttons=buttons)
        step(self.c, frames)

    def table(self):
        raw = bytes.fromhex(self.c.peek(0xD000, 4096, wram_bank=3) + self.c.peek(0xE000, 93 * 126 - 4096, wram_bank=3) +
                            self.c.peek(0xA000, 64 * 126, wram_bank=3))
        return [raw[i * 126:(i + 1) * 126] for i in range(157)]

    def record(self, record):
        """(slot, its bytes) of the live object from a record of bank 92."""
        for i, s in enumerate(self.table()):
            if s[0] != 0xFF and s[0x24] == 0x92 and word(s, 0x25) == record:
                return i, s
        return None

    def eyes(self):
        return [(word(s, 0x34), word(s, 0x37)) for s in self.table()
                if s[0] != 0xFF and s[4] == 0x14 and 0x5E16 <= word(s, 2) < 0x5E48]

    def player(self):
        s = self.table()[0]
        return word(s, 0x34), word(s, 0x37)

    def state(self, flag, count=3):
        """The bytes after a record's flag in WRAM bank 4."""
        return bytes.fromhex(self.c.peek(flag, 1 + count, wram_bank=4))

    def byte(self, addr):
        return int(self.c.peek(addr, 1), 16)

    def info(self):
        info = self.c.command("shantae_view_info")
        for _ in range(info["count"]):
            self.c.read()
        return info


def jar_full(jar):
    """In its script's first loop (14:5C05-5C10), waiting for a whip."""
    return jar is not None and word(jar[1], 2) < 0x5C11 and jar[1][0x18] == 0


def break_jar(g, wait=2):
    """Whip the jar `wait` frames on (after 2 the eye waits six frames for its phase)."""
    assert jar_full(g.record(JAR)), "the jar is not full"
    g.press("-", wait)
    g.press("B", 2)
    path = []
    for _ in range(50):
        g.press("-", 1)
        path += g.eyes()
    assert path, "no eye came out of the jar"
    assert all(x == TABLE_X for x, _ in path), ("the eye left the table", sorted({x for x, _ in path}))
    assert g.eyes() == [(TABLE_X, 5293)], ("the eye is not resting on the table", g.eyes())
    return path


def phases(g, state):
    boxes = []
    for wait in range(1, 9):
        g.load(state)
        break_jar(g, wait)
        boxes.append(g.info()["eye_boxes"])
    assert sum(boxes) >= 7, ("the eye never needed its box", boxes)
    print(f"PASS: the eye settled on the table from all eight frame phases (boxes given: {boxes})")


def puzzle(g):
    keys = g.byte(KEYS)
    path = break_jar(g)
    # To the wall on the left, turn, and whip it across the room.
    g.press("L", 160); g.press("-", 10); g.press("R", 2); g.press("-", 6)
    g.press("B", 2)
    for _ in range(50):
        g.press("-", 8)
        path += g.eyes()
    (x, y), = g.eyes()
    assert path[-1] == path[-2] == path[-3] and x > TABLE_X + 40, ("the eye did not come to rest across the room", x, y)
    assert min(px for px, _ in path) >= WALL_X, ("the eye went into the wall", min(px for px, _ in path))
    # Beside it, crouch and whip: up through the socket.
    while g.player()[0] < x - 44:
        g.press("R", 1)
    g.press("-", 10); g.press("D", 8); g.press("DB", 2); g.press("D", 24)
    for _ in range(120):
        if not g.eyes():
            break
        g.press("-", 1)
    assert not g.eyes(), ("the socket did not take the eye", g.eyes())
    socket, statue = g.state(0xD1C0), g.record(STATUE)[1]
    assert socket[1:3] == b"\x01\x05" and g.state(0xD1B8)[1] == 1, (socket.hex(), g.state(0xD1B8).hex())
    g.press("-", 120)
    assert g.record(STATUE)[1][0x18] == 1, "the statue did not open"
    # Into the key, and through its message.
    g.press("R", 72)
    for _ in range(12):
        if g.byte(KEYS) != keys:
            break
        g.press("B", 4); g.press("-", 40)
    assert g.byte(KEYS) == keys + 1, ("no key", keys, g.byte(KEYS))
    print(f"PASS: the eye rested at {x},{y}, the socket took it, and the key was taken (CA87 {keys} -> {keys + 1})")
    return path


def reach(g):
    break_jar(g)
    g.c.command("shantae_flight")
    g.press("-", 2)
    x0 = g.player()[0]
    while g.player()[0] > x0 - 300:
        g.press("L", 4)
    g.press("-", 30)
    while g.player()[0] < x0:
        g.press("R", 4)
    g.press("-", 30)
    assert g.eyes() == [(TABLE_X, 5293)], ("the eye was released 300 pixels away", g.eyes())
    assert not jar_full(g.record(JAR))
    print("PASS: 300 pixels away and back, the eye is still on the table")


def two_puzzles(g):
    break_jar(g)
    other = g.state(0xD1B4)
    assert jar_full(g.record(OTHER_JAR)), "puzzle 1's jar is not awake and full"
    g.c.command("shantae_flight")
    g.press("-", 2)
    y0, frames = g.player()[1], 0
    while g.record(OTHER_JAR):
        assert frames < 400, "puzzle 1's jar was never released"
        g.press("D", 4)
        frames += 4
    g.press("D", 16)
    while g.player()[1] > y0:
        g.press("U", 4)
    g.press("-", 20)
    assert g.eyes() == [(TABLE_X, 5293)], g.eyes()
    assert jar_full(g.record(OTHER_JAR)), "puzzle 1's jar spawned empty while puzzle 2's eye was out"
    frames = 0
    while g.eyes():
        assert frames < 1200, "the eye was never released"
        g.press("L", 8)
        frames += 8
    g.press("-", 4)
    assert g.state(0xD1B8)[1:3] == b"\x00\x05", ("the eye did not go back into its jar", g.state(0xD1B8).hex())
    assert g.state(0xD1B4)[1:] == other[1:], ("puzzle 1's jar was written", other.hex(), g.state(0xD1B4).hex())
    print("PASS: puzzle 1's jar spawned full with puzzle 2's eye out, and the eye went back into its own jar")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("state", type=Path, nargs="?", default=ROOT / "logs/states/eye-puzzle.state")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--lost", type=Path, default=ROOT / "logs/states/eye-lost.state",
                    help="a state saved after the eye was lost")
    ap.add_argument("--native", action="store_true", help="the original view (the puzzle only), the socket noted by hand")
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--port", type=int, default=14390)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="shantae-eyes-", ignore_cleanup_errors=True) as tmp:
        g = Game(tmp, args.exe.resolve(), not args.native, args.width, args.height, args.port)
        try:
            if not args.native:
                phases(g, args.state)
            for part in (puzzle,) if args.native else (puzzle, reach, two_puzzles):
                g.load(args.state)
                part(g)
                assert args.native or g.info()["eyes"], "no list was read for its own puzzle"
            if not args.native and args.lost.exists():
                jars = g.info()["jars"]
                g.load(args.lost)
                assert g.info()["jars"] == jars + 1, ("the jar was not started again", jars, g.info()["jars"])
                assert jar_full(g.record(JAR)) and not g.eyes(), "the jar is not full again"
                print("PASS: the state saved with the eye lost loads with its jar full again")
                puzzle(g)
            elif not args.native:
                print(f"SKIP: no {args.lost}")
            fallback = g.c.command("interp_fallbacks")
            assert fallback["total_fallbacks"] == 0, fallback
        finally:
            g.close()


if __name__ == "__main__":
    main()
