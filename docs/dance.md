# Easier dancing

"Easier dancing" (`dance.c`, on by default; Mods page and Esc → Shantae) changes two things
about Shantae's dances: the steps are entered like a code, at any speed, and after an animal
transformation, and after turning back into herself, she blinks and cannot be hurt for two
seconds, as after a hit. Each has an
option; with both at *Original*, or the feature off, the dance plays as the original, frame
for frame.

Addresses are `bank:address` in the USA ROM (CRC32 `E994B59B`). The routines were read in
Ghidra 12 with GhidraBoy (`tools/ghidra_listing.py`) and confirmed by tracing the game
through the debug server. Object scripts are bytecode for the script VM (00:1342, opcode
table at 00:0600), decoded by hand.

## How she dances in the original

Select in her idle routine starts the dance script 0E:4179. It makes 0E:4D40 her movement
routine, clears the dance's RAM, sets a beat of four ticks (slot+$71 = $0400) and fills a
ring of 16 steps (CB5B–CB6A) with Down (0E:425D). Then it loops:

1. It calls a **pose** from a table by the step in CB5A (op 38 at 0E:41A0, then 0E:41C5):
   0 the plain dance (0E:4677), 1 Down (0E:47CB), 2 B (0E:48AB), 4 Right (0E:4992), 6 Up
   (0E:4A7C), 7 A (0E:4B5C), 9 Left (0E:4C43). A pose is eight beats or more of animation.
   During some of its beats the **window** is open (slot+$18 = 0, ops `30 00`/`30 01`, and
   0E:4D26 shuts it again if a button is already held as it opens).
2. While the window is open, 0E:4D40 ORs the held buttons into CB59.
3. At the pose's end 0E:4D8F turns CB59 into the next step in CB5A: exactly one of Down,
   B, Right, Up, A or Left gives its number, anything else (nothing, or two buttons) 0.
4. 0E:42A3 records the step at CB6C in the ring; a Down marks a start (CB6B).
5. 0E:42D2 compares the steps since the last Down with every dance in the table at 0E:4342
   whose bit is set in CB1B+n (learned). An entry is eight bytes: its steps, the bit's mask
   and byte, the script to run and the form (CB81; $FF for healing and warps). A match needs
   one more step recorded after the dance's last.
6. On a match slot+$18 becomes $FF, CB6D–CB6F hold the script and the dance script goes on
   at 0E:41F9: the transformation's flash, then a jump to that script.

So one step is taken every pose, in rhythm; a beat with no press, or with two, breaks the
sequence; and the dance begins a pose after its last step. A new Select at any time ends the
dance (0E:4D48, script 06:49F2).

The table, which is also the in-game Dance Menu:

| Steps | Dance |
|-------|-------|
| ↓ → | monkey |
| ↓ ← | elephant |
| ↓ A | spider |
| ↓ B | harpy |
| ↓ ↑ A | heal |
| ↓ ↑ ↑ | tinkerbat (GBA) |
| ↓ ↑ ← → , ↓ ↑ → ← B A , ↓ ↑ → → B A , ↓ ↑ ← ← B A , ↓ ↑ B ← B → | the five warps |

Every dance begins with Down, and none has Down after its first step.

## Quick steps

`quick_steps` in `shantae.ini` (1, default; 0 the original rhythm).

- The window never opens: the `CP 0` on slot+$18 in 0E:4D40 (0E:4D54) compares with a value
  that makes it "not zero", so CB59 stays clear and the original records only 0s, which
  match nothing.
- Every new press is a step, taken where 0E:4D40 tests for Select (the `AND 4` at 0E:4D48),
  unless Select itself is pressed. A step that goes on to some learned dance from the steps
  so far is kept; a Down starts over; any other press is skipped. When the steps make a
  whole dance it matches as 0E:42D2 matches, in the same tick: CB6D–CB6F, CB81 and slot+$18
  as the original sets them, and her script on at 0E:41F9.
- Two presses in one tick count in the order that goes on (Down first).
- Each press shows its pose: CB5A is set to its step and her script started at 0E:41C5 with
  nothing on its call stack, so the pose plays from its first beat in the tick of the press,
  and a later press starts its own. Afterwards the loop's next step is 0, the plain dance.
  The press that completes a dance goes straight to the transformation.
- The steps so far are kept in CB6D, which the original only uses for a match's script:
  the high nibble is the table entry whose first steps they are, the low nibble how many.
  The dance's start (the ring's fill, `LD A,$01` at 0E:4260) clears it. Kept in game RAM,
  it is saved with save states and rewind like everything else.

## Invincibility after transforming

`transform_invincible` (1, default; 0 the original).

CB56 counts the reasons she cannot be hurt: the damage handler returns while it is set
(04:4E5F), 06:72CD adds one and 06:72D2 takes one off. A hit (measured against a slider in
the desert) adds one, plays 39 ticks of knockback, and her form's hurt script then spawns a
blinker (06:7265, script 06:72DA): every 4 ticks it sets her slot+$32 to $80 (hidden) or $7F
(shown), 15 times, and then takes the one off. In all about 160 ticks safe, the last 120
blinking.

A transformation adds one at the match (0E:4207) and spawns an object (0E:4458, script
0E:4E82) that sets the new form (0E:4EC8), waits while she is transformed and her entrance
plays, and takes the one off when the entrance ends: about 70 ticks after she appears, 50
after she can move, with nothing to show it. A slider that reaches her a moment later hits
her at once.

With the option, when 0E:4EC8 sets a form (CB81 is not $FF: the heal dance's puff passes
$FF), it adds one more to CB56 and spawns the same blinker, so she flashes and cannot be hurt
for 120 ticks from the moment she appears. 0E:4EC8 was called by the script VM's native call
(op 32, 00:1A46), which restores the ROM bank when it returns, so the routine's `RET` becomes
a jump to 06:7265 and the blinker returns to the VM in its place: the `CP $FF` at 0E:4ECB
stops there, and the runtime's step hook (`shantae_dance_step`) does the routine's last
store (`LD (CB72),A`) and the jump.

### Turning back

Select in a form turns her back into Shantae with the script 0E:4000 (decoded by its
opcodes): routine 0E:40BD, two calls to 06:72CD (CB56 is 2), an effect object (0E:408D,
script 0E:4DF1), then routine 0E:40C4 and the change back (frames 0E:403F–0E:4075; CB56 drops
to 1 about 70 ticks in), a call to 06:72D2 at 0E:407B, and her idle script
06:49F2 (or the crouch, 06:6044). So she is safe while she turns back, about 120 ticks, and
the last one comes off on the tick she can move again; from then nothing protects her (the
user's report on 2026-10-07). A slider in the desert that reaches her while she turns back
hits her on the next tick.

With the option, that call goes to the blinker spawner 06:7265 instead, which takes over the
one she still has: she flashes and cannot be hurt for 120 ticks from the tick she can move,
and the blinker takes it off at the end. If nothing is left by then CB56 is made 1 first, for
the blinker to take off. Op 32 (00:1A46) reaches the routine with `JP HL` (00:1A66), so the
generated dispatcher offers 06:72D2 to `game_dispatch_override` (`ram_native.c`), which asks
`shantae_dance_dispatch`; it tells this call from the routine's other callers by op 32's
stack: the return to 00:1A5B, the script's DE at 0E:407F (just past this call), her slot in BC
and the script's bank 0E in the saved A.

## Hook sites

`[[imm_override]]` sites in `shantae.toml`; the hook is `shantae_dance_imm` in `dance.c`,
called from `extras.c`.

| Site | Instruction | Hook |
|------|-------------|------|
| 0E:4260 | `LD A,$01` (the ring's fill) | clear the steps so far |
| 0E:4D48 | `AND $04` on FF8C | this tick's presses as steps, and their pose |
| 0E:4D54 | `CP $00` on slot+$18 | the window stays shut |
| 0E:4ECB | `CP $FF` on CB81 | a form is set: stop, then the blinker (step hook) |

And one dispatch hook: 06:72D2 reached from op 32 at 0E:407B (the turn back's last call) goes
to 06:7265, the blinker (`shantae_dance_dispatch`, from `game_dispatch_override`).

## Checks

`python tools/check_dance.py` plays `logs/states/dance.state` (the user's state1 on
2026-10-06: Shantae in a desert pit with every dance learned):

- Every dance of the table matches on the frame of its last step, the presses a frame apart,
  with the table's script and form.
- Skipped presses (before Down; Right after ↓ ↑ ← ←; toward a dance not learned), Down
  starting over, Down and Right in one frame, and four steps on four frames.
- With the original rhythm, the same fast presses match nothing.
- Landed beside a slider with the debug flight and turned into the monkey: the original is
  hit six frames after its protection ends; with the option she blinks and is not hurt for
  the 120 frames from her appearance.
- The monkey flown beside the slider, then Select: she is safe while she turns back in both;
  the original is hit a frame after she can move, with the option she blinks from that frame
  and is not hurt for 120 frames. v0.1.11 fails this (its protection ends a frame after she
  can move, and the slider hits her).
- `--original <exe>` (the previous release's): with the feature off, and with both options
  at *Original*, 298 frames of dances in and out of rhythm give the same object and dance RAM.
