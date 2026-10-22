"""The transformation buttons (transform.c) and their HUD (hud.c).

The states come from tools/make_states.py (a debug game: every dance learned).
A button is pressed through the debug server's `shantae_transform` (the same
request a bound key or button makes in game_on_frame).

- Every form against its dance: from dance.state (Shantae standing in a desert
  pit at 400,1984), danced with quick steps and left 400 frames, and switched
  with its button and left as long. The form (CB72), her movement routine and
  the background and sprite palettes must be the same (the button calls the
  form's palette routine, 06:4015, as the dance's silhouette does).
- Each form, switched to, walks and jumps, and its button again turns her back
  into Shantae, who walks.
- Mid-jump the elephant takes over in the air and lands; a press while she
  walks switches at once.
- A one-tile corridor (corridor.state): the monkey and the tinkerbat fit, the
  elephant, harpy and spider are refused, and turning back crouches her.
- In a shop's menu (shop.state) and while a freed genie's scene moves her
  (genie.state, B by the cage) nothing switches, and a press made there does
  not switch after it.
- A form whose dance is not learned does not switch; with the option off
  nothing does.
- The heal button heals as the heal dance's 0E:4421 does: 10 gems (CA7D) a
  heart (4 of CA80) up to CA82 while the gems last, in a form too; nothing
  when she is whole, has under 10 gems or has not learned the dance.
- Beside "Easier dancing" with quick steps: a button mid-dance switches at
  once, Select then turns her back and the first warp dance (Down Up Left
  Right) warps her; a button pressed while a danced transformation plays is
  not taken.
- The HUD draws its six slots in three columns (the default), one row or one
  column, and nothing with the option or the HUD off. (Where it goes in the
  picture, and through shader presets, is the window's: see the HUD's notes in
  extras_ui.cpp.)
- --original <exe> (the previous release's): with no button pressed the
  player's object and CB50-CB9F are the same as the previous release's every
  frame of a route of moves and a dance.

Requires the saved states (python tools/make_states.py); never writes user saves or settings.
"""
import argparse
from pathlib import Path
import tempfile

from check_moveset import Game, word
from view_state_repro import step
from view_probe import ROOT

STEPS = {1: "R", 2: "L", 3: "B", 4: "A", 5: "UU"}   # after Down, as the dance table has them
NAMES = {0: "Shantae", 1: "monkey", 2: "elephant", 3: "harpy", 4: "spider", 5: "tinkerbat"}


def info(g):
    s = g.slot()
    return dict(form=g.mem(0xCB72, 1)[0], routine=(s[0x19], word(s, 0x1B)), x=word(s, 0x34), y=word(s, 0x37))


def run(g, route):
    for buttons, frames in route:
        g.c.command("set_input", buttons=buttons)
        step(g.c, frames)
    g.c.command("set_input", buttons="-")


def switch(g, form, frames=4):
    g.c.command("shantae_transform", form=form)
    step(g.c, frames)
    return info(g)


def settled(g):
    hw = g.c.command("hw_state")
    return info(g), hw["bg_palette"], hw["obj_palette"]


def check_every_form(g, states):
    for form in range(1, 6):
        g.load(states / "dance.state")
        run(g, [("T", 1), ("-", 4)] + [p for b in "D" + STEPS[form] for p in ((b, 1), ("-", 3))] + [("-", 400)])
        danced = settled(g)
        g.load(states / "dance.state")
        switch(g, form, 400)
        pressed = settled(g)
        assert danced[0]["form"] == form, (NAMES[form], "the dance did not make the form", danced[0])
        assert pressed[0]["form"] == form and pressed[0]["routine"] == danced[0]["routine"], (
            NAMES[form], danced[0], pressed[0])
        assert pressed[1:] == danced[1:], (NAMES[form], "palettes differ from the dance's", danced[1:], pressed[1:])
    print("PASS: every form by its button as by its dance: the form, her routine, the background and sprite palettes")


def check_moves(g, states):
    for form in range(1, 6):
        g.load(states / "dance.state")
        a = switch(g, form)
        run(g, [("R", 20), ("-", 2)])
        b = info(g)
        run(g, [("A", 8)])
        c = info(g)
        run(g, [("-", 60)])
        d = info(g)
        e = switch(g, form)
        run(g, [("L", 20), ("-", 2)])
        f = info(g)
        assert a["form"] == form and b["x"] > a["x"], (NAMES[form], "does not walk", a, b)
        assert c["y"] < d["y"] == 1984, (NAMES[form], "does not jump and land", c, d)
        assert e["form"] == 0 and f["x"] < e["x"], (NAMES[form], "does not turn back and walk", e, f)
    print("PASS: each form walks and jumps, and its button again turns her back into Shantae, who walks")


def check_on_the_move(g, states):
    g.load(states / "dance.state")
    run(g, [("A", 6)])
    a = switch(g, 2, 2)
    run(g, [("-", 60)])
    b = info(g)
    assert a["form"] == 2 and a["y"] < 1984 and b["y"] == 1984 and b["form"] == 2, ("elephant mid-jump", a, b)
    g.load(states / "dance.state")
    run(g, [("R", 10)])
    c = switch(g, 1, 2)
    assert c["form"] == 1, ("monkey mid-walk", c)
    print("PASS: the elephant takes over mid-jump and lands; a press while she walks switches at once")


def check_corridor(g, states):
    fits = {1: True, 2: False, 3: False, 4: False, 5: True}
    for form, fit in fits.items():
        g.load(states / "corridor.state")
        a = switch(g, form, 6)
        assert (a["form"] == form) == fit, (NAMES[form], "in the corridor", a)
        if fit:
            b = switch(g, form, 6)
            assert b["form"] == 0 and b["routine"] == (6, 0x6094), (NAMES[form], "turning back does not crouch", b)
    print("PASS: a one-tile corridor: the monkey and tinkerbat fit and turn back crouched; the elephant, harpy and "
          "spider are refused")


def check_blocked(g, states):
    g.load(states / "shop.state")
    a = switch(g, 1, 40)
    run(g, [("A", 2), ("-", 60), ("B", 2), ("-", 60)])
    b = info(g)
    assert a["form"] == 0 and b["form"] == 0, ("the shop", a, b)
    g.load(states / "genie.state")
    run(g, [("B", 2), ("-", 5)])
    c = switch(g, 1, 10)
    run(g, [("-", 30)])
    d = switch(g, 1, 10)
    assert c["form"] == 0 and c["routine"] == (6, 0x74AC), ("the genie's scene, backing off", c)
    assert d["form"] == 0 and d["routine"] == (6, 0x77AB), ("the genie's scene, waiting", d)
    print("PASS: nothing switches in a shop's menu or a freed genie's scene, nor after it from a press made there")


def check_unlearned(g, off, states):
    g.load(states / "dance.state")
    learned = g.mem(0xCB3A, 1)[0]
    g.c.command("poke", addr="0xcb3a", hex=f"{learned & ~1:02x}")
    a = switch(g, 1)
    b = switch(g, 2)
    assert a["form"] == 0 and b["form"] == 2, ("an unlearned monkey", a, b)
    off.load(states / "dance.state")
    c = switch(off, 1)
    assert c["form"] == 0, ("the option off", c)
    print("PASS: a form not yet learned does not switch, nor does any with the option off")


def heal_state(g):
    m = g.mem(0xCA7D, 6)
    return word(m, 0), m[3], g.mem(0xCB72, 1)[0]


def check_heal(g, states):
    cases = [  # pokes, form first, (gems, health) after
        ({0xCA80: "04"}, 0, (481, 12)),                 # two hearts for 20 gems
        ({}, 0, (501, 12)),                             # whole: nothing
        ({0xCA80: "04", 0xCA7D: "0900"}, 0, (9, 4)),    # under 10 gems: nothing
        ({0xCA80: "04", 0xCA7D: "0f00"}, 0, (5, 8)),    # one heart for the gems there are
        ({0xCA80: "04"}, 2, (481, 12)),                 # as the elephant, who stays one
        ({0xCA80: "04", 0xCB3A: "ef"}, 0, (501, 4)),    # the heal dance not learned
    ]
    for pokes, form, want in cases:
        g.load(states / "dance.state")
        for addr, value in pokes.items():
            g.c.command("poke", addr=hex(addr), hex=value)
        if form:
            switch(g, form, 60)
        g.c.command("shantae_transform", form=6)
        step(g.c, 4)
        gems, health, now = heal_state(g)
        assert (gems, health) == want and now == form, ("the heal", pokes, form, (gems, health, now), want)
    print("PASS: the heal button heals as the heal dance does (10 gems a heart, while they last), in a form too, "
          "and nothing when she is whole, has under 10 gems or has not learned it")


def check_dancing(g, states):
    """Beside "Easier dancing" with quick steps (on in this game by default)."""
    def tap(buttons):
        run(g, [(buttons, 1), ("-", 3)])
    g.load(states / "dance.state")
    tap("T")
    tap("D")
    a = switch(g, 1, 30)
    assert a["form"] == 1 and a["routine"] == (0x0D, 0x4510), ("a button mid-dance", a)
    tap("T")
    run(g, [("-", 150)])
    b = info(g)
    assert b["form"] == 0, ("Select turns her back", b)
    where = g.mem(0xC9F9, 2)
    for buttons in "TDULR":
        tap(buttons)
    run(g, [("-", 400)])
    assert g.mem(0xC9F9, 2) != where, ("the first warp dance after the buttons", where)
    g.load(states / "dance.state")
    for buttons in "TDR":
        tap(buttons)
    run(g, [("-", 10)])
    switch(g, 2, 200)
    c = info(g)
    assert c["form"] == 1, ("a button while the dance's transformation plays", c)
    print("PASS: with quick steps a button mid-dance switches at once, Select turns her back and the warp dance "
          "warps; a button pressed while a danced transformation plays is not taken")


def check_hud(g, off, nohud, row, column, states):
    for game in (g, off, nohud, row, column):
        game.load(states / "dance.state")
    # Six slots of 26 pixels, a pixel apart: three columns by default.
    for game, size in ((g, (80, 53)), (row, (161, 26)), (column, (26, 161))):
        r = game.c.command("shantae_hud")
        assert r["drawn"] and (r["width"], r["height"]) == size, ("the HUD", game.name, r)
    for game, name in ((off, "the option off"), (nohud, "the HUD off")):
        r = game.c.command("shantae_hud")
        assert not r["drawn"], (name, r)
    print("PASS: the HUD draws six slots in three columns, one row or one column, and nothing with the option or "
          "the HUD off")


ROUTE = [("-", 10), ("R", 30), ("RB", 30), ("A", 12), ("-", 40), ("B", 4), ("-", 30), ("D", 20), ("DL", 30),
         ("-", 10), ("T", 1), ("-", 4), ("D", 1), ("-", 3), ("R", 1), ("-", 300)]


def check_original(old, games, state):
    for g in [old] + games:
        g.load(state)
    frame = 0
    for buttons, frames in ROUTE:
        for g in [old] + games:
            g.c.command("set_input", buttons=buttons)
        for _ in range(frames):
            for g in [old] + games:
                step(g.c, 1)
            want = old.slot() + old.mem(0xCB50, 0x50)
            for g in games:
                assert g.slot() + g.mem(0xCB50, 0x50) == want, f"frame {frame} ({buttons}): {g.name} differs"
            frame += 1
    print(f"PASS: {', '.join(g.name for g in games)}: {frame} frames of moves and a dance with no button pressed, "
          "the same as the previous release's (her object and CB50-CB9F)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--states", type=Path, default=ROOT / "logs/states")
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--original", type=Path, help="an exe from before the feature, to compare with")
    ap.add_argument("--port", type=int, default=14700)
    args = ap.parse_args()
    exe = args.exe.resolve()
    with tempfile.TemporaryDirectory(prefix="shantae-transform-", ignore_cleanup_errors=True) as tmp:
        games = []

        def game(name, exe=exe, **settings):
            games.append(Game(tmp, name, exe, args.port + len(games), **settings))
            return games[-1]
        try:
            on = game("buttons")
            off = game("buttons off", transform_buttons=0)
            nohud = game("no HUD", hud=0)
            row = game("HUD in a row", hud_columns=6)
            column = game("HUD in a column", hud_columns=1)
            check_every_form(on, args.states)
            check_moves(on, args.states)
            check_on_the_move(on, args.states)
            check_corridor(on, args.states)
            check_blocked(on, args.states)
            check_unlearned(on, off, args.states)
            check_heal(on, args.states)
            check_dancing(on, args.states)
            check_hud(on, off, nohud, row, column, args.states)
            if args.original:
                old = game("previous release", exe=args.original.resolve())
                check_original(old, [game("on, as the previous"), game("off, as the previous", transform_buttons=0)],
                               args.states / "dance.state")
            else:
                print("SKIP: against the previous release (no --original exe given)")
        finally:
            for g in games:
                g.close()


if __name__ == "__main__":
    main()
