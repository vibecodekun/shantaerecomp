"""Only the ROM this build was recompiled from may start: the launcher and the runtime.

The ROMs: roms/shantae.gbc (Shantae (USA), SHA-256 1b92e22d...), and made from
it a one-bit bad dump (same size and header) and a 2 MB underdump; --other adds
a ROM of another game. For each:

- Launcher: opened on the ROM (LNG_SCRIPT, 1280x800), the line under the box
  art must read "ROM verified" in green for Shantae and "ROM not recognized" in
  amber for the rest (the colour is read from a screenshot, kept in
  logs/rom-gate/), and clicking PLAY must start the game for Shantae and do
  nothing for the rest. Before the fix the launcher had no fingerprint for a
  SHA-256-only game: it showed "not recognized" for every ROM, Shantae too,
  and PLAY passed them all to the runtime.
- Runtime ("Skip launcher on boot"): booted with no launcher, Shantae must
  start and the rest must be stopped by the runtime's own check (on Windows its
  "Wrong ROM" box, then the file picker; a watcher closes both).

Never touches user settings or saves: every run gets its own state folder.
"""
import argparse
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
FRAMES = 30
SIZE = (1280, 800)
PLAY = (1118, 723)               # the PLAY button at SIZE
VERDICT = (60, 515, 480, 555)    # the band holding the line under the box art
DIALOGS = ("Wrong ROM", "Select Game Boy ROM")


def watch_dialogs(stop, seen):
    """Close the runtime's mismatch box and the file picker after it, noting each."""
    user32 = ctypes.windll.user32
    while not stop.is_set():
        for title in DIALOGS:
            window = user32.FindWindowW(None, title)
            if window:
                if title not in seen:
                    seen.append(title)
                user32.PostMessageW(window, 0x0010, 0, 0)   # WM_CLOSE: OK, then Cancel
        time.sleep(0.05)


def verdict(shot):
    """'verified' (green), 'not recognized' (amber) or None, from the line's text colour."""
    green = amber = 0
    for r, g, b in Image.open(shot).convert("RGB").crop(VERDICT).get_flattened_data():
        if g > r + 60 and g > b + 30:
            green += 1
        elif r > g + 30 and g > b + 60:
            amber += 1
    if max(green, amber) < 20:
        return None
    return "verified" if green > amber else "not recognized"


def run(exe, rom, launcher, shot=None):
    """Returns (started, dialogs seen, exit code or None on a timeout)."""
    with tempfile.TemporaryDirectory(prefix="shantae-rom-gate-") as state:
        Path(state, "rom.cfg").write_text(str(rom.resolve()) + "\n", encoding="utf-8")
        env = dict(os.environ, GBRECOMP_STATE_DIR=state, GBRECOMP_HIDDEN_WINDOW="1",
                   SDL_AUDIODRIVER="dummy", LNG_FORCE_SCALE="1")
        if launcher:
            env["LNG_SCRIPT"] = (f"size:{SIZE[0]}x{SIZE[1]};wait:30;shot:{shot.resolve()};"
                                 f"click:{PLAY[0]},{PLAY[1]};wait:60")
        else:
            env["GBRECOMP_NO_LAUNCHER"] = "1"
        seen, stop = [], threading.Event()
        watcher = threading.Thread(target=watch_dialogs, args=(stop, seen), daemon=True)
        if os.name == "nt":
            watcher.start()
        try:
            result = subprocess.run([str(exe.resolve()), "--limit-frames", str(FRAMES)], cwd=state,
                                    env=env, capture_output=True, text=True, errors="replace",
                                    timeout=90)
            output, code = result.stdout + result.stderr, result.returncode
        except subprocess.TimeoutExpired:
            output, code = "", None
        finally:
            stop.set()
            if watcher.is_alive():
                watcher.join()
    return f"[LIMIT] Reached frame limit {FRAMES}" in output, seen, code


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    parser.add_argument("--rom", type=Path, default=ROOT / "roms/shantae.gbc")
    parser.add_argument("--other", type=Path, help="a ROM of another game, to be refused too")
    args = parser.parse_args()

    shots = ROOT / "logs/rom-gate"
    shots.mkdir(parents=True, exist_ok=True)
    failed = 0
    with tempfile.TemporaryDirectory(prefix="shantae-rom-gate-roms-") as tmp:
        rom = args.rom.read_bytes()
        bad = bytearray(rom)
        bad[0x14000] ^= 0x01                     # one bit in bank 5
        Path(tmp, "bad-dump.gbc").write_bytes(bad)
        Path(tmp, "underdump.gbc").write_bytes(rom[:len(rom) // 2])
        cases = [("shantae", args.rom, True),
                 ("bad-dump", Path(tmp, "bad-dump.gbc"), False),
                 ("underdump", Path(tmp, "underdump.gbc"), False)]
        if args.other:
            cases.append(("other", args.other, False))

        for name, path, good in cases:
            shot = shots / f"{name}.png"
            shot.unlink(missing_ok=True)
            started, seen, code = run(args.exe, path, True, shot)
            seen_line = verdict(shot) if shot.exists() else None
            want = "verified" if good else "not recognized"
            if code is None:
                play = "timed out"
            elif started:
                play = "PLAY started the game"
            elif seen:
                play = "PLAY passed it to the runtime's check"
            else:
                play = "PLAY did nothing"
            ok = seen_line == want and started == good and not seen and code is not None
            failed |= not ok
            print(f"{'PASS' if ok else 'FAIL'} launcher {name}: \"ROM {seen_line}\", {play}")

            started, seen, code = run(args.exe, path, False)
            stopped = "Wrong ROM" in seen if os.name == "nt" else code not in (0, None)
            ok = code is not None and (started if good else (not started and stopped))
            failed |= not ok
            how = ("timed out" if code is None else "started" if started else
                   "stopped by the Wrong ROM box" if "Wrong ROM" in seen else f"exited {code}")
            print(f"{'PASS' if ok else 'FAIL'} runtime {name}: {how}")
    print(f"screenshots in {shots}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
