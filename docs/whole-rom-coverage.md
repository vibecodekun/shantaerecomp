# Shantae native ROM and writable-code coverage

The stock USA ROM now has ahead-of-time native dispatch coverage at every byte
of all 256 physical banks. Missing ROM entries are no longer discovered by
playing farther into the game. Writable programs identified from ROM writers
have separate location-specific native translations and direct tests.

ROM SHA-256: `1b92e22d5510c51bab97d23074e4aad7464d93eb15f7596ef7da0a5efa27a19d`.

## ROM coverage

`exhaustive_rom = true` in `shantae.toml` retains analyzed functions as the fast
path and emits a native instruction entry for every other possible ROM PC.
There are 4,210,688 mapped address slots, including the MBC5 bank-zero alias at
4000, and 8,421,376 slots including HALT-bug fetch variants. The generator shares
53,930 operand-specialized native bodies. A map lookup selects the already
compiled body; it does not decode the ROM opcode at runtime.

Operands crossing 3FFF/4000 or 7FFF/8000 are read live with instruction timing.
All 256 possible CB extensions at a window edge have native translations.
The 39-bank `scan_banks` list affects optimization/discovery only. In particular,
bank 2D's audio engine no longer depends on discovered runtime entry points.

`generated/shantae_exhaustive_bank_<window>.asm` contains an overlapping
disassembly at every byte: window 0 is fixed bank 0, and window N+1 is switchable
bank N. Data is intentionally included. This conservative listing is not a
semantic classification of which bytes are instructions during normal play.

`tools/audit_whole_rom.py` checks every map entry and listing byte, all referenced
native bodies, shared instruction signatures against an independent instruction
length table, and the absence of generated interpreter/fallback calls. Its
machine-readable output is `logs/whole-rom-coverage.json`.

## Writable programs

`ram_native.c` translates these programs directly. Jump destinations remain live
operands, so their ROM destinations need not be observed in a replay.

| Memory | Program | Initialization evidence |
|---|---|---|
| C374 | VBlank JP vector | 01:41A3; queued transfers patch its destination |
| C385 | Text opcode JP vector | 00:0B6E; opcode table dispatch patches the word |
| FFA1 | Graphics JP vector | 00:2730; transfer-width table selects the destination |
| FFA3 | Text JP vector | 01:4748 / 01:47F6 select 0B9D / 0BBA |
| WRAM 3:D000 + n*7E + 1A/29, n=0..31 | 64 object JP slots | 01:4CB6 |
| WRAM 7:DD40 + n*13 + 2/5/8/B, n=0..11 | 48 callback JP slots | 01:7120 |
| FF80..FF89 | Six DMA instructions: LD A,page; LDH (46),A; LD A,count; DEC A; JR NZ; RET | 01:7CFC copies ten bytes from 01:7D0A |

CPU-address echo mappings are supported where they map to these WRAM slots.
The DMA translation preserves individual instruction timing and entry points,
including active DMA, single stepping, and live page/count operands. The old
bulk DMA shortcut is not used by the production dispatcher. Generic RAM/high
memory opcode helpers are also disabled in exhaustive mode.

`tools/audit_ram_coverage.py` verifies the exact bytes of eight ROM writer
regions and emits `generated/shantae_ram.asm` and `logs/ram-coverage.json`.
A whole-ROM scan finds 13 immediate C3-to-memory store candidates: 11 belong to
the listed initializers; 01:41A8 writes a data pointer byte and 0C:40A4 writes the
low byte of the object callback destination 40C3. The bank-7 callback pool was
found by this static scan, without gameplay.

This writer inventory is not a formal reachability proof. It does not prove
that arbitrary ROM control flow, corrupted state, cheats, or other writable
bytes cannot form another executable program. Unknown writable-code execution
still raises a fatal diagnostic. That guard prevents hidden interpretation; it
does not count the unknown program as covered.

## Direct validation

Use the commands in the root README with the MinGW toolchain on PATH. These
checks use private contexts and do not read or write battery saves.

| Check | Result |
|---|---|
| Whole-ROM map/signature audit | All 256 banks / 8,421,376 slots, zero missing entries |
| `whole_rom_check variants` | 110,912 instruction comparisons, zero generated interpreter entries |
| `whole_rom_check production` | 110,912 instruction comparisons through production dispatch |
| `ram_native_check` | 291,191 instruction comparisons, zero generated interpreter entries |
| Native-target regression | 3,966 entries, two script calls, two far returns; zero failures |
| Annotation regressions | 12 tests passed |

Both ROM check modes traverse every dispatch slot and compare representatives
of every emitted body, both flag states, boundary cases, PPU access transitions,
and all cross-window CB opcodes. They do not execute every ROM slot in every
machine state. The optional `all` mode checks every legal slot individually;
that longer mode has not been run.

The RAM test executes the actual ROM initializers, verifies the resulting
programs, then compares all 65,536 patched JP destinations (normal and HALT-bug
fetch), all 116 slots with all 256 ROM banks and applicable echo aliases, all DMA
entry points and byte operand values, and complete DMA loops for every count.
The reference interpreter bypasses shared RAM shortcuts in single-step mode.
Diagnostic opcode inspection also uses non-mutating reads, so a debug lookahead
into VRAM cannot alter only one side of a comparison.

The 3,966 native-target regressions include all 93 distinct bank-2D addresses
from the previously recorded fallback log. Those addresses are regression
fixtures, not a dependency of the exhaustive map generation.

Logs: `logs/whole-rom-audit.log`, `logs/whole-rom-variants.log`,
`logs/whole-rom-production.log`, `logs/ram-native-check.log`,
`logs/whole-rom-native-regressions.log`, `logs/whole-rom-annotation-tests.log`.

No longer gameplay replay was used for these coverage checks. The earlier
replay was interrupted and is not claimed as a completed validation run.
Explicit interpreter/differential modes remain available for testing; normal
compiled execution does not silently fall back. Full ROM dispatch coverage is
not a claim that all hardware behavior or the entire game is bug-free.

## Startup regression: DMA return timing (2026-09-20)

Normal startup hit `Uncompiled writable-memory execution 001:FFFF` in the first
VBlank handler. At 00:0A54 it calls the copied DMA routine at FF80. After the
stock 40-count wait loop, the runtime has four DMA cycles left. Both the native
FF89 RET and the reference interpreter previously popped the stack before
advancing any RET cycles, so the blocked WRAM reads returned FF FF. The earlier
differential tests accepted this because both implementations made the same error.

RET now clocks its fetch/conditional decision before the low stack read, then
clocks the next bus phase before the high read, keeping the total 16/20 cycles.
This follows the local SameBoy `ret`/`cycle_read` ordering. Generated ROM returns,
the native RAM return, and the independent interpreter implementation use these
phases. EI delay is still decremented once per instruction.

The RAM regression now independently requires the stock routine to return to
1234 with SP=D000 after 680 cycles, with DMA finished and all 160 OAM bytes copied.
It also checks DMA release at each stack-read boundary in both CPU speed modes,
and verifies that splitting timing does not advance EI delay multiple times.
`tools/check_startup.py` tests the real game loop with isolated saves/settings;
`tools/build.sh` runs its 120-frame check after linking.

Validation after the fix:

- Both whole-ROM modes: 8,421,376 mapped slots and 110,912 instruction comparisons each.
- RAM: 291,197 comparisons, including the independent expected-return checks.
- Native dispatch: 3,873 entries, two script calls, and two far returns; zero failures.
- Actual executable using the supplied E: ROM: 1,800 startup frames and a 9,000-frame
  scripted run, both with no interpreter fallback. Captures show the title screen
  and Shantae moving through the opening level.
- Windowed SDL/ANGLE run on the RTX 3060: 1,800 frames, clean exit, no interpreter
  fallback (`logs/startup-windowed.log`). All launch tests used isolated save data.

Logs are `logs/startup-*-check.log`, `logs/startup-smoke.log`, and
`logs/startup-gameplay.log`. Captures are `logs/startup-replay/frame-1500.png`
(title), `frame-7000.png`, and `frame-8500.png` (opening level).
