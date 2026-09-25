"""Check that a town replays identically with and without the expanded view.

Towns keep the original activation (their building-name panel keeps the
original picture), so guest WRAM, HRAM and OAM must match the native replay at
every full capture of view_state_repro.py. Doors, labels and entrances depend
on that; see docs/expanded-view.md.
"""
import argparse
import json
from pathlib import Path


def captures(path):
    return {int(p.stem.split("-")[1]): json.loads(p.read_text()) for p in path.glob("frame-*.json")}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("native", type=Path, help="view_state_repro.py --native capture")
    ap.add_argument("expanded", type=Path, help="view_state_repro.py capture of the same route")
    args = ap.parse_args()
    native, expanded = captures(args.native), captures(args.expanded)
    frames = sorted(native.keys() & expanded.keys())
    assert frames, "no common full captures"
    for f in frames:
        for key in ("wram", "hram", "oam"):
            assert native[f][key] == expanded[f][key], f"frame {f}: {key} differs"
        assert not expanded[f]["view"]["expanded"] and not expanded[f]["view"]["widened"], \
            f"frame {f}: town presented or activated expanded"
    print(f"Town parity: {len(frames)} full captures identical (WRAM, HRAM, OAM)")


if __name__ == "__main__":
    main()
