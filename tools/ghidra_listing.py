"""Ghidra's listing of ROM ranges, without opening Ghidra.

Runs Ghidra headless with the GhidraBoy loader (tools/build_ghidraboy.py
installs it) and prints its disassembly of each range, one instruction a
line. The first run imports roms/shantae.gbc into a project in logs/ghidra
(about a minute, no auto-analysis); later runs open it read-only.

    python tools/ghidra_listing.py 6:4AA7:4B19 6:5E52:5F0C

A range is BANK:START:END in hex. Bank 0 is 0000-3FFF; the others are mapped
at 4000-7FFF. Object scripts are bytecode, not code (00:1342 runs them); a
range over one prints nonsense.

Usage: python tools/ghidra_listing.py [--ghidra DIR] RANGE [RANGE ...]
--ghidra defaults to $GHIDRA_INSTALL_DIR.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
PROJECT = ROOT / "logs" / "ghidra"
ROM = ROOT / "roms" / "shantae.gbc"


def headless(ghidra, *args):
    script = ghidra / "support" / ("analyzeHeadless.bat" if os.name == "nt" else "analyzeHeadless")
    run = subprocess.run([str(script), str(PROJECT), "shantae", *args], capture_output=True, text=True)
    return run.stdout + run.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ghidra", type=Path, default=os.environ.get("GHIDRA_INSTALL_DIR"))
    ap.add_argument("ranges", nargs="+", metavar="BANK:START:END")
    args = ap.parse_args()
    if args.ghidra is None:
        ap.error("pass --ghidra or set GHIDRA_INSTALL_DIR")
    for r in args.ranges:
        if not re.fullmatch(r"[0-9A-Fa-f]{1,2}:[0-9A-Fa-f]{1,4}:[0-9A-Fa-f]{1,4}", r):
            ap.error(f"not BANK:START:END in hex: {r}")
    PROJECT.mkdir(parents=True, exist_ok=True)
    if not (PROJECT / "shantae.rep").exists():
        log = headless(args.ghidra, "-import", str(ROM), "-noanalysis")
        (PROJECT / "import.log").write_text(log)
        if "Import succeeded" not in log:
            sys.exit(f"the import failed; see {PROJECT / 'import.log'}")
    log = headless(args.ghidra, "-process", ROM.name, "-readOnly", "-noanalysis", "-scriptPath",
                   str(Path(__file__).resolve().parent / "ghidra"), "-postScript", "Listing.java", *args.ranges)
    (PROJECT / "last.log").write_text(log)
    lines = re.findall(r"Listing\.java> (.*?) *\(GhidraScript\)", log)
    if not lines:
        sys.exit(f"no listing; see {PROJECT / 'last.log'}")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
