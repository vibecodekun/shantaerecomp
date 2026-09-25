"""Compare the ROM world-map lookup with the game's streamed VRAM cells."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
ROM = (ROOT / "roms/shantae.gbc").read_bytes()


def read(bank, addr):
    return ROM[bank * 0x4000 + addr - 0x4000] if 0x4000 <= addr < 0x8000 else ROM[addr]


def tile(w, h, x, y):
    directory = (w[0x9f9] << 8) + (y >> 8) * 64 + (x >> 8) * 2
    page, bank = read(w[0x9fa], directory), read(w[0x9fa], directory + 1)
    cell = (page << 8) + ((y & 255) >> 4) * 32 + ((x & 255) >> 4) * 2
    lo, hi = read(bank, cell), read(bank, cell + 1)
    bank, ptr = h[0x5f] + (lo & 15), (hi << 8) | (lo & 240)
    quadrant = ((y & 8) >> 2) + ((x & 8) >> 3)
    return read(bank, ptr + quadrant), read(bank, ptr + 4 + quadrant)


def main():
    for path in (ROOT / "logs/view-probe").glob("*.json"):
        d = json.loads(path.read_text())
        w, h, v = [bytes.fromhex(d[k]) for k in ("wram", "hram", "vram")]
        if not w[0x9fa]:
            continue
        cx = int.from_bytes(w[0x9fb:0x9fd], "little")
        cy = int.from_bytes(w[0x9fd:0x9ff], "little")
        good, bad = 0, []
        for y in range(cy // 8 + 1, (cy + 128) // 8):
            for x in range(cx // 8 + 1, (cx + 160) // 8):
                expected = tile(w, h, x * 8, y * 8)
                offset = 0x1800 + (y % 32) * 32 + x % 32
                actual = v[offset], v[0x2000 + offset]
                if expected == actual:
                    good += 1
                else:
                    bad.append((x, y, expected, actual))
        print(path.name, "camera", cx, cy, "matched", good, "mismatched", len(bad), bad[:8])


if __name__ == "__main__":
    main()
