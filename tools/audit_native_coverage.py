"""Audit object-script native pointer candidates against generated metadata.

Run after regeneration. This checks discovery/emission, not whole-game reachability.
The optional targets file is consumed by the compiled dispatch regression test.
"""
import argparse
import json
from pathlib import Path

import gen_annotations as annotations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    metadata = json.loads((annotations.ROOT / "generated/shantae_metadata.json").read_text())
    # Metadata contains named function entries, including annotation seeds.
    entries = {(int(f["bank"]), int(f["address"], 16)) for f in metadata["functions"]}
    targets = annotations.native_record_targets()
    targets.update(annotations.far_inline_returns())
    missing = sorted(set(targets) - entries)
    unresolved = sorted({(b, a, s, o) for b, a, s, o in annotations.native_records()
                         if b not in annotations.CODE_BANKS or (b == 0 and a >= 0x4000)})
    report = {
        "scope": "Byte-scanned native pointer candidates in configured code/script banks; not proof of complete disassembly.",
        "candidate_targets": len(targets),
        "emitted_function_entries": len(set(targets) & entries),
        "missing": [{"bank": b, "address": f"0x{a:04X}", "source": targets[b, a]} for b, a in missing],
        "unconfigured_or_unrepresentable_candidates": [
            {"bank": b, "address": f"0x{a:04X}", "source": s, "opcode": f"0x{o:02X}"}
            for b, a, s, o in unresolved],
    }
    if args.targets:
        args.targets.write_text("".join(f"{b:02X} {a:04X}\n" for b, a in sorted(targets)), encoding="ascii")
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Native candidates: {len(targets)}, emitted: {len(set(targets) & entries)}, missing: {len(missing)}")
    print(f"Unconfigured/unsupported candidates requiring classification: {len(unresolved)}")
    for b, a in missing[:20]:
        print(f"MISSING {b:02X}:{a:04X} from {targets[b, a]}")
    return bool(missing)


if __name__ == "__main__":
    raise SystemExit(main())
