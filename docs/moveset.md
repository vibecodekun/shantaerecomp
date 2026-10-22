# Smoother movement

"Smoother movement" (`moveset.c`, on by default; Mods page and Esc → Shantae) changes how
Shantae's base form handles: holding B runs at once, a whip no longer stops her, B whips
and Select dances the moment she lands, her air speed follows B, she can crawl at
walking speed, and B whips while crawling under a low ceiling. It is a
set of hooks in her own
movement routines in bank 6. Its *Transformations* option (`forms.c`, below) does the same
for the monkey and the tinkerbat, gives the spider a run and every climb a faster one with B,
keeps the harpy's speed (her run going on through her talons) and her flaps through her
talons and on the frame she lands, lets a charge be pressed again, turned, jumped out of and
started from the elephant's turn, landing and stomp, walks the elephant through her turn,
gives the tinkerbat the monkey's box, and makes a web something A takes hold of, stays on to
its edges and jumps off. Off, none of them does anything and the game plays as the original,
frame for frame.

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
| Landing | 06:59E9 | 06:59C2 | a landing with no direction held: A, a direction or Down, never B or Select |
| Whip | 06:5E52 | 06:5D61 | 8 frames of 3 ticks (+$6E counts them); the hit on the fifth (04:5641) |
| Whip, resumed | 06:5E52 | 06:5DCB | from the frame in +$6E: an air whip that landed |
| Air whip | 06:6CCD | 06:6C2C | moves like the jump |
| Crouch | 06:5F3C, 06:6094 | 06:5F0C, 06:6044 | going down, and down |
| Crawl | 06:648B | 06:6259 | half a pixel a tick (`$000080`, left `$FFFF7F`) |
| Crouch whip | 06:670D | 06:6612, 06:6681 | |

Four things follow from this code, and they are what the feature changes.

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

**A standing landing loses B and Select.** When the jump routine lands her (06:5683) with
Left or Right held she starts moving, with Down she crouches, and otherwise it sets the
landing script 06:59C2: the landing routine 06:59E9 for four frames of landing (24 ticks),
then the idle script. The landing routine takes a new A (a jump, 06:5A0B), a direction or
Down, but has no test for B or Select, and the landing script is set over the air whip that
a new B started earlier in the same tick (06:560C); the jump routine has no Select at all.
So a B or a Select pressed on the frame she lands, or on any of the 24 after it, is lost (the
user's report on 2026-10-10: the whip "as soon as I land on the ground from a standstill",
and Select the same). The other forms' landings take their attack (the tinkerbat's 1C:5596
tests B at 1C:55AC) or have none.

The crawl has the same shape: B pressed is a crouch whip on the spot (06:64D1), and with
the run flag set and a direction held the crawl routine stands her up into the run
(06:65B1), as does the crawl's script (06:6259). Nothing ever sets +$65, which the crouch
whip's script tests at its end to go back to the crawl, so it always ends in the crouch
routine, which starts the crawl again a tick later.

**Under a low ceiling the crawl loses B.** Both crouch routines ask 06:4263 whether there
is room to stand and keep the answer in `CB41`. The crouch routine (06:6094) then still
tests a new B (06:60ED), but the crawl routine jumps past its B test, the run, A and the
release of Down when it is set (`JR NZ` at 06:64CF to the directions at 06:6513). In a
corridor one tile high, B while crawling did nothing, and only a crouch held still whipped
(the user's report on 2026-10-15, their state2 in the first labyrinth: "it's only when i
whip from a standstill and start moving that i can whip freely").

## What the feature does

All of this applies to her base form (`CB72` = 0). Two other forms have a run of their
own that reads the same run flag (0D:47ED, 1C:4DC8); they are in the next section.

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

**B and Select as she lands.** The idle routine tests a new B (the whip, 06:4B9F), then A
(the jump), then Select (the dance: script 0E:4179, 06:4B5D), each a branch that sets her
script and returns, as the landing routine's A branch (06:5A78) does. In the landing routine
a new B, or a new Select without B or A, takes the A's branch (the `AND 1` at 06:5A0D and
the `CP 1` at 06:5A0F see it) to the whip script 06:5D61 or the dance script 0E:4179
instead of the jump's 06:5364 (the script address at 06:5A86 and 06:5A89, its bank at
06:5A8C). With *Cancel* and a direction held B is not seen, and the direction starts her
moving as in the idle routine. On the landing frame itself, the script the landing sets
(06:56BB, 06:56BE, its bank at 06:56C1) is the whip in place of 06:59C2 when her script is
the air whip that the jump routine has just started for a new B, so the code watcher 0E:5DE0
that guards the jump routine's B still applies (it never takes Select), or the dance for a
new Select without A or B. Neither happens while scripts hold her input (`CB70` set, which
the idle routine tests before any button). The whip and the dance are the ones from standing,
and "Reduce input lag" runs them in the tick of the press as it does from standing.

**Air speed** (`air_speed_b`). The `CP 0` after each read of +$64 (06:5637, 06:5663,
06:6D37, 06:6D63) compares with a value that makes it "not zero" while B is held and
"zero" when it is not.

**Crawl** (`fast_crawl`). With B held the crawl's speed operands (06:6575, 06:6578,
06:657B left; 06:6595, 06:6598, 06:659B right) give walk speed, and another `$100` comes
off the script's timer each tick, so the crawl's animation and its sound run at twice the
rate. The run flag stays clear while Down is held, so she does not stand up.

**B under a low ceiling.** In every whip mode, a new B in the crawl routine with `CB41`
set starts the crouch whip as 06:6552 does elsewhere: the `AND $20` of the Left test at
06:6515, where the routine goes under a low ceiling, sets the crouch whip's script
06:6612. The routine then carries on to the directions, so on that tick the crawl's speed
operands give no step (06:6552 returns before the crawl's), and with no direction held the
crouch's script it sets (06:6531, `LD A,$44` and `LD A,$60` at 06:653F and 06:6542) is
the crouch whip's. Its routine 06:670D takes it from there, sliding or on the spot, and
"Reduce input lag" runs it in the tick of the press. With *Cancel* and a direction held B
is not seen, as at 06:64D3.

**One step a frame.** "Reduce input lag" runs a routine her script has just picked in the
same tick. Where the routine before it has already moved her (a whip that lands, or one
begun from a walk), the slide waits for the next tick; and on the tick a slide hands on
to the run or the crawl, the slide's own step is left out when that routine will move
her. With "Reduce input lag" off the slide starts on the tick after the press, as every
move does.

Nothing is kept between ticks: every decision is made from the joypad and her object, so
save states, rewind and Preemptive Frames need nothing extra.

## Transformations

`smooth_forms` in `shantae.ini` (1, default; 0 leaves the forms as the original). It follows
the feature's other options: *Whip on the move* for the monkey's claw and the tinkerbat's
sword and the spider's shot, *Air speed* for their jumps. `CB72` is the form: 1 monkey,
2 elephant, 3 harpy, 4 spider, 5 tinkerbat.

**The monkey (bank 0D) and the tinkerbat (bank 1C)** are built like Shantae:

| Move | Monkey | Tinkerbat |
|------|--------|-----------|
| Idle | 0D:4510 | 1C:4BDB |
| Walk | 0D:4840 | 1C:4E0F |
| Run | 0D:53FC | 1C:5C00 |
| Start moving (script; walk, or run with `CB3C`) | 0D:47ED | 1C:4DC8 |
| Jump, fall | 0D:4CE2 | 1C:5375 |
| Attack (script) | claw 0D:4FC8 (0D:4F95) | sword 1C:57AC (1C:5770) |
| Air attack | | 1C:56A5 (1C:5659) |
| Climb | 0D:4ABE | 1C:5080 |

A new B on the ground attacks (the `AND 2` at 0D:4578, 0D:4894, 1C:4C43, 1C:4E63), and the
attack zeroes her speed on every tick with three `LD A,0` to +$41 (0D:4FCC, 1C:57B0): 16
ticks on the spot for the claw, 13 for the sword. Unlike the whip, the attack routine checks
for falling, and once its hit is out (its script sets slot+$18) a direction hands on to "start
moving". Their jumps take run speed when +$64 is set (`CP 0` at 0D:4E48, 0D:4E74 in the
monkey's jump; 1C:5518, 1C:5544 in the tinkerbat's fall and jump; 1C:5724, 1C:5750 in her air
sword), and the run flag `CB3C` waits for 15 ticks of B as it does for Shantae. With the
option:

- The run flag follows B (with Shantae's, before each tick's routines, scripts and early move),
  except on the tick of a new B. Their walk routines test the run flag *before* the new B
  (0D:4877 before 0D:4894; 1C:4E46 before 1C:4E63), where Shantae's tests the B first
  (06:509A before 06:50AC). v0.1.11 set the flag on the press itself, so B pressed while
  walking always ran and never attacked; the slide worked only from standing, after a
  landing, or when a press happened to fall on a tick in the idle routine (the user's
  report on 2026-10-07: "only through rapid b presses"). The original's flag waits 15 ticks
  of B, so it is never set on a new press either. With *Cancel* and a direction held the
  flag follows B on that tick too, since there the press is meant to run. An early move
  sees no new presses (`run_player_move` clears `FF8C`), so it has the flag as B is held.
- *Slide*: with a direction held the attack's speed is run speed with B held, walk speed
  without, and turns her; once the hit is out, with "Reduce input lag", the step is left to
  the routine "start moving" hands on to in the same tick. *Cancel*: a new B with a direction
  held is not seen, so she runs at once, and an attack in progress goes on to "start moving"
  when B and a direction are held.
- *Air speed*: the `CP 0` compares with a value that is "not zero" while B is held. The
  monkey's air claw (0D:4ED7) only turns her (0D:4F85, 0D:4F8D) and keeps the speed of the
  jump's last tick, which is walk speed after B is let go to press it; with the option a
  direction gives it the jump's speed, run speed with B held, as the tinkerbat's air sword has.
- Climbing (0D:4ABE, 1C:5080) is a pixel a tick (the speeds at 0D:4B6F, 0D:4B8B; 1C:5131,
  1C:514D); with B held it is two, and the climb's animation counter (+$66, `ADD`/`SUB $40`)
  moves twice as far.

**The harpy (bank 0D)** has no run flag. Her run (0D:6F38) adds $10 a tick against $08 of
drag, up to two pixels a tick; her talons (0D:751F, script 0D:74E5) keep her speed, but end
in her idle routine (0D:6D3A), whose three `LD A,0` to +$41 (0D:6D86) stop her for a tick
before the run starts again from nothing. With the option the idle keeps her speed when a
direction is held the way she is moving, so the run goes on from it. That also keeps her
speed when she lands from flight with the direction held.

The talon routine is the mover and the landing test alone, so through its 20 ticks her speed
stood where the talon found it: a talon early in the run, at a fraction of a pixel a tick, held
her there, and the run then had to build from it (the user's report on 2026-10-21: "i get some
slight speed, claw, then lose speed"). With the option, at her movement `JP` (the dispatcher in
`ram_native.c`), a held direction changes her speed during the talon as her run does on the
ground (0D:7073, 0D:70BE: $10 that way while under two pixels, then the drag of 0D:69D4) and her
flight does in the air (0D:7374, 0D:73AF), and turns her. With "Reduce input lag" the talon and
the run are each run in the tick their script picks them, after the routine before has moved
her; the talon's first step then waits for the next tick, and the idle that hands on to the run
takes no step of its own (it sets the run's script and her facing, as 0D:6E21 and 0D:6E47 do),
so she moves a step a tick through a talon and out of it.

She flies by flapping. A in her flight routine (0D:72A6, the test at 0D:72F4) calls 0D:7355,
which starts the flap script 0D:7210. Decoded by its opcodes, that script sets the flight
routine (op 2E), calls 0D:7256 (op 32), which takes $18 from her vertical speed at +$44 and
makes it at least $FFFD7F upward (about 2.5 pixels a tick; gravity adds $28 a tick), sets the
stance `CB54` to 1 (op 94), and queues sound $19 (op BC `19 40 1E 00`). The talon routine
(0D:751F) is only the mover 0D:680B and the landing test 0D:694C and reads no buttons, so
during the talon's 20 ticks every A was lost and she sank (the user's report on 2026-10-07).
With the option, at the start of each tick's script pass (00:130B, where the flap script's own
lift would come), a new A while her routine is the talon and her script still the talon's
(0D:74E5–0D:751E; a landing this tick has set 0D:6A54) does what the flap script does for
it: the lift of 0D:7256, `CB54` = 1, and the sound, queued as op BC queues it (00:13D7: `C203`
moves on 8 and `01 19 40 1E 00` goes to `C204` + `C203`). The talon's script, animation and
hit (04:5641) go on untouched, and the talon ends as it does in the air (its script tests
`CB54` at 0D:7515). From her run on the ground an A lifts her off the same way; the original
lands her from flight in the run with `CB54` still 1, as here.

An A could also be lost as she reached the ground (the user's report and video on 2026-10-21,
the A that "occasionally" did nothing in the air). Her flight (0D:72A6), her glide (0D:6C35,
script 0D:6BF0) and her run (0D:6F38) take A first and start the flap (0D:7355, 0D:6CD5,
0D:7044), and only then move her; when the mover puts her down, or the ground drops away under
her run, the same routine sets the glide's script (0D:72C2, the run's 0D:69A9) or the landing's
(0D:6C55 the run, 0D:6CA4 standing, 0D:73EA) over the flap's, which never runs. With "Reduce
input lag" the glide is run again in that tick and lands her, so two frames lost the press. Over
the desert's slopes the user played on in their state3, A on the frames around each touch of the
ground did nothing. With the option, at the start of the script pass, a new A with her routine
the flight, the glide or the run and her script one of those, set this tick and not yet run
(its start, its timer $0080), sets the flap's script again.

**The tinkerbat has the monkey's box.** Hers (00:2975 loads it with each frame of animation,
at +$47–+$4E) is 10 by 20 pixels; the monkey's is 7 by 14, with the same feet and middle. The
wall and floor tests (00:29F6, the climb's wall test 1C:4F51) take the whole box, so the
16-pixel pipes and passages the monkey climbs through were walls to her: the ice tower's
hidden passage behind the ledge of the user's state2 (from the wall at x 5288 through to a
warp squid's alcove, crossed by a shaft 16 pixels wide), and the pipe maze above the beach of
the user's state1 of 2026-10-10. With the option, before each tick's movement her frame's box
is the monkey's, feet and middle kept. The box is rewritten only when it is her frame's or
the monkey's, and her object's other box (+$4F–+$56) is unchanged. v0.1.12 to v0.1.14 made
the box smaller only near such a gap, read from the map, which missed the pipes.

**The charges.** The elephant charges with B (script 0D:64A5, routine 0D:650E) and the
tinkerbat with Down and B (1C:646D, 1C:64CA). The charge routines read no buttons, so a
charge ran until its speed was gone. With the option a new B (Down and B) during one charges
again, at full speed, and a new A jumps (0D:60B6, 1C:51F9), as from standing. The tinkerbat's
charge started only from standing (1C:4C82): her walk, run and landing take B as the sword.
Down and B charge from those too, so she can charge while pressing into what she breaks (the
orange pillar at the beach, 16:5966: a charge hits what it touches when it starts, since
00:39A3 needs the object's +$1F, set when she pushes into it; two hits, then 160 frames of
crumbling). A charge goes the way she faces (the scripts' native calls 0D:64E9 and 1C:64A5
read +$31), and the idle and walk routines take B after a direction, so B with the other way
held charged the way she faced; with the option a charge starting on a tick, from a charge
too, faces the way held (the user's report on 2026-10-13).

**The elephant's turn, landing and stomp** (the same report). Her turn (script 0D:5EF7,
routine 0D:5F1E: two frames of five ticks, then 0D:5F15 faces her about), her landing
(0D:61D8, 30 ticks) and her stomp (0D:62A6, a landing with Down held once it is learned:
the hit through 04:5641, then four frames of ten ticks in 0D:62D8) take no B, and the turn
and the stomp no buttons at all, so a B during the turn was lost and the stomp held her for
40 ticks. With the option, at the start of the script pass (as for the charges): B charges
and A jumps from the turn, facing the way it turns her; B charges from the landing; and from
the tick after the stomp's hit B charges, A jumps and a direction hands on to the idle
script, whose routine walks or turns her. With nothing pressed the stomp plays out.

The turn also stood her: the other way held from standing was ten ticks of the turn on the spot
(its script's op 68 zeroes her speed, and its routine is the mover alone), then a tick in the
idle routine, whose `LD A,0` (0D:5C57) stop her as it starts the walk; from a walk, the walk
routine (0D:5E9C, 0D:5EBA) set the turn's script and then, finding her speed zero, its idle
script over it (0D:5E35), a tick more (the user's report on 2026-10-21: "she does a turn
animation before you finally start moving"). With the option she walks the way held through the
turn, at the walk's speed, from her movement `JP`, while the turn's animation plays; the idle
after it keeps that speed with the direction held the way she faces, and the walk's turn is
kept (its idle script's address at 0D:5E43 and 0D:5E46 is the turn's when the turn was set this
tick). Where "Reduce input lag" runs the idle in the tick of the turn's last step, the idle
keeps no speed, so she moves a step a tick; the walk it hands on to sets its own.

**The spider (bank 13)** walks a pixel a tick (13:4C2E, 13:4C43) and has no run. With the
option B held is two, with the walk's animation at twice the rate, and with *Air speed* the
same in her jump (13:5280, 13:5295). A new B starts her shot (script 13:4C77), whose routine
13:4CA0 is a `RET`: she stands for its six ticks. With *Whip on the move* not *Original*, a
direction held during it moves her with the walk's mover (13:4BE5) through the dispatcher at
her movement `JP` (`ram_native.c`), at run speed with B held, so B runs and shoots at once.

**Webs.** On a web (tiles $64, routine 13:6068) the spider moves a pixel a tick each way,
undone when the tile there is solid; with B held each step and its undo are two, and her
animation (+$75) twice. The tinkerbat climbs webs with a copy of it (1C:6113, with no shot),
the same with B held. Their jump and fall (13:5105, 1C:5375) take hold of a web whenever the
tile by her feet is one (the tests 13:476B and 1C:5D41 end in `CP $64`), and the tinkerbat's
jump script checks before she has left the ground (1C:5282, through 1C:529B, then the grab
script 1C:5F6F), so a jump in front of a web stuck to it; on a web A did nothing. With the
option the test finds a web in the air only on a new A (its return address tells the jump's
test from the climb's), never as the jump starts, and a new A on a web jumps off it with the
script for climbing off its top (13:5063, 1C:51B7): A jumps, A again takes hold, and A on the
web jumps off.

Each step on a web is undone where the tile is solid: after the web test's `CP $64`, a
`CP $02` (13:60F0, 13:612C, 13:6168, 13:61A4; 1C:617F, 1C:61BB, 1C:61F7, 1C:6233) tells a wall
from anything else, and any other tile sets a bit of +$74 for the way she went, which the
routine's end (13:61FB, 1C:628A) turns into leaving the web: a jump off its top or its sides, a
drop off its bottom. So with A taking hold and jumping off, a web still let go of her by itself
at its edges (the user's report on 2026-10-21, their state2 in the first labyrinth, the tinkerbat
and the spider alike: "i'd rather stick to spider walls until i press "a" again"). With the
option that `CP $02` compares equal for every tile that is not a web: the step is undone, she
stays on the web at its edge, and A jumps off.

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
| 06:6515 | `AND $20` | the crawl under a low ceiling: B starts the crouch whip (and no crawl step on that tick at 06:6575-659B) |
| 06:653F, 6542 | `LD A,$44/$60` | the crouch's script there with no direction: the crouch whip's |
| 06:6DA4, 6DA7 | `LD A,$CB/$5D` | cancel: the script an air whip lands in |
| 06:6DC3, 6DC6 | `LD A,$81/$66` | cancel: the same with Down held |
| 06:56BB, 56BE, 56C1 | `LD A,$C2/$59/$06` | the landing frame: the whip's script for the air whip's B, the dance's for Select |
| 06:5A0D, 5A0F | `AND $01`, `CP $01` | the landing routine: a new B or Select takes the jump's branch |
| 06:5A86, 5A89, 5A8C | `LD A,$64/$53/$06` | that branch's script is the whip's or the dance's |

The transformations' sites; the hook is `shantae_forms_imm` in `forms.c`, the run flag and
the tinkerbat's box are `shantae_forms_tick`, called with Shantae's, the presses at the start
of the script pass (the harpy's flaps, in a talon and as she lands, the charges, the elephant's
turn, landing and stomp, A on a web) are `shantae_forms_script_pass`, called at the existing
00:130B site, and the spider's shot on the move, the harpy's talon and her idle's hand-on to the
run, and the elephant's walk through her turn are `shantae_forms_dispatch`, called by the
dispatcher at her movement `JP` (`ram_native.c`).

| Site | Instruction | Hook |
|------|-------------|------|
| 0D:4FCC, 4FCF, 4FD2 | `LD A,$00` | the claw's speed |
| 1C:57B0, 57B3, 57B6 | `LD A,$00` | the sword's speed |
| 0D:4E48, 4E74; 1C:5518, 5544, 5724, 5750 | `CP $00` | air speed from B |
| 0D:4F89, 4F91 | `LD A,$01/$00` to +$31 | the air claw turns her: and the jump's speed |
| 0D:4B73…4B9D; 1C:5135…515F | `LD A` speed bytes, `ADD`/`SUB $40` | climbing with B held |
| 0D:4578, 4894; 1C:4C43, 4E63 | `AND $02` | cancel: no attack with a direction held |
| 0D:6D86, 6D89, 6D8C | `LD A,$00` | the harpy keeps her speed |
| 0D:5C57, 5C5A, 5C5D | `LD A,$00` | the elephant's idle keeps the speed of her turn |
| 0D:5E43, 5E46 | `LD A,$E6/$5B` | the walk's turn is not replaced by its idle script |
| 13:4C32…4C4D, 13:528B…52A6 | `LD A` speed bytes | the spider's walk and jump with B held |
| 13:60DE…61AD; 1C:616D…623C | `SUB`/`ADD $01` | a step on a web and its undo, with B held |
| 13:61CE; 1C:625D | `ADD A,$80/$40` | the web's animation, with B held |
| 13:478D; 1C:5D69 | `CP $64` | the web test: in the air only on a new A |
| 13:60F0, 612C, 6168, 61A4; 1C:617F, 61BB, 61F7, 6233 | `CP $02` | a step off a web's edge is undone, as into a wall |

## Checks

The states are made from a cold boot by `python tools/make_states.py`, with WayForward's
debug mode: the code at "Press Start", Start Debug Game (every dance learned, 04:49D8), and
a scene of the debug grid, with Shantae set down by the debug flight (its docstring has the
details). It writes them to `logs/states/`.

`python tools/check_moveset.py` plays `logs/states/moveset.state` (her base form on the
floor of a pit in the desert, scene 1A, with a platform above its left wall) with the
feature off and on, a frame at a time, and reads her object:

- B + Right: the original stands for 24 frames and then runs; the slide moves 2 pixels on
  every frame from the press, hits on the twelfth and runs from the 24th, with and
  without "Reduce input lag"; turning and jumping during it; cancel runs at once with no
  hit, whips with B alone, and ends the whip on a direction.
- Air speed: 2 pixels a frame with B held and 1 with it released, from a standing jump
  and from a running one.
- A whip begun in the air and landed: on the spot in the original, 2 pixels on every
  frame with the slide, straight into the run with cancel.
- B pressed after a standing jump on the frame she lands and on each of the 24 after it: no
  whip in the original; with the feature the whip from that frame, its hit 12 frames on as
  from standing, with and without "Reduce input lag" and in every whip mode; with Right held
  too, the slide whips and cancel runs.
- Select pressed the same way: no dance in the original; with the feature the dance from
  that frame, its routine and script frame for frame as from standing, with and without
  "Reduce input lag", and Down, Right from there makes the monkey. A with Select jumps and
  B with Select whips, as from standing; with `CB70` set (input held) neither B nor Select
  starts anything.
- A slide off the platform's edge: whip, air whip (the hit lands in the air), whip, run.
- The crawl: half a pixel; with B one pixel, crouched throughout; the original stands
  and runs; the crawl option off.
- A low ceiling (`--corridor-state`, by default `logs/states/corridor.state`: crouched in
  the first labyrinth's corridor one tile high, as in the user's state2 of 2026-10-15). B
  and Right while crawling: the original crawls on with no whip; the slide whips from the
  press (its hit on the crouch whip's frame) at a pixel a frame into the crawl, and without
  "Reduce input lag" from the frame after; cancel crawls on. B alone on a tick of the crawl
  routine, with its `+$65` counting down and without: the crouch whip on the spot (the
  original none).
- Another form (`--form-state`, by default the tinkerbat of `tinkerbat-gap.state`): with
  Transformations off, 324 frames the same with the feature on and off.

`python tools/check_forms.py` plays the transformations, from `logs/states/dance.state`
(turned into the monkey, the elephant, the harpy, the spider and the tinkerbat),
`logs/states/tinkerbat-gap.state` (the tinkerbat on the ice tower's ledge, scene 3A, where the
user's state2 of 2026-10-06 stood), `logs/states/pillar.state` (the tinkerbat on the beach,
scene 3C, left of the orange pillar) and `logs/states/web.state` (in front of a web, scene 42,
turned into the spider and the tinkerbat), with Transformations off and on:

- The monkey and the tinkerbat, B and a direction from standing: the original attacks on the spot
  for 16 and 13 frames, then runs; the slide moves 2 pixels on every frame from the press
  into the run; cancel runs at once with no attack.
- The same with B pressed while walking: the original attacks on the spot; the slide attacks,
  moving 2 pixels a frame after the walk's own step on the press tick and on into the run, or 1
  with B tapped and back to the walk; cancel runs at once. v0.1.11 fails this (the run, no
  attack).
- A standing jump with B held: 2 pixels a frame, 1 when B is released (the original 1).
- The harpy's talons in her run: she goes on at her speed; the original starts again.
- The harpy flapping (A every six frames), then a talon with A every four frames: she rises
  more than 20 pixels through it and six sounds are queued (the talon's and five flaps'); the
  talon lasts its 20 frames and its hit comes out. The original sinks, with the talon's sound
  only; so does v0.1.11.
- The tinkerbat with the monkey's box: down the wall into the hidden passage and through it,
  then up the shaft's far wall into the alcove; up the beach's end wall into the pipe maze and
  up its shaft. The original climbs past.
- The tinkerbat walking into the pillar, Down and B: two charges break it and she walks on
  once it has crumbled; the original's walk takes B as the sword.
- The charges: B (Down and B) during one charges again at full speed, A jumps; with Left held,
  from standing or during a charge right, the charge goes left. The original ignores the
  presses and charges the way she faced.
- The elephant's turn: B charges and A jumps the way it turns her; B on her landing's first,
  11th and 26th frames charges; on the frame after a stomp's hit a direction walks, B charges
  and A jumps, and with Down alone it plays its 40 frames. The original drops those presses.
- The elephant turning left, from standing and from a walk right: a pixel left on every frame
  from the press, through the turn into the walk; Left tapped for three frames turns her and
  stands her. The original stands for the turn's ten frames, and from a walk a frame more.
- The harpy's talon two frames into her run left: through its 20 frames the speed goes on
  8 more a frame, as the run's, and no frame moves her more than her speed. The original holds
  the speed she had.
- The harpy's A on each frame from 2 to 49 after a standing flap, through her landing: each
  queues a flap's sound. The original loses the landing frames.
- Climbing with B held, the monkey and the tinkerbat: two pixels a frame. The monkey's air
  claw: run speed with B held, walk speed without.
- The spider: two pixels a frame with B held; a shot on the move keeps her moving and its web
  still flies; on the web two pixels a frame with B held.
- Webs, the spider and the tinkerbat: a jump in front of one is a jump, A in the air takes
  hold, A on it jumps off and A again takes hold; the tinkerbat climbs two pixels a frame
  with B held. The original takes hold as the jump starts and ignores A on the web.
- Web edges, the spider and the tinkerbat: from the web wall, Up to its top (which opens onto
  the air at y 1205), then Left, Right and Down to its edges: on the web throughout, standing
  at each edge, and A there jumps off. The original jumps off at the top.
- The spider's shot in the expanded view (426 by 240): it flies until it leaves the picture
  (here the room's left edge, 49 frames), in the original view its 29 (`docs/expanded-view.md`).
- `--original <exe>`: with Transformations off, 1844 frames of the tinkerbat's, the monkey's,
  the elephant's, the harpy's and the spider's routes (attacks, charges, turns, stomps, shots,
  flaps, climbs, webs) give the same object as the previous release, byte for byte.
- `--original <exe>` (the previous release's): with the feature off, 324 frames of whips,
  runs, jumps and crawls give the same object, byte for byte.

Not covered: the crouch whip's cancel under a low ceiling, which takes the crawl script
that skips the run test (06:6260).
