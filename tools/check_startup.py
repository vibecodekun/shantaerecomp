"""Run the actual game frame loop with isolated settings and battery saves."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=root / "generated/build/shantae.exe")
    parser.add_argument("--rom", type=Path, default=root / "roms/shantae.gbc")
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--log", type=Path, default=root / "logs/startup-smoke.log")
    parser.add_argument("--wrapper", default="",
                        help="command to run the exe under, e.g. 'qemu-aarch64-static -L <root>'")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    if args.frames < 1:
        parser.error("--frames must be positive")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        # Let a crashing child fail the test instead of opening a WER dialog.
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    with tempfile.TemporaryDirectory(prefix="shantae-startup-") as state:
        Path(state, "rom.cfg").write_text(str(args.rom.resolve()) + "\n", encoding="utf-8")
        env = dict(os.environ, GBRECOMP_STATE_DIR=state, GBRECOMP_NO_LAUNCHER="1")
        command = shlex.split(args.wrapper) + [
            str(args.exe.resolve()), "--benchmark", "--limit-frames", str(args.frames),
            "--report-interpreter-hotspots", "--log-frame-fallbacks"]
        with args.log.open("w", encoding="utf-8") as log:
            try:
                result = subprocess.run(command, cwd=state, env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=args.timeout)
            except subprocess.TimeoutExpired:
                print(f"FAIL startup timed out; see {args.log}")
                return 1
    output = args.log.read_text(encoding="utf-8", errors="replace")
    reached = f"[LIMIT] Reached frame limit {args.frames}" in output
    no_fallback = "[INTERP] No interpreter fallback recorded."
    fallback = no_fallback not in output or any(
        ("[INTERP]" in line and no_fallback not in line) or
        "[FALLBACK]" in line or "[NATIVE]" in line for line in output.splitlines())
    if result.returncode or not reached or fallback:
        print(f"FAIL startup (exit={result.returncode}, reached={reached}); see {args.log}")
        return 1
    print(f"PASS startup: {args.frames} frames through the game loop; see {args.log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
