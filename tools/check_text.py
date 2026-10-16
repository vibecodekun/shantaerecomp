"""Text speed (dialogue.c) against holding B, the game's own fast text.

Debug scene 3C opens on Shantae's remark as she lands: a text of many boxes
whose lines break by themselves, each full box passed over with B held ($FE)
on the way to its end ($FF), the box scrolling up at each new line. From the
debug grid (logs/states/debug-grid.state, made by tools/make_states.py) the
scene is loaded in four instances, each run to the same frame of the text's
first box:

- held: text speed 0, B held until the text waits at its end (C3AB = 1, the
  speaker's script in 01:57E7-580F): the original's fastest way, a character
  a frame;
- fast (speed 3) and instant (speed 10): B held the same way;
- plain: speed 10 with nothing pressed, then A pressed: the original pace,
  A doing nothing to the text.

Speed 3 must be at the end in a quarter of the held run's frames (four
characters a frame, the scroll's waits four times as quick), instant in at
most two frames, both with the box's map (D800-D89F and DC00-DC9F in WRAM
bank 1) and the text's state (C3A6-C3B0) as the held run has them at its end,
and Shantae where she stands. Then each lets go for 30 frames, presses B and
plays 240 frames more: what the speaker's script does after its text must go
through the held run's steps in the same order (its script position,
C3A6-C3B0, and the box's map while the script is in the text), sooner by
the frames the B press passes over (B counts as in use for a few frames
after it is let go, so the text's last waits go by at once). The plain run
must print a character every five frames, as the original does without B.

The shop (logs/states/shop.state, the first town's, at its menu): the cursor
down to the cream (30 gems) and B pressed every other frame for 300 frames,
at speed 0 and 10. At 0 a cream takes the original's 112 frames (its text,
the pause after "Yes", and the gems taken 10 every eighth frame); at 10 at
most 16, the gems still going down 10 at a time, and buying stopping when
the gems (500) no longer cover a cream.

Isolated settings and saves; the states are not modified.

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
MODES = {"held": 0, "fast": 3, "instant": 10, "plain": 10}
SHOP_MODES = {"shop-original": 0, "shop-fast": 10}
GEMS = 0xCA7D
CREAM = 30


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


def steps(states):
    """The states one after another, each once."""
    out = []
    for st in states:
        if not out or differs(out[-1], st):
            out.append(st)
    return out


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
        g = Game(tmp, mode, args.exe.resolve(), port, text_speed=MODES[mode])
        try:
            slot = start(g)
            r = dict(start=state(g, slot))
            if mode == "plain":
                columns = []
                for _ in range(30):
                    step(g.c, 1)
                    columns.append(g.mem(0xC3A6, 1)[0])
                g.c.command("set_input", buttons="A")
                step(g.c, 1)
                g.c.command("set_input", buttons="-")
                step(g.c, 1)
                r.update(columns=columns, end=state(g, slot), ended=bool(at_end(g)))
            else:
                g.c.command("set_input", buttons="B")
                for n in range(1, 20000):
                    step(g.c, 1)
                    if at_end(g):
                        break
                assert at_end(g), f"{mode}: holding B never reached the end"
                r.update(frames=n, end=state(g, slot), after=follow(g, slot, args.frames))
            r.update(info=g.c.command("shantae_hold_info"))
            result[mode] = r
        finally:
            g.close()


def shop(args, mode, port, result):
    """From the shop's menu: down to the cream, B every other frame; (gems, frame) as they go."""
    with tempfile.TemporaryDirectory(prefix="shantae-shop-", ignore_cleanup_errors=True) as tmp:
        g = Game(tmp, mode, args.exe.resolve(), port, text_speed=SHOP_MODES[mode])
        try:
            g.load(ROOT / "logs/states/shop.state")
            for buttons, frames in (("D", 2), ("-", 8)) * 3:
                g.c.command("set_input", buttons=buttons)
                step(g.c, frames)
            gems = [(word(g.mem(GEMS, 2), 0), -1)]
            for f in range(300):
                g.c.command("set_input", buttons="B" if f % 2 == 0 else "-")
                step(g.c, 1)
                now = word(g.mem(GEMS, 2), 0)
                if now != gems[-1][0]:
                    gems.append((now, f))
            result[mode] = gems
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
               for i, mode in enumerate(MODES)]
    threads += [threading.Thread(target=shop, args=(args, mode, args.port + len(MODES) + i, result))
                for i, mode in enumerate(SHOP_MODES)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    missing = [m for m in list(MODES) + list(SHOP_MODES) if m not in result]
    if missing:
        sys.exit(f"FAILED: {', '.join(missing)} did not finish (see above)")
    held, failures = result["held"], []
    print(f"held (speed 0): B for {held['frames']} frames to the end of the text")
    if held["info"]["text_waits"]:
        failures.append(f"held: speed 0 shortened {held['info']['text_waits']} waits")
    limits = {"fast": (held["frames"] + 3) // 4 + 4, "instant": 2}
    for mode in ("fast", "instant"):
        r = result[mode]
        if r["frames"] > limits[mode]:
            failures.append(f"{mode}: {r['frames']} frames to the end, more than {limits[mode]}")
        for key in ("box", "text"):
            if r["end"][key] != held["end"][key]:
                failures.append(f"{mode}: {key} differs at the end")
        if r["end"]["at"] != held["start"]["at"] or held["end"]["at"] != held["start"]["at"]:
            failures.append(f"{mode}: Shantae moved ({held['start']['at']} -> {r['end']['at']})")
        mine, theirs = steps(r["after"]), steps(held["after"])
        diff = next((i for i, (a, b) in enumerate(zip(theirs, mine)) if differs(a, b)), None)
        if diff is not None or len(mine) != len(theirs):
            failures.append(f"{mode}: after B, step {diff} of {len(theirs)} differs ({len(mine)} steps)")
            continue
        print(f"{mode} (speed {MODES[mode]}): at the end in {r['frames']} frames, the box and the text as "
              f"held; after B the same {len(theirs)} steps in {len(r['after'])} frames")
    plain = result["plain"]
    moves = sum(1 for a, b in zip(plain["columns"], plain["columns"][1:]) if b != a)
    if moves > 30 // 5 + 1 or plain["info"]["text_waits"] or plain["ended"]:
        failures.append(f"plain: {moves} characters in 30 frames without B, "
                        f"{plain['info']['text_waits']} waits shortened, ended {plain['ended']}")
    else:
        print(f"plain: {moves} characters in 30 frames without B (a character every five frames), "
              f"A left the text as it was")
    for mode, most in (("shop-original", None), ("shop-fast", 16)):
        gems = result[mode]
        start, left = gems[0][0], gems[-1][0]
        drops = {a[0] - b[0] for a, b in zip(gems, gems[1:])}
        bought = [f for value, f in gems[1:] if (start - value) % CREAM == 0]
        gaps = [b - a for a, b in zip(bought, bought[1:])]
        if drops != {10}:
            failures.append(f"{mode}: the gems went down by {sorted(drops)}, not 10 at a time")
        elif most is None and (not gaps or set(gaps) != {112}):
            failures.append(f"{mode}: creams {gaps} frames apart, not the original's 112")
        elif most is not None and (not gaps or max(gaps) > most or left != start % CREAM):
            failures.append(f"{mode}: creams {gaps} frames apart, {left} gems left")
        else:
            print(f"{mode} (speed {SHOP_MODES[mode]}): {len(bought)} creams in 300 frames, {gaps[0]} frames "
                  f"apart, the gems {start} -> {left}, 10 at a time")
    if failures:
        sys.exit("FAILED:\n  " + "\n  ".join(failures))
    print("PASS")


if __name__ == "__main__":
    main()
