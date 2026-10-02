"""The fourth labyrinth's picture puzzles and key doors with the expanded view.

The state (default logs/states/flip-puzzle.state, the user's state1 on
2026-10-02 at 1920x1080) stands as a tinkerbat under picture 3 of map 5B:CA
(its quarters at 7784-7847,1048-1111, records in bank CD from 7930, turned
2,3,1,0 in CAA8-CAAB) with picture 1 (6304-6367,848-911), solved, awake 1480
pixels left in the same view. A quarter (object 0A:4320) loads its piece of the
picture to 8A00 + $100 x its place in VRAM bank 1; before the fix every
picture awake loaded there, so both showed puzzle 3's left half beside puzzle
1's right half.

Each part plays the expanded view beside the original (the same state and
buttons with the view off) and compares what the two show of a thing, pixel
for pixel, in world coordinates:

- Pictures: loaded, VRAM holds picture 3's four pieces (the ROM's, by the
  turns), the view shows picture 3 as the original does, and picture 1 as the
  original shows it once flown there (the debug flight).
- The whip: a jump and a swipe turn picture 3's upper quarters (CAA8, CAA9 go
  2,3 -> 3,0). Picture 1 must not change on any frame of it, and both end as
  the original has them.
- The puzzle: each quarter turned alone (a jump or a standing swipe from 7760
  or 7832, facing left) until place n shows piece n, the two views compared
  after every swipe; the chest opens, the key comes out, and walking into it
  takes it (CA88 + 1, CAB6 = 3). --native plays this part in the original
  view alone.
- Doors: with door 5 (1208,912; its state the byte after its flag, D1E1 in
  bank 4) opened by hand and door 1 (1288,416) shut, flown to 1333,700 where
  the view has both: each must look as the original shows it once flown there
  (before the fix the open door was drawn with the shut one's tiles), and by
  each door VRAM (8E00, bank 1) holds that door's own tiles.

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

SLOTS = [0xD000 + i * 126 if i < 93 else 0xA000 + (i - 93) * 126 for i in range(157)]
TURNS, SOLVED, KEYS = 0xCAA0, 0xCAB4, 0xCA88
PICTURES = {1: (6304, 848), 3: (7784, 1048)}          # the two the state's view has
PIECES = 0x53A8                                        # bank 26: 16 scripts a puzzle, place + 4 x turn
DOORS = {1: (1288, 416), 5: (1208, 912)}               # 32 x 40
DOOR_TILES = {0: (0x77, 0x4F10), 1: (0x77, 0x5450)}    # shut, open
DOOR_OPEN = 0xD1E1                                     # door 5's state, WRAM bank 4
# The swipe that turns a quarter alone, facing left: where, and frames of jump.
ALONE = {0: (7760, 12), 2: (7760, 0), 1: (7832, 12), 3: (7832, 0)}
CLEAR = 7744   # standing here, Shantae is not in front of the picture


def word(data, at):
    return int.from_bytes(data[at:at + 2], "little")


class Game:
    def __init__(self, tmp, exe, expanded, width, height, port):
        tmp = Path(tmp, "expanded" if expanded else "original")
        tmp.mkdir()
        self.tmp = tmp
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
        self.name = "the view" if expanded else "the original"
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
        step(self.c, 12)

    def press(self, buttons, frames):
        self.c.command("set_input", buttons=buttons)
        step(self.c, frames)

    def mem(self, addr, count, **banks):
        return bytes.fromhex(self.c.peek(addr, count, **banks))

    def table(self):
        raw = self.mem(0xD000, 4096, wram_bank=3) + self.mem(0xE000, 93 * 126 - 4096, wram_bank=3) + \
            self.mem(0xA000, 64 * 126, wram_bank=3)
        return [raw[i * 126:(i + 1) * 126] for i in range(157)]

    def player(self):
        s = self.mem(SLOTS[0], 126, wram_bank=3)
        return word(s, 0x34), word(s, 0x37)

    def info(self):
        info = self.c.command("shantae_view_info")
        for _ in range(info["count"]):
            self.c.read()
        return info

    def fly(self, x, y):
        """The debug flight to x,y (4 pixels a frame, through walls), then still."""
        self.c.command("shantae_flight")
        self.press("-", 2)
        for _ in range(4000):
            px, py = self.player()
            far = [abs(d) for d in (px - x, py - y) if abs(d) > 3]
            if not far:
                break
            self.press(("L" if px > x + 3 else "R" if px < x - 3 else "") +
                       ("U" if py > y + 3 else "D" if py < y - 3 else ""), max(1, min(8, min(far) // 4 - 1)))
        else:
            raise AssertionError(("the flight never arrived", self.player(), x, y))
        self.press("-", 30)

    def picture(self):
        """The frame presented: its size and rows of RGB."""
        path = Path(self.tmp, "frame.ppm")
        self.c.command("screenshot", path=path.as_posix())
        data = path.read_bytes()
        fields, at = [], 0
        while len(fields) < 4:
            end = at
            while data[end] not in b" \n":
                end += 1
            fields.append(data[at:end])
            at = end + 1
        assert fields[0] == b"P6" and fields[3] == b"255", fields
        return int(fields[1]), int(fields[2]), data[at:]

    def origin(self):
        """World position of the presented picture's first pixel."""
        if not self.expanded:
            w = self.mem(0xC9FB, 4)
            return word(w, 0), word(w, 2)
        info = self.info()
        assert info["expanded"], "the view is not presented expanded"
        (x0, y0, x1, y1), cx, cy = info["shown"], info["camera_x"], info["camera_y"]
        width, height = info["width"], info["height"]

        def axis(lo, hi, camera, size, screen):
            most = hi - size
            return lo + int((most - lo) / 2) if most < lo else min(max(camera - (size - screen) // 2, lo), most)
        return axis(x0, x1, cx, width, 160), axis(y0, y1, cy, height, 144)

    def still(self):
        """Wait for the camera to come to rest (it eases down after a jump)."""
        origin = self.origin()
        for _ in range(120):
            self.press("-", 2)
            if self.origin() == origin:
                return
            origin = self.origin()
        raise AssertionError((self.name, "the camera never came to rest"))

    def crop(self, x, y, width, height, origin=None):
        """What is shown of the world box at x,y. The frame presented is the
        one before the camera the game now reports, so with the camera moving
        the caller passes the origin from before the step."""
        ox, oy = origin or self.origin()
        w, h, rgb = self.picture()
        left, top = x - ox, y - oy
        bottom = h if self.expanded else 128   # above the status bar
        assert 0 <= left and left + width <= w and 0 <= top and top + height <= bottom, \
            (self.name, "does not show", x, y, "from", ox, oy)
        return b"".join(rgb[((top + r) * w + left) * 3:((top + r) * w + left + width) * 3] for r in range(height))


ROM = (ROOT / "roms/shantae.gbc").read_bytes()


def rom(bank, addr, count):
    return ROM[bank * 0x4000 + addr - 0x4000:][:count]


def pieces(g, puzzle):
    """The 16 tiles each of the puzzle's four places shows, from the ROM by its turns."""
    turns = g.mem(TURNS + 4 * (puzzle - 1), 4)
    out = []
    for place in range(4):
        script = word(rom(0x26, PIECES + 3 * ((puzzle - 1) * 16 + place + 4 * turns[place]), 2), 0)
        op, bank, lo, hi, vram_bank, dest = rom(0x26, script, 6)
        assert op == 0x2C and vram_bank == 1 and dest == 0x8A + place, (puzzle, place, hex(script))
        out.append(rom(bank, (lo | hi << 8) + 1, 256))
    return out


def loaded(g):
    return [g.mem(0x8A00 + 0x100 * place, 256, vram_bank=1) for place in range(4)]


def turns(g, puzzle=3):
    return g.mem(TURNS + 4 * (puzzle - 1), 4)


def same(x, n, what, at, width, height):
    """Both show the box alike, as much of it as the original's screen has
    (standing under picture 3 its top 16 rows are above the screen), with
    Shantae out of the way."""
    for g in (x, n):
        walk_to(g, CLEAR)
        g.still()
    ox, oy = n.origin()
    left, top = max(at[0], ox), max(at[1], oy)
    right, bottom = min(at[0] + width, ox + 160), min(at[1] + height, oy + 128)
    assert (right - left) * (bottom - top) * 2 >= width * height, ("the original shows too little of", what)
    box = (left, top, right - left, bottom - top)
    assert x.crop(*box) == n.crop(*box), f"{what} is not shown as the original shows it"


def walk_to(g, x):
    for _ in range(600):
        px = g.player()[0]
        if px == x:
            break
        g.press("R" if px < x else "L", 1)
    g.press("-", 8)
    assert g.player()[0] == x, ("could not walk to", x, g.player())


def swipe(g, jump):
    if jump:
        g.press("A", jump)
        g.press("AB", 2)
    else:
        g.press("B", 2)


def turn(games, quarter):
    """Turn one quarter of picture 3 alone, in every game given."""
    for g in games:
        x, jump = ALONE[quarter]
        walk_to(g, x)
        g.press("L", 2)
        g.press("-", 6)
        before = turns(g)
        swipe(g, jump)
        for _ in range(120):
            g.press("-", 1)
            if turns(g) != before and g.mem(0xC022, 1)[0] == 0:
                break
        g.press("-", 30)
        after = turns(g)
        assert [i for i in range(4) if before[i] != after[i]] == [quarter], \
            (g.name, "quarter", quarter, before.hex(), after.hex())


def check_pictures(x, n, first):
    info = x.info()
    assert loaded(x) == pieces(x, 3), "VRAM does not hold picture 3's own pieces"
    assert info["loads_kept"] and info["own_tiles"] >= 128, (info["loads_kept"], info["own_tiles"])
    same(x, n, "picture 3", PICTURES[3], 64, 64)
    assert x.crop(*PICTURES[1], 64, 64) == first, "picture 1 is not shown as the original shows it"
    print(f"PASS: VRAM holds picture 3's pieces and both pictures are the original's "
          f"({info['own_tiles']} cells drawn from their own, {info['loads_kept']} loads kept out of VRAM)")


def check_whip(x, n, first):
    for g in (x, n):
        assert turns(g).hex() == "02030100", (g.name, turns(g).hex())
        swipe(g, 12)
    origin = x.origin()
    for frame in range(70):
        x.press("-", 1)
        shown = x.crop(*PICTURES[1], 64, 64, origin)
        origin = x.origin()
        assert shown == first, f"picture 1 changed {frame + 1} frames after the whip"
    n.press("-", 70)
    for g in (x, n):
        assert turns(g).hex() == "03000100" and g.mem(0xC022, 1)[0] == 0, (g.name, turns(g).hex())
    assert loaded(x) == pieces(x, 3), "VRAM does not hold picture 3's pieces after the whip"
    same(x, n, "picture 3 after the whip", PICTURES[3], 64, 64)
    print("PASS: a jump and a swipe turned picture 3's upper quarters (2,3 -> 3,0) and picture 1 stayed as it was")


def check_puzzle(games, first):
    keys = [g.mem(KEYS, 1)[0] for g in games]
    for quarter in (0, 2, 1, 3):
        while any(turns(g)[quarter] != quarter for g in games):
            turn(games, quarter)
            if len(games) == 2:
                same(*games, f"picture 3 with quarter {quarter} turned", PICTURES[3], 64, 64)
            if games[0].expanded:
                assert games[0].crop(*PICTURES[1], 64, 64) == first, "picture 1 changed"
    for g in games:
        assert turns(g).hex() == "00010203", (g.name, turns(g).hex())
        for _ in range(300):
            if g.mem(SOLVED + 2, 1)[0] == 2:
                break
            g.press("-", 1)
        assert g.mem(SOLVED + 2, 1)[0] == 2, (g.name, "the chest did not open", g.mem(SOLVED, 5).hex())
        g.press("-", 60)
        for _ in range(400):   # through the key over the chest (7809), from either side
            if g.mem(SOLVED + 2, 1)[0] == 3:
                break
            g.press("R" if g.player()[0] < PICTURES[3][0] + 10 else "L", 1)
        g.press("-", 20)
    for g, had in zip(games, keys):
        assert g.mem(SOLVED + 2, 1)[0] == 3 and g.mem(KEYS, 1)[0] == had + 1, \
            (g.name, "no key", g.mem(SOLVED, 5).hex(), had, g.mem(KEYS, 1)[0])
    print(f"PASS: picture 3 solved one quarter at a time, the chest opened and the key was taken "
          f"(CA88 {keys[0]} -> {keys[0] + 1}) in {' and '.join(g.name for g in games)}")


def door_states(g):
    """Each live door's number and whether it is open (+18), from script 26:620C."""
    return {s[0x23]: s[0x18] for s in g.table()
            if s[0] != 0xFF and s[4] == 0x26 and 0x620C <= word(s, 2) < 0x6293}


def both_doors(g):
    """Door 1 shut and door 5 open are awake (a larger view has others too)."""
    states = door_states(g)
    return states.get(1) == 0 and states.get(5) == 1


def check_doors(x, n, state):
    for g in (x, n):
        g.load(state)
        g.c.command("poke", addr=hex(DOOR_OPEN), hex="01", wram_bank=4)
    # The original's doors, each from beside it.
    original = {}
    for door, (dx, dy) in DOORS.items():
        n.fly(dx + 30, dy - 32)
        assert door_states(n) == {door: int(door == 5)}, door_states(n)
        original[door] = n.crop(dx, dy, 32, 40)
    assert original[1] != original[5], "the original shows the open door like the shut one"
    # Both in the view, neither within the original's reach.
    x.fly(1333, 700)
    assert both_doors(x), door_states(x)
    for door, at in DOORS.items():
        assert x.crop(*at, 32, 40) == original[door], f"door {door} is not shown as the original shows it"
    # Beside each, the tiles in VRAM are its own and both still look right.
    for door, (dx, dy) in DOORS.items():
        x.fly(dx + 30, dy - 32)
        assert both_doors(x), door_states(x)
        assert x.mem(0x8E00, 256, vram_bank=1) == rom(*DOOR_TILES[int(door == 5)], 256), \
            f"VRAM does not hold door {door}'s tiles beside it"
        for other, at in DOORS.items():
            assert x.crop(*at, 32, 40) == original[other], \
                f"door {other} is not shown as the original shows it from beside door {door}"
    print("PASS: door 5 open and door 1 shut in one view each look as the original shows them, "
          "and VRAM holds the tiles of the one Shantae is beside")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("state", type=Path, nargs="?", default=ROOT / "logs/states/flip-puzzle.state")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--native", action="store_true", help="the puzzle in the original view alone")
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--port", type=int, default=14392)
    args = ap.parse_args()
    exe = args.exe.resolve()
    with tempfile.TemporaryDirectory(prefix="shantae-pictures-", ignore_cleanup_errors=True) as tmp:
        n = Game(tmp, exe, False, args.width, args.height, args.port + 1)
        x = None
        try:
            if args.native:
                n.load(args.state)
                check_puzzle([n], None)
            else:
                x = Game(tmp, exe, True, args.width, args.height, args.port)
                # Picture 1 as the original shows it: flown beside it (the
                # camera is 52 left of her and her sprite 19-39 right), from
                # two heights so that nothing of her is in it.
                n.load(args.state)
                n.fly(PICTURES[1][0] - 43, PICTURES[1][1] + 8)
                first = n.crop(*PICTURES[1], 64, 64)
                n.fly(PICTURES[1][0] - 43, PICTURES[1][1] - 8)
                assert n.crop(*PICTURES[1], 64, 64) == first, "the original's picture 1 changed with her height"
                for part in (check_pictures, check_whip):
                    x.load(args.state)
                    n.load(args.state)
                    part(x, n, first)
                x.load(args.state)
                n.load(args.state)
                check_puzzle([x, n], first)
                check_doors(x, n, args.state)
            for g in (x, n):
                if g:
                    fallback = g.c.command("interp_fallbacks")
                    assert fallback["total_fallbacks"] == 0, fallback
        finally:
            n.close()
            if x:
                x.close()


if __name__ == "__main__":
    main()
