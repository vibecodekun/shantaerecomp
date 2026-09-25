"""Find bank-0 routines that take inline arguments after the CALL and derive
how many bytes they consume.

Pattern: POP HL (the return address) ... LD A,(HL+) / INC HL ... PUSH HL / RET.
The straight-line path from the POP is walked (following JP nn / JR); the
argument length is the number of HL increments before the PUSH HL that resumes
the caller. Anything else that rewrites HL, a conditional branch, or a RET
before the PUSH makes the length variable and the routine is reported only.

Usage: python tools/inline_args.py          -> table of routines
       python tools/inline_args.py --toml   -> [[inline_call]] entries
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROM = (ROOT / "roms" / "shantae.gbc").read_bytes()

LEN2 = {0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E, 0x18, 0x20, 0x28, 0x30, 0x38,
        0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE, 0xE0, 0xF0, 0xE8, 0xF8, 0xCB, 0x10}
LEN3 = {0x01, 0x11, 0x21, 0x31, 0x08, 0xC2, 0xC3, 0xCA, 0xD2, 0xDA,
        0xC4, 0xCC, 0xCD, 0xD4, 0xDC, 0xEA, 0xFA}
CONDITIONAL = {0x20, 0x28, 0x30, 0x38, 0xC2, 0xCA, 0xD2, 0xDA, 0xC0, 0xC8, 0xD0, 0xD8,
               0xC4, 0xCC, 0xD4, 0xDC}
# Instructions that change HL other than the counted increments.
HL_WRITES = {0x21, 0x2B, 0xE1, 0xF8, 0x09, 0x19, 0x29, 0x39, 0x3A, 0x32, 0xF9,
             0x26, 0x2E, 0x2C, 0x2D, 0x24, 0x25} | set(range(0x60, 0x70))


def arg_length(entry):
    """(n, None) for a fixed length, or (None, reason)."""
    if ROM[entry] != 0xE1:
        return None, "no POP HL"
    pc, n = entry + 1, 0
    for _ in range(64):
        op = ROM[pc]
        if op == 0x2A or op == 0x23:
            n += 1
            pc += 1
            continue
        if op == 0xE5:
            return n, None
        if op in (0xC9, 0xD9, 0xE9):
            return None, "returns/jumps before PUSH HL"
        if op in CONDITIONAL:
            return None, "conditional branch before PUSH HL"
        if op in HL_WRITES:
            return None, f"HL rewritten by {op:02X}"
        if op == 0xC3:
            pc = ROM[pc + 1] | (ROM[pc + 2] << 8)
            if pc >= 0x4000:
                return None, "leaves bank 0"
            continue
        if op == 0x18:
            pc = pc + 2 + (ROM[pc + 1] - 256 if ROM[pc + 1] > 127 else ROM[pc + 1])
            continue
        if op == 0xCD:
            return None, "calls out before PUSH HL"
        pc += 3 if op in LEN3 else 2 if op in LEN2 else 1
    return None, "too long"


def call_counts():
    counts = {}
    for m in re.finditer(rb"\xcd(..)", ROM[:0x40 * 0x4000], re.S):
        t = m.group(1)[0] | (m.group(1)[1] << 8)
        if t < 0x4000:
            counts[t] = counts.get(t, 0) + 1
    return counts


def routines():
    counts = call_counts()
    out = []
    for addr in range(0x0100, 0x4000):
        if ROM[addr] != 0xE1 or counts.get(addr, 0) < 2:
            continue
        before = ROM[addr - 1]
        if before not in (0xC9, 0xD9) and ROM[addr - 3] != 0xC3 and ROM[addr - 2] != 0x18:
            continue
        n, why = arg_length(addr)
        out.append((addr, counts[addr], n, why))
    return out


def main():
    rows = routines()
    if "--toml" in sys.argv:
        for addr, calls, n, why in rows:
            if n is not None and n > 0:
                print(f"[[inline_call]]  # {calls} call sites")
                print(f"routine = 0x{addr:04X}")
                print(f"arg_bytes = {n}")
                print("far_target = false\n")
        return
    for addr, calls, n, why in rows:
        print(f"00:{addr:04X} calls={calls:4d}  " + (f"args={n}" if n is not None else f"variable ({why})"))


if __name__ == "__main__":
    main()
