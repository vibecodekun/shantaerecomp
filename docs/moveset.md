# Smoother movement

"Smoother movement" (`moveset.c`, on by default; Mods page and Esc → Shantae) changes how
Shantae's base form handles: holding B runs at once, a whip no longer stops her, her air
speed follows B, and she can crawl at walking speed. It is a set of hooks in her own
movement routines in bank 6. Off, none of them does anything and the game plays as the
original, frame for frame.

Addresses are `bank:address` in the USA ROM (CRC32 `E994B59B`). The routines were read in
Ghidra 12 with GhidraBoy (`tools/build_ghidraboy.py`) and confirmed by tracing the game
through the debug server.

## How she moves in the original

The player is an object like any other: `CA13` points at her slot. Each tick the game
calls every object's movement routine (00:0C45: slot+$19 its bank, +$1A a `JP` to it) and
then runs every object's script (00:1305: +2–+4, with a timer at +$16). Her routines read
the joypad (`FF8B` held, `FF8C` newly pressed) and switch scripts; a script shows the
animation frames and installs the next routine.

| Move | Routine | Script | Notes |
|------|---------|--------|-------|
| Idle | 06:4AA7 | 06:49F2 | B pressed → whip, A → jump, a direction → "start moving", Down → crouch |
| Start moving | | 06:4FCE | walks, or runs when the run flag `CB3C` is 1 |
| Walk | 06:502A | 06:4FD5 | 1 pixel a tick (`$000100` at +$41; left is `$FFFEFF`) |
| Run | 06:6A8C | 06:6A37 | 2 pixels a tick, while B is held; sets +$64 |
| Jump, fall | 06:5520 | 06:5364, 06:5457 | 2 pixels a tick when +$64 is set, else 1 |
| Whip | 06:5E52 | 06:5D61 | 8 frames of 3 ticks (+$6E counts them); the hit on the fifth (04:5641) |
| Whip, resumed | 06:5E52 | 06:5DCB | from the frame in +$6E: an air whip that landed |
| Air whip | 06:6CCD | 06:6C2C | moves like the jump |
| Crouch | 06:5F3C, 06:6094 | 06:5F0C, 06:6044 | going down, and down |
| Crawl | 06:648B | 06:6259 | half a pixel a tick (`$000080`, left `$FFFF7F`) |
| Crouch whip | 06:670D | 06:6612, 06:6681 | |

Three things follow from this code, and they are what the feature changes.

**B is both the whip and the run.** A separate script (06:70BE) watches B: it sets the run
flag `CB3C` once B has been held for 15 ticks and clears it on release. "Start moving"
(06:4FCE) and the walk routine (06:50AC) run when it is set. But the idle and walk routines
test for a *new* B press first (06:4AD4, 06:509A) and start the whip, and the whip routine
zeroes her speed on every tick (06:5E52: three `LD A,0` to +$41). So B with a direction,
from standing or walking, is 24 ticks of whip on the spot, a tick in the idle routine, and
then the run.

**Air speed is decided on the ground.** The jump and air whip routines read +$64 and move
at 2 pixels when it is set (06:5632, 06:565E, 06:6D32, 06:6D5E). Only the run script sets
it (06:6A3D); the walk and idle scripts clear it. A standing jump therefore moves at 1
pixel a tick however long B has been held, and a running jump at 2 even with B released.

**A whip that lands stops her.** The air whip routine moves freely, but when it lands it
resumes the ground whip at the same frame (06:6D96 → script 06:5DCB), whose routine zeroes
her speed.

The crawl has the same shape: B pressed is a crouch whip on the spot (06:64D1), and with
the run flag set and a direction held the crawl routine stands her up into the run
(06:65B1), as does the crawl's script (06:6259). Nothing ever sets +$65, which the crouch
whip's script tests at its end to go back to the crawl, so it always ends in the crouch
routine, which starts the crawl again a tick later.

## What the feature does

All of it applies to her base form only (`CB72` = 0). Two other forms have a run of their
own that reads the same run flag (0D:47ED, 1C:4DC8); they are left as they are.

**The run flag follows B.** Before each tick's movement routines (00:0C4B), before its
scripts (00:130B), and before a move that "Reduce input lag" runs early, `CB3C` is set to
whether B is held. There is no 15-tick wait: landing with B and a direction runs at once.
It stays clear while she is crouched under a low ceiling (`CB54` = 2 and `CB41` set), and,
with the crawl option, while Down is held.

**Whip on the move** (`whip_moving` in `shantae.ini`: 0 original, 1 slide, 2 cancel).

- *Slide.* The three `LD A,0` of the whip routine (06:5E56, 06:5E59, 06:5E5C) and of the
  crouch whip routine (06:6711, 06:6714, 06:6717) give a speed instead of zero when a
  direction is held: run speed with B held and walk speed without, or crawl speed
  crouched (walk speed with B and the crawl option). The whip's script, its hit and its
  sound are untouched. The standing whip turns her with the D-pad at any frame, as the
  air whip and the crouch whip already do, and sets +$64 as the run does, so that a jump
  out of it keeps its speed when the air speed option is off.

  On the last tick of the whip's script (it waits at 06:5DC7 or 06:5E4E with the timer
  about to run out) her script is pointed at the run (06:6A37) or "start moving"
  (06:4FCE) directly, in place of the idle script and its tick at rest. The crouch whip
  gets +$65 set while she slides, so that its own script ends in the crawl.

  The whip routines never check for leaving the ground (they call 06:475C but not
  06:47B9, since she could not move). When the mover reports her off the ground (`CB42`
  set and `CB3F` clear, the test of 06:47B9) the standing whip is handed to the air whip:
  its routine (06:6CCD), stance and the entry of its script for the frame in +$6E
  (06:6C53, 06:6C61, 06:6C6F, 06:6C7D, 06:6C8B, 06:6CA4, 06:6CB4, 06:6CBD), the mirror of
  what the original does when an air whip lands. The crouch whip falls (script 06:5457).

- *Cancel.* The tests for a new B press in the idle, walk, crouch and crawl routines
  (`AND 2` at 06:4AD6, 06:509C, 06:60EF, 06:64D3) see no press while a direction is held,
  so she starts moving, and with the run flag following B that is a run. A whip in
  progress ends when B and a direction are held (its routine sets the run or crawl
  script). An air whip that lands goes to the run or "start moving" script in place of
  06:5DCB (the script address at 06:6DA4 and 06:6DA7), or with Down held to the crawl
  (06:6DC3, 06:6DC6).

**Air speed** (`air_speed_b`). The `CP 0` after each read of +$64 (06:5637, 06:5663,
06:6D37, 06:6D63) compares with a value that makes it "not zero" while B is held and
"zero" when it is not.

**Crawl** (`fast_crawl`). With B held the crawl's speed operands (06:6575, 06:6578,
06:657B left; 06:6595, 06:6598, 06:659B right) give walk speed, and another `$100` comes
off the script's timer each tick, so the crawl's animation and its sound run at twice the
rate. The run flag stays clear while Down is held, so she does not stand up.

**One step a frame.** "Reduce input lag" runs a routine her script has just picked in the
same tick. Where the routine before it has already moved her (a whip that lands, or one
begun from a walk), the slide waits for the next tick; and on the tick a slide hands on
to the run or the crawl, the slide's own step is left out when that routine will move
her. With "Reduce input lag" off the slide starts on the tick after the press, as every
move does.

Nothing is kept between ticks: every decision is made from the joypad and her object, so
save states, rewind and Preemptive Frames need nothing extra.

## Hook sites

All are `[[imm_override]]` sites in `shantae.toml`; the hook is `shantae_moves_imm` in
`moveset.c`, called from `extras.c`.

| Site | Instruction | Hook |
|------|-------------|------|
| 00:0C4B, 00:130B | `LD A,$03` (existing sites) | the run flag follows B |
| 06:5E56, 5E59, 5E5C | `LD A,$00` | the whip's speed; the tick's changes at the first |
| 06:6711, 6714, 6717 | `LD A,$00` | the crouch whip's speed |
| 06:5637, 5663, 6D37, 6D63 | `CP $00` | air speed from B |
| 06:6575, 6578, 657B | `LD A,$7F/$FF/$FF` | crawl speed, left |
| 06:6595, 6598, 659B | `LD A,$80/$00/$00` | crawl speed, right |
| 06:4AD6, 509C, 60EF, 64D3 | `AND $02` | cancel: no whip with a direction held |
| 06:6DA4, 6DA7 | `LD A,$CB/$5D` | cancel: the script an air whip lands in |
| 06:6DC3, 6DC6 | `LD A,$81/$66` | cancel: the same with Down held |

## Checks

`python tools/check_moveset.py` plays `logs/states/moveset.state` (her base form on the
floor of a pit in the desert, with a platform above its left wall) with the feature off
and on, a frame at a time, and reads her object:

- B + Right: the original stands for 24 frames and then runs; the slide moves 2 pixels on
  every frame from the press, hits on the twelfth and runs from the 24th, with and
  without "Reduce input lag"; turning and jumping during it; cancel runs at once with no
  hit, whips with B alone, and ends the whip on a direction.
- Air speed: 2 pixels a frame with B held and 1 with it released, from a standing jump
  and from a running one.
- A whip begun in the air and landed: on the spot in the original, 2 pixels on every
  frame with the slide, straight into the run with cancel.
- A slide off the platform's edge: whip, air whip (the hit lands in the air), whip, run.
- The crawl: half a pixel; with B one pixel, crouched throughout; the original stands
  and runs; the crawl option off.
- Another form (`--form-state`, the tinkerbat of `flip-puzzle.state`): 324 frames the
  same with the feature on and off.
- `--original <exe>` (the previous release's): with the feature off, 324 frames of whips,
  runs, jumps and crawls give the same object, byte for byte.

Not covered: a low ceiling (the state's area has none), where the run flag is held clear
and the crouch whip's cancel takes the crawl script that skips the run test (06:6260).
