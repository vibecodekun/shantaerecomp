"""Shantae's moves with "Smoother movement" (moveset.c), beside the original.

The state (default logs/states/moveset.state, made by tools/make_states.py
from a debug game) stands in her base form at 393,1984 on the floor of a pit
in the desert: flat from 346 to 456, a wall at 338 on the left and at 463 on
the right, and above the left wall a platform at 1960 whose edge is at 339.

Every part plays the same buttons in the original view with the feature off
(the original moves) and on, a frame at a time, and reads her object: where
she is in 256ths of a pixel (+$33-+$35), her movement routine (+$1B), and the
whip's hit (CBA2 set for the tick it lands, CBA5 its kind: 0 standing,
1 crouched, 2 in the air).

- Off: holding B and a direction is a whip on the spot and then a run; a jump
  keeps the speed she left the ground with; a whip that lands stops her; with
  B a crawl stands up and runs. With --original (the previous release's exe)
  every frame of a route through all of these is the same in both.
- Slide: B + Right moves two pixels on every frame from the press, the hit
  lands on the twelfth, and the run follows without a frame at rest, with
  "Reduce input lag" on and off. Turning and jumping work during it.
- Cancel: B + Right is a run with no whip; B alone whips; a direction with B
  held ends the whip.
- Air speed: two pixels a frame while B is held, one when it is released.
- A whip begun in the air lands and carries on at two pixels a frame, and one
  begun on the platform slides off its edge and carries on in the air.
- Landing (the user's report on 2026-10-10): after a standing jump the landing
  routine 06:59E9 takes no B, so the original loses a press on the landing
  frame and the 24 after it. With the feature each of them whips, the same
  whip as from standing; with a direction, the slide whips and cancel runs.
- Crawl: half a pixel a frame; with B one pixel, crouched throughout, through
  the crouch whip and after it. With the crawl option off, the original's.
- Another form (--form-state, by default the tinkerbat of tinkerbat-gap.state,
  whose own run reads the same run flag): with Transformations off
  (smooth_forms=0; check_forms.py has them on), the feature on is the feature
  off.

Requires the saved state (python tools/make_states.py); never writes user saves or settings.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from view_probe import Debug, ROOT
from view_state_repro import step

IDLE, WALK, RUN, JUMP, LANDING = 0x4AA7, 0x502A, 0x6A8C, 0x5520, 0x59E9
WHIP, AIR_WHIP, CROUCH, CRAWL, CROUCH_WHIP = 0x5E52, 0x6CCD, 0x6094, 0x648B, 0x670D
WALKS, RUNS, CRAWLS = 256, 512, 128   # 256ths of a pixel a frame, to the right
SLIDE, CANCEL = 1, 2


def word(data, at):
    return int.from_bytes(data[at:at + 2], "little")


class Game:
    def __init__(self, tmp, name, exe, port, **settings):
        tmp = Path(tmp, name)
        tmp.mkdir()
        Path(tmp, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(tmp, "shantae.ini").write_text(
            "expanded_view=0\n" + "".join(f"{key}={value}\n" for key, value in settings.items()))
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
        self.name = name

    def close(self):
        try:
            fallback = self.c.command("interp_fallbacks")
            assert fallback["total_fallbacks"] == 0, (self.name, fallback)
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

    def mem(self, addr, count, **banks):
        return bytes.fromhex(self.c.peek(addr, count, **banks))

    def slot(self):
        return self.mem(word(self.mem(0xCA13, 2), 0), 126, wram_bank=3)

    def x(self):
        return word(self.slot(), 0x34)

    def play(self, buttons, frames):
        """Hold the buttons for the frames; what her object was after each."""
        self.c.command("set_input", buttons=buttons)
        out = []
        for _ in range(frames):
            step(self.c, 1)
            s, hit = self.slot(), self.mem(0xCBA2, 4)
            out.append(dict(x=int.from_bytes(s[0x33:0x36], "little"), y=word(s, 0x37), routine=word(s, 0x1B),
                            facing=s[0x31], stance=self.mem(0xCB54, 1)[0], hit=hit[0], kind=hit[3], slot=s))
        return out


def steps(frames, before):
    """How far she moved on each frame, from where she was before the first."""
    xs = [before] + [f["x"] for f in frames]
    return [b - a for a, b in zip(xs, xs[1:])]


def where(g):
    return int.from_bytes(g.slot()[0x33:0x36], "little")


def routines(frames):
    """The routines she went through, in order."""
    out = []
    for f in frames:
        if not out or out[-1] != f["routine"]:
            out.append(f["routine"])
    return out


def hits(frames):
    return [f["kind"] for f in frames if f["hit"]]


def check_run(off, slide, cancel, nolag, state):
    for g in (off, slide, cancel, nolag):
        g.load(state)
    at = where(off)
    f = off.play("BR", 30)
    assert steps(f, at)[:24] == [0] * 24 and all(x["routine"] == WHIP for x in f[:24]), "the original did not stop to whip"
    assert f[-1]["routine"] == RUN and hits(f) == [0], (routines(f), hits(f))

    f = slide.play("BR", 30)
    assert steps(f, at) == [RUNS] * 30, ("the slide", steps(f, at))
    assert routines(f) == [WHIP, RUN] and f[23]["routine"] == WHIP and f[24]["routine"] == RUN, routines(f)
    assert hits(f) == [0] and f[12]["hit"], "the slide's whip did not hit on its twelfth frame"
    # Turn during the next whip, then jump out of it.
    slide.play("R", 2)
    at = where(slide)
    f = slide.play("BL", 6)
    assert all(x["routine"] == WHIP and x["facing"] == 1 for x in f[1:]) and steps(f, at)[1:] == [-RUNS - 1] * 5, \
        ("turning in the slide", steps(f, at))
    at = where(slide)
    f = slide.play("ABL", 8)
    assert routines(f) == [JUMP] and steps(f, at) == [-RUNS - 1] * 8, ("the jump out of the slide", steps(f, at))

    at = where(nolag)
    f = nolag.play("BR", 30)
    assert steps(f, at) == [0] + [RUNS] * 29 and routines(f) == [WHIP, RUN] and hits(f) == [0], \
        ("the slide without Reduce input lag", steps(f, at), routines(f))

    at = where(cancel)
    f = cancel.play("BR", 30)
    assert steps(f, at) == [RUNS] * 30 and routines(f) == [RUN] and hits(f) == [], \
        ("cancel", steps(f, at), routines(f), hits(f))
    cancel.play("-", 30)
    at = where(cancel)
    f = cancel.play("B", 8)
    assert routines(f) == [WHIP] and steps(f, at) == [0] * 8, "cancel: B alone did not whip on the spot"
    f = cancel.play("BL", 6)
    assert routines(f) == [RUN] and steps(f, at) == [-RUNS - 1] * 6 and hits(f) == [], \
        ("cancel: a direction did not end the whip", routines(f), steps(f, at))
    print("PASS: B + Right. Original: 24 frames on the spot, then the run. Slide: 2 pixels on every frame from "
          "the press, the hit on frame 12, the run from frame 24 (with and without Reduce input lag), turning "
          "and jumping in it. Cancel: the run at once and no whip; B alone whips; a direction ends it")


def check_air(off, on, state):
    for g, held, released in ((off, WALKS, RUNS), (on, RUNS, WALKS)):
        # A standing jump with B already held.
        g.load(state)
        g.play("B", 30)
        g.play("AB", 2)
        at = where(g)
        f = g.play("ABL", 10)
        assert routines(f) == [JUMP] and steps(f, at) == [-held - 1] * 10, (g.name, "standing jump", steps(f, at))
        at = where(g)
        f = g.play("AL", 8)
        assert steps(f, at) == [-WALKS - 1] * 8, (g.name, "B released in a standing jump", steps(f, at))
        # A jump out of a run, B released in the air. From 369, so that the
        # slide's run-up ends short of the wall on the right.
        g.load(state)
        g.play("L", 24)
        g.play("-", 14)
        f = g.play("BR", 28)
        assert f[-1]["routine"] == RUN, (g.name, routines(f))
        assert g.play("ABR", 1)[0]["routine"] == JUMP, g.name
        at = where(g)
        f = g.play("AR", 6)
        assert steps(f, at) == [released] * 6, (g.name, "B released in a running jump", steps(f, at))
    print("PASS: air speed. Original: a standing jump 1 pixel a frame with B held, a running jump 2 with it "
          "released. With the feature: 2 while B is held and 1 when it is released, in both")


def land(g, state):
    """B held, a jump, a direction, and a whip that lands: the frames from the whip's press."""
    g.load(state)
    g.play("BL", 30)
    g.play("ABL", 30)
    g.play("L", 8)
    at = where(g)
    return g.play("BL", 30), at


def check_landing(off, slide, cancel, state):
    f, at = land(off, state)
    landed = next(i for i, x in enumerate(f) if x["routine"] == WHIP)
    assert routines(f)[:2] == [AIR_WHIP, WHIP] and steps(f, at)[landed + 1:landed + 4] == [0, 0, 0], \
        ("the original's whip did not stop her when it landed", routines(f), steps(f, at))
    f, at = land(slide, state)
    landed = next(i for i, x in enumerate(f) if x["routine"] == WHIP)
    assert routines(f) == [AIR_WHIP, WHIP, RUN] and steps(f, at)[1:] == [-RUNS - 1] * 29 and hits(f) == [2], \
        ("slide: the whip that landed", routines(f), steps(f, at), hits(f))
    # The run begins on the landing frame, which has its step as well as the
    # air whip's: "Reduce input lag" does that on any landing into a walk or run.
    f, at = land(cancel, state)
    moved = steps(f, at)[1:]
    assert routines(f) == [AIR_WHIP, RUN] and moved.count(-RUNS - 1) == 28 and moved.count(2 * (-RUNS - 1)) == 1, \
        ("cancel: the whip that landed", routines(f), steps(f, at))
    print(f"PASS: a whip begun in the air lands on frame {landed}. Original: it finishes on the spot. Slide: it "
          "finishes at 2 pixels a frame and the run follows. Cancel: the run from the landing")


def landing(g, state):
    """The frame a standing jump lands on, the first of 24 in the landing routine."""
    g.load(state)
    f = g.play("A", 6) + g.play("-", 80)
    land = next(i for i, x in enumerate(f) if x["routine"] == LANDING)
    assert routines(f[land:]) == [LANDING, IDLE] and sum(x["routine"] == LANDING for x in f) == 24, \
        (g.name, "the landing", [hex(r) for r in routines(f)])
    return land


def land_and_press(g, state, land, k, buttons):
    """A standing jump, the buttons pressed for one frame k frames after it lands: the frames from the press."""
    g.load(state)
    g.play("A", 6)
    g.play("-", land - 6 + k)
    return g.play(buttons, 1) + g.play("-", 30)


def check_land_whip(off, slide, cancel, nolag, state):
    """B pressed as she lands from a standing jump (the user's report on 2026-10-10). The landing
    routine runs on the landing frame's next 24 frames; the script puts her in the idle routine at
    the end of the last, so a press is lost on 25 frames."""
    land = landing(off, state)
    for k in range(25):
        f = land_and_press(off, state, land, k, "B")
        assert WHIP not in routines(f) and AIR_WHIP not in routines(f) and hits(f) == [], \
            ("the original whipped as she landed", k, [hex(r) for r in routines(f)])
    for g in (off, slide, cancel, nolag):
        assert landing(g, state) == land, g.name
        # Back in the idle routine, B whips: the hit is on this frame of it.
        ref = land_and_press(g, state, land, 25, "B")
        assert routines(ref)[0] == WHIP and hits(ref) == [0], (g.name, "the whip from standing")
        at = next(i for i, x in enumerate(ref) if x["hit"])
        if g is off:
            continue
        for k in range(25):
            f = land_and_press(g, state, land, k, "B")
            assert f[0]["routine"] == WHIP and hits(f) == [0] and f[at]["hit"] and steps(f, f[0]["x"])[1:] == [0] * 30, \
                (g.name, "B as she lands", k, [hex(r) for r in routines(f)], hits(f))
    # A direction pressed with B in the landing: the slide moves, cancel runs.
    f = land_and_press(slide, state, land, 5, "BR")
    assert f[0]["routine"] == WHIP and hits(f) == [0], ("slide: B and Right as she lands", routines(f))
    f = land_and_press(cancel, state, land, 5, "BR")
    assert WHIP not in routines(f) and hits(f) == [], ("cancel: B and Right as she lands", routines(f))
    print(f"PASS: B pressed on the frame a standing jump lands and on each of the 24 after it. Original: no whip "
          f"(the landing routine 06:59E9 takes no B). With the feature: the whip from that frame, its hit {at} frames "
          "on as from standing, with and without Reduce input lag; with a direction, the slide whips and cancel runs")


def check_ledge(slide, state):
    slide.load(state)
    slide.play("L", 60)    # to the wall under the platform
    slide.play("AL", 40)   # and up on to it
    slide.play("L", 40)
    for _ in range(200):
        if slide.x() >= 322:
            break
        slide.play("R", 1)
    slide.play("-", 12)
    start = slide.slot()
    assert word(start, 0x34) == 322 and word(start, 0x37) == 1960, (word(start, 0x34), word(start, 0x37))
    at = where(slide)
    f = slide.play("BR", 32)
    assert routines(f) == [WHIP, AIR_WHIP, WHIP, RUN], [hex(r) for r in routines(f)]
    assert steps(f, at) == [RUNS] * 32 and hits(f) == [2] and f[-1]["y"] == 1984, (steps(f, at), hits(f), f[-1]["y"])
    print("PASS: a slide off the platform's edge carries on as the air whip, hits in the air, lands and "
          "finishes on the ground into the run, 2 pixels on every frame")


def check_crawl(off, slide, cancel, plain, state):
    """`plain` has the feature on and the crawl option off."""
    for g in (off, slide, cancel, plain):
        g.load(state)
        g.play("D", 8)
        g.play("DL", 4)
        at = where(g)
        f = g.play("DL", 12)
        assert routines(f) == [CRAWL] and steps(f, at) == [-CRAWLS - 1] * 12, (g.name, "the crawl", steps(f, at))
    at = where(off)
    f = off.play("BDL", 40)
    assert routines(f) == [CROUCH_WHIP, CROUCH, RUN] and steps(f, at)[:24] == [0] * 24 and f[-1]["stance"] == 0, \
        ("the original did not stand up and run", routines(f))
    at = where(slide)
    f = slide.play("BDL", 40)
    assert routines(f) == [CROUCH_WHIP, CRAWL] and all(x["stance"] == 2 for x in f), routines(f)
    assert steps(f, at) == [-WALKS - 1] * 40 and hits(f) == [1], ("the crawl with B", steps(f, at), hits(f))
    at = where(slide)
    f = slide.play("DL", 6)
    assert steps(f, at) == [-CRAWLS - 1] * 6, ("B released in the crawl", steps(f, at))
    at = where(cancel)
    f = cancel.play("BDL", 30)
    assert routines(f) == [CRAWL] and steps(f, at) == [-WALKS - 1] * 30 and hits(f) == [], \
        ("cancel: the crawl with B", routines(f), steps(f, at), hits(f))
    at = where(plain)
    f = plain.play("BDL", 30)
    assert routines(f) == [CROUCH_WHIP, RUN] and steps(f, at)[:24] == [-CRAWLS - 1] * 24 and hits(f) == [1], \
        ("the crawl option off", routines(f), steps(f, at))
    print("PASS: the crawl is half a pixel a frame. Original with B: a whip on the spot, then she stands and "
          "runs. With the feature: 1 pixel on every frame, crouched throughout, through the crouch whip "
          "(slide) or without one (cancel). With the crawl option off: the slide at crawl speed, then the run")


ROUTE = [("-", 4), ("BR", 34), ("ABR", 20), ("R", 12), ("BR", 20), ("-", 14), ("D", 8), ("DL", 16),
         ("BDL", 34), ("L", 10), ("AL", 6), ("ABL", 30), ("BL", 30), ("B", 30), ("AB", 2), ("ABR", 24), ("-", 30)]


def check_original(off, old, state):
    """The feature off against the previous release: her whole object, every frame."""
    off.load(state)
    old.load(state)
    frame = 0
    for buttons, frames in ROUTE:
        for a, b in zip(off.play(buttons, frames), old.play(buttons, frames)):
            assert a["slot"] == b["slot"], f"frame {frame} ({buttons}): off differs from the previous release"
            frame += 1
    print(f"PASS: with the feature off, {frame} frames of whips, runs, jumps and crawls match the previous "
          "release's, byte for byte of her object")


def check_form(off, slide, state):
    """Another form (the tinkerbat of the picture puzzle's state): the feature on is the feature off."""
    off.load(state)
    slide.load(state)
    form = slide.mem(0xCB72, 1)[0]
    assert form, "the state is not in another form"
    frame, banks = 0, set()
    for buttons, frames in ROUTE:
        for a, b in zip(off.play(buttons, frames), slide.play(buttons, frames)):
            assert a["slot"] == b["slot"], f"frame {frame} ({buttons}): the feature changed form {form}"
            banks.add(a["slot"][0x19])
            frame += 1
    assert 6 not in banks, banks
    print(f"PASS: in form {form} (routines in bank {', '.join(f'{b:02X}' for b in sorted(banks))}) {frame} frames "
          "of the same route are the same with the feature on and off")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("state", type=Path, nargs="?", default=ROOT / "logs/states/moveset.state")
    ap.add_argument("--form-state", type=Path, default=ROOT / "logs/states/tinkerbat-gap.state",
                    help="a state in another form, which the feature must leave alone")
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--original", type=Path, help="an exe from before the feature, to compare the feature off with")
    ap.add_argument("--port", type=int, default=14400)
    args = ap.parse_args()
    exe = args.exe.resolve()
    with tempfile.TemporaryDirectory(prefix="shantae-moveset-", ignore_cleanup_errors=True) as tmp:
        games = []

        def game(name, exe=exe, **settings):
            games.append(Game(tmp, name, exe, args.port + len(games), **settings))
            return games[-1]
        try:
            off = game("off", smooth_moves=0)
            slide = game("slide", whip_moving=SLIDE)
            cancel = game("cancel", whip_moving=CANCEL)
            nolag = game("slide without Reduce input lag", whip_moving=SLIDE, reduce_input_lag=0)
            plain = game("crawl option off", whip_moving=SLIDE, fast_crawl=0)
            check_run(off, slide, cancel, nolag, args.state)
            check_air(off, slide, args.state)
            check_landing(off, slide, cancel, args.state)
            check_land_whip(off, slide, cancel, nolag, args.state)
            check_ledge(slide, args.state)
            check_crawl(off, slide, cancel, plain, args.state)
            if args.form_state.exists():
                check_form(off, game("slide, transformations off", whip_moving=SLIDE, smooth_forms=0), args.form_state)
            else:
                print(f"SKIP: another form (no {args.form_state.name})")
            if args.original:
                # Off there too: releases from v0.1.8 have the feature on by
                # default, and older ones ignore the setting.
                check_original(off, game("previous release", exe=args.original.resolve(), smooth_moves=0), args.state)
            else:
                print("SKIP: the feature off against the previous release (no --original exe given)")
        finally:
            for g in games:
                g.close()


if __name__ == "__main__":
    main()
