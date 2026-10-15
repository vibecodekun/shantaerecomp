"""The world above the bottom UI (expanded_view.c, `ui_fit`), in every scene of the debug grid.

From the debug grid (logs/states/debug-grid.state, made by tools/make_states.py)
each scene is loaded (CBFD, CBFC = 1) in a hidden window, so that the bottom UI
is the window's layer at its own size as players see it, and played for up to
700 frames: the frame 30 frames after a dialogue box comes up (WY below 128,
WX 7), then, through the dialogue with B, a frame back in play (or the last of
the 700). Three setups, each in its own instance at once:

- user: a 1920x1080 view in Aspect Fit, the bottom UI at 2x (the user's of
  2026-10-15);
- default: the default 256x240 view in Pixel Perfect (4x), the bottom UI at 1x;
- auto: a 426x240 view in Aspect Fit, the bottom UI on Auto (7.5x).

In every frame taken where the view draws the room (shantae_view_info: ready,
`world` = the picture rows of the room's first world row, of the row under its
last, and of the bottom UI's top):

- the room's floor, where the picture shows it, is above the bottom UI, its
  rows under the original's status bar too where they are its world (the
  opening's purple ground under the bridge, the user's report of 2026-10-15),
  but in a room the picture scrolls through with its camera above its
  greatest y, which brings the floor up as it goes down; with Pixel Perfect a
  dialogue box may cover a tile of it (FIT_WHOLE_SLACK);
- with a box up, the world rows the picture showed in play (fit_rows) are all
  above it (the same tile with Pixel Perfect), so nothing goes off the top
  either, and the zoom is no more than in play.

Prints a line a scene and setup, and every failure at the end. With --shots
DIR it saves the window of each frame taken. Isolated settings; never writes
user saves or settings.

    python tools/check_ui_fit.py
    python tools/check_ui_fit.py --setups user --scenes 00,03,3C --shots logs/ui-fit
"""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time

from view_probe import Debug, ROOT

SETUPS = {
    "user": ("view_width=1920\nview_height=1080\nui_scale=2\n", 1920, 1080, 1),
    "default": ("view_width=256\nview_height=240\nui_scale=1\n", 1920, 1080, 0),
    "auto": ("view_width=426\nview_height=240\nui_scale=0\n", 1920, 1080, 1),
}
# 66 scenes, 00-43; these load nothing.
EMPTY = {0x12, 0x13, 0x21, 0x32, 0x33, 0x38}
WHOLE_SLACK = 8   # FIT_WHOLE_SLACK in expanded_view.c
PIXEL_PERFECT = 0


class Game:
    def __init__(self, tmp, name, ini, window, port, exe):
        state = Path(tmp, name)
        state.mkdir()
        Path(state, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(state, "shantae.ini").write_text("expanded_view=1\nroom_zoom=1\nui_fit=1\n" + ini)
        Path(state, "runtime_prefs.ini").write_text("video.fullscreen=0\nvideo.vsync=0\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=str(state), GBRECOMP_NO_LAUNCHER="1",
                   GBRECOMP_HIDDEN_WINDOW="1", GBRECOMP_DEBUG_PORT=str(port))
        self.log = open(Path(state, "run.log"), "w")
        self.proc = subprocess.Popen([str(exe)], cwd=state, env=env,
                                     stdout=self.log, stderr=subprocess.STDOUT,
                                     creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + 30
        while True:
            try:
                self.c = Debug(port)
                break
            except OSError:
                if self.proc.poll() is not None or time.monotonic() > deadline:
                    raise
                time.sleep(0.05)
        self.c.command("pause")
        self.c.command("window", width=window[0], height=window[1], scaling_mode=window[2])

    def step(self, frames):
        self.c.command("step", count=frames)
        while self.c.read().get("event") != "step_done":
            pass

    def info(self):
        info = self.c.command("shantae_view_info")
        for _ in range(info["count"]):
            self.c.read()
        return info

    def box(self):
        ppu = self.c.command("ppu_state")
        return ppu["WY"] if int(ppu["LCDC"], 16) & 0x20 and ppu["WX"] == 7 and 0 < ppu["WY"] < 128 else 0

    def close(self):
        try:
            self.c.command("quit")
            self.proc.wait(timeout=10)
        finally:
            if self.proc.poll() is None:
                self.proc.terminate()
                self.proc.wait(timeout=10)
            self.log.close()


def judge(info, box, whole):
    """What is wrong with a frame: [] when nothing."""
    if not info["ready"] or info["zoom"] <= 0 or info["town"]:
        return []
    top, floor, ui_top = info["world"]
    slack = WHOLE_SLACK if whole and box else 0
    wrong = []
    # A box's picture may have grown past the rows of play (into Pixel
    # Perfect's margin): rows play did not show may be under it. And a room
    # taller than the picture whose camera is above its greatest y brings the
    # floor up as it goes down, as the original's does.
    scrolls = top < 0 and info["camera_y"] < info["room"][3] - 144
    if floor <= info["height"] and floor > ui_top + slack and not scrolls and \
            (not box or floor - max(top, 0) <= info["fit_rows"]):
        wrong.append(f"the floor {floor - ui_top} rows under the bottom UI")
    if box and info["fit_rows"] > 0:
        shown = min(floor, ui_top) - max(top, 0)
        if shown < info["fit_rows"] - slack:
            wrong.append(f"{info['fit_rows'] - shown} of the {info['fit_rows']} rows of play gone")
        if info["zoom_target"] > info["fit_play"] + 1e-6:
            wrong.append(f"zoomed in to {info['zoom_target']:.3f} from {info['fit_play']:.3f}")
    return wrong


def brief(info):
    return (f"{info['width']}x{info['height']} at {info['zoom']:.3f}, rows {info['fit_rows']}, "
            f"world {info['world']}")


def run(args, name, port, scenes, report, failures):
    ini, ww, wh, mode = SETUPS[name]
    with tempfile.TemporaryDirectory(prefix="shantae-ui-fit-", ignore_cleanup_errors=True) as tmp:
        g = Game(tmp, name, ini, (ww, wh, mode), port, args.exe.resolve())
        try:
            for n in scenes:
                g.c.command("set_input", buttons="-")
                g.c.command("load_state", path=args.grid.resolve().as_posix())
                g.step(2)
                g.c.command("poke", addr="0xcbfc", hex=f"01{n:02x}")
                g.step(300)
                g.c.command("poke", addr="0xcc04", hex="00")
                taken = []
                since = None
                for f in range(0, 700, 2):
                    g.step(2)
                    wy = g.box()
                    since = (since if since is not None else f) if wy else None
                    if wy and f - since >= 30:
                        taken.append(("box", wy, g.info()))
                        if args.shots:
                            g.c.command("window_screenshot", path=(args.shots / f"{name}-{n:02X}-box.png").resolve().as_posix())
                            g.step(1)
                        break
                if taken:
                    quiet = 0
                    for i in range(120):   # through the dialogue: B held runs a line out, a new B the next
                        g.c.command("set_input", buttons="B" if i % 11 < 10 else "-")
                        g.step(4)
                        quiet = 0 if g.box() else quiet + 4
                        if quiet >= 60:
                            break
                    g.c.command("set_input", buttons="-")
                    g.step(30)
                wy = g.box()
                taken.append(("play" if not wy else "box", wy, g.info()))
                if args.shots:
                    g.c.command("window_screenshot", path=(args.shots / f"{name}-{n:02X}-after.png").resolve().as_posix())
                    g.step(1)
                line = []
                for kind, wy, info in taken:
                    wrong = judge(info, wy, mode == PIXEL_PERFECT)
                    line.append(f"{kind} {brief(info)}" + (" (not drawn)" if not info["ready"] else "")
                                + "".join(f" FAIL: {w}" for w in wrong))
                    failures.extend(f"{name} {n:02X} {kind}: {w}" for w in wrong)
                report.append((name, n, " | ".join(line)))
                print(f"{name} {n:02X} " + " | ".join(line), flush=True)
        finally:
            g.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--grid", type=Path, default=ROOT / "logs/states/debug-grid.state")
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--setups", default=",".join(SETUPS))
    ap.add_argument("--scenes", default="", help="hex scene numbers, comma-separated (default: all)")
    ap.add_argument("--shots", type=Path, help="save each frame taken here")
    ap.add_argument("--port", type=int, default=14800)
    args = ap.parse_args()
    scenes = [int(s, 16) for s in args.scenes.split(",")] if args.scenes else \
        [n for n in range(0x44) if n not in EMPTY]
    if args.shots:
        args.shots.mkdir(parents=True, exist_ok=True)
    report, failures = [], []
    threads = [threading.Thread(target=run, args=(args, name, args.port + i, scenes, report, failures))
               for i, name in enumerate(args.setups.split(","))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    boxes = sum(1 for _, _, line in report if line.startswith("box"))
    print(f"{len(report)} scene runs, {boxes} with a dialogue box")
    if failures:
        print("FAIL:\n  " + "\n  ".join(failures))
        raise SystemExit(1)
    print("PASS: the world above the bottom UI in every scene taken: the floor above it, and a dialogue "
          "box's rows of play kept above it")


if __name__ == "__main__":
    main()
