"""The transformations with "Smoother movement" (forms.c), beside the original.

Two states, made by tools/make_states.py from a debug game (every dance
learned): logs/states/dance.state (Shantae in a desert pit at 400,1984),
turned into the monkey and the harpy with a quick dance; and
logs/states/tinkerbat-gap.state (the tinkerbat on a ledge of the ice tower at
5245,1176, where the user's state2 on 2026-10-06 stood). Right of the
ledge a wall drops to a hidden passage 16 pixels tall (y 1432-1447) that runs
left through the wall, across a shaft 16 pixels wide (x 5240-5255) with no
floor, to the alcove of a warp squid. Every part plays the same buttons with
Transformations on and off and reads her object.

- Monkey and tinkerbat, B and a direction from standing (Right for the
  monkey, Left for the tinkerbat, whose ledge ends a little to her right): the
  original attacks on the spot (16 and 13 frames) and then runs; the slide
  moves 2 pixels on every frame from the press and runs on; cancel runs at
  once with no attack.
- The same with B pressed while walking (the user's report on 2026-10-07:
  v0.1.11 ran at once there, with no attack, since the walk routines test the
  run flag before the new B): the original attacks on the spot; the slide
  attacks and moves 2 pixels a frame after the walk's step, or 1 with B
  tapped; cancel runs at once.
- A standing jump with B held: 2 pixels a frame, 1 with it released; the
  original 1 either way.
- The harpy: after a talon in her run she goes on at the speed she had; the
  original starts her run again from nothing.
- The harpy flapping with A, then a talon with A pressed during it (the same
  report): she rises through it, a flap's sound with each press, and the
  talon plays out to its hit; the original drops all of those presses and she
  sinks.
- The tinkerbat squeezes: climbing down the wall with B, Down and Left she
  steps into the passage and runs through it; from the shaft, climbing its far
  wall with B, Up and Left, she steps into the passage's far side and reaches
  the alcove. The original climbs past both.
- --original <exe> (the previous release's): with Transformations off, the
  monkey's, the harpy's and the tinkerbat's routes give the same object, byte
  for byte.

Requires the saved states (python tools/make_states.py); never writes user saves or settings.
"""
import argparse
from pathlib import Path
import tempfile

from check_moveset import Game, word
from view_state_repro import step
from view_probe import ROOT

MONKEY_CLAW, MONKEY_RUN, MONKEY_JUMP, MONKEY_WALK = 0x4FC8, 0x53FC, 0x4CE2, 0x4840
SWORD, TINK_RUN, TINK_FALL, TINK_CLIMB, TINK_WALK = 0x57AC, 0x5C00, 0x5375, 0x5080, 0x4E0F
HARPY_RUN, HARPY_TALON, HARPY_FLAP = 0x6F38, 0x751F, 0x72A6
SOUNDS = 0xC203   # op BC's newest entry, 8 bytes on per sound
TARGETS = {1: (0xDB, 0x43), 3: (0xEB, 0x43)}   # 0E:4342's scripts for the monkey and the harpy


def frames(g, buttons, count):
    """Hold the buttons; her object after each frame."""
    g.c.command("set_input", buttons=buttons)
    out = []
    for _ in range(count):
        step(g.c, 1)
        s = g.slot()
        out.append(dict(x=int.from_bytes(s[0x33:0x36], "little"), y=word(s, 0x37), routine=word(s, 0x1B),
                        speed=int.from_bytes(s[0x41:0x44], "little"), height=word(s, 0x4D), slot=s))
    return out


def steps(f, before):
    xs = [before] + [x["x"] for x in f]
    return [b - a for a, b in zip(xs, xs[1:])]


def where(g):
    return int.from_bytes(g.slot()[0x33:0x36], "little")


def routines(f):
    out = []
    for x in f:
        if not out or out[-1] != x["routine"]:
            out.append(x["routine"])
    return out


def transform(g, form):
    """Select, then the match 0E:42D2 makes, so that any build transforms the same way."""
    frames(g, "T", 1)
    frames(g, "-", 3)
    lo, hi = TARGETS[form]
    g.c.command("poke", addr="0xcb6d", hex=bytes([lo, hi, 0x0E]).hex())
    g.c.command("poke", addr="0xcb81", hex=f"{form:02x}")
    me = word(g.mem(0xCA13, 2), 0)
    g.c.command("poke", addr=hex(me + 0x16), hex="8000", wram_bank=3)
    g.c.command("poke", addr=hex(me + 0x18), hex="ff", wram_bank=3)
    g.c.command("poke", addr=hex(me + 0x02), hex="f9410eff", wram_bank=3)
    frames(g, "-", 130)   # the flash, the entrance
    assert g.mem(0xCB72, 1)[0] == form


def check_attack(off, slide, cancel, load, attack, run, length, name, way):
    """way: "R" or "L" (the tinkerbat's ledge ends a little to her right)."""
    buttons, runs = "B" + way, 0x200 if way == "R" else -0x201   # left speed is the complement, $FFFDFF
    for g in (off, slide, cancel):
        load(g)
    # The original's tinkerbat walks for two frames between the sword and the run.
    at = where(off)
    f = frames(off, buttons, 30)
    assert steps(f, at)[:length] == [0] * length and routines(f)[0] == attack and routines(f)[-1] == run, \
        (name, "the original", routines(f), steps(f, at))
    at = where(slide)
    f = frames(slide, buttons, 30)
    assert steps(f, at) == [runs] * 30 and routines(f) == [attack, run], (name, "slide", steps(f, at), routines(f))
    at = where(cancel)
    f = frames(cancel, buttons, 30)
    assert steps(f, at) == [runs] * 30 and attack not in routines(f), (name, "cancel", steps(f, at), routines(f))
    print(f"PASS: the {name}, B and a direction from standing. Original: {length} frames of attack on the spot, then "
          "the run. Slide: 2 pixels on every frame from the press, through the attack into the run. Cancel: the run "
          "at once")


def check_walk_attack(off, slide, cancel, load, attack, run, walk, name, way):
    """B pressed while walking: an attack, not the run (v0.1.11 ran at once)."""
    walks, runs = (0x100, 0x200) if way == "R" else (-0x101, -0x201)

    def walk_then(g, plan):
        """Short of the monkey's pit wall (about 64 pixels on)."""
        load(g)
        frames(g, way, 4)
        assert routines(frames(g, way, 1)) == [walk], (name, g.name, "not walking")
        at = where(g)
        f = []
        for buttons, count in plan:
            f += frames(g, buttons, count)
        return f, steps(f, at)

    f, s = walk_then(off, [("B" + way, 26)])
    on_spot = [d for x, d in zip(f[1:], s[1:]) if x["routine"] == attack]
    assert routines(f)[0] == attack and run in routines(f) and len(on_spot) > 8 and not any(on_spot), \
        (name, "the original", routines(f), s)
    # The press tick is the walk's step; the attack moves from the next.
    f, s = walk_then(slide, [("B" + way, 26)])
    assert routines(f) == [attack, run] and s == [walks] + [runs] * 25, (name, "slide", routines(f), s)
    f, s = walk_then(slide, [("B" + way, 1), (way, 23)])
    assert routines(f)[:2] == [attack, walk] and s == [walks] * 24, (name, "slide, B tapped", routines(f), s)
    f, s = walk_then(cancel, [("B" + way, 26)])
    assert attack not in routines(f) and s == [runs] * 26, (name, "cancel", routines(f), s)
    print(f"PASS: the {name}, B pressed while walking. Original: the attack on the spot. Slide: the attack, moving 2 "
          "pixels a frame with B held into the run, 1 with B tapped (v0.1.11 ran at once, no attack). Cancel: the run")


def check_jump(off, on, load, name, jump):
    # Run and walk speed less the air's drag: $1F9 and $F9 a frame.
    for g, held in ((off, 0xF9), (on, 0x1F9)):
        load(g)
        frames(g, "B", 30)
        frames(g, "AB", 2)
        at = where(g)
        f = frames(g, "ABL", 8)
        assert jump in routines(f) and steps(f, at)[2:] == [-held] * 6, (name, g.name, steps(f, at))
        at = where(g)
        f = frames(g, "AL", 6)
        assert steps(f, at)[1:] == [-0xF9] * 5, (name, g.name, "B released", steps(f, at))
    print(f"PASS: the {name}'s standing jump with B held: 2 pixels a frame (the original 1), 1 when B is released")


def check_harpy(off, on, state):
    for g, keeps in ((off, False), (on, True)):
        g.load(state)
        transform(g, 3)
        f = frames(g, "R", 40)
        before = f[-1]["speed"]
        assert routines(f)[-1] == HARPY_RUN and before > 0x100, (g.name, hex(before))
        f = frames(g, "BR", 1) + frames(g, "R", 30)
        assert HARPY_TALON in routines(f) and routines(f)[-1] == HARPY_RUN
        after = next(x["speed"] for x in f if x["routine"] == HARPY_RUN)
        assert (after >= before) == keeps and (keeps or after <= 0x10), (g.name, hex(before), hex(after))
    print("PASS: the harpy's talon in her run. Original: the run starts again from nothing. With Transformations: "
          "she goes on at the speed she had")


def check_harpy_flap(off, on, state):
    """Flapping, then a talon in the air with five A presses during it."""
    for g, flaps in ((off, False), (on, True)):
        g.load(state)
        transform(g, 3)
        for _ in range(6):
            frames(g, "A", 1)
            frames(g, "-", 5)
        assert routines(frames(g, "-", 1)) == [HARPY_FLAP], (g.name, "not flying")
        top, sounds = g.slot()[0x37] | g.slot()[0x38] << 8, g.mem(SOUNDS, 1)[0]
        f = frames(g, "B", 1)
        for _ in range(5):
            f += frames(g, "A", 1) + frames(g, "-", 3)
        talon = [x for x in f if x["routine"] == HARPY_TALON]
        assert talon == f[:len(talon)] and len(talon) == 20 and any(x["slot"][0x18] for x in talon), \
            (g.name, "the talon did not play out", routines(f))
        # The talon's own sound, and with the change one more for each press.
        queued = (g.mem(SOUNDS, 1)[0] - sounds) % 0x80 // 8
        end = talon[-1]["y"]
        if flaps:
            assert end < top - 20 and queued == 6, (g.name, top, end, queued)
        else:
            assert end > top and queued == 1, (g.name, top, end, queued)
    print("PASS: the harpy's talon while flying, A pressed during it. Original: the presses are lost and she sinks. "
          "With Transformations: each press flaps, with its sound, and she rises; the talon plays out to its hit")


def check_squeeze(off, on, state):
    def climb_down(g):
        g.load(state)
        frames(g, "BR", 27)
        frames(g, "BL", 30)
        return frames(g, "BDL", 40)

    f = climb_down(off)
    assert all(x["x"] >> 8 >= 0x1492 - 2 for x in f), "the original went into the passage"
    f = climb_down(on)
    inside = [x for x in f if x["y"] == 1400 and x["x"] >> 8 < 0x1492]
    assert inside and min(x["x"] >> 8 for x in f) < 0x1470 and TINK_RUN in routines(f), \
        ("she did not go into the passage", [(x["x"] >> 8, x["y"]) for x in f[::5]])
    # Into the passage and on into the shaft, then up its far wall (about 50
    # pixels at a pixel a frame) into the passage's far side.
    on.load(state)
    frames(on, "BR", 27)
    frames(on, "BL", 30)
    frames(on, "BDL", 20)
    frames(on, "BL", 24)
    f = frames(on, "BUL", 60) + frames(on, "BL", 20)
    # Her box (x + 22 to x + 32) past the shaft, then in the alcove (5190-5226).
    far = [x for x in f if x["y"] == 1400 and x["x"] >> 8 < 0x145A]
    assert far and min(x["x"] >> 8 for x in f) < 5200, \
        ("she did not reach the alcove", [(x["x"] >> 8, x["y"], hex(x["routine"])) for x in f[::6]])
    print("PASS: the tinkerbat squeezes: climbing down the wall with B, Down and Left she steps into the hidden "
          "passage (16 pixels tall) and runs through it; from the shaft, climbing its far wall, she reaches the "
          "alcove. The original climbs past")


TINK_ROUTE = [("-", 4), ("BR", 30), ("R", 10), ("-", 10), ("B", 20), ("AB", 2), ("ABL", 20), ("-", 20),
              ("BL", 40), ("BR", 27), ("BL", 30), ("BDL", 80), ("-", 30)]
MONKEY_ROUTE = [("-", 4), ("BR", 30), ("R", 10), ("-", 10), ("B", 20), ("AB", 2), ("ABL", 20), ("-", 20),
                ("BL", 40), ("R", 12), ("BR", 30), ("-", 20)]
HARPY_ROUTE = [("-", 4)] + [("A", 1), ("-", 5)] * 4 + [("B", 1)] + [("A", 1), ("-", 3)] * 5 + \
              [("-", 40), ("R", 40), ("BR", 1), ("R", 4), ("AR", 1), ("R", 40)]


def check_original(off, old, tink_state, dance_state):
    total = 0
    for name, state, form, route in (("tinkerbat", tink_state, None, TINK_ROUTE),
                                      ("monkey", dance_state, 1, MONKEY_ROUTE),
                                      ("harpy", dance_state, 3, HARPY_ROUTE)):
        for g in (off, old):
            g.load(state)
            if form:
                transform(g, form)
        for buttons, count in route:
            for a, b in zip(frames(off, buttons, count), frames(old, buttons, count)):
                assert a["slot"] == b["slot"], f"{name}, frame {total} ({buttons}): off differs from the previous release"
                total += 1
    print(f"PASS: with Transformations off, {total} frames of the tinkerbat's, the monkey's and the harpy's attacks, "
          "runs, jumps, flaps and climbs match the previous release's, byte for byte of her object")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dance-state", type=Path, default=ROOT / "logs/states/dance.state")
    ap.add_argument("--tinkerbat-state", type=Path, default=ROOT / "logs/states/tinkerbat-gap.state")
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--original", type=Path, help="an exe from before the feature, to compare the feature off with")
    ap.add_argument("--port", type=int, default=14640)
    args = ap.parse_args()
    exe = args.exe.resolve()
    with tempfile.TemporaryDirectory(prefix="shantae-forms-", ignore_cleanup_errors=True) as tmp:
        games = []

        def game(name, exe=exe, **settings):
            # Easier dancing off: the transformations here come from the same
            # pokes in every build, and no blinker touches her object.
            games.append(Game(tmp, name, exe, args.port + len(games), easy_dance=0, **settings))
            return games[-1]
        try:
            off = game("off", smooth_forms=0)
            slide = game("slide", whip_moving=1)
            cancel = game("cancel", whip_moving=2)

            def monkey(g):
                g.load(args.dance_state)
                transform(g, 1)

            def tinkerbat(g):
                g.load(args.tinkerbat_state)
                frames(g, "-", 4)

            check_attack(off, slide, cancel, monkey, MONKEY_CLAW, MONKEY_RUN, 16, "monkey", "R")
            check_attack(off, slide, cancel, tinkerbat, SWORD, TINK_RUN, 13, "tinkerbat", "L")
            check_walk_attack(off, slide, cancel, monkey, MONKEY_CLAW, MONKEY_RUN, MONKEY_WALK, "monkey", "R")
            check_walk_attack(off, slide, cancel, tinkerbat, SWORD, TINK_RUN, TINK_WALK, "tinkerbat", "L")
            check_jump(off, slide, monkey, "monkey", MONKEY_JUMP)
            check_jump(off, slide, tinkerbat, "tinkerbat", TINK_FALL)
            check_harpy(off, slide, args.dance_state)
            check_harpy_flap(off, slide, args.dance_state)
            check_squeeze(off, slide, args.tinkerbat_state)
            if args.original:
                # v0.1.11 and later have Transformations, on by default.
                check_original(off, game("previous release", exe=args.original.resolve(), smooth_forms=0),
                               args.tinkerbat_state, args.dance_state)
            else:
                print("SKIP: Transformations off against the previous release (no --original exe given)")
        finally:
            for g in games:
                g.close()


if __name__ == "__main__":
    main()
