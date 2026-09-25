"""Generate shantae.annotations: curated routines and candidate entry points.

Byte scans are coverage hints, not a complete code/data disassembly. In
particular, object scripts contain native pointers in multiple layouts.

Usage: python tools/gen_annotations.py > shantae.annotations
"""
import argparse
import contextlib
import io
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROM = (ROOT / "roms" / "shantae.gbc").read_bytes()

# Routines identified by hand (Ghidra / runtime tracing).
CURATED = [
    ("00:0540", "farcall", "CALL $0540 / dw addr / db bank; bank kept in HRAM $FF91"),
    ("00:0564", "farcall_return", "bank-restore stub pushed as return address by farcall"),
    ("00:056B", "farjump_reset_sp", "far jump after LD SP,$CFFF (scene change)"),
    ("00:0571", "farjump", "far jump, keeps the stack"),
    ("00:0581", "farcall_e_hl", "far call: E = bank, HL = address"),
    ("00:1342", "object_script_dispatch", "opcode indexes the word table at $0600; DE is script PC"),
    ("00:1A18", "script_set_native_callback", "opcode $2E: db bank / dw native address"),
    ("00:1A46", "script_call_native", "opcode $32: dw native address / db bank"),
    ("00:0884", "vblank_handler", "ends in JP $C374 (patchable WRAM vector)"),
    ("00:1C7A", "load_palettes", "inline args; +$40 source on GBA = brightened palettes"),
    ("00:1E16", "object_handler_exit_bank1", "restore ROM bank 1, JP object_loop_next"),
    ("00:3962", "rst00_jumptable", "RST $00 dispatcher: inline dw table indexed by A"),
    ("00:3973", "detect_hardware", "HRAM $FFFE = 0 DMG / 1 GBC / 3 GBA"),
    ("01:4E69", "object_loop_next", "object handlers JP back here"),
    ("01:4E6D", "object_loop", "walks object lists in WRAM bank 7, JP (obj+$29)"),
    ("01:7DE1", "reset_main", "far-jumped to from boot"),
]


def banked(off):
    bank, a = divmod(off, 0x4000)
    return bank, a + (0x4000 if bank else 0)


def object_handler_constants():
    """LD HL,$002A / ADD HL,BC / LD A,lo / LD (HL+),A / LD A,hi / LD (HL+),A:
    the only writers of an object's handler address (the JP stub at obj+$29
    runs with ROM bank 1 mapped)."""
    found = {}
    pat = bytes([0x21, 0x2A, 0x00, 0x09, 0x3E])
    i = ROM.find(pat)
    while i != -1:
        if ROM[i + 6] == 0x22 and ROM[i + 7] == 0x3E and ROM[i + 9] == 0x22:
            target = ROM[i + 5] | (ROM[i + 8] << 8)
            bank = 0 if target < 0x4000 else 1
            site = "%02X:%04X" % banked(i)
            found.setdefault((bank, target), site)
        i = ROM.find(pat, i + 1)
    return found


CONFIG = tomllib.loads((ROOT / "shantae.toml").read_text())
# Banks that hold code: the same list the analyzer's linear scan uses.
CODE_BANKS = CONFIG["options"]["scan_banks"]
# Inline-argument routines: routine -> config entry (see shantae.toml).
INLINE_CALLS = {ic["routine"]: ic for ic in CONFIG.get("inline_call", [])}


def rom_byte(bank, addr):
    return ROM[addr] if addr < 0x4000 else ROM[bank * 0x4000 + addr - 0x4000]


def after_boundary(bank, addr):
    """The previous instruction ends a routine: RET/RETI/JP HL, JR e or JP nn."""
    if rom_byte(bank, addr - 1) in (0xC9, 0xD9, 0xE9):
        return True
    if rom_byte(bank, addr - 2) == 0x18:
        return True
    return rom_byte(bank, addr - 3) == 0xC3


def pointer_table_targets(min_run=3):
    """Runs of >= min_run consecutive words pointing into the same bank's
    window where >= 75% of targets start right after a routine boundary.
    Only the boundary-aligned targets are returned."""
    found = {}
    for bank in CODE_BANKS:
        lo, hi = (0x0150, 0x4000) if bank == 0 else (0x4000, 0x8000)
        start = 0x0000 if bank == 0 else 0x4000
        for parity in (0, 1):
            run = []
            for a in range(start + parity, start + 0x3FFF, 2):
                w = rom_byte(bank, a) | (rom_byte(bank, a + 1) << 8)
                if lo <= w < hi:
                    run.append((a, w))
                    continue
                if len(run) >= min_run:
                    good = [t for _, t in run if after_boundary(bank, t)]
                    if len(good) * 4 >= len(run) * 3:
                        for t in good:
                            found.setdefault((bank, t), "%02X:%04X" % (bank, run[0][0]))
                run = []
    return found


def native_record_targets():
    """Candidate native references from the object VM, independent of suffix.

    ROM $062E -> $1A18 installs {bank, lo, hi} in object fields $19/$1B/$1C.
    ROM $0632 -> $1A46 calls {lo, hi, bank} through $1A66 (JP HL).
    ROM $067A -> $16D2 optionally installs another object's callback: its
    arguments are {dw object-pointer, dw script, db script-bank, db native-bank,
    dw native}. Neither native record requires a particular following opcode.

    Scan the configured code/script banks at every byte, since scripts are
    interleaved with native routines. This intentionally over-approximates
    references; it does not certify that every candidate is script-aligned.
    Target banks remain limited to the configured code banks.
    """
    found = {}
    for bank, target, site, opcode in native_records():
        if bank not in CODE_BANKS or (bank == 0 and target >= 0x4000):
            continue
        found.setdefault((bank, target), f"{site} (opcode ${opcode:02X})")
    return found


def native_records():
    """Yield valid ROM pointer candidates, including unconfigured target banks.

    Keep the latter visible to the coverage audit rather than silently claiming
    coverage for the entire cartridge. Records cannot straddle ROM windows.
    """
    for source_bank in CODE_BANKS:
        start = source_bank * 0x4000
        end = min(start + 0x4000, len(ROM))
        for off in range(start, end):
            op = ROM[off]
            if op in (0x2E, 0x32) and off + 4 <= end:
                b, lo, hi = ((1, 2, 3) if op == 0x2E else (3, 1, 2))
            elif op == 0x7A and off + 9 <= end:
                # The first argument points to a WRAM object-pointer variable.
                pointer = ROM[off + 1] | (ROM[off + 2] << 8)
                if not 0xC000 <= pointer < 0xE000:
                    continue
                b, lo, hi = 6, 7, 8
            else:
                continue
            bank = ROM[off + b]
            target = ROM[off + lo] | (ROM[off + hi] << 8)
            if not 0x0150 <= target < 0x8000 or bank * 0x4000 >= len(ROM):
                continue
            if target < 0x4000:
                bank = 0
            elif bank == 0:
                # MBC5 can map bank zero at $4000, but this analyzer cannot
                # represent both bank-zero windows. Report it in the audit.
                yield bank, target, "%02X:%04X" % banked(off), op
                continue
            if rom_byte(bank, target) in INVALID_OPS:
                continue
            target_offset = target if bank == 0 else bank * 0x4000 + target - 0x4000
            if ROM[target_offset:target_offset + 8] == bytes([0xFF]) * 8:
                # Erased ROM padding is not a plausible native routine.
                continue
            yield bank, target, "%02X:%04X" % banked(off), op


def far_inline_returns():
    """Recover returns from banked callees that consume caller inline bytes.

    E.g. 04:4C5B reads the caller's bank at SP+3 and advances the saved
    return address at SP+4 by two, before reading those bytes via $0474.
    CALL $0540 itself consumes only the preceding three-byte far pointer.
    Recognize the complete stack-update sequence, including carry to the high
    byte, rather than treating any LD HL,SP+n as an inline-argument routine.
    """
    prefix = bytes.fromhex("f8 03 2a e0 94 7e e0 92 c6")
    suffix = bytes.fromhex("22 7e e0 93 ce 00 22")
    callees = {}
    for bank in CODE_BANKS:
        start, end = bank * 0x4000, (bank + 1) * 0x4000
        off = ROM.find(prefix, start, end)
        while off != -1:
            if off + 17 <= end and ROM[off + 10:off + 17] == suffix:
                callees[banked(off)] = ROM[off + 9]
            off = ROM.find(prefix, off + 1, end)
    found = {}
    for bank in CODE_BANKS:
        start, end = bank * 0x4000, min((bank + 1) * 0x4000, len(ROM))
        off = ROM.find(bytes.fromhex("cd 40 05"), start, end)
        while off != -1:
            if off + 6 <= end:
                addr = ROM[off + 3] | (ROM[off + 4] << 8)
                callee_bank = ROM[off + 5] if addr >= 0x4000 else 0
                count = callees.get((callee_bank, addr), 0)
                if count and off + 6 + count < end:
                    found[banked(off + 6 + count)] = (
                        f"%02X:%04X -> {callee_bank:02X}:{addr:04X}, {count} extra argument bytes" % banked(off))
            off = ROM.find(bytes.fromhex("cd 40 05"), off + 1, end)
    return found


def jp_stub_tables(min_run=2):
    """Runs of consecutive 'JP nn' stubs (3-byte stride, targets in the same
    bank window) entered by computed jumps. A 3-byte load/store/call directly
    before the run is part of the table too (it falls into the first stub),
    e.g. 04:4E44 LD ($CBC4),A before two JP $4E4D."""
    three_byte_heads = (0xEA, 0xFA, 0x01, 0x11, 0x21, 0x31, 0xCD)
    found = {}
    for bank in CODE_BANKS:
        lo, hi = (0x0000, 0x4000) if bank == 0 else (0x4000, 0x8000)
        a = lo
        while a + 3 <= hi:
            run = []
            b = a
            while b + 3 <= hi and rom_byte(bank, b) == 0xC3:
                target = rom_byte(bank, b + 1) | (rom_byte(bank, b + 2) << 8)
                if not (target < 0x4000 or lo <= target < hi):
                    break
                run.append(b)
                b += 3
            if len(run) >= min_run:
                site = "%02X:%04X" % (bank, run[0])
                for stub in run:
                    found.setdefault((bank, stub), site)
                head = run[0] - 3
                if head >= lo and rom_byte(bank, head) in three_byte_heads:
                    found.setdefault((bank, head), site)
                a = b
            else:
                a += 1
    return found


INVALID_OPS = {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD}
LEN2_OPS = {0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E, 0x18, 0x20, 0x28, 0x30, 0x38,
            0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE, 0xE0, 0xF0, 0xE8, 0xF8, 0xCB, 0x10}
LEN3_OPS = {0x01, 0x11, 0x21, 0x31, 0x08, 0xC2, 0xC3, 0xCA, 0xD2, 0xDA,
            0xC4, 0xCC, 0xCD, 0xD4, 0xDC, 0xEA, 0xFA}
TERMINATORS = {0xC9, 0xD9, 0xC3, 0x18, 0xE9}   # RET, RETI, JP nn, JR e, JP HL


def inline_args_length(bank, args, ic):
    """Mirror of the analyzer's inline_args_length()."""
    record = ic.get("record_bytes", 0)
    if not record:
        return ic.get("arg_bytes", 3)
    length = 0
    for _ in range(256):
        if rom_byte(bank, args + length) == 0:
            return length + 1
        length += record
    return length


def decodes_to_terminator(bank, addr, hi, limit=256):
    """Valid SM83 opcodes all the way to an unconditional RET/JP/JR within
    `limit` instructions (inline far-call arguments skipped). Random data
    almost never decodes that far: 11 of 256 opcodes are invalid."""
    for _ in range(limit):
        if addr >= hi:
            return False
        op = rom_byte(bank, addr)
        if op in INVALID_OPS:
            return False
        if op in TERMINATORS:
            return True
        length = 3 if op in LEN3_OPS else 2 if op in LEN2_OPS else 1
        if op == 0xCD and addr + 2 < hi:
            ic = INLINE_CALLS.get(rom_byte(bank, addr + 1) | (rom_byte(bank, addr + 2) << 8))
            if ic:
                if ic.get("no_return"):
                    return True             # far jump: routine ends here
                length = 3 + inline_args_length(bank, addr + 3, ic)
        addr += length
    # `limit` valid instructions with no terminator: unrolled loops and long
    # routines. Data survives that far with probability ~e^-11.
    return True


def boundary_starts():
    """Addresses right after RET/RETI/JP nn/JR e that decode as a routine.
    Catches null handlers (lone RET) and methods reached only through data."""
    found = set()
    for bank in CODE_BANKS:
        lo, hi = (0x0100, 0x4000) if bank == 0 else (0x4000, 0x8000)
        for a in range(lo + 3, hi):
            after = (rom_byte(bank, a - 1) in (0xC9, 0xD9) or rom_byte(bank, a - 3) == 0xC3 or
                     rom_byte(bank, a - 2) == 0x18 or
                     (rom_byte(bank, a - 1) == 0xFF and rom_byte(bank, a - 2) == 0xFF))  # padding
            if after and rom_byte(bank, a) not in (0x00, 0xFF) and decodes_to_terminator(bank, a, hi):
                found.add((bank, a))
    return found


def main():
    print("# Generated by tools/gen_annotations.py -- do not edit by hand.")
    print("# function BB:AAAA name ; comment")
    print()
    print("# Curated routines")
    seen = set()
    for addr, name, note in CURATED:
        print(f"function {addr} {name} ; {note}")
        seen.add(addr)
    print()
    print("# Object handlers (set via LD HL,$002A / ADD HL,BC / LD A,lo / ... helpers)")
    for (bank, target), site in sorted(object_handler_constants().items()):
        addr = f"{bank:02X}:{target:04X}"
        if addr in seen:
            continue
        seen.add(addr)
        print(f"function {addr} obj_handler_{bank:02x}_{target:04x} ; set at {site}")
    print()
    print("# Code-pointer table targets (script opcode handlers, state tables, ...)")
    for (bank, target), table in sorted(pointer_table_targets().items()):
        addr = f"{bank:02X}:{target:04X}"
        if addr in seen:
            continue
        seen.add(addr)
        print(f"function {addr} ptr_{bank:02x}_{target:04x} ; table at {table}")
    print()
    print("# Candidate native targets from object-script opcodes $2E, $32 and $7A")
    for (bank, target), site in sorted(native_record_targets().items()):
        addr = f"{bank:02X}:{target:04X}"
        if addr in seen:
            continue
        seen.add(addr)
        print(f"function {addr} native_{bank:02x}_{target:04x} ; record at {site}")
    print()
    print("# Returns past extra inline arguments consumed by banked far-call targets")
    for (bank, target), site in sorted(far_inline_returns().items()):
        addr = f"{bank:02X}:{target:04X}"
        if addr in seen:
            continue
        seen.add(addr)
        print(f"function {addr} far_return_{bank:02x}_{target:04x} ; {site}")
    print()
    print("# JP-stub jump tables (entered by computed jumps)")
    for (bank, addr16), site in sorted(jp_stub_tables().items()):
        addr = f"{bank:02X}:{addr16:04X}"
        if addr in seen:
            continue
        seen.add(addr)
        print(f"function {addr} jpstub_{bank:02x}_{addr16:04x} ; table at {site}")
    print()
    print("# Routine starts after RET/RETI/JP/JR that decode to a terminator")
    for bank, addr16 in sorted(boundary_starts()):
        addr = f"{bank:02X}:{addr16:04X}"
        if addr in seen:
            continue
        seen.add(addr)
        print(f"function {addr} sub_{bank:02x}_{addr16:04x}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="atomically replace this annotations file")
    args = parser.parse_args()
    if args.output:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            main()
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(buffer.getvalue(), encoding="utf-8")
        temporary.replace(args.output)
    else:
        main()
