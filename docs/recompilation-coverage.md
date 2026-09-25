# Shantae recompilation coverage investigation

Historical discovery investigation. The subsequent [whole-ROM native coverage work](whole-rom-coverage.md) removes the ROM-entry dependency on these heuristic scans; the evidence and earlier test results below are retained.

ROM: USA, 4 MiB, CRC32 `E994B59B`, SHA-256
`1b92e22d5510c51bab97d23074e4aad7464d93eb15f7596ef7da0a5efa27a19d`.
The project's ROM was byte-identical to the supplied cartridge image.

## Confirmed gaps

The original gameplay log contained 118 interpreter entries at three distinct
ROM locations. All three are referenced by the object-script VM:

| Missed native entry | Script record | Meaning |
|---|---|---|
| `06:59E9` | `06:59C2`, opcode `$2E` | Install a bank/address callback |
| `06:57E1` | `06:57BF`, opcode `$32` | Call an address/bank native routine |
| `07:5A8D` | `07:5A54`, opcode `$32` | Call an address/bank native routine |

The dispatcher at `00:1342` reads the opcode through DE and looks up a word at
`$0600 + opcode`. These table entries and handlers establish the layouts:

* `$062E -> $1A18`: consumes bank, address low, address high and writes the
  callback to object fields `$19`, `$1B`, `$1C`.
* `$0632 -> $1A46`: consumes address low, address high, bank; writes the MBC5
  bank register at `$2000`; calls through `00:1A66` (`JP HL`); then restores
  the caller's bank and resumes the script.
* `$067A -> $16D2`: updates another object's script and, optionally, its native
  callback. Its callback is the last three bytes of its eight-byte payload.

The previous scanner accepted `$2E` only before selected suffix bytes or after
a RET byte. There is no such condition in the ROM handler. It also omitted
`$32`. The new scan treats the opcode payloads independently of neighboring
bytes, bounds records to their ROM window, checks target banks/addresses, and
rejects erased padding. These remain candidate matches rather than a complete
parser proving script boundaries.

A scripted run that entered the inventory exposed a second issue:

| Call site | Banked callee | Correct continuation |
|---|---|---|
| `05:6516` | `04:4C5B` | `05:651E` |
| `05:6528` | `04:4C5B` | `05:6530` |

The callee reads the saved caller bank at SP+3, adds two to the saved return
address at SP+4 (including carry into its high byte), then reads those extra
inline bytes through `$0474`. The old analyzer knew only the three-byte pointer
consumed by the `$0540` far-call dispatcher. The annotation generator now
recognizes the complete stack-update sequence in banked callees and seeds the
continuation after their extra arguments at every matching far-call site.

This recovers 1,227 continuation sites. Together with the native script records,
the audit covers 3,873 unique entries. All 14,462 previous annotation entries
remain present; the regenerated file has 16,569 entries. No new addresses were
added to the runtime-harvest manifest to obtain these fixes.

## Reproducible checks

`tools/test_annotations.py` checks record layouts, boundary handling, erased
padding, the ROM opcode table, and the five actual missed addresses.

`tools/audit_native_coverage.py` checks that all discovered entries appear in
generated metadata and writes the test target list. The compiled
`native_dispatch_check` target checks every entry through the real dispatcher
against the interpreter, with memory comparison and fallback treated as failure.
It also runs the two observed `$32` transitions and the two banked far calls
through their corrected returns. It creates fresh MBC5 contexts without saves.

`logs/native-dispatch-check.log` and `logs/native-coverage.json` hold the results.
`logs/pit-jump-route.input` records the opening-level replay inputs; gameplay
fallback summaries and selected screenshots are in `logs/pitjump.after.log` and
`logs/pitjump_*`. `logs/differential.after.log` records the 3,000-frame differential
check with the inventory input sequence and fallback treated as failure.

Final results: all 12 discovery tests passed; all 3,873 compiled entry checks,
two script calls and two far returns passed with memory comparison. The
18,000-frame opening-level replay recorded zero interpreter fallbacks through
pit deaths and repeated jump inputs. The differential run matched 48,273,566
steps over 3,000 frames with generated-side fallback treated as failure.

The native check is stronger than metadata inspection, but neither it nor a
successful replay establishes whole-game coverage. The audit reports raw
pointer candidates in unconfigured banks and the unsupported bank-zero
switchable-window representation separately. Many are incidental byte matches
in data; classifying them requires more disassembly. Variable-length `$0B54`
scripts and other computed transfers also need further analysis before claiming
a complete ROM disassembly.

For address-preserving inspection without a Ghidra project, `tools/disasm_range.cpp`
uses the existing recompiler's decoder. Build it with the recompiler include path
and `decoder.cpp`/`rom.cpp`, then pass ROM, bank, start and end (hex). Linear output
still needs manual separation of code and inline data.
