"""The transformations with "Smoother movement" (forms.c), beside the original.

Four states, made by tools/make_states.py from a debug game (every dance
learned): logs/states/dance.state (Shantae in a desert pit at 400,1984),
turned into the monkey, the elephant, the harpy and the tinkerbat with a quick
dance; logs/states/tinkerbat-gap.state (the tinkerbat on a ledge of the ice
tower at 5245,1176, where the user's state2 on 2026-10-06 stood), where right
of the ledge a wall drops to a hidden passage 16 pixels tall (y 1432-1447)
that runs left through the wall, across a shaft 16 pixels wide (x 5240-5255)
with no floor, to the alcove of a warp squid; logs/states/pillar.state (the
tinkerbat on the beach of the user's state1 of 2026-10-10, left of the orange
pillar at 2248,1912, with the pipe maze above the wall at the beach's end);
and logs/states/web.state (Shantae in front of the web wall of the user's
state3), turned into the spider. Every part plays the same buttons with
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
- The tinkerbat has the monkey's box: climbing down the wall with B, Down and
  Left she steps into the passage and runs through it; from the shaft,
  climbing its far wall with B, Up and Left, she steps into the passage's far
  side and reaches the alcove. The original climbs past both.
- The same in the pipe maze (the user's report on 2026-10-10): from the beach
  she climbs its end wall into the pipe above it (16 pixels tall) and walks
  in, and from that pipe's end up its 16-pixel shaft into the next pipe. The
  original climbs past the first and hits its head in the second.
- The tinkerbat against the pillar (the same report): walking into it, Down
  and B are the charge, and two charges break it. The original's walk takes B
  as the sword and the pillar stands.
- The elephant's and the tinkerbat's charges: B (Down and B) during one
  charges again at full speed, and A jumps. The original ignores both.
- Climbing with B held, the monkey and the tinkerbat: two pixels a frame
  (the original one).
- The monkey's air claw, after B was let go for a frame to press it: run
  speed with B held, walk speed without (the original keeps the speed it was
  pressed at, walk speed with Air speed).
- The spider: two pixels a frame with B held (the original one); a shot on
  the move keeps her moving, and its web still flies (the original stands for
  the shot); on the web two pixels a frame with B held.
- The elephant (the user's report on 2026-10-13): B during her turn charges
  the way she turns and A jumps; B with the other way held, from standing or
  during a charge, charges that way; B on the landing frames charges; and on
  the frame after a stomp's hit a direction walks, B charges and A jumps. The
  original ignores the turn's and the landing's presses, charges the way she
  faced and holds the stomp for 40 frames. The tinkerbat's Down and B with
  the other way held charges that way too.
- Webs (the same report), the spider and the tinkerbat: a jump in front of
  the web is a jump, A in the air takes hold, and A on the web jumps off it,
  with A again taking hold; the tinkerbat climbs two pixels a frame with B
  held. The original takes hold as the jump starts and ignores A on the web.
- The spider's shot in the expanded view (426x240): it flies while it is in
  the picture, to the room's left edge; in the original view it lives its 30
  frames, as without the view.
- --original <exe> (the previous release's): with Transformations off, the
  monkey's, the elephant's, the harpy's, the spider's and the tinkerbat's
  routes give the same object, byte for byte.

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
AIR_CLAW, MONKEY_CLIMB = 0x4ED7, 0x4ABE
ELEPHANT_CHARGE, ELEPHANT_JUMP, TINK_CHARGE = 0x650E, 0x610B, 0x64CA
ELEPHANT_TURN, ELEPHANT_LANDING, ELEPHANT_STOMP, ELEPHANT_WALK = 0x5F1E, 0x61D8, 0x62D8, 0x5DB9
SPIDER_SHOT, SPIDER_WEB, SPIDER_JUMP, TINK_WEB = 0x4CA0, 0x6068, 0x5105, 0x6113
SPIDER_SHOT_FLYING = (0x1A, 0x78DD)   # the shot's routine
GROWN_SLOTS = [0xD000 + i * 126 if i < 93 else 0xA000 + (i - 93) * 126 for i in range(157)]
SOUNDS = 0xC203   # op BC's newest entry, 8 bytes on per sound
# 0E:4342's scripts: the monkey, the elephant, the harpy, the spider, the tinkerbat.
TARGETS = {1: (0xDB, 0x43), 2: (0xE3, 0x43), 3: (0xEB, 0x43), 4: (0xF3, 0x43), 5: (0xFB, 0x43)}
PILLAR = (2248, 1912)


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


def put(g, x, y):
    """Her position poked: she falls to the ground below."""
    me = word(g.mem(0xCA13, 2), 0)
    g.c.command("poke", addr=hex(me + 0x33), hex=bytes([0, x & 0xFF, x >> 8, 0, y & 0xFF, y >> 8]).hex(),
                wram_bank=3)
    frames(g, "-", 30)


def live_objects(g):
    """The slots in use: (index, slot bytes)."""
    raw = g.mem(0xD000, 32 * 126, wram_bank=3)
    return [(i, raw[i * 126:(i + 1) * 126]) for i in range(32) if raw[i * 126] != 0xFF]


def grown_objects(g):
    """The live slots of the expanded view's table (object_slots.c): (index, slot bytes)."""
    raw = g.mem(0xD000, 4096, wram_bank=3) + g.mem(0xE000, 93 * 126 - 4096, wram_bank=3) + \
        g.mem(0xA000, 64 * 126, wram_bank=3)
    return [(i, raw[i * 126:(i + 1) * 126]) for i in range(len(GROWN_SLOTS)) if raw[i * 126] != 0xFF]


def pillar_routine(g, index):
    s = dict(live_objects(g)).get(index)
    return (s[0x19], word(s, 0x1B)) if s else None


def check_pillar(off, on, state):
    """Walking into the pillar, Down and B (the user's report on 2026-10-10)."""
    for g, breaks in ((off, False), (on, True)):
        g.load(state)
        index = next(i for i, s in live_objects(g) if (word(s, 0x34), word(s, 0x37)) == PILLAR and s[0x19] == 0x16)
        assert pillar_routine(g, index) == (0x16, 0x5966), (g.name, "the pillar", pillar_routine(g, index))
        f = frames(g, "R", 40)
        assert routines(f)[-1] == TINK_WALK, (g.name, "not walking into the pillar")
        for _ in range(2):
            f += frames(g, "DR", 2) + frames(g, "DRB", 1) + frames(g, "DR", 25)
        broken = pillar_routine(g, index) != (0x16, 0x5966)
        if breaks:
            assert TINK_CHARGE in routines(f) and SWORD not in routines(f) and broken, \
                (g.name, [hex(r) for r in routines(f)], pillar_routine(g, index))
            f = frames(g, "R", 240)   # it crumbles for 160 frames (script 16:59AE), then she walks on
            assert f[-1]["x"] >> 8 > PILLAR[0] + 40, (g.name, "not past the pillar", f[-1]["x"] >> 8)
        else:
            assert SWORD in routines(f) and TINK_CHARGE not in routines(f) and not broken, \
                (g.name, [hex(r) for r in routines(f)], pillar_routine(g, index))
    print("PASS: the tinkerbat walking into the pillar, Down and B. Original: the sword, and the pillar stands. "
          "With Transformations: the charge, two of them break it and she walks on")


def check_recharge(off, on, state):
    """B during the elephant's charge, Down and B during the tinkerbat's, then A."""
    for form, name, charge, press, jump in ((2, "elephant", ELEPHANT_CHARGE, "B", ELEPHANT_JUMP),
                                            (5, "tinkerbat", TINK_CHARGE, "DB", TINK_FALL)):
        for g, again in ((off, False), (on, True)):
            g.load(state)
            transform(g, form)
            frames(g, "-", 60)   # the entrance
            frames(g, "L", 1)
            frames(g, "-", 40)   # facing left, 60 pixels from the pit's wall
            at = where(g)
            f = frames(g, press, 1) + frames(g, "-", 7)
            first = steps(f, at)
            assert routines(f) == [charge] and first[0] < -0x200, (name, g.name, [hex(r) for r in routines(f)], first)
            at = where(g)
            f = frames(g, press, 1) + frames(g, "-", 3)
            second = steps(f, at)
            assert routines(f) == [charge], (name, g.name, "the second press", [hex(r) for r in routines(f)])
            # A charge slows by $08 a frame: again, it is back at its first speed.
            assert (second[1] <= first[0] + 0x10) == again and (second[1] > first[-1]) != again, \
                (name, g.name, first, second)
            top = g.slot()[0x37] | g.slot()[0x38] << 8
            f = frames(g, "A", 1) + frames(g, "-", 6)
            if again:
                assert routines(f)[0] == jump and f[-1]["y"] < top - 8, (name, g.name, [hex(r) for r in routines(f)])
            else:
                assert jump not in routines(f) and f[-1]["y"] == top, (name, g.name, [hex(r) for r in routines(f)])
    print("PASS: the elephant's charge (B) and the tinkerbat's (Down and B): pressed again during it, a new "
          "charge at full speed; A jumps out of it. Original: both ignored")


def check_pipes(off, on, state):
    """The tinkerbat into the pipe maze above the beach's end (the user's report on 2026-10-10)."""
    for g, fits in ((off, False), (on, True)):
        g.load(state)
        put(g, 2460, 1930)
        f = frames(g, "R", 10) + frames(g, "UR", 50) + frames(g, "R", 20)
        inside = [x for x in f if x["y"] == 1904 and x["x"] >> 8 > 2470]
        assert TINK_CLIMB in routines(f) and bool(inside) == fits, \
            (g.name, "the pipe", [(x["x"] >> 8, x["y"]) for x in f[::5]])
        # From that pipe's end up its shaft (16 pixels wide) into the next.
        g.load(state)
        put(g, 2540, 1900)
        f = frames(g, "R", 10) + frames(g, "UR", 60)
        above = [x for x in f if x["y"] == 1872 and x["x"] >> 8 >= 2560]
        assert bool(above) == fits, (g.name, "the shaft", [(x["x"] >> 8, x["y"]) for x in f[::5]])
    print("PASS: the tinkerbat climbs into the pipe maze (16-pixel pipes) and up its shaft with the monkey's box. "
          "Original: she climbs past the pipe and hits her head in the shaft")


def check_climb_b(off, on, pillar_state):
    """Climbing the beach's end wall, the monkey and the tinkerbat, with B held."""
    for form, climb in ((5, TINK_CLIMB), (1, MONKEY_CLIMB)):
        for g, fast in ((off, False), (on, True)):
            g.load(pillar_state)
            if form != 5:
                frames(g, "T", 1)
                frames(g, "-", 120)   # turned back
                transform(g, form)
            put(g, 2460, 1930)
            frames(g, "R", 10)
            frames(g, "UR", 8)
            top = g.slot()[0x37] | g.slot()[0x38] << 8
            f = frames(g, "BUR", 8)
            assert routines(f) == [climb], (form, g.name, [hex(r) for r in routines(f)])
            up = top - f[-1]["y"]
            assert (up >= 13) == fast and up >= 5, (form, g.name, up)
            f = frames(g, "BDR", 8)
            down = f[-1]["y"] - (top - up)
            assert (down >= 13) == fast and down >= 5, (form, g.name, down)
    print("PASS: climbing with B held, the monkey and the tinkerbat: two pixels a frame up and down "
          "(the original one)")


def check_air_claw(off, on, state):
    """A running jump, B let go for a frame to press it: the monkey's air claw."""
    for g, steers in ((off, False), (on, True)):
        g.load(state)
        transform(g, 1)
        frames(g, "BR", 20)
        frames(g, "ABR", 3)
        frames(g, "BR", 4)
        frames(g, "R", 1)
        at = where(g)
        f = frames(g, "BR", 8)
        held = steps(f, at)[1:]
        at = where(g)
        f += frames(g, "R", 4)
        let_go = steps(f[8:], at)
        assert all(x["routine"] == AIR_CLAW for x in f), (g.name, [hex(r) for r in routines(f)])
        if steers:
            assert held == [0x200] * 7 and let_go == [0x100] * 4, (g.name, held, let_go)
        else:
            assert len(set(held + let_go)) == 1, (g.name, held, let_go)
    print("PASS: the monkey's air claw after a running jump: 2 pixels a frame with B held, 1 without. Original: "
          "the speed she pressed it at")


def check_spider(off, on, state):
    """The spider: B to run, a shot on the move, B on the web."""
    for g, smooth in ((off, False), (on, True)):
        g.load(state)
        transform(g, 4)
        frames(g, "-", 20)
        at = where(g)
        f = frames(g, "L", 6)
        assert steps(f, at)[1:] == [-0x101] * 5, (g.name, "walk", steps(f, at))
        before = {i for i, _ in live_objects(g)}
        at = where(g)
        f = frames(g, "BL", 14)
        s = steps(f, at)
        shot = [d for x, d in zip(f, s) if x["routine"] == SPIDER_SHOT]
        webs = [o for i, o in live_objects(g) if i not in before and o[4] == 0x1A]
        assert len(shot) >= 4 and webs, (g.name, "the shot", [hex(r) for r in routines(f)], len(webs))
        if smooth:
            assert s == [-0x201] * 14, (g.name, "the shot on the run", s)
        else:
            # The press's own step is the walk's.
            assert shot[1:] == [0] * (len(shot) - 1) and s[-1] == -0x101, (g.name, "the shot", s)
        frames(g, "-", 10)
        # A jumps; A again takes hold (the original took hold with the first).
        f = frames(g, "AU", 6) + frames(g, "U", 3) + frames(g, "AU", 1) + frames(g, "U", 2)
        assert routines(f)[-1] == SPIDER_WEB, (g.name, "not on the web", [hex(r) for r in routines(f)])
        top = f[-1]["y"]
        f = frames(g, "U", 6)
        assert top - f[-1]["y"] == 6, (g.name, "web", top, f[-1]["y"])
        f = frames(g, "BU", 6)
        assert top - 6 - f[-1]["y"] == (12 if smooth else 6), (g.name, "web with B", top, f[-1]["y"])
    print("PASS: the spider: 2 pixels a frame with B held, through a shot on the move whose web still flies, and "
          "on the web. Original: 1, and she stands for the shot")


def elephant(g, state):
    g.load(state)
    transform(g, 2)
    frames(g, "-", 60)   # the entrance; she faces right, 63 pixels from the pit's right wall


def check_elephant_turn(off, on, state):
    """B and A during the elephant's turn (the user's report on 2026-10-13)."""
    for g, takes in ((off, False), (on, True)):
        for press, move in (("LB", ELEPHANT_CHARGE), ("LA", ELEPHANT_JUMP)):
            elephant(g, state)
            at, top = where(g), word(g.slot(), 0x37)
            f = frames(g, "L", 3) + frames(g, press, 1) + frames(g, "L", 8)
            assert routines(f)[0] == ELEPHANT_TURN, (g.name, press, [hex(r) for r in routines(f)])
            if not takes:
                assert move not in routines(f), (g.name, press, [hex(r) for r in routines(f)])
                continue
            assert routines(f)[:2] == [ELEPHANT_TURN, move] and f[-1]["slot"][0x31] == 1, \
                (g.name, press, [hex(r) for r in routines(f)])
            if move == ELEPHANT_CHARGE:
                assert at - f[-1]["x"] > 0x1000, (g.name, "the charge, left", steps(f, at))
            else:
                assert f[-1]["y"] < top - 10, (g.name, "the jump", [x["y"] for x in f])
    print("PASS: the elephant's turn: B charges and A jumps the way it turns her. Original: both lost")


def check_charge_way(off, on, state):
    """B (Down and B) with the other way held, from standing and during a charge right."""
    for form, press, charge in ((2, "B", ELEPHANT_CHARGE), (5, "DB", TINK_CHARGE)):
        for g, turns in ((off, False), (on, True)):
            for start in ("standing", "charging"):
                g.load(state)
                transform(g, form)
                frames(g, "-", 60)
                if start == "charging":
                    frames(g, press, 1)
                    frames(g, "-", 6)
                at = where(g)
                f = frames(g, "L" + press, 1) + frames(g, "-", 6)
                assert routines(f) == [charge], (form, g.name, start, [hex(r) for r in routines(f)])
                left = f[-1]["x"] < at
                assert left == turns and f[-1]["slot"][0x31] == turns, (form, g.name, start, steps(f, at))
    print("PASS: the elephant's B and the tinkerbat's Down and B with Left held, from standing and during a charge "
          "right: a charge left. Original: right, the way she faced")


def check_landing_charge(off, on, state):
    """B on the frames of the elephant's landing (0D:61D8), which takes A and the directions."""
    for g, takes in ((off, False), (on, True)):
        for k in (0, 10, 25):
            elephant(g, state)
            f = frames(g, "A", 6)
            while f[-1]["routine"] != ELEPHANT_LANDING:
                assert len(f) < 90, (g.name, "no landing", [hex(r) for r in routines(f)])
                f += frames(g, "-", 1)
            f = frames(g, "-", k) + frames(g, "B", 1) + frames(g, "-", 4)
            assert (ELEPHANT_CHARGE in routines(f)) == takes, (g.name, k, [hex(r) for r in routines(f)])
    print("PASS: B on the elephant's landing frames (the first, the 11th, the 26th) charges. Original: lost")


def check_stomp(off, on, state):
    """A landing with Down held is the stomp: its hit, then 40 frames the original holds her for."""
    def stomp(g):
        elephant(g, state)
        f = frames(g, "A", 6)
        while f[-1]["routine"] != ELEPHANT_STOMP:
            assert len(f) < 90, (g.name, "no stomp", [hex(r) for r in routines(f)])
            f += frames(g, "D", 1)
        return f[-1]["x"]

    for g, free in ((off, False), (on, True)):
        stomp(g)
        f = frames(g, "D", 45)
        held = sum(x["routine"] == ELEPHANT_STOMP for x in f)
        assert held == 39 and f[-1]["routine"] != ELEPHANT_STOMP, (g.name, "the stomp alone", held)
        for press, move in (("DR", ELEPHANT_WALK), ("B", ELEPHANT_CHARGE), ("A", ELEPHANT_JUMP)):
            at = stomp(g)
            f = frames(g, press, 1) + frames(g, press if press == "DR" else "-", 4)
            if free:
                assert routines(f)[0] != ELEPHANT_STOMP and move in routines(f), \
                    (g.name, press, [hex(r) for r in routines(f)])
                assert move == ELEPHANT_JUMP or f[-1]["x"] > at, (g.name, press, steps(f, at))
            else:
                assert routines(f) == [ELEPHANT_STOMP], (g.name, press, [hex(r) for r in routines(f)])
    print("PASS: the elephant's stomp: on the frame after its hit a direction walks, B charges and A jumps; "
          "with nothing pressed it plays its 40 frames. Original: 40 frames whatever is pressed")


def check_webs(off, on, state):
    """A in front of a web, A in the air, A on it; B climbing (the user's report on 2026-10-13)."""
    for form, web, ground in ((4, SPIDER_WEB, 1400), (5, TINK_WEB, 1400)):
        for g, smooth in ((off, False), (on, True)):
            g.load(state)
            transform(g, form)
            frames(g, "-", 20)
            f = frames(g, "A", 12) + frames(g, "-", 50)
            if smooth:
                assert web not in routines(f) and min(x["y"] for x in f) < ground - 40 and f[-1]["y"] == ground, \
                    (form, g.name, "a jump", [hex(r) for r in routines(f)], [x["y"] for x in f[::6]])
                f = frames(g, "A", 6) + frames(g, "-", 3) + frames(g, "A", 1) + frames(g, "-", 4)
            assert routines(f)[-1] == web, (form, g.name, "taking hold", [hex(r) for r in routines(f)])
            top = frames(g, "U", 10)[-1]["y"]
            climbed = top - frames(g, "BU", 10)[-1]["y"]
            assert climbed == (20 if smooth else 10), (form, g.name, "B climbing", climbed)
            f = frames(g, "A", 1) + frames(g, "-", 60)
            if smooth:
                assert routines(f)[-1] != web and min(x["y"] for x in f) < top - 40 and f[-1]["y"] == ground, \
                    (form, g.name, "A on the web", [hex(r) for r in routines(f)], [x["y"] for x in f[::6]])
                f = frames(g, "U", 20) + frames(g, "A", 6) + frames(g, "-", 3) + frames(g, "A", 1) + frames(g, "-", 4)
                assert routines(f)[-1] == web, (form, g.name, "A again", [hex(r) for r in routines(f)])
            else:
                assert routines(f) == [web], (form, g.name, "A on the web", [hex(r) for r in routines(f)])
    print("PASS: webs, the spider and the tinkerbat: a jump in front of one is a jump, A in the air takes hold, A on "
          "it jumps off and A again takes hold; the tinkerbat climbs two pixels a frame with B held. Original: the "
          "jump takes hold, A on the web does nothing, and the tinkerbat climbs one pixel a frame")


def shot_flight(g, state, grown):
    """The spider walking left, B: the frames her shot flies and its last x."""
    g.load(state)
    transform(g, 4)
    frames(g, "-", 20)
    frames(g, "L", 2)
    frames(g, "BL", 1)
    frames(g, "L", 4)
    flown, last = 0, None
    for _ in range(200):
        shots = [s for _, s in (grown_objects(g) if grown else live_objects(g))
                 if (s[0x19], word(s, 0x1B)) == SPIDER_SHOT_FLYING]
        if not shots and flown:
            break
        if shots:
            flown += 1
            last = word(shots[0], 0x34)
        frames(g, "-", 1)
    return flown, last


def check_shot_view(plain, view, state):
    """The spider's shot left across the pit: its room's left edge is x 256."""
    flown, last = shot_flight(plain, state, False)
    assert 26 <= flown <= 30 and last > 300, ("the original view", flown, last)
    wide, far = shot_flight(view, state, True)
    info = view.c.command("shantae_view_info")
    for _ in range(info["count"]):
        view.c.read()
    assert wide > flown + 10 and far < 260 and info["shots_kept"] > 0, ("the expanded view", wide, far, info["shots_kept"])
    print(f"PASS: the spider's shot in the expanded view flies {wide} frames, to x {far} at the room's edge; in the "
          f"original view its {flown} frames, to x {last}")


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
    print("PASS: the tinkerbat with the monkey's box: climbing down the wall with B, Down and Left she steps into "
          "the hidden passage (16 pixels tall) and runs through it; from the shaft, climbing its far wall, she "
          "reaches the alcove. The original climbs past")


TINK_ROUTE = [("-", 4), ("BR", 30), ("R", 10), ("-", 10), ("B", 20), ("AB", 2), ("ABL", 20), ("-", 20),
              ("BL", 40), ("BR", 27), ("BL", 30), ("BDL", 80), ("-", 30)]
MONKEY_ROUTE = [("-", 4), ("BR", 30), ("R", 10), ("-", 10), ("B", 20), ("AB", 2), ("ABL", 20), ("-", 20),
                ("BL", 40), ("R", 12), ("BR", 30), ("-", 20), ("BL", 20), ("ABL", 3), ("BL", 4), ("L", 1),
                ("BL", 8), ("L", 4), ("-", 30)]
HARPY_ROUTE = [("-", 4)] + [("A", 1), ("-", 5)] * 4 + [("B", 1)] + [("A", 1), ("-", 3)] * 5 + \
              [("-", 40), ("R", 40), ("BR", 1), ("R", 4), ("AR", 1), ("R", 40)]
ELEPHANT_ROUTE = [("-", 4), ("L", 1), ("-", 40), ("B", 1), ("-", 6), ("B", 1), ("-", 4), ("A", 1), ("-", 40),
                  ("R", 30), ("B", 1), ("-", 6), ("A", 1), ("-", 40), ("L", 3), ("LB", 1), ("L", 12), ("R", 3),
                  ("RA", 1), ("R", 40), ("LB", 1), ("-", 6), ("RB", 1), ("-", 30), ("A", 6), ("-", 31), ("B", 1),
                  ("-", 40), ("A", 6), ("D", 33), ("DR", 1), ("B", 1), ("A", 1), ("D", 45)]
PILLAR_ROUTE = [("-", 4), ("R", 40)] + [("DR", 2), ("DRB", 1), ("DR", 25)] * 2 + \
               [("D", 2), ("DB", 1), ("-", 6), ("DB", 1), ("-", 4), ("A", 1), ("-", 40), ("L", 30), ("A", 1),
                ("-", 30), ("DB", 1), ("-", 30)]
SPIDER_ROUTE = [("-", 20), ("L", 6), ("BL", 14), ("L", 1), ("BL", 3), ("-", 10), ("AU", 8), ("U", 6), ("BU", 6),
                ("BL", 6), ("BD", 6), ("B", 1), ("-", 20), ("A", 1), ("-", 40), ("A", 6), ("-", 3), ("A", 1),
                ("-", 20)]
TINK_WEB_ROUTE = [("-", 20), ("A", 12), ("-", 40), ("U", 10), ("BU", 10), ("BL", 10), ("A", 1), ("-", 40), ("A", 6),
                  ("-", 3), ("A", 1), ("-", 20), ("DLB", 1), ("-", 20)]


def check_original(off, old, tink_state, dance_state, pillar_state, web_state):
    total = 0
    for name, state, form, route in (("tinkerbat", tink_state, None, TINK_ROUTE),
                                      ("monkey", dance_state, 1, MONKEY_ROUTE),
                                      ("elephant", dance_state, 2, ELEPHANT_ROUTE),
                                      ("harpy", dance_state, 3, HARPY_ROUTE),
                                      ("tinkerbat at the pillar", pillar_state, None, PILLAR_ROUTE),
                                      ("spider", web_state, 4, SPIDER_ROUTE),
                                      ("tinkerbat on the web", web_state, 5, TINK_WEB_ROUTE)):
        for g in (off, old):
            g.load(state)
            if form:
                transform(g, form)
        for buttons, count in route:
            for a, b in zip(frames(off, buttons, count), frames(old, buttons, count)):
                assert a["slot"] == b["slot"], f"{name}, frame {total} ({buttons}): off differs from the previous release"
                total += 1
    print(f"PASS: with Transformations off, {total} frames of the tinkerbat's, the monkey's, the elephant's, the "
          "harpy's and the spider's attacks, charges, turns, stomps, shots, runs, jumps, flaps and climbs, on webs "
          "too, match the previous release's, byte for byte of her object")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dance-state", type=Path, default=ROOT / "logs/states/dance.state")
    ap.add_argument("--tinkerbat-state", type=Path, default=ROOT / "logs/states/tinkerbat-gap.state")
    ap.add_argument("--pillar-state", type=Path, default=ROOT / "logs/states/pillar.state")
    ap.add_argument("--web-state", type=Path, default=ROOT / "logs/states/web.state")
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
            check_pipes(off, slide, args.pillar_state)
            check_pillar(off, slide, args.pillar_state)
            check_recharge(off, slide, args.dance_state)
            check_climb_b(off, slide, args.pillar_state)
            check_air_claw(off, slide, args.dance_state)
            check_spider(off, slide, args.web_state)
            check_elephant_turn(off, slide, args.dance_state)
            check_charge_way(off, slide, args.dance_state)
            check_landing_charge(off, slide, args.dance_state)
            check_stomp(off, slide, args.dance_state)
            check_webs(off, slide, args.web_state)
            view = game("expanded view", expanded_view=1, view_width=426, view_height=240, room_zoom=0)
            view.c.command("window", width=1278, height=720, scaling_mode=0)
            check_shot_view(slide, view, args.dance_state)
            if args.original:
                # v0.1.11 and later have Transformations, on by default.
                check_original(off, game("previous release", exe=args.original.resolve(), smooth_forms=0),
                               args.tinkerbat_state, args.dance_state, args.pillar_state, args.web_state)
            else:
                print("SKIP: Transformations off against the previous release (no --original exe given)")
        finally:
            for g in games:
                g.close()


if __name__ == "__main__":
    main()
