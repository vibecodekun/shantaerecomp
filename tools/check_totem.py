"""The labyrinth's totem puzzle with the expanded view, from a saved state.

The state (default logs/states/totem-orb.state, the user's state2 on
2026-09-26 at 1920x1080) stands beside the totem at 1960,3176 with its
bottom and middle stones already right and the notes at C004/C006 pointing
at slots other objects took. One whip at the top stone at the height of a
jump solves it: the pedestal (0A:4058) leaves its wait at 0C:4BFB and
becomes the orb, a crouching whip frees the key, and walking left into it
takes it (CA85, "You found a key!"). --native plays it in the original view
with the notes put right by hand, the original's own outcome.

Requires the local saved state; never writes user saves or settings.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

from view_probe import Debug, ROOT
from view_state_repro import step

SLOTS = [0xD000 + i * 126 if i < 93 else 0xA000 + (i - 93) * 126 for i in range(157)]
PEDESTAL_RECORD = 0x6A05   # bank 4D: object 0A:4058 at 1968,3232, totem 2
STONES = {0x69F4: 0, 0x69E3: 1, 0x69D2: 2}   # the totem's stone records by place


def word(data, at):
    return int.from_bytes(data[at:at + 2], "little")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("state", type=Path, nargs="?", default=ROOT / "logs/states/totem-orb.state")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--native", action="store_true", help="the original view, notes put right by hand")
    ap.add_argument("--port", type=int, default=14386)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="shantae-totem-", ignore_cleanup_errors=True) as tmp:
        Path(tmp, "rom.cfg").write_text(str(ROOT / "roms/shantae.gbc") + "\n")
        Path(tmp, "shantae.ini").write_text(
            f"expanded_view={int(not args.native)}\nview_width={args.width}\nview_height={args.height}\nroom_zoom=1\n")
        env = dict(os.environ, GBRECOMP_STATE_DIR=tmp, GBRECOMP_NO_LAUNCHER="1",
                   GBRECOMP_DEBUG_PORT=str(args.port))
        with open(Path(tmp, "run.log"), "w") as log:
            proc = subprocess.Popen([str(ROOT / "generated/build/shantae.exe"), "--benchmark"],
                                    cwd=tmp, env=env, stdout=log, stderr=subprocess.STDOUT,
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
                if not args.native:
                    c.command("window", width=1920, height=1080, scaling_mode=0)
                c.command("set_input", buttons="-")
                c.command("load_state", path=args.state.resolve().as_posix())
                step(c, 2)

                def table():
                    raw = bytes.fromhex(c.peek(0xD000, 4096, wram_bank=3) +
                                        c.peek(0xE000, 93 * 126 - 4096, wram_bank=3) +
                                        c.peek(0xA000, 64 * 126, wram_bank=3))
                    return {a: raw[i * 126:(i + 1) * 126] for i, a in enumerate(SLOTS)}

                def live(record):
                    return [a for a, s in table().items() if s[0] != 0xFF and s[0x24] == 0x4D and word(s, 0x25) == record]

                def press(buttons, frames):
                    c.command("set_input", buttons=buttons)
                    step(c, frames)

                stones = {place: live(record) for record, place in STONES.items()}
                pedestal = live(PEDESTAL_RECORD)
                assert all(len(s) == 1 for s in stones.values()) and len(pedestal) == 1, (stones, pedestal)
                pedestal = pedestal[0]
                low = bytes.fromhex(c.peek(0xC000, 0x10))
                notes = [word(low, 4 + 2 * place) for place in range(3)]
                print("notes", [hex(n) for n in notes], "stones", {p: hex(s[0]) for p, s in stones.items()})
                if args.native:
                    c.command("poke", addr="0xc004",
                              hex="".join(stones[p][0].to_bytes(2, "little").hex() for p in range(3)))
                keys = int(c.peek(0xCA85, 1), 16)

                def state():
                    s = table()[pedestal]
                    return s[4], word(s, 2)

                assert state() == (0x0C, 0x4BFB), ("the pedestal is not waiting", state())
                # The top stone only: whip at the height of a jump.
                press("A", 12); press("AB", 2); press("A", 18); press("-", 80)
                bank, pc = state()
                assert bank == 0x0C and 0x41E1 <= pc < 0x4340, ("no orb", hex(bank), hex(pc))
                faces = [table()[stones[p][0]][0x65] for p in range(3)]
                print(f"orb: pedestal at {bank:02X}:{pc:04X}, faces {faces}")
                c.command("screenshot", path=Path(ROOT / "logs/totem-orb.png").resolve().as_posix())
                # Crouch and whip the orb, then walk left into the key.
                press("D", 10); press("DB", 2); press("D", 40); press("-", 60)
                press("L", 60); press("-", 60)
                c.command("screenshot", path=Path(ROOT / "logs/totem-key.png").resolve().as_posix())
                got = int(c.peek(0xCA85, 1), 16)
                assert got == keys + 1, ("no key", keys, got)
                fallback = c.command("interp_fallbacks")
                assert fallback["total_fallbacks"] == 0, fallback
                print(f"PASS: the orb, and the key taken (CA85 {keys} -> {got})")
                c.command("quit")
                proc.wait(timeout=10)
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=10)


if __name__ == "__main__":
    main()
