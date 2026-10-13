"""The saved states the checks play, made from a cold boot with WayForward's debug mode.

At "Press Start" the code Left x2, Right x8, Left x6, Right x2, Left x7, Right x6,
Left x8 opens the developer menu; its Start Debug Game (0A:46A4) sets debug mode
(CC04) and gives the debug inventory (04:49D8): every dance learned (CB3A = $FF,
CB3B bits 0-2, the tinkerbat's among them), the bits CB38-CB39 set high, full
items. File Select's New 1 then shows the scene grid, where poking a scene into
CBFD and 1 into CBFC loads it. Each state is a scene, with Shantae moved by the
debug flight (the `shantae_flight` command; her position poked while she flies)
and set down where the checks expect her. Debug mode is turned off again (CC04
= 0) before each save, so Select+A is no flight and the game plays as a normal
one with that inventory.

- debug-grid.state: the scene grid.
- moveset.state, dance.state: scene 1A (map 74:AB, the desert by the
  labyrinth's door), Shantae walked off the platform she starts on into the
  pit: floor y 1984 from x 346 to 456, a wall at 338 and 463, the platform at
  y 1960 above the left wall. She stands at x 393 (moveset) and 400 (dance),
  facing right. A slider (bank 15) waits on the higher ground to the right.
- tinkerbat-gap.state: scene 3A (map 5B:CA, the ice tower), set down on the
  ledge at 5245,1176 and turned into the tinkerbat. Right of the ledge a wall
  drops to the hidden passage check_forms.py squeezes through.
- pillar.state: scene 3C (map 4D:D4, the beach of the user's state1 of
  2026-10-10), set down at 2194,1944 and turned into the tinkerbat, left of
  the orange pillar (16:5966, x 2248) that a charge breaks in two hits; right
  of it the beach ends in a wall at x 2496 with the pipe maze above. Shantae's
  remark on landing is held through with B. The pillar rises (script 16:5867)
  once its counter, C00D plus its +$20, is 4; the debug game has 3, so it is
  poked, as the user's game had it.
- web.state: scene 42 (map 67:85, as the user's state3), set down at
  4128,1400 in her base form, on a floor in front of a wall of web tiles ($64)
  the spider climbs.

Writes logs/states/ (or --out); never touches user saves or settings.

    python tools/make_states.py
"""
import argparse
from pathlib import Path
import tempfile

from check_moveset import Game, word
from view_state_repro import step
from view_probe import ROOT

TINKERBAT = 5
SCRIPT_MATCHED = (0xF9, 0x41, 0x0E)   # 0E:41F9, where the dance script goes on after a match
TINKERBAT_SCRIPT = (0xFB, 0x43, 0x0E)   # 0E:43FB, its entry in the table at 0E:4342


def press(g, buttons, count):
    g.c.command("set_input", buttons=buttons)
    step(g.c, count)


def debug_grid(g, path):
    """From a cold boot: the code at "Press Start", Start Debug Game, File Select's New 1."""
    press(g, "-", 300)
    for buttons, count in (("L", 2), ("R", 8), ("L", 6), ("R", 2), ("L", 7), ("R", 6), ("L", 8)):
        for _ in range(count):
            press(g, buttons, 4)
            press(g, "-", 4)
    press(g, "-", 30)
    press(g, "S", 4)
    press(g, "-", 90)       # the developer menu
    press(g, "D", 4)
    press(g, "-", 10)
    press(g, "A", 4)
    press(g, "-", 240)      # Start Debug Game: File Select
    press(g, "A", 4)
    press(g, "-", 120)      # New 1: the scene grid
    assert g.mem(0xCC04, 1)[0] == 1, "debug mode is off"
    assert g.mem(0xCB3A, 2) == b"\xff\x07", ("the debug inventory", g.mem(0xCB3A, 2).hex())
    g.c.command("save_state", path=Path(path).resolve().as_posix())


def scene(g, grid, number):
    g.c.command("set_input", buttons="-")
    g.c.command("load_state", path=Path(grid).resolve().as_posix())
    step(g.c, 2)
    g.c.command("poke", addr="0xcbfc", hex=f"01{number:02x}")
    press(g, "-", 360)
    g.c.command("poke", addr="0xcc04", hex="00")


def fly_to(g, x, y):
    """The debug flight, her position poked, the flight ended: she falls to the ground below."""
    g.c.command("shantae_flight")
    step(g.c, 2)
    me = word(g.mem(0xCA13, 2), 0)
    g.c.command("poke", addr=hex(me + 0x33), hex=bytes([0, x & 0xFF, x >> 8, 0, y & 0xFF, y >> 8]).hex(),
                wram_bank=3)
    press(g, "-", 60)
    for buttons in ("T", "-", "-"):   # Select ends the flight
        press(g, buttons, 1)
    press(g, "-", 60)


def transform(g, script, form):
    """Select, then what the match 0E:42D2 leaves, so that it is the same in any build."""
    press(g, "T", 1)
    press(g, "-", 3)
    g.c.command("poke", addr="0xcb6d", hex=bytes(script).hex())
    g.c.command("poke", addr="0xcb81", hex=f"{form:02x}")
    me = word(g.mem(0xCA13, 2), 0)
    g.c.command("poke", addr=hex(me + 0x16), hex="8000", wram_bank=3)
    g.c.command("poke", addr=hex(me + 0x18), hex="ff", wram_bank=3)
    g.c.command("poke", addr=hex(me + 0x02), hex=bytes(SCRIPT_MATCHED + (0xFF,)).hex(), wram_bank=3)
    press(g, "-", 400)   # the flash, the silhouette, the entrance
    assert g.mem(0xCB72, 1)[0] == form and not g.mem(0xCB56, 1)[0], "the transformation did not end"


def where(g):
    s = g.slot()
    return word(s, 0x34), word(s, 0x37)


def walk_to(g, x):
    g.c.command("set_input", buttons="R" if where(g)[0] < x else "L")
    for _ in range(600):
        if where(g)[0] == x:
            break
        step(g.c, 1)
    assert where(g)[0] == x, ("walking", x, where(g))
    press(g, "-", 2)


def objects(g, bank):
    """The live objects whose script is in the bank: (script, x, y)."""
    raw = g.mem(0xD000, 32 * 126, wram_bank=3)
    slots = [raw[i * 126:(i + 1) * 126] for i in range(32)]
    return [(word(s, 2), word(s, 0x34), word(s, 0x37)) for s in slots if s[0] == 0 and s[4] == bank]


def clear_pit(g):
    """The sand worm (bank 15) that stands at the pit's right wall, 454,1935, comes
    apart after nine whips; what it drops is picked up by the wall."""
    walk_to(g, 425)
    for _ in range(20):
        if not objects(g, 0x15):
            break
        press(g, "B", 2)
        press(g, "-", 30)
    assert not objects(g, 0x15), ("the sand worm", objects(g, 0x15))
    press(g, "-", 60)
    walk_to(g, 456)
    press(g, "-", 30)


def save(g, path, at, form=0):
    assert where(g) == at and g.mem(0xCB72, 1)[0] == form, (Path(path).name, where(g), g.mem(0xCB72, 1)[0])
    g.c.command("save_state", path=Path(path).resolve().as_posix())
    print(f"{Path(path).name}: map {g.mem(0xC9F9, 2).hex()}, at {at[0]},{at[1]}, form {form}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--out", type=Path, default=ROOT / "logs/states")
    ap.add_argument("--port", type=int, default=14680)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    grid = args.out / "debug-grid.state"
    with tempfile.TemporaryDirectory(prefix="shantae-states-", ignore_cleanup_errors=True) as tmp:
        # The original moves and dance, so that the routes below are the game's own.
        g = Game(tmp, "states", args.exe.resolve(), args.port, smooth_moves=0, easy_dance=0)
        try:
            debug_grid(g, grid)
            print(f"{grid.name}: the scene grid, debug mode on")
            for name, x in (("moveset.state", 393), ("dance.state", 400)):
                scene(g, grid, 0x1A)
                assert where(g) == (264, 1960), ("scene 1A", where(g))
                clear_pit(g)
                walk_to(g, x - 20)
                walk_to(g, x)   # from the left, so that she faces right
                press(g, "-", 30)
                save(g, args.out / name, (x, 1984))
            scene(g, grid, 0x3A)
            fly_to(g, 5245, 1160)
            transform(g, TINKERBAT_SCRIPT, TINKERBAT)
            save(g, args.out / "tinkerbat-gap.state", (5245, 1176), TINKERBAT)
            scene(g, grid, 0x3C)
            fly_to(g, 2194, 1928)
            for _ in range(12):   # her remark: B held runs a line out, a new B the next
                press(g, "B", 40)
                press(g, "-", 4)
            transform(g, TINKERBAT_SCRIPT, TINKERBAT)
            pillar = [s for s in objects(g, 0x16) if (s[1], s[2]) == (2248, 1912)]
            assert pillar and g.mem(0xC00E, 1)[0] == 3, ("the pillar and its counter", objects(g, 0x16))
            g.c.command("poke", addr="0xc00e", hex="04")
            press(g, "-", 200)   # it rises
            save(g, args.out / "pillar.state", (2194, 1944), TINKERBAT)
            scene(g, grid, 0x42)
            fly_to(g, 4128, 1384)
            save(g, args.out / "web.state", (4128, 1400))
        finally:
            g.close()


if __name__ == "__main__":
    main()
