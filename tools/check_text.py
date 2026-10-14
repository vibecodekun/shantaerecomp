"""Instant text (dialogue.c) against holding B, the game's own fast text.

Debug scene 3C opens on Shantae's remark as she lands: a text of many boxes
whose lines break by themselves, each full box waiting for B ($FE) on the way
to its end ($FF). From the debug grid (logs/states/debug-grid.state, made by
tools/make_states.py) the scene is loaded in three instances, each run to the
same frame of the text's first box:

- held: B is held until the text waits at its end (C3AB = 1, the speaker's
  script in 01:57E7-580F): the original's fastest way, a character a frame,
  the boxes waiting for B passed over;
- instant: A is pressed for one frame there;
- paused: the text is left to print to its first wait for B, and A is pressed
  there.

The instant and paused runs must be at the end one frame after the A, with the
box's map (D800-D89F and DC00-DC9F in WRAM bank 1) and the text's state
(C3A6-C3B0) as the held run has them at its end, and Shantae where she stands
in all three. Then each lets go for 30 frames, presses B and plays 240 frames
more: what the speaker's script does after its text must match the held run's
frame for frame (its script position, C3A6-C3B0, and the box's map while the
script is in the text; once it is done the window is the status bar's, whose
animations are at another point 280 frames later). With the option off, A at
the first box must do nothing to the text.

Isolated settings and saves; the grid state is not modified.

    python tools/check_text.py
"""
import argparse
from pathlib import Path
import sys
import tempfile
import threading

from check_moveset import Game, word
from view_state_repro import step
from view_probe import ROOT

SCENE = 0x3C
TEXT_LOOPS = {0x01: (0x55F6, 0x5820), 0x1A: (0x765E, 0x77B3)}
END_WAIT = (0x57E7, 0x5810)   # 01: blinking at $FE or $FF
FIRST_WAIT = (0x57DD, 0x5810)


def speaker(g):
    """(slot address, bank, script pc) of the slot in a text loop, or None."""
    raw = g.mem(0xD000, 32 * 126, wram_bank=3)
    for i in range(32):
        s = raw[i * 126:(i + 1) * 126]
        lo, hi = TEXT_LOOPS.get(s[4], (0, 0))
        if s[0] != 0xFF and lo <= word(s, 2) < hi:
            return 0xD000 + i * 126, s[4], word(s, 2)
    return None


def state(g, slot=None):
    """What the text has shown and where it is; the slot's script position."""
    box = g.mem(0xD800, 0xA0, wram_bank=1) + g.mem(0xDC00, 0xA0, wram_bank=1)
    text = g.mem(0xC3A6, 11)
    pc = None
    if slot:
        s = g.mem(slot, 6, wram_bank=3)
        pc = (s[0], s[4], word(s, 2))
    me = g.slot()
    return dict(box=box, text=text, pc=pc, at=(word(me, 0x34), word(me, 0x37)))


def in_loop(pc):
    """Whether a slot (status, bank, script pc) is live in a text loop."""
    lo, hi = TEXT_LOOPS.get(pc[1], (0, 0)) if pc else (0, 0)
    return pc and pc[0] != 0xFF and lo <= pc[2] < hi


def differs(a, b):
    return a["pc"] != b["pc"] or a["text"] != b["text"] or (in_loop(a["pc"]) and a["box"] != b["box"])


def at_end(g):
    sp = speaker(g)
    return sp and g.mem(0xC3AB, 1)[0] == 1 and sp[1] == 0x01 and END_WAIT[0] <= sp[2] < END_WAIT[1]


def start(g):
    """The scene from the grid, to the fourth character of its first box."""
    g.c.command("set_input", buttons="-")
    g.c.command("load_state", path=(ROOT / "logs/states/debug-grid.state").resolve().as_posix())
    step(g.c, 2)
    g.c.command("poke", addr="0xcbfc", hex=f"01{SCENE:02x}")
    for _ in range(600):
        step(g.c, 1)
        sp = speaker(g)
        if sp and g.mem(0xC3A6, 1)[0] >= 4:
            return sp[0]
    raise AssertionError("the remark never started")


def follow(g, slot, frames):
    """Let go 30 frames, a new B, then `frames` more: the state after each."""
    g.c.command("set_input", buttons="-")
    step(g.c, 30)
    g.c.command("set_input", buttons="B")
    step(g.c, 2)
    g.c.command("set_input", buttons="-")
    out = []
    for _ in range(frames):
        step(g.c, 1)
        out.append(state(g, slot))
    return out


def run(args, mode, port, result):
    with tempfile.TemporaryDirectory(prefix="shantae-text-", ignore_cleanup_errors=True) as tmp:
        g = Game(tmp, mode, args.exe.resolve(), port, instant_text=0 if mode == "off" else 1)
        try:
            slot = start(g)
            before = state(g, slot)
            r = dict(start=before)
            if mode == "held":
                g.c.command("set_input", buttons="B")
                for n in range(1, 20000):
                    step(g.c, 1)
                    if at_end(g):
                        break
                assert at_end(g), "holding B never reached the end"
                r.update(frames=n, end=state(g, slot))
            elif mode == "off":
                g.c.command("set_input", buttons="A")
                step(g.c, 1)
                g.c.command("set_input", buttons="-")
                step(g.c, 1)
                r.update(end=state(g, slot), ended=bool(at_end(g)))
            else:
                if mode == "paused":
                    for _ in range(2000):
                        step(g.c, 1)
                        sp = speaker(g)
                        if sp and FIRST_WAIT[0] <= sp[2] < FIRST_WAIT[1]:
                            break
                    assert sp and FIRST_WAIT[0] <= sp[2] < FIRST_WAIT[1], ("no wait for B", sp)
                    step(g.c, 3)
                    r.update(waiting=state(g, slot))
                g.c.command("set_input", buttons="A")
                step(g.c, 1)
                r.update(frames=1, ended=bool(at_end(g)), end=state(g, slot))
            if mode != "off":
                r.update(after=follow(g, slot, args.frames))
            result[mode] = r
        finally:
            g.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--frames", type=int, default=240)
    ap.add_argument("--port", type=int, default=14720)
    args = ap.parse_args()
    result = {}
    threads = [threading.Thread(target=run, args=(args, mode, args.port + i, result))
               for i, mode in enumerate(("held", "instant", "paused", "off"))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    missing = [m for m in ("held", "instant", "paused", "off") if m not in result]
    if missing:
        sys.exit(f"FAILED: {', '.join(missing)} did not finish (see above)")
    held, failures = result["held"], []
    print(f"held: B for {held['frames']} frames to the end of the text")
    for mode in ("instant", "paused"):
        r = result[mode]
        if not r["ended"]:
            failures.append(f"{mode}: not at the end one frame after A ({r['end']['pc']})")
            continue
        for key in ("box", "text"):
            if r["end"][key] != held["end"][key]:
                failures.append(f"{mode}: {key} differs at the end")
        if r["end"]["at"] != held["start"]["at"] or held["end"]["at"] != held["start"]["at"]:
            failures.append(f"{mode}: Shantae moved ({held['start']['at']} -> {r['end']['at']})")
        diff = next((i for i, (a, b) in enumerate(zip(held["after"], r["after"])) if differs(a, b)), None)
        if diff is not None:
            keys = [k for k in held["after"][diff] if held["after"][diff][k] != r["after"][diff][k]]
            failures.append(f"{mode}: after B, frame {diff} differs ({', '.join(keys)})")
            continue
        texted = sum(in_loop(a["pc"]) for a in held["after"])
        extra = f", from its first wait for B" if mode == "paused" else ""
        print(f"{mode}: A at the end in 1 frame{extra}; the box and the text as held; "
              f"{len(r['after'])} frames after B the same ({texted} of them in the text)")
    off = result["off"]
    column, row = off["end"]["text"][0] - off["start"]["text"][0], off["end"]["text"][1] - off["start"]["text"][1]
    if off["ended"] or row or column > 1:
        failures.append(f"off: A changed the text (column +{column}, row +{row})")
    else:
        print("off: A leaves the text as it was")
    if failures:
        sys.exit("FAILED:\n  " + "\n  ".join(failures))
    print("PASS")


if __name__ == "__main__":
    main()
