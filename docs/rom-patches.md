# ROM patches

A player can play Shantae with an IPS or BPS patch: a translation, a fix, a hack. On the
launcher's **Mods** page, **ROM PATCH** → **Browse...** picks the patch file, and the line
under it says at once what it does on this build. The patch is applied to the ROM in memory
as the game starts, every time, so the ROM file is left as it is; "Skip launcher on boot"
keeps it. A ROM file that already has one of the patches below applied is accepted as the
ROM too. In-game saves are the same file with or without a patch (the save name is fixed);
save states are kept to the ROM they were made on, so a state made with one patch does not
load with another, or with none.

Addresses are `bank:address` in the USA ROM (CRC32 `E994B59B`). The code was read in Ghidra 12
with GhidraBoy (`tools/ghidra_listing.py`, whose `--patch` lists a patched ROM).

## Why a recompiled game needs more than applying the patch

Every instruction of the ROM was compiled ahead of time from the bytes of the stock ROM, its
operands included (`exhaustive_rom`: a native body for every byte of every bank). The game
still reads its data from the ROM image the launcher loads, so a patch that changes only data
(text, graphics, palettes, levels) works by being applied. A patch that changes code would
not: the native code would go on doing what the stock bytes said. Running the changed code
through the runtime's interpreter would work, but it would be the one part of the game not
compiled, slower, outside the differential checks, and outside the game's hooks, so it is
not done. The build compiles the changed code instead.

## How it works

At build time (`shantae.toml`: `rom_patches = "patches"`), the recompiler applies every IPS and
BPS patch in `patches/` to the stock ROM. For every instruction position whose bytes the patch
changes (every byte of every bank is a position, with its HALT-bug variant, and bank zero mapped
at 4000-7FFF too), it compiles a native body from the patched bytes, the same way as every
stock position's. 355,676 positions across the eight patches, all but 1,384 sharing bodies the
ROM already has. The table `<prefix>_patch_slots` lists each with the bytes it was compiled
from. Every analyzed routine (the fast path, `body_*`) records the ROM bytes its instructions
came from, inlined callees included, and starts with a guard: a flag set when a patch changes
one of them.

At boot, the runtime (`gbrecompiled/runtime/src/gb_rom_patch.c`) applies the chosen patch
and compares the result with the stock ROM:

- a changed position runs the body compiled for exactly its new bytes, found by position and
  bytes, so patches whose changes do not overlap work together too;
- a routine with a changed instruction does not run its own code: its guard steps through it
  one instruction at a time on the per-position bodies, which follow the patched bytes, as the
  exhaustive path does for code outside the analyzed routines. Every other routine runs as
  before;
- a changed position with no body compiled for its bytes (a patch this build was not made with)
  stops the game with a message if it is ever reached, after saving the battery RAM. Data a
  patch changes is never executed, so it never gets there.

Nothing runs on the interpreter. The game's hooks (the `[[imm_override]]` sites and the dispatch
overrides) are for the stock instructions: a changed instruction at a hook site is compiled
without its hook, and the unchanged instructions of a stepped routine keep theirs. No patch below
changes a hooked instruction.

A patch that changes the ROM's size is refused: MBC5's banks past the 256 of the stock ROM
have no code compiled for them. A patch is recognized by the SHA-256 of its result, so a copy of
a patch below under another name is recognized too; its title comes from `patches/patches.toml`.

The launcher's line under the patch:

| Line | When | PLAY |
|------|------|------|
| *Recognized: (title). It is built into this version of the game...* (green) | a patch below | starts it |
| *This patch changes the game's text, graphics or other data, not its code...* (green) | another patch that changes no instruction of an analyzed routine | starts it |
| *This patch changes the game's code in N places this version wasn't built for (the first at BB:AAAA)...* (amber) | another patch that changes code | starts it; the game stops if it reaches one |
| *This patch changes the ROM's size...*, *does not apply to this ROM...*, *The ROM picked is already patched...* (amber) | | does not start it |

The recompiler's analysis of what is code is generous (`aggressive_scan`), and the translations'
strings sit between scripts and code: an unknown text-only patch can read as changing code
here, and plays correctly anyway, since the strings are never executed.

## The patches the build is compiled with

`patches/` is not in the repository (bring your own, as with the ROM). [patches/README.md](../patches/README.md)
lists the files the release build was compiled with and their SHA-256s.

| Patch | Bytes changed | Positions compiled | Its code changes |
|-------|--------------:|-------------------:|------------------|
| French translation by Anthiflo (v1.2) | 53,643 | 108,425 | 12:5725 (the dance floor fix, below); 1A:72D5-72DF, the shop list's `LD A,'O'` / `'U'` / `'T'`, to "---" |
| Italian translation | 30,041 | 61,072 | none: text and graphics |
| Polish translation by PieniondzPS (v2) | 50,982 | 103,030 | 1A:72D5-72DF, the shop's "OUT", to "NIE" |
| Brazilian Portuguese translation by Sliter and ajkmetiuk (v1), with the dance floor fix | 65,330 | 131,774 | 12:5725 |
| The same, with the dance floor fix, GBA Enhanced mode and GBC palettes | 65,336 | 131,800 | 12:5725, 00:397B, 00:1CB4 |
| Dance floor hang fix by Griever | 17 | 36 | 12:5725, the routine a round's controller calls once the round is over: `CALL 58A9` to `CALL 6E00`, a routine it adds in the bank's empty space that sets D719-D71C to 8, 8, 8, 2 and then calls 58A9 as the original did |
| GBA Enhanced mode on any Game Boy Color (Polar-Star) | 2 | 8 | 00:397B, in the hardware check: `SLA B` to `LD B,2`, so FFFE is 3 (GBA) on any CGB |
| GBC palettes by Drenn (v1.1) | 4 | 18 | 00:1CB4, in the palette loader: `LDH A,($FE)` to `LD A,1`, so it never picks the brightened GBA copy (and the header checksum) |

Most of what the translations change inside analyzed routines is not code: the dance floor's
messages (12:53B4-57E2, "GET READY", "TIME'S UP!", "WINNINGS:"), the item names (1F:4801-4A9C),
the developer menu (0A:45B4-471D, "Start Debug Game") and pointer records (04:6DFC, 05:6D3E) are
data between scripts that the recompiler also analyzed as code. They are compiled like the rest,
and never run.

### The dance floor fix

The Dance Parlor's floor (Scuttle Town at night, rank chosen, 10 gems or more) runs a round under
a controller object, script 12:5713. Once the round is over, its first op (op 32) calls the
native routine 12:5725, which calls 58A9 (it draws A9:4978 through 00:0595) and goes on. About
180 frames later the round's end makes an object, script 12:63C4, in object slot 14 (WRAM bank
3), which it does not place: the object starts where the slot's last object was left,
slot+$34-$38, D718-D71C. On cleared WRAM, as on emulators and in this runtime, that is 0,0, and
the game stays on "NOT ENOUGH GEMS!" (or the winnings) for good. Griever's fix sets D719-D71C to
8, 8, 8, 2 (x $08xx, y $0208) at the start of 12:5725, and the object made there 180 frames
later starts at them and ends some 40 frames after. Played to a round's end here, the fix's ten
instructions run in the round's 1,891st frame (from the owner's last line), and Shantae is back
with the owner about 330 frames later; the stock game, before this build, stayed on "NOT ENOUGH
GEMS!".

This build fixes the stock game the same way without the patch: `extras.c` writes those four
bytes through the game's memory map as the controller's op 32 at 12:5713 dispatches (the script
VM's `LD H,$06` at 00:1345 and 00:1A12, the "Text speed" hook's site), so the patch, when chosen,
writes the same values again. 58A9's dozen other callers are left alone.

Two of the small fixes are already options of this build: *GBA Enhanced mode* boots as a Game
Boy Advance by default, and *Colors: Original GBC colors* (the default) has the GBC palettes
patch's effect at 00:1CB6 without changing the ROM. With the GBA Enhanced patch the Colors
option still decides the palettes; with the GBC palettes patch they are the GBC ones whatever
it says, since its `LD A,1` at 00:1CB4 comes before the option's `CP 3` at 00:1CB6.

## Checks

`python tools/check_patches.py` (`--only NAME` for some): for every patch in `patches/`, chosen
on the stock ROM as a player chooses it:

- every instruction position it changes, run through the game's dispatcher on the patched ROM
  against the reference interpreter, from both flag states (`whole_rom_check ROM patches
  PATCH...`): 1,060,826 comparisons for the eight patches. This found 20 analyzed entries with
  nothing emitted (an empty block, such as 0B:79FF and 0E:74CD, in the banks' unused space) that
  returned without running anything; two translations put bytes there. The recompiler now emits
  them as one step through the exhaustive path;
- at boot the runtime recognizes it, with every position it changes compiled;
- from a cold boot, the game's native code against the reference interpreter on the patched ROM,
  instruction by instruction, registers and memory compared after every one (`--frames`, 300):
  the patches that change the hardware check or the palette loader run their changed code here,
  in a routine stepped through;
- from a cold boot, WayForward's debug mode to the scene grid, scene 3C's text and the first
  town's shop to its menu, pictured in `logs/patches/<patch>/`, with no interpreter; then the same
  differential from the shop's menu, the cursor moved, an item bought, out again;
- with the dance floor fix (12:5725 changed): from the patch's debug grid, the dance floor the
  way a player gets there at night. The Zombie Caravan (row N's fourth entry), Up at the Caravan
  Gates, where she starts, to the land outside; Select and the first warp dance (Down Up Left
  Right), which sets her down outside Scuttle Town's wall; Up held at its door (the door object,
  script 18:4425, takes Up held 11 frames while she stands 29 to 7 pixels left of its x); the
  Dance Parlor (door label 7); the owner, B through his talk (Yes, rank E); the round left to
  run with nothing pressed. The controller must reach 12:5713-5724 with D719-D71C at 8, 8, 8, 2,
  Shantae must be back at the owner (x 1335) 600 frames later, pictured in
  `logs/patches/<patch>/`, and a differential from 120 frames before the round's end runs the
  fix's code;
- the patched ROM file itself, picked as the ROM with no patch on, starts as that patch.

`python tools/check_dance_floor.py` plays the same route on the stock game from a cold boot (the
fix in `extras.c`) and runs the round's end against the interpreter.

And three patches it makes: one changing the boot's hardware check (00:3976 `LD A,$00` to
`LD A,$01`), which no build has native code for, must stop the game there with its message;
one changing a byte no analyzed routine reaches must run; one growing the ROM must be refused. The launcher's
line, read from a screenshot, must be green for a patch below and the data patch, amber for the
code patch (PLAY starts it) and the size patch (PLAY does not), and PLAY must keep the patch for
the next start.
