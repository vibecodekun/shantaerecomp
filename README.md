# Shantae (GBC) static recompilation

Shantae (USA) recompiled to native code with [gbrecompiled](https://github.com/mstan/gbrecompiled).
Boots as a Game Boy Advance by default, so the GBA Enhanced extras are on (title badge, the Bandit
Town Tinkerbat secret), while the palette loader is kept on the original GBC colors. On top of
that: an expanded world view (an aspect ratio of your choice, or Adaptive to fill the whole
screen), no slowdown, reduced input lag, smoother movement (running, whipping and crawling
without losing momentum), rewind, and RetroArch shader presets through librashader.

## Download

Builds for Windows (x64, x86, ARM64) and Linux (x86_64, aarch64; a tarball or an AppImage) are on
the [Releases](https://github.com/vibecodekun/shantaerecomp/releases/latest) page. Extract one
and run `shantae.exe` / `shantae`; the launcher asks for the ROM on the first start.

**No ROM is included.** You need your own Shantae (USA) Game Boy Color ROM, the No-Intro dump:
SHA-256 `1b92e22d5510c51bab97d23074e4aad7464d93eb15f7596ef7da0a5efa27a19d` (CRC32 `E994B59B`).
The launcher reads "ROM verified" under the box art and enables PLAY only for that file. Any other
file reads "ROM not recognized" and does not start: bad dumps, other games, and Shantae's other
releases (World, Switch, Limited Run Games), which are different ROMs and not supported yet. The
code was recompiled from that exact ROM.

## Layout

| Path | What |
|------|------|
| `gbrecompiled/` | Recompiler and runtime (submodule: [fork](https://github.com/vibecodekun/gbrecompiled/tree/shantae) of [mstan/gbrecompiled](https://github.com/mstan/gbrecompiled), branch `shantae`) |
| `recomp-ui/` | Launcher and in-game menu (submodule: [fork](https://github.com/vibecodekun/recomp-ui/tree/shantae) of [RetroPortingToolKit/recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui), branch `shantae`) |
| `shantae.toml` | Recompiler config: far-call / inline-argument routines, scan banks, palette override site |
| `shantae.annotations` | Generated candidate entry points (automatically refreshed by `tools/build.sh`) |
| `dispatch_misses.toml` | Runtime-harvested entry points (Tier-0), auto-ingested by the recompiler |
| `extras.c` | Shantae game hooks: hardware mode (GBA Enhanced on/off), palette override, `shantae.ini` settings |
| `expanded_view.c`, `expanded_background.inc` | Expanded world compositor, live background objects, and wider object activation |
| `object_slots.c` | With the expanded view, the object table grown from 32 slots to 157 (past DFFF and at A000 in bank 3) and the collision node pool from 12 to one per slot (towns keep the original 32 and 12) |
| `ram_native.c` | Native translations of writable JP vectors and the copied DMA routine |
| `moveset.c` | "Smoother movement": the whip slide and cancel, the run and air speed from B, and the faster crawl, as hooks in Shantae's movement routines ([docs/moveset.md](docs/moveset.md)) |
| `forms.c` | "Smoother movement" for the transformations: the monkey's and tinkerbat's attack slide, run and air speed, the harpy's speed and flaps through her talons, the tinkerbat's squeeze ([docs/moveset.md](docs/moveset.md#transformations)) |
| `dance.c` | "Easier dancing": the dance's steps entered like a code, each with its pose, and the blink after a transformation and after turning back ([docs/dance.md](docs/dance.md)) |
| `launcher_options.c` | Built-in GBA, expanded-view and gameplay features on the launcher's Mods page |
| `extras_ui.cpp` | In-game Shantae settings, including expanded-view size and the movement options |
| `game_build.cmake` | Adds game hooks, UI, and regression targets to the generated project |
| `generated/` | Recompiler output (not committed) — never edit; regenerate |
| `roms/shantae.gbc` | Stock ROM (CRC32 E994B59B): you supply it; never committed |
| `tools/build.sh` | Rebuild everything (below) |
| `tools/gen_annotations.py`, `tools/inline_args.py` | Derive `shantae.annotations` and the `[[inline_call]]` entries from the ROM |
| `tools/audit_whole_rom.py`, `tools/whole_rom_check.c` | Exhaustive bank/address audit and native-instruction differential checks |
| `tools/audit_ram_coverage.py`, `tools/ram_native_check.c` | ROM writer evidence, RAM disassembly, and direct RAM program checks |
| `tools/audit_native_coverage.py` | Check discovered native script targets and banked inline returns against emitted metadata |
| `tools/test_annotations.py`, `tools/native_dispatch_check.c` | Discovery regressions and compiled-dispatch differential checks |
| `tools/expanded_view_check.c`, `tools/object_slots_check.c` | Expanded-view compositor checks; the grown object table through the game's own routines |
| `tools/check_towns.py` | Every town with the expanded view against the original, frame for frame, from a debug-grid state |
| `tools/check_totem.py` | The labyrinth's totem puzzle with the expanded view: the orb, then the key, from a saved state |
| `tools/check_budgets.py` | Spawners that share a count of what they have made, with the expanded view against the original, from a cold boot |
| `tools/check_crow.py` | Sky's crow at the desert labyrinth with the expanded view: its dialogue opens the door, and it is there however Shantae comes back, from a saved state |
| `tools/check_eyes.py` | The third labyrinth's eye puzzle with the expanded view: the eye settles where its jar was, the socket takes it, the statue gives its key, and another puzzle in view is left alone, from a saved state |
| `tools/check_pictures.py` | The fourth labyrinth's picture puzzles and key doors with the expanded view against the original, pixel for pixel: each picture and door shows its own state, the puzzle is solved and its key taken, from a saved state |
| `tools/check_moveset.py` | Shantae's moves with "Smoother movement" beside the original, frame for frame: the whip slide and cancel, air speed, a whip that lands or slides off a ledge, the crawl; and the feature off against the previous release, from a saved state |
| `tools/check_forms.py` | The transformations with "Smoother movement" beside the original: the monkey's and tinkerbat's slide (from standing and walking), cancel and air speed, the harpy's talons in her run and while flapping, the tinkerbat's squeeze into the ice tower's hidden passage; Transformations off against the previous release, from two saved states |
| `tools/check_dance.py` | "Easier dancing": every dance of the ROM's table on its last step, skipped and restarted steps, the original rhythm, a slider against the new monkey and against Shantae turned back, with and without the blink; the feature off against the previous release, from a saved state |
| `tools/check_rom_gate.py` | Only the ROM the build was recompiled from starts: the launcher's "ROM verified" line and PLAY, and the runtime's own check, for the ROM, bad dumps made from it, and optionally another game |
| `tools/check_leave.py` | Leaving from the in-game menus: the first confirmed Return to Launcher ends the game and opens the launcher, Quit ends it and opens nothing, from the Escape menu, the settings window and the debug server |
| `tools/build_ghidraboy.py`, `tools/ghidraboy-ghidra12.patch` | Rebuild/install the GhidraBoy extension, ported to Ghidra 12 by the patch (instructions in the script) |
| `tools/ghidra_listing.py`, `tools/ghidra/Listing.java` | Ghidra's disassembly of ROM ranges (`6:4AA7:4B19`) from the command line: headless, read-only, far-call aware; imports the ROM into `logs/ghidra` on first use |
| `tools/build_librashader.sh` | Build `librashader.dll` (OpenGL runtime; x64, or x86/arm64 when named) and stage the slang-shaders presets |
| `third_party/librashader/` | Its output: the x64 DLL, `windows-<arch>/librashader.dll`, `shaders/`, and `patches/` applied to the librashader source; `linux-<arch>/librashader.so` from `tools/build_linux.sh` |
| `tools/build_windows.sh`, `tools/windows/` | Windows releases for x64, x86 and ARM64 (below): pinned msys2 libraries, CMake toolchain, DLL staging, packaging, checks |
| `third_party/windows/` | The pinned msys2 packages and the x86/ARM64 sysroots unpacked from them (`tools/windows/bootstrap.sh`) |
| `tools/build_linux.sh`, `tools/linux/` | Linux releases for x86_64 and aarch64 (below): toolchain bootstrap, CMake toolchain, packaging, checks |
| `LICENSE`, `THIRD-PARTY-LICENSES.md` | This project's license (PolyForm Noncommercial 1.0.0) and the components the releases carry |

## Build and run

The recompiler and the Windows build run on Windows with [MSYS2](https://www.msys2.org)
installed at `C:\msys64`, plus Git and Python 3.11+:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-angleproject
```

```bash
git clone --recursive https://github.com/vibecodekun/shantaerecomp.git
cd shantaerecomp
mkdir -p roms && cp /path/to/your/Shantae.gbc roms/shantae.gbc
bash tools/build.sh                 # recompiler -> generated/ -> generated/build/shantae.exe
```

Already cloned without `--recursive`? Run `git submodule update --init`.

The generated project builds for size (MinSizeRel, the recompiled ROM code at
`-O1`); `game_build.cmake` builds the runtime and this game's modules
(`expanded_view.c`, `object_slots.c`, `moveset.c`, `forms.c`, `dance.c`, `extras.c`,
`ram_native.c`) at `-O2`,
since the PPU, APU, timers and the expanded-view compositor run every frame.
The ROM code keeps its own level, so this does not recompile it.

The build also runs a 120-frame startup smoke test through the actual game loop.
Run it independently with `python tools/check_startup.py`; `--rom`, `--exe`, and
`--frames` select another ROM path, binary, or duration. It uses temporary settings
and saves, checks that the frame limit was reached, and fails on native execution
errors or interpreter fallback. Its log is `logs/startup-smoke.log`.

Only the ROM the build was recompiled from starts. The recompiler embeds its SHA-256, the
generated `main()` hands it to the launcher before the launcher opens (the line under the box
art and the PLAY button follow it), and the runtime checks the file again at boot, which is what
stops a wrong ROM when "Skip launcher on boot" is on (on Windows a "Wrong ROM" box, then the file
picker). `python tools/check_rom_gate.py` checks both with the ROM, a one-bit bad dump and an
underdump made from it; `--other` adds another game's ROM.

The menus' Return to Launcher and Quit end the game at the confirming press. The runtime notes
the request in a flag that `gb_platform_poll_events()` reads; it used to push an SDL_QUIT, which
the menu's hold caught and pushed back every frame without the main loop ever seeing it (SDL_PollEvent
stops at the poll sentinel queued before the push), so the game ran on, unpaced, behind the open
menu until a second leave. The relaunch registered by Return to Launcher also now checks the
last leave asked for, so a Quit after it no longer opens the launcher.
`python tools/check_leave.py` leaves through the Escape menu, the settings window and the debug
server, and fails on v0.1.9.

Run `generated/build/shantae.exe`. In the launcher, **Mods** holds Shantae's options:

- **GBA Enhanced mode** (on by default): boot as a Game Boy Advance, so the title shows "GBA Enhanced!"
  and the Bandit Town Tinkerbat secret is available. Off = plain Game Boy Color.
- **Colors**: *Original GBC colors* (default) or *GBA brightened colors*.
- **Remove slowdown** (on by default): the original drops to half speed whenever a
  frame's logic does not finish before VBlank, and the wider expanded views, which
  keep more enemies active, do so often (state1 at 426×240 lagged 86 of 240
  frames). With this on, the frame waits at the end of line 143 until the game
  has finished it: PPU, timers, audio and serial stand still while only the CPU
  runs, so music and screen timing are unchanged. Every frame loop ends at 00:0851,
  which sets FF8F for the VBlank handler, so FF8F still clear at line 143 means an
  unfinished frame. A wait is capped at two frames of CPU time; after hitting the
  cap (a screen load, not lag) it resumes only once a frame finishes unaided. The
  mechanism is `gb_frame_hold_hook` in the runtime (`gbrt.h`). Off keeps the
  original slowdown. The Esc → Shantae checkbox applies it immediately.
- **Reduce input lag** (on by default): sprites appear one frame sooner, and
  Shantae's moves start on the tick they are pressed. With the runtime's
  Preemptive Frames at 1 (below), a press shows on the very next picture: hold A
  or B in frame advance and the jump or whip shows on advance 1 (the original
  game: 4 and 3). Two changes:
  - *Sprites.* The VBlank handler streams each tick's sprite graphics (C4C0) to
    VRAM with an HBlank DMA (00:0A7D), finished only by the end of the next picture,
    so 01:667C points the OAM DMA page FF81 at the previous tick's buffer (D700/D800).
    The scroll is held back to match: each tick starts by committing the camera the
    previous tick drew its sprites with (00:26F6 → 03:722F). With this on, hooks on
    the `[[imm_override]]` sites 00:0A50 and 00:0A7D copy the buffer the tick just
    wrote (FFD9), upload its graphics with a general DMA, and move the copied
    sprites by the camera's last step (FFE1/FFE3) onto the committed scroll, the
    camera the expanded view's surround uses. The committed scroll's camera is
    the scroll less the background offset (C9D2/C9D4) it was built with, noted
    at 03:730E after both camera routines' build: bosses move the offset later
    in the tick (Risky's ship bobs it), and the value at the VBlank could leave
    the copy a pixel off. FF81 is restored after the copy, so game state is
    unchanged; the camera still follows a frame behind, as in the original.
  - *Moves.* Every loop runs each object's movement routine (00:0C45) and then
    each object's script (00:1305). The player's idle routine turns a press into a
    script branch, and the script picks the new movement routine (jump 06:5520,
    walk, whip, ...), which then first ran in the next tick. When the player's
    script (slot at CA13) has picked a new routine, hooks on 00:130B and 00:1384 and
    the runtime's `gb_step_hook` run it once right after the script, with the
    registers and banks it interrupts saved on the game's stack (so a save state
    taken meanwhile is safe). The whole move then runs a tick earlier, on the same
    path.

  The Esc → Shantae checkbox applies it immediately.
- **Smoother movement** (on by default): Shantae's base form keeps her momentum.
  In the original, B is both the whip and the run: a B press on the ground is always
  a whip, which stops her for its 24 frames, and the run only starts once B has been
  held for 15; a jump keeps the speed she left the ground with, so a standing jump is
  at walking speed whatever is held; a whip begun in the air and landed finishes on
  the spot; and the crawl moves half a pixel a frame, standing up into a run when B
  is held. With this on, holding B runs at once, and three options choose the rest
  (details and addresses in [docs/moveset.md](docs/moveset.md)):
  - **Whip on the move**: what B does while a direction is held, standing or crouched.
    *Slide* (default): the whip comes out and hits as usual while she keeps moving, at
    running speed with B held and walking speed without, turning with the D-pad; the
    run follows without a frame at rest, a whip that lands carries on along the
    ground, and one that slides off a ledge carries on in the air. *Cancel*: B with a
    direction is no whip at all, she runs (or crawls on) at once, and a whip in
    progress ends when B and a direction are held; stand still to whip. *Original*:
    she stops for the whole whip.
  - **Air speed**: *Hold B for running speed* (default): two pixels a frame while B is
    held and one when it is released, jumping, falling or whipping in the air.
    *Original*: the speed she left the ground with.
  - **Crawl**: *Hold B for walking speed* (default): she stays down and crawls a pixel a
    frame, twice as fast, with the animation at twice the rate. *Original*: half a
    pixel, and B stands her up into a run.
  - **Transformations**: *Like Shantae* (default): the monkey and the tinkerbat run at once
    with B held, their claw and sword follow Whip on the move whenever B is pressed,
    standing or walking (in the original they stop for 16 and 13 frames, even mid-run), and
    their jumps follow Air speed. The harpy keeps her speed when her talons end (the
    original stops her for a frame and starts the run again from nothing), and A still
    flaps during a talon, with the flap's sound, while the talon plays out (the original
    drops those presses and she sinks). The tinkerbat squeezes to the monkey's height
    where only that fits, so she climbs and walks into gaps the monkey can, such as the ice
    tower's hidden passage to a warp squid: her box is 20 pixels tall, the monkey's 14, and
    the passage 16.
    *Original*: they move as in the game. The elephant and the spider are unchanged.

  Off plays as the original, frame for frame. The Esc → Shantae controls apply at once.
- **Easier dancing** (on by default): after Select, the dance's steps are entered like a
  code (details and addresses in [docs/dance.md](docs/dance.md)):
  - **Dance steps**: *Quick* (default): every press is a step at once and shows its own
    pose. A press that goes on to no dance she knows is skipped, Down starts over (every
    dance begins with it), and the transformation, healing or warp begins on the last
    step. *Original*: one step every eight beats, in rhythm; a missed or doubled beat
    breaks the dance, and it begins a pose after the last step.
  - **After transforming**: *Blink* (default): after an animal transformation she flashes
    and cannot be hurt for two seconds from the moment she appears, as after a hit, and
    the same after turning back into Shantae, from the moment she can move again.
    *Original*: she is safe only until the new form's entrance ends, or until she can
    move after turning back, with nothing to show it, and an enemy nearby hits her at once.

  Off plays as the original, frame for frame. The Esc → Shantae controls apply at once.
- **Expanded view** (off by default): a larger world view with the original pixel
  scale and the status bar at the bottom, 256×240 (NES size) unless changed. Enable
  it on the Mods page, then launch. Its options set the size: **Adaptive** fills the
  whole screen or window, whatever its shape (Height is then the least height); an
  **Aspect ratio** preset (Game Boy 10:9, NES 16:15, 4:3, 16:10, 16:9, 21:9, 32:9)
  sets the width for the current height, and **Width** and **Height** (160–8192 and
  144–8192) adjust one pixel at a time; any other size is shown as Custom. Rooms
  smaller than the view zoom in to fill it (**Room zoom**). The in-game Esc → Shantae
  section has the same controls, and a size changed there applies at once; turning
  the view on or off takes effect on the next launch.

**Preemptive Frames** (Esc → Advanced → Display; 1 by default for Shantae) is the
runtime's port of RetroArch's preemptive frames (`runahead.c`, `preempt_run`).
A ring keeps the state from before each of the last N frames; when the input
changes, the oldest is loaded and those frames run again with the new input,
silently, before the next frame is shown. The game sees each press N frames
sooner and is otherwise unchanged. One frame is the delay every Game Boy game
has (a tick's result shows in the picture after the next VBlank), and all that
Reduce input lag leaves; more than that skips the first frames of a move.
Loading a state empties the ring; while paused, loading runs N frames to
refill it, as RetroArch runs one. Saved as `emulation.preemptive_frames` in
`runtime_prefs.ini`.

Settings are saved to `shantae.ini` next to the exe. `expanded_view=1`,
`view_width=256` and `view_height=240` select the NES-size view; `remove_slowdown=0`
brings the slowdown back and `reduce_input_lag=0` the original input timing;
`smooth_moves=0` the original moves, with `whip_moving` (0 original, 1 slide, 2 cancel),
`air_speed_b`, `fast_crawl` and `smooth_forms` its options; `easy_dance=0` the original
dance, with `quick_steps` and `transform_invincible` its options. Menus and dialogue retain
their original centered layout.
Towns keep the original picture, object activation and 32-slot object table
(their building-name panel covers the bottom, their camera wraps at 640 pixels,
and their doors are found by the low byte of the distance), so every door, label
and entrance is where the original has it. `tools/check_towns.py` checks all five
(Scuttle Town, Water Town, Oasis Town, the Zombie Caravan and Bandit Town, the
debug grid's N row) against the original: walking and running round each town
plays frame for frame the same, and every door shows its label, opens on the
same frame and leads to the same room (and, walking back out, to the same
spot). Rooms whose
camera is pinned to one screen, such as the first boss's arena, keep the
original picture and activation too. Rooms narrower or shorter than the view
(Risky's ship in the opening) are centered. This is experimental: the
saved-state regressions, the towns and the opening area have been checked, not
a full playthrough.

The expanded view draws the ROM world map plus the game's current background
objects and metasprites, positioned with the scroll the game actually committed
to the hardware (so slowdown cannot shift the surround against the native
picture). Sprites use the camera without the background-only offset that bosses
drawn in the background add to the scroll. It uses world coordinates so moving/collapsed scenery
does not leave stale tiles when the camera reverses. In rooms shown expanded,
enemies activate and remain active across the larger area; their behavior can
therefore begin earlier than in the original game. To hold them, the game's
object table has 157 slots instead of 32 while the view is on, towns aside (as many as 16-bit
addresses leave room for; the water tower's rooms at 1920×1080 want about 130),
and the collision pool that platforms and hazards take from has a node for every
slot instead of 12. Encounters that start the moment they exist (the water
tower's mini-boss) still wait for the original distance; NPCs, their houses and
other set pieces appear with the view. The labyrinth's totem puzzles keep one list of stones
for every totem, and the view woke a second totem whose stones took the list over, so matching
the stones never brought the orb (or its key); a totem's pedestal now checks its own stones.
Spawners that share a count of what they have made (the swamp creatures in one level, and three
kinds of enemy spawner) counted the ones the view kept far behind Shantae, so fewer appeared
near her; they now count what the original would still have around her.
The view woke objects farther out than it kept them, so some were let go the frame after they
appeared. Sky's crow, which the desert labyrinth's door brings out as the door appears, went
while the door stayed, and without its dialogue the door never opened. Objects are now kept as
far past where they appear as the original keeps them, and a save state taken with the crow
missing gets it back when loaded with the view on.
The third labyrinth's eye puzzles (whip a jar, knock the eye that comes out into its statue's
socket) went wrong three ways. The eye moves for up to seven frames before it has its own
collision box, with whatever box the last object in its memory slot left; with the view that was
often a tall enemy's, which reached into the floor and pushed the eye up to 53 pixels sideways,
into the wall and out of the room, differently on every try. The puzzles also share their lists
of open sockets and of which eye is out, and the view has several puzzles awake at once, so the
room's socket never took the eye and one puzzle's eye could be written into another's jar. And
the eye was let go 160 pixels off the original screen while its broken jar stayed. The eye now
has its own box from the start, each puzzle reads its own eyes and sockets, and the eye is kept as
far as the view keeps its jar. A save state taken after an eye was lost that way, with its jar
left empty, has the jar full again when loaded with the view on.
The fourth labyrinth's picture puzzles (four quarters, each turned by a whip until the picture
is whole) draw their pieces into one set of tiles that every picture in the level uses, and its
key doors do the same with their shut and open looks; the third labyrinth's doors too. The
original only ever has one picture or door on screen. The view has several awake, all drawing
into those tiles, so every picture showed a mix of two puzzles' pieces and they all seemed to
turn at once: the picture in front of Shantae could not be told from its solution, and an opened
door could look like a wall beside a shut one. Now only the one the original would have awake
draws into the tiles, and the view draws every picture and door from its own pieces.
Camera/physics reads and the native PPU remain unchanged.
See [expanded-view implementation and checks](docs/expanded-view.md).

## Shader presets (librashader)

RetroArch `.slangp` presets (CRT, LCD, handheld, scalers, Mega Bezel...) run through
[librashader](https://github.com/SnowflakePowered/librashader). Set it up once:

```bash
bash tools/build_librashader.sh     # needs Rust (cargo); ~2 min the first time
bash tools/build.sh                 # or just ninja -C generated/build
```

The script clones librashader 0.12.0 into `third_party/librashader/checkout`
the first time (`LIBRASHADER_SRC` points it at a checkout of your own instead),
exports that checkout without modifying it, applies
`third_party/librashader/patches/`, builds the OpenGL runtime only, and copies
the checkout's slang-shaders collection. The build then stages `librashader.dll`
and `shaders/` next to `shantae.exe`. Without them the game runs as before.
The DLL links the C and C++ runtimes in (`crt-static`), so players need no
Visual C++ Redistributable. `bash tools/build_librashader.sh x86 arm64` builds
the other Windows arches' DLLs (`tools/build_windows.sh` does this itself).

In game: **Esc → Shader presets**, or the settings menu's **Shader Presets
(librashader)** section. Pick a preset (the filter box takes words such as
`crt` or `handheld gbc`), tune its parameters, and **Reset All** to go back.
Choice and parameters are saved in `runtime_prefs.ini`
(`shader.slang`, `shader.slang_param.*`). **Preset draws the whole window** is
for bezel presets such as Mega Bezel. **Shader folder** points the picker at
another collection, e.g. RetroArch's `shaders_slang`. `GBRECOMP_SHADER_PRESET`
overrides the preset for one run. Mega Bezel opens with a few seconds of static
and its logo; its *When to Show Intro* parameter turns that off.

**Edit Preset** (a button beside the picker, and the list at the end of the
section) changes how a preset is set up, the way RetroArch's Shader menu does:
it holds the passes of whichever preset was picked, and you can set each pass's
filter, scale, wrap mode, framebuffer formats, frame count mod and alias,
reorder and remove passes, add single `.slang` passes, append or prepend whole
presets, or **Start Empty** to build one from nothing. Changes apply a moment
after they are made (or with **Apply**), through the same background compile as
any preset, as an edited copy (`shader_presets/.builder.slangp`), so the
preset's own file is left as it is; the picker lists that copy as
*"<preset>, edited (not saved)"* until another edit replaces it, and **Undo
Changes** goes back to the preset. **Save Preset** writes the chain, with its
parameters as tuned, to `shader_presets/<name>.slangp` beside the other state
(the name starts as the edited preset's; a folder's own presets are never
overwritten, your saved ones can be); saved presets are listed first in the
picker and saved as `shader.slang=saved:<name>.slangp`. **Delete Preset**
(beside the picker while one of yours is on) and **Delete All My Presets**
remove them after asking, to the Recycle Bin on Windows (elsewhere for good);
nothing in the shader folder is ever deleted or written. The editor
reads presets as librashader does (`gbrecompiled/runtime/src/slang_preset.cpp`:
`#reference` order, first-wins pass settings, last-wins parameters), so a
rewritten stock preset parses the same as the original: checked against
librashader's own parse for 2514 of the 2515 readable stock presets (the other
uses Mega Bezel's `$PRESET$` path wildcard).

To see a preset while tuning it, **Game Dimming** (how dark the game gets behind
a menu; 0 leaves it untouched) and **Menu Opacity** (the menus' own backgrounds)
are in the settings menu's Playback group and **Esc → Display**
(`ui.menu_dim`, `ui.menu_opacity`). Menus hold the game while
open (**Pause in Menu**, `ui.pause_in_menu`), and no key or button reaches the
game or its shortcuts while one is open, typing into a filter included.

How it works under ANGLE (see `gbrecompiled/runtime/src/librashader_chain.cpp`):

- The context is OpenGL ES 3.1. Presets compile to GLSL ES 3.10, which is what
  arrays of arrays (crt-geom and others) need.
- A preset that has not loaded before is first compiled in a hidden child
  process while the game keeps running. Microsoft's HLSL optimizer recurses
  until the stack overflows on the two stock `vectorscale` presets, which kills
  the process. After such a crash the child runs again with the optimizer off
  (the runtime routes ANGLE's `D3DCompile` through itself for this), and a
  preset that works that way is always loaded that way; vectorscale does, and
  the menu says so. Presets that fail anyway are refused and the previous look
  stays. Results are in `librashader_probe.txt`.
- Compiled programs are cached in `shader_cache/`. The largest Mega Bezel preset
  compiles for about a minute the first time and loads in under 2 s afterwards.

`third_party/librashader/patches/` makes librashader's GLSL ES output work
under ANGLE:

| Patch | Fixes |
|-------|-------|
| 0001 | History copies used a blit ANGLE rejects; presets reading previous frames read black |
| 0002 | Integer varyings need `flat` on the vertex side in GLSL ES (crt-yah, ntsc-blastem) |
| 0003 | ANGLE's D3D translator cannot write global arrays-of-arrays constants; they are assigned in `main()` instead (crt-hyllian, crt-sony-megatron, crt-nobody, dithering) |
| 0004 | Quoted preset values end at the closing quote, as in RetroArch (`"../..//x"`, `"true""`) |
| 0005 | RelaxedPrecision is dropped, working around a SPIRV-Cross GLSL ES bug (simple-crt) |

A sweep of the stock collection (every non-bezel preset plus every 25th Mega
Bezel preset, 1095 in all) has 1075 working: 1069 render in its first frames,
the two vectorscale presets work through the optimizer-off fallback, three Mega Bezel
screen-only presets are dark only during their intro, and `120hz-safe-BFI`
draws black frames by design. The other 20 are broken in the collection itself
or need an HDR display: presets that point at files
that were moved (`glow_trails`, `ray_traced_curvature`, `royale-curve-append`,
`crt-sony-megatron-v2-gba-gbi`, two `phosphorlut` presets), the nedi
`bilateral-variant` presets (they use the vhs pass without its `play`
texture), scanline-classic's HDR10/wide-gamut presets (they read `EnableHDR`,
`MaxNits` and `PaperWhiteNits`, which librashader does not provide; its HDR
values are `HDRMode` and `BrightnessNits`), and `test/feedback-noncausal`,
which is invalid on purpose.

## Windows (x64, x86 and ARM64)

`tools/build_windows.sh` builds and packages the Windows releases. x64 is the
`generated/build` that `tools/build.sh` makes (brought up to date first); the
other two build from the same `generated/` C:

```bash
bash tools/build_windows.sh         # all three -> dist/windows/ (or name x64, x86, arm64)
bash tools/windows/check.sh         # startup smoke + differential checks
```

- **x86** (32-bit) builds in `generated/build-x86` with msys2's mingw32 GCC,
  the same compiler and runtime family as x64 (`pacman -S mingw-w64-i686-gcc`).
  Floating point is SSE2, as on x64 (`-msse2 -mfpmath=sse`), and the exe is
  large-address-aware (4 GB of address space on 64-bit Windows).
- **ARM64** builds in `generated/build-arm64` with mingw64's clang and lld
  cross-compiling to `aarch64-w64-mingw32` against msys2's clangarm64 runtime
  (libc++, compiler-rt, UCRT); msys2's own ARM64 compilers only run on ARM64.
- SDL2, ANGLE and the ARM64 runtime come from msys2 packages pinned by SHA-256
  in `tools/windows/bootstrap.sh` (their msys2 signatures were checked when
  they were pinned), unpacked into `third_party/windows/`
  (`$SHANTAE_WINDOWS_CACHE`, no spaces); nothing is installed into msys2. x64
  and ARM64 have the same ANGLE (2.1.r25748); x86 has msys2's last 32-bit
  ANGLE package, 2.1.r21358.
- `librashader.dll` is `tools/build_librashader.sh <arch>`'s, through Rust's
  MSVC targets (Visual Studio's x86 and ARM64 C++ tools).
- `tools/windows/dlls.sh` follows the imports of the exe and of
  `librashader.dll`: each build dir gets the DLLs it needs beside the exe, the
  zip carries exactly those, and the build fails if one is missing or is
  another arch's.
- `dist/windows/shantae-recomp-<version>-windows-<arch>.zip` is a folder with
  `shantae.exe`, its DLLs, `assets/`, `shaders/` and
  `tools/windows/README-windows.txt`; `<version>` is `$SHANTAE_VERSION`, or the
  date.
- `tools/windows/check.sh` checks every build with
  `tools/windows/check_imports.py` (each module is the arch's, and every
  function imported from a DLL beside the exe is exported by it), then runs
  `tools/check_startup.py` and the 3000-frame differential check on each build
  this PC can run: x64 and x86 on an x64 PC; ARM64 needs an ARM64 PC.
- On 32-bit x86, librashader calls its GL loader `extern "system"` (stdcall)
  while its C header's `libra_gl_loader_t` has no convention, so the runtime
  declares its loader `__stdcall`; with a cdecl loader every preset crashed its
  probe. x64 and ARM64 have one calling convention, so they never saw this.

## Linux (x86_64 and aarch64)

`tools/build_linux.sh` builds the Linux releases from `generated/`. The generated
C is the same on every platform, so the recompiler only runs on Windows: run
`tools/build.sh` first, then, in WSL (Ubuntu) or on any Debian/Ubuntu machine:

```bash
wsl bash tools/build_linux.sh       # both arches -> dist/linux/ (x86_64 or aarch64 for one)
wsl bash tools/linux/check.sh       # startup smoke + differential checks
```

The first run fetches its toolchain into `~/.cache/shantae-linux`
(`tools/linux/bootstrap.sh`, `$SHANTAE_LINUX_CACHE` moves it; nothing needs
root): Zig as the C/C++ cross compiler, CMake and Ninja, each arch's Ubuntu 24.04
X11/Wayland/audio/GL headers and libraries through an unprivileged apt, SDL2
built static, Rust and cargo-zigbuild for librashader, Debian 10's libraries for
the checks, and appimagetool. After that a build takes about 2.5 minutes per
arch; the sources are rsynced to the Linux file system and build incrementally.

- The binaries target **glibc 2.28** (Zig's glibc stubs), so they run on Debian
  10+, Ubuntu 20.04+, RHEL 8+, Fedora, Arch, SteamOS, Raspberry Pi OS 64-bit
  and so on. SDL2 and the C++ runtime (libc++) are linked in; at start they
  load only glibc and `libGL.so.1`. X11, Wayland (with libdecor), KMSDRM,
  PipeWire, PulseAudio, ALSA, udev and D-Bus are opened at run time when the
  system has them. Plain `char` is signed on aarch64 too (`-fsigned-char`), as
  on x86. `build_linux.sh` fails a binary that needs a newer glibc symbol.
- `librashader.so` is built from `third_party/librashader/src` (exported and
  patched by `tools/build_librashader.sh`) with cargo-zigbuild into
  `third_party/librashader/linux-<arch>/`, and the game finds it beside itself
  through its RUNPATH (`$ORIGIN`).
- `dist/linux/shantae-recomp-<version>-linux-<arch>.tar.gz` is a folder that runs
  from anywhere writable and keeps its state inside it;
  `Shantae_Recomp-<version>-<arch>.AppImage` keeps its state beside the
  .AppImage (`gb_host_paths`). `<version>` is `$SHANTAE_VERSION`, or the date.
  The tarball carries `tools/linux/README-linux.txt`; `tools/linux/shantae.png`
  is the AppImage icon (a placeholder: recomp-ui's brand mark).
- `tools/linux/check.sh` runs `tools/check_startup.py` on the host's glibc and on
  Debian 10's glibc 2.28 (aarch64 through qemu-user), and the 3000-frame
  differential check (`--quick`: 300 frames under qemu).

What the runtime does differently on Linux: a shader preset's probe child is
this executable forked and exec'd again (`/proc/self/exe`; there is no HLSL
optimizer to retry without), and deleted presets go to the freedesktop.org
Trash. On every platform, a machine with no working sound output now runs the
game silently rather than failing at `SDL_Init`, and `imgui.ini` is kept in the
state folder. The launcher only forces SDL's EGL path on Windows (ANGLE needs
it; on Linux it would also need `libGLESv2.so.2`).

After changing the recompiler or runtime, run the direct coverage and differential
checks below. Gameplay replays can test a particular regression; they are not the
method for discovering missing ROM entries or expanding coverage.

## Whole-ROM native coverage

`exhaustive_rom = true` generates a native entry at every byte of all 256 ROM
banks, including bank zero mapped at `$4000`, cross-window operands, and HALT-bug
fetch variants. Existing analyzed functions remain the fast path; other ROM
addresses dispatch to ahead-of-time compiled instruction bodies with constant
operands. There is no runtime opcode decoder or interpreter call in that ROM path.
The older `scan_banks` list controls only function discovery/optimization now.

The build audits all **8,421,376** normal/HALT-bug address slots, verifies shared
instruction signatures against the ROM, and rejects generated interpreter calls.
Listings are `generated/shantae_exhaustive_bank_<window>.asm`: window 0 is fixed
bank zero; window N+1 is switchable bank N. These intentionally disassemble at
**every byte**, including overlapping interpretations of data. They establish
conservative executable coverage, not a manually annotated code/data map.

Run the full checks with the MinGW toolchain on PATH:

```bash
python tools/test_annotations.py
python tools/audit_whole_rom.py
python tools/audit_ram_coverage.py
python tools/audit_native_coverage.py --targets logs/native-targets.txt --report logs/native-coverage.json
cmake -G Ninja -S generated -B generated/build
ninja -C generated/build whole_rom_check ram_native_check native_dispatch_check
generated/build/whole_rom_check.exe roms/shantae.gbc variants
generated/build/whole_rom_check.exe roms/shantae.gbc production
generated/build/ram_native_check.exe roms/shantae.gbc
generated/build/native_dispatch_check.exe roms/shantae.gbc logs/native-targets.txt
```

Both whole-ROM test modes audit every mapped slot and compare representatives of
all used native instruction bodies against the reference interpreter, with memory
comparison, both flag outcomes, and every cross-window CB opcode. `production`
uses the real game dispatcher; `variants` exercises the exhaustive entries
directly. Optional `all` compares every legal normal/HALT-bug address individually
and takes substantially longer. Tests use private contexts and do not load or save
battery data. The interpreter remains available as an explicit testing reference.

The identified RAM programs have explicit native translations: 116 patched JP
vectors (including both WRAM object pools) and six DMA instruction entries.
`generated/shantae_ram.asm` lists them. The RAM check executes their actual ROM
initializers and compares all 65,536 JP destinations, every slot across all ROM
banks, and DMA operands/timing against the independent interpreter decoder.
With the expanded view the object pool in bank 3 runs on to 157 slots, past
DFFF and at A000, and the collision node pool in bank 7 on past its 12 at E000;
their vectors go through the same translation (`object_slots_check`).
Generic runtime RAM opcode helpers are disabled in production dispatch.
**Unexpected executable RAM or illegal opcodes stop with a diagnostic;
they do not silently enter the interpreter.** This is a guard, not proof that all
possible writable-memory programs have been statically classified. Whole-ROM
coverage does not establish a bug-free full playthrough.

See [the whole-ROM coverage report](docs/whole-rom-coverage.md) for implementation,
validation and limitations. The [earlier coverage investigation](docs/recompilation-coverage.md)
records the original script/callback gaps and their regressions.

## License

This project's own code is licensed under the
[PolyForm Noncommercial License 1.0.0](LICENSE). The submodules and the
libraries the releases carry keep their own licenses; see
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

Shantae is a trademark of WayForward Technologies. This project contains no
ROM and is not affiliated with or endorsed by WayForward, Capcom, or Nintendo.
