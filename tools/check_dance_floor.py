"""The dance floor's round end on the stock game (extras.c, dance_floor_round_over).

Once a round on the Dance Parlor's floor is over, its controller (script
12:5713) calls 12:5725, whose call to 58A9 makes an object in slot 14 without
placing it. On cleared WRAM, as here, it starts at 0,0 and the game hangs on
"NOT ENOUGH GEMS!" (or the winnings). extras.c sets D719-D71C to 8, 8, 8, 2 as
that op dispatches, as Griever's dance floor hang fix does in the ROM.

From a cold boot: WayForward's debug mode to the scene grid (make_states.py),
then the way a player gets to the dance floor at night (check_patches.py's
dance_floor): the Zombie Caravan, out its gates, the first warp dance to
Scuttle Town's wall, Up held at its door, the Dance Parlor, the owner, a round
left to run with nothing pressed. The round must end with D719-D71C set and
Shantae back at the owner, with no interpreter; then the game's native code
against the reference interpreter from 120 frames before the round ends,
--frames frames. Pictured in logs/dance-floor/.

Never touches user settings or saves.

    python tools/check_dance_floor.py
"""
import argparse
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_patches as cp   # noqa: E402
import make_states           # noqa: E402

ROOT = cp.ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=ROOT / "generated/build/shantae.exe")
    ap.add_argument("--frames", type=int, default=300, help="round-end differential frames")
    ap.add_argument("--port", type=int, default=14760)
    args = ap.parse_args()
    exe = args.exe.resolve()
    out = ROOT / "logs/dance-floor"
    out.mkdir(parents=True, exist_ok=True)
    failures = []
    with tempfile.TemporaryDirectory(prefix="shantae-dance-floor-", ignore_cleanup_errors=True) as tmp:
        g = cp.Game(exe, cp.state_dir(tmp, "grid", ROOT / "roms/shantae.gbc"), args.port)
        try:
            make_states.debug_grid(g, out / "debug-grid.state")
        finally:
            g.close()
        frame, fixed, back, fallbacks = cp.dance_floor(exe, tmp, None, out, args.port + 1)
        print(f"the round is over at frame {frame}; D719-D71C {fixed.hex(' ')}; "
              f"after it Shantae is at x {back} (the owner {cp.PARLOR_OWNER}); interpreter fallbacks {fallbacks}")
        if fixed != b"\x08\x08\x08\x02":
            failures.append(f"D719-D71C not set: {fixed.hex(' ')}")
        if back != cp.PARLOR_OWNER:
            failures.append(f"not back at the owner after the round (x {back}): the dance floor hangs")
        if fallbacks:
            failures.append(f"{fallbacks} interpreter fallbacks")
        matched, _, mismatch, code = cp.differential(exe, tmp, "round-end", None, args.frames,
                                                     out / "round-end.state")
        if matched:
            print(f"round-end differential: matched {matched[0]} instructions / {matched[1]} frames")
        else:
            failures.append(f"round-end differential: {' | '.join(mismatch) or f'no verdict (exit {code})'}")
    if failures:
        print("FAIL:\n  " + "\n  ".join(failures))
        sys.exit(1)
    print("PASS: the dance floor's round ends and returns to the parlor, matching the interpreter")


if __name__ == "__main__":
    main()
