"""Verify every emitted ROM/HALT-bug dispatch slot, independently of gameplay."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GENERATED = ROOT / "generated"

# Independent SM83 instruction lengths, used to verify that all addresses sharing
# a native body really have the same instruction/operands (not merely a nonzero ID).
LENGTH = [1] * 256
for _op in (0x06, 0x0E, 0x10, 0x16, 0x18, 0x1E, 0x20, 0x26, 0x28, 0x2E,
            0x30, 0x36, 0x38, 0x3E, 0xC6, 0xCB, 0xCE, 0xD6, 0xDE, 0xE0,
            0xE6, 0xE8, 0xEE, 0xF0, 0xF6, 0xF8, 0xFE):
    LENGTH[_op] = 2
for _op in (0x01, 0x08, 0x11, 0x21, 0x31, 0xC2, 0xC3, 0xC4, 0xCA, 0xCC,
            0xCD, 0xD2, 0xD4, 0xDA, 0xDC, 0xEA, 0xFA):
    LENGTH[_op] = 3
ILLEGAL = {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD}


def signature(rom, bank, base, offset, bug):
    physical = bank * 0x4000 + offset
    opcode = rom[physical]
    if opcode in ILLEGAL:
        return "illegal"
    length = LENGTH[opcode]
    live = offset + length - bug > 0x4000 and length > 1
    operands = () if live or opcode == 0x10 else tuple(
        rom[physical + n - bug] for n in range(1, length))
    override = bank == 0 and base + offset == 0x1CB6
    return opcode, bug, live, operands, override


def main():
    rom = (ROOT / "roms/shantae.gbc").read_bytes()
    metadata = json.loads((GENERATED / "shantae_exhaustive.json").read_text())
    banks = len(rom) // 0x4000
    assert banks == metadata["banks"] == 256
    assert len(rom) == metadata["rom_bytes"]
    assert hashlib.sha256(rom).hexdigest() == metadata["rom_sha256"]
    bodies = set()
    for path in GENERATED.glob("shantae_exhaustive_group_*.c"):
        bodies.update(map(int, re.findall(r"static void native_(\d+)\(", path.read_text())))
    assert bodies == set(range(metadata["native_bodies"])), "missing native body"
    per_bank = []
    signatures = {}
    for window in range(banks + 1):
        source = (GENERATED / f"shantae_exhaustive_map_{window}.c").read_text()
        values = list(map(int, re.search(r"= \{(.*?)\};", source, re.S)[1].replace("\n", "").strip().rstrip(",").split(",")))
        assert len(values) == 0x8000, f"missing entries in window {window}"
        assert set(values) <= bodies, f"undefined native bodies in window {window}"
        lines = (GENERATED / f"shantae_exhaustive_bank_{window}.asm").read_text().splitlines()[1:]
        assert len(lines) == 0x4000
        bank, base = (window - 1, 0x4000) if window else (0, 0)
        for bug in range(2):
            for offset in range(0x4000):
                native_id = values[bug * 0x4000 + offset]
                expected = signature(rom, bank, base, offset, bug)
                assert signatures.setdefault(native_id, expected) == expected, (
                    f"native body {native_id} aliases different instructions at {bank:02X}:{base + offset:04X}")
        for offset, line in enumerate(lines):
            assert line.startswith(f"{bank:02x}:{base + offset:04x}  {rom[bank * 0x4000 + offset]:02x}  ")
        per_bank.append({"bank": bank, "base": f"0x{base:04X}", "normal_entries": 16384, "halt_bug_entries": 16384})
    # Inspect only files in the actual generated build, ignoring stale outputs.
    cmake = (GENERATED / "CMakeLists.txt").read_text()
    for filename in set(re.findall(r"\bshantae[^\s/]*\.c\b", cmake)):
        if filename.endswith("_main.c"):
            continue  # Explicit reference interpreter is a supported test mode.
        source = (GENERATED / filename).read_text()
        assert not re.search(r"\bgb_interpret\s*\(", source), filename
        assert not re.search(r"\bgbrt_note_dispatch_fallback\s*\(", source), filename
    report = {
        "rom_sha256": hashlib.sha256(rom).hexdigest(),
        "physical_banks": banks,
        "rom_address_slots": (banks + 1) * 16384,
        "normal_and_halt_bug_slots": (banks + 1) * 32768,
        "native_bodies": len(bodies),
        "missing_entries": 0,
        "generated_interpreter_calls": 0,
        "all_slot_instruction_signatures_verified": True,
        "scope": "Every-byte conservative native coverage, including data; not a semantic code/data classification or whole-game playthrough.",
        "windows": per_bank,
    }
    (ROOT / "logs/whole-rom-coverage.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {banks} banks, {report['normal_and_halt_bug_slots']:,} dispatch slots; no missing entries or interpreter calls")


if __name__ == "__main__":
    main()
