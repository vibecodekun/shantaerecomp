"""The dance with "Easier dancing" (dance.c), beside the original.

The state (default logs/states/dance.state, made by tools/make_states.py from a
debug game, which has every dance learned) stands in Shantae's base form at
400,1984 in a desert pit. A slider (an object of bank 15) waits at 737,1960 to
the right.

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
  is protected until her silhouette ends (the background's palettes back to
  their colours, about 70 frames after she appears; CB56 back to 0) and is hit
  soon after; with the option (the user's report on 2026-10-10) nothing blinks
  during the silhouette, she blinks (the blinker 06:72DA) from the frame it
  ends, is safe throughout, and is not hurt until the blink ends.
- Every transformation of the table the same way, in place, and the heal
  dance with no blink.
- Turning back (the user's report on 2026-10-07): the monkey flown beside the
  slider and Select. The turn back (script 0E:4000) protects her until she
  can move; then the original has nothing and the slider hits her. With the
  option she blinks from that frame and cannot be hurt for 120 frames.
- --original <exe> (the previous release's): with the feature off, and with it
  on and both options at the original, the player's object and the dance's
  RAM are the same every frame of a route of dances in and out of rhythm.

Requires the saved state (python tools/make_states.py); never writes user saves or settings.
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
    """Land beside the slider, dance the monkey; per frame: health, CB56, a blinker, the form and the
    background's palettes (the hardware's palette RAM)."""
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
    for b in ["T", "-", "-", "D", "-", "R"] + ["-"] * 280:
        g.c.command("set_input", buttons=b)
        step(g.c, 1)
        # The blinker (06:72DA) hides her sprite with slot+$32 = $80 for four
        # frames in eight; it may take any slot of the grown table.
        out.append(dict(hp=g.mem(0xCA80, 1)[0], safe=g.mem(0xCB56, 1)[0], hidden=g.slot()[0x32] == 0x80,
                        form=g.mem(0xCB72, 1)[0], palette=g.c.command("hw_state")["bg_palette"]))
    return out


def silhouette(f, appears):
    """The frames of the silhouette: from the first with a white background after she appears (BGR555
    $7FFF) to the last before the background has its colours from before the dance again."""
    white = next(i for i, x in enumerate(f) if i > appears and x["palette"].startswith("FF7F"))
    back = next(i for i, x in enumerate(f) if i > white and x["palette"] == f[0]["palette"])
    return white, back


def check_iframes(off, on, state):
    """The blink after transforming starts when the silhouette ends (the user's report on 2026-10-10)."""
    f = iframes(off, state)
    appears = next(i for i, x in enumerate(f) if x["form"] == 1)
    white, back = silhouette(f, appears)
    hit = next((i for i, x in enumerate(f) if x["hp"] < f[0]["hp"]), None)
    unsafe = next(i for i, x in enumerate(f) if i > appears and not x["safe"])
    # The original's protection comes off (0E:4F7D) in the tick the colours come back, shown a frame later.
    assert unsafe == back - 1, ("the original's protection did not end with the silhouette", white, back, unsafe)
    assert hit is not None and unsafe <= hit < unsafe + 30 and not any(x["hidden"] for x in f[:hit]), \
        ("the original was not hit when its protection ended", appears, unsafe, hit)
    f = iframes(on, state)
    assert silhouette(f, appears) == (white, back), ("the silhouette", silhouette(f, appears), (white, back))
    match = next(i for i, x in enumerate(f) if x["safe"])
    hidden = [i for i, x in enumerate(f) if x["hidden"]]
    safe_until = next(i for i, x in enumerate(f) if i > appears and not x["safe"])
    # Nothing during the silhouette; then 15 blinks of 4 frames hidden and 4 shown.
    assert hidden and unsafe <= hidden[0] <= back and len(hidden) == 60 and hidden[-1] < safe_until, \
        (white, back, hidden[:3], len(hidden), safe_until)
    assert all(x["safe"] for x in f[match:safe_until]) and 118 <= safe_until - unsafe <= 122, \
        (match, unsafe, safe_until)
    assert all(x["hp"] == f[0]["hp"] for x in f[:safe_until]), "hurt while protected"
    print(f"PASS: the slider reaches the new monkey. The silhouette lasts from frame {white - appears} to "
          f"{back - appears} after she appears. Original: protected until it ends, hit {hit - unsafe} frames later. "
          f"With the option: no blink during it; she blinks from frame {hidden[0] - appears} and cannot be hurt for "
          f"{safe_until - unsafe} frames from its end, safe throughout from the match, and is not hurt")


def check_every_form(on, state):
    """Each dance of the table that sets a form, and the heal: in place, with the option."""
    forms = [e for e in dances() if e[4] != 0xFF]
    heal = next(e for e in dances() if e[0] == [1, 6, 7])
    for steps_, *_, form in forms + [heal]:
        on.load(state)
        f = []
        for b in ["T", "-", "-"] + sum([[LETTER[s], "-"] for s in steps_], []) + ["-"] * 280:
            on.c.command("set_input", buttons=b)
            step(on.c, 1)
            f.append(dict(safe=on.mem(0xCB56, 1)[0], hidden=on.slot()[0x32] == 0x80, form=on.mem(0xCB72, 1)[0],
                          palette=on.c.command("hw_state")["bg_palette"]))
        match = next(i for i, x in enumerate(f) if x["safe"])
        white = next(i for i, x in enumerate(f) if x["palette"].startswith("FF7F"))
        back = next(i for i, x in enumerate(f) if i > white and x["palette"] == f[0]["palette"])
        unsafe = next(i for i, x in enumerate(f) if i > match and not x["safe"])
        hidden = [i for i, x in enumerate(f) if x["hidden"]]
        if form == 0xFF:
            assert f[-1]["form"] == 0 and not hidden and unsafe < back, ("the heal", match, unsafe, back, hidden[:3])
            continue
        assert f[-1]["form"] == form and hidden and hidden[0] == back and len(hidden) == 60, \
            (form, white, back, hidden[:3], len(hidden))
        assert 118 <= unsafe - back <= 122, (form, back, unsafe)
    print(f"PASS: all {len(forms)} transformations: safe from the match until 120 frames after the silhouette ends, "
          "blinking from the frame its colours come back. The heal: no blink, as in the original")


def turn_back(g, state):
    """The monkey danced in the pit, flown to x 624, then Select: she turns back into Shantae while the
    slider comes from 737, and it reaches her as she can move."""
    g.load(state)
    for b in ["T", "-", "-", "D", "-", "R"] + ["-"] * 200:
        g.c.command("set_input", buttons=b)
        step(g.c, 1)
    assert g.mem(0xCB72, 1)[0] == 1, "not the monkey"
    g.c.command("shantae_flight")
    step(g.c, 2)
    g.c.command("set_input", buttons="R")
    step(g.c, 56)
    for b in ["T", "-", "-"]:   # the first Select ends the flight
        g.c.command("set_input", buttons=b)
        step(g.c, 1)
    out = []
    for b in ["T"] + ["-"] * 260:
        g.c.command("set_input", buttons=b)
        step(g.c, 1)
        s = g.slot()
        out.append(dict(hp=g.mem(0xCA80, 1)[0], safe=g.mem(0xCB56, 1)[0], hidden=s[0x32] == 0x80,
                        form=g.mem(0xCB72, 1)[0], routine=(s[0x19], word(s, 0x1B))))
    return out


def check_turn_back(off, on, state):
    """The turn back (script 0E:4000) protects her until she can move: from then the original has nothing."""
    for g in (off, on):
        f = turn_back(g, state)
        assert f[0]["form"] == 0 and f[0]["safe"], (g.name, "the turn back did not start", f[0])
        back = next(i for i, x in enumerate(f) if x["routine"] == (0x06, 0x4AA7))
        assert all(x["safe"] for x in f[:back]), (g.name, "hurtable while turning back")
        hit = next((i for i, x in enumerate(f) if x["hp"] < f[0]["hp"]), None)
        hidden = [i for i, x in enumerate(f) if x["hidden"]]
        if g is off:
            assert not f[back]["safe"] and hit is not None and back < hit < back + 60 and \
                not [i for i in hidden if i < hit], \
                ("the original was not hit after turning back", back, hit, f[back])
            print_off = f"hit {hit - back} frame{'s' if hit - back != 1 else ''} after she can move"
        else:
            safe_until = next(i for i, x in enumerate(f) if i > back and not x["safe"])
            assert hidden and hidden[0] - back <= 1 and len(hidden) == 60 and hidden[-1] < safe_until, \
                (back, hidden[:3], len(hidden), safe_until)
            assert 118 <= safe_until - back <= 122 and hit is None, (back, safe_until, hit)
    print(f"PASS: turning back into Shantae beside the slider, protected while she turns back. Original: {print_off}. "
          f"With the option: she blinks and cannot be hurt for {safe_until - back} frames from the frame she can "
          "move, and is not hurt")


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
            check_every_form(quick, args.state)
            check_turn_back(plain, quick, args.state)
            if args.original:
                # v0.1.11 and later have the feature, on by default.
                old = game("previous release", exe=args.original.resolve(), easy_dance=0)
                check_original(old, [game("feature off", easy_dance=0),
                                     game("both options original", quick_steps=0, transform_invincible=0)], args.state)
            else:
                print("SKIP: the feature off against the previous release (no --original exe given)")
        finally:
            for g in games:
                g.close()


if __name__ == "__main__":
    main()
