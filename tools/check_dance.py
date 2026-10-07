"""The dance with "Easier dancing" (dance.c), beside the original.

The state (default logs/states/dance.state, the user's state1 on 2026-10-06)
stands in Shantae's base form at 400,1984 in a desert pit, with every dance
learned. A slider (an object of bank 15) waits at 737,1960 to the right.

- Quick steps: after Select every new press is a step, matched against the
  ROM's own table (0E:4342) at once. Every dance of the table matches on the
  frame of its last step: the dance script goes on at 0E:41F9 with CB6D-CB6F
  holding the table's script and CB81 its form.
- A press that goes on to no learned dance is skipped (before Down, after a
  finished prefix, or toward an unlearned dance), Down starts over, two
  presses in one frame count, and steps on consecutive frames count.
- With quick steps off (the original rhythm) the same fast presses match
  nothing.
- After a transformation: landed at 660 with the debug flight and the monkey
  danced, the slider runs into her while she is in her entrance. The original
  is hit as soon as its protection ends (CB56 back to 0, about 70 frames after
  she appears); with the option she blinks (the blinker 06:72DA) and is not
  hurt until it ends.
- --original <exe> (the previous release's): with the feature off, and with it
  on and both options at the original, the player's object and the dance's
  RAM are the same every frame of a route of dances in and out of rhythm.

Requires the local saved state; never writes user saves or settings.
"""
import argparse
from pathlib import Path
import tempfile

from check_moveset import Game, word
from view_state_repro import step
from view_probe import ROOT

ROM = (ROOT / "roms/shantae.gbc").read_bytes()
LETTER = {1: "D", 2: "B", 4: "R", 6: "U", 7: "A", 9: "L"}
SCRIPT_AFTER_MATCH = 0x4211   # 0E:41F9 runs to its first wait in the frame of the match


def rom(addr):
    return ROM[0x0E * 0x4000 + addr - 0x4000]


def dances():
    """The table at 0E:4342: (steps, learned byte, mask, script, form)."""
    out, at = [], 0x4342
    while rom(at) | rom(at + 1):
        seq, steps = rom(at) | rom(at + 1) << 8, []
        while rom(seq):
            steps.append(rom(seq))
            seq += 1
        out.append((steps, rom(at + 3), rom(at + 2), rom(at + 4) | rom(at + 5) << 8 | rom(at + 6) << 16, rom(at + 7)))
        at += 8
    return out


def press(g, letters):
    """One frame with the buttons held, then one with nothing."""
    g.c.command("set_input", buttons=letters)
    step(g.c, 1)
    g.c.command("set_input", buttons="-")
    step(g.c, 1)


def dance(g, letters):
    """Select, then each entry of letters as a press; the player's script and
    CB6D-CB6F, CB81 after the last press's frame."""
    g.c.command("set_input", buttons="T")
    step(g.c, 1)
    g.c.command("set_input", buttons="-")
    step(g.c, 2)
    for i, b in enumerate(letters):
        g.c.command("set_input", buttons=b)
        step(g.c, 1)
        if i == len(letters) - 1:
            s = g.slot()
            target = g.mem(0xCB6D, 3)
            return dict(script=(s[4], word(s, 2)), target=target[0] | target[1] << 8 | target[2] << 16,
                        form=g.mem(0xCB81, 1)[0])
        g.c.command("set_input", buttons="-")
        step(g.c, 1)


def matched(result, entry):
    return result["script"] == (0x0E, SCRIPT_AFTER_MATCH) and result["target"] == entry[3] and result["form"] == entry[4]


def check_table(g, state):
    table = dances()
    assert len(table) == 11, len(table)
    for entry in table:
        g.load(state)
        r = dance(g, [LETTER[s] for s in entry[0]])
        assert matched(r, entry), (entry, r)
    print(f"PASS: all {len(table)} dances of 0E:4342 match on the frame of their last step, each press one frame "
          "apart, with the table's script and form")


def check_generous(g, state):
    by_steps = {tuple(e[0]): e for e in dances()}
    monkey, warp4 = by_steps[(1, 4)], by_steps[(1, 6, 9, 9, 2, 7)]
    cases = [
        (["U", "D", "R"], monkey, "a press before Down is skipped"),
        (["D", "U", "L", "L", "R", "B", "A"], warp4, "Right after Down Up Left Left is skipped"),
        (["D", "U", "D", "R"], monkey, "Down starts over"),
        (["DR"], monkey, "Down and Right in the same frame"),
    ]
    for letters, entry, what in cases:
        g.load(state)
        r = dance(g, letters)
        assert matched(r, entry), (what, r)
    # Consecutive frames, no release between: every new press counts.
    g.load(state)
    g.c.command("set_input", buttons="T")
    step(g.c, 1)
    for b in ["-", "D", "U", "L", "R"]:
        g.c.command("set_input", buttons=b)
        step(g.c, 1)
    s = g.slot()
    assert (s[4], word(s, 2)) == (0x0E, SCRIPT_AFTER_MATCH), "four steps on four frames"
    # An unlearned dance is skipped like a wrong press: the monkey's bit off.
    g.load(state)
    learned = g.mem(0xCB3A, 1)[0]
    g.c.command("poke", addr="0xcb3a", hex=f"{learned & 0xFE:02x}")
    r = dance(g, ["D", "R", "L"])
    assert matched(r, by_steps[(1, 9)]), ("Right skipped toward the unlearned monkey", r)
    print("PASS: skipped presses (before Down, off every dance, toward an unlearned dance), Down starting over, "
          "two presses in a frame and four on consecutive frames")


def check_rhythm(g, state):
    g.load(state)
    dance(g, ["D", "R"])
    g.c.command("set_input", buttons="-")
    for _ in range(64):
        step(g.c, 1)
        s = g.slot()
        assert (s[4], word(s, 2)) != (0x0E, SCRIPT_AFTER_MATCH) and g.mem(0xCB72, 1)[0] == 0, "the rhythm matched"
    print("PASS: with the original rhythm, Down and Right a frame apart match nothing in 64 frames")


def iframes(g, state):
    """Land beside the slider, dance the monkey; per frame: health, CB56, a blinker, the form."""
    g.load(state)
    g.c.command("shantae_flight")
    step(g.c, 2)
    g.c.command("set_input", buttons="R")
    step(g.c, 65)
    g.c.command("set_input", buttons="T")
    step(g.c, 1)
    g.c.command("set_input", buttons="-")
    step(g.c, 2)
    out = []
    for b in ["T", "-", "-", "D", "-", "R"] + ["-"] * 200:
        g.c.command("set_input", buttons=b)
        step(g.c, 1)
        # The blinker (06:72DA) hides her sprite with slot+$32 = $80 for four
        # frames in eight; it may take any slot of the grown table.
        out.append(dict(hp=g.mem(0xCA80, 1)[0], safe=g.mem(0xCB56, 1)[0], hidden=g.slot()[0x32] == 0x80,
                        form=g.mem(0xCB72, 1)[0]))
    return out


def check_iframes(off, on, state):
    f = iframes(off, state)
    appears = next(i for i, x in enumerate(f) if x["form"] == 1)
    hit = next((i for i, x in enumerate(f) if x["hp"] < f[0]["hp"]), None)
    unsafe = next(i for i, x in enumerate(f) if i > appears and not x["safe"])
    assert hit is not None and unsafe <= hit < unsafe + 30 and not any(x["hidden"] for x in f[:hit]), \
        ("the original was not hit when its protection ended", appears, unsafe, hit)
    f = iframes(on, state)
    hidden = [i for i, x in enumerate(f) if x["hidden"]]
    safe_until = next(i for i, x in enumerate(f) if i > appears and not x["safe"])
    # 15 blinks of 4 frames hidden and 4 shown, from the frame she appears.
    assert hidden and hidden[0] - appears <= 1 and len(hidden) == 60 and hidden[-1] < safe_until, \
        (appears, hidden[:3], len(hidden), safe_until)
    assert 118 <= safe_until - appears <= 122, (appears, safe_until)
    assert all(x["hp"] == f[0]["hp"] for x in f[:safe_until]), "hurt while blinking"
    print(f"PASS: the slider reaches the new monkey. Original: protected until frame {unsafe - appears} after she "
          f"appears, hit {hit - unsafe} frames later. With the option: she blinks and cannot be hurt for "
          f"{safe_until - appears} frames from the frame she appears, and is not hurt")


ROUTE = [("T", 1), ("-", 6), ("D", 20), ("-", 6), ("R", 20), ("-", 40), ("T", 1), ("-", 10), ("T", 1), ("-", 3),
         ("D", 1), ("-", 1), ("U", 1), ("-", 1), ("A", 1), ("-", 60), ("T", 1), ("-", 2), ("D", 1), ("R", 1),
         ("-", 120)]


def check_original(old, games, state):
    """Each game against the previous release: the player's object, CB56-CB88, every frame."""
    for g in [old] + games:
        g.load(state)
    frame = 0
    for buttons, frames in ROUTE:
        for g in [old] + games:
            g.c.command("set_input", buttons=buttons)
        for _ in range(frames):
            for g in [old] + games:
                step(g.c, 1)
            want = old.slot() + old.mem(0xCB56, 0x33)
            for g in games:
                assert g.slot() + g.mem(0xCB56, 0x33) == want, f"frame {frame} ({buttons}): {g.name} differs"
            frame += 1
    print(f"PASS: {', '.join(g.name for g in games)}: {frame} frames of dances in and out of rhythm, the same as "
          "the previous release's (her object and CB56-CB88)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("state", type=Path, nargs="?", default=ROOT / "logs/states/dance.state")
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--original", type=Path, help="an exe from before the feature, to compare the feature off with")
    ap.add_argument("--port", type=int, default=14600)
    args = ap.parse_args()
    exe = args.exe.resolve()
    with tempfile.TemporaryDirectory(prefix="shantae-dance-", ignore_cleanup_errors=True) as tmp:
        games = []

        def game(name, exe=exe, **settings):
            games.append(Game(tmp, name, exe, args.port + len(games), **settings))
            return games[-1]
        try:
            quick = game("quick steps")
            rhythm = game("original rhythm", quick_steps=0)
            plain = game("original invincibility", transform_invincible=0)
            check_table(quick, args.state)
            check_generous(quick, args.state)
            check_rhythm(rhythm, args.state)
            check_iframes(plain, quick, args.state)
            if args.original:
                old = game("previous release", exe=args.original.resolve())
                check_original(old, [game("feature off", easy_dance=0),
                                     game("both options original", quick_steps=0, transform_invincible=0)], args.state)
            else:
                print("SKIP: the feature off against the previous release (no --original exe given)")
        finally:
            for g in games:
                g.close()


if __name__ == "__main__":
    main()
