"""Replay a saved-state camera route without modifying user saves/settings."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from view_probe import Debug, ROOT


def step(client, frames):
    client.command("step", count=frames)
    while client.read().get("event") != "step_done":
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("state", type=Path)
    ap.add_argument("--route", default="R:180,L:180,R:180")
    ap.add_argument("--interval", type=int, default=6)
    ap.add_argument("--native", action="store_true")
    ap.add_argument("--width", type=int, default=256)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--port", type=int, default=14371)
    ap.add_argument("--room-zoom", type=int, default=0,
                    help="room_zoom: 0 off (the view as it is), 1 fill, 2 whole room")
    ap.add_argument("--window", default="",
                    help="WxH[:scaling mode] the view is presented in (room zoom depends on it)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="shantae-repro-") as state:
        Path(state, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(state, "shantae.ini").write_text(
            f"expanded_view={int(not args.native)}\nview_width={args.width}\nview_height={args.height}\n"
            f"room_zoom={args.room_zoom}\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=state, GBRECOMP_NO_LAUNCHER="1",
                   GBRECOMP_DEBUG_PORT=str(args.port))
        with (args.out / "run.log").open("w") as log:
            proc = subprocess.Popen([str(ROOT / "generated/build/shantae.exe"), "--benchmark"],
                                    cwd=state, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            try:
                deadline = time.monotonic() + 20
                while True:
                    try:
                        c = Debug(args.port)
                        break
                    except OSError:
                        if proc.poll() is not None or time.monotonic() > deadline:
                            raise
                        time.sleep(0.02)
                c.command("pause")
                if args.window:
                    size, _, mode = args.window.partition(":")
                    ww, wh = map(int, size.split("x"))
                    c.command("window", width=ww, height=wh, scaling_mode=int(mode or 0))
                c.command("load_state", path=args.state.resolve().as_posix())
                c.command("set_input", buttons="-")
                step(c, 4)
                elapsed, timeline = 0, []

                def capture(full=False):
                    info = c.command("shantae_view_info")
                    draws = [c.read() for _ in range(info["count"])]
                    entry = dict(elapsed=elapsed, view=info, draws=draws, ppu=c.command("ppu_state"),
                                 window=c.command("window"))
                    path = args.out / f"frame-{elapsed:04d}"
                    c.command("screenshot", path=path.with_suffix(".png").resolve().as_posix())
                    if full:
                        entry["wram"] = c.peek(0xc000, 4096) + "".join(c.peek(0xd000, 4096, wram_bank=b) for b in range(1, 8))
                        entry["vram"] = "".join(c.peek(0x8000, 8192, vram_bank=b) for b in range(2))
                        entry["hram"] = c.peek(0xff80, 127)
                        entry["oam"] = c.peek(0xfe00, 160)
                        entry["hw"] = c.command("hw_state")
                        path.with_suffix(".json").write_text(json.dumps(entry))
                    timeline.append({k: v for k, v in entry.items() if k not in ("wram", "vram", "hram", "oam", "hw")})

                capture(True)
                for part in args.route.split(","):
                    # buttons:frames[:tapped], the tapped buttons pressed every other frame
                    buttons, duration, *tapped = part.split(":")
                    held = buttons.strip("-")
                    c.command("set_input", buttons=buttons)
                    remaining = int(duration)
                    while remaining:
                        count = min(args.interval, remaining)
                        if tapped:
                            for i in range(count):
                                c.command("set_input", buttons=held + tapped[0] if (elapsed + i) % 2 == 0 else held or "-")
                                step(c, 1)
                        else:
                            step(c, count)
                        remaining -= count
                        elapsed += count
                        capture(elapsed % 60 == 0 or remaining == 0)
                (args.out / "timeline.json").write_text(json.dumps(timeline))
                print(f"Captured {len(timeline)} frames; scene gate closed {sum(not e['view']['ready'] for e in timeline)} times")
                c.command("quit")
                proc.wait(timeout=10)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=10)


if __name__ == "__main__":
    main()
