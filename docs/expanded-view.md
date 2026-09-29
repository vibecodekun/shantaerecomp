# Expanded Shantae view

Enable **Mods → Expanded view** before launching. The default is 256×240 game
pixels. The feature's options (mirrored in the Esc menu's Shantae section) set
any size from 160×144 to 8192×8192, the whole 32×32-sector map: an aspect
preset sets the width for the current height, rounded down so a view never
exceeds its ratio (16:9 at 240 is 426, which fits 2560 and 3840 wide screens at
a whole-number scale). Width and height then change a pixel at a time. The
aspect is not stored; a size shows the preset it matches, or Custom.

**Adaptive** (first in the aspect list) fills the whole screen or window and
follows it when the window is resized or the scaling mode changes. The height
becomes the least height: with Pixel Perfect scaling the view takes the largest
whole-pixel scale that still shows it and fills the rest of the window (240 on
1920×1080 is 480×270; on 2560×1440 426×240; on 3440×1440 573×240); a window
shorter than it is filled at one game pixel per screen pixel. Aspect Fit, Fill
and Stretch keep the height exactly and match the window's shape (427×240 on
1920×1080). A windowed start is shaped like the desktop; the window scale steps
down until it fits the screen. Choosing Custom from Adaptive keeps the size on
screen. The launcher shows the adaptive size for its main screen, fullscreen
with Pixel Perfect scaling.

The settings are `expanded_view=1`, `view_width=256`, `view_height=240` and
`view_adaptive=0` in `shantae.ini`. Turning the view on or off takes effect on
the next launch; while it is on, size changes in the Esc menu apply on the next
frame. Loading existing save states is supported; host presentation caches are
cleared and rebuilt after loading.

**Original view.** Menus, the inventory, dialogue and one-screen rooms show
the original 160×144 picture (see the scene gate and one-screen rooms below).
By default it is presented on its own, like the game without the view: the
160×144 picture goes through the shader chain and is scaled to the window by
the Scaling Mode (Pixel Perfect on a 1920×1080 screen is 7×, 1120×1008), so a
preset that works per game pixel (an LCD grid, scanlines) works on it too.
The Esc menu's **Original view** (and the launcher's Original view scale) can
instead pick Pixel Perfect, Aspect Fit, Aspect Fill or Stretch for it alone, a
whole scale (shortened to the largest that fits the window), or **Inside the
view**, the old layout: centered in the view at one view pixel per game pixel.
The keys are `native_scaling` (-1 the Scaling Mode, 0-3 those modes, 4 a whole
scale, -2 inside the view) and `native_scale`. The picture switches with the
game's scene: into and out of one-screen rooms and the inventory the game
fades or cuts to black first, so the size changes on a black frame, while a
dialogue box appears on one frame and the picture switches with it. Frames
that are not new guest frames (the LCD off, a frame presented again) keep the
last frame's choice, and the shader history is cleared at each switch.

**Room zoom.** A room smaller than the view, from a corridor one screen tall
to an arena the camera locks to during a fight, would sit at the view's pixel
size in a field of black. With **Fill the screen** (the default) the view
shrinks to the part worth showing and the window scales it up until the room
fills the screen: to the nearest whole scale with Pixel Perfect, the room
scrolling along a side that no longer fits. **Whole room** zooms only as far as
keeps every side that fits whole (whole scales round down); **Off** is the
view as it is. The zoom never goes past the original screen (the picture stays
at least 160×144) nor below the view's own scale, and never shows more than the
view. At 1920×1080 on a 1080p screen: the 192-wide hold of Risky's ship and
the 319×128 corridors go to 7× (274×154 of the world), the 511×255 rooms and
the 2047×255 corridor to 4× (480×270), the opening area (3071×471) and other
corridors about 500 tall to 2× (960×540); a 3071×767 room (1.38×) stays at 1×
with Pixel Perfect and fills the height exactly with Aspect Fit; the water
tower's map, whose bounds are the whole map, stays at 1×. The key is
`room_zoom` (0 off, 1 fill, 2 whole room).

The zoom follows the room bounds C9DB–C9E2 the frame was drawn with (a room
box of the camera range plus 160×128, with the status bar). Rooms are entered
through black (doors, deaths and the inventory fade or cut out), so a new map
directory or a dark native frame snaps to the new zoom. Bounds that change in
play ease there over 16 frames, geometrically, with the view growing or
shrinking around the camera: of the 21 bounds-only changes (03:7A3D, 03:7AFE)
17 run from object and boss scripts, not room loads, such as 1A:54FD locking
the camera to a 191×221 arena (7×) and the scripts that give the whole map
back (`span 8032,8064`). While the game declines frames (menus, dialogue, the
LCD off, one-screen rooms) the zoom stays as it was. The runtime asks the game
for the picture each frame through `gb_custom_fit`, which may shrink the
resolved view and give the scale to present it at; activation follows the
picture presented, like an adaptive view, so a zoomed room activates the
objects near what it shows rather than across the whole view.
`shantae_view_info` reports `width`/`height` (the picture), `zoom` and
`zoom_target`.

**Rooms keep to their pictures.** A scene's camera bounds can take in more
than it shows. The intro keeps the whole opening map (07:405C) while an object
pans the camera up the column that shows Shantae's house, until 07:4079
narrows the bounds to the bridge; the game over (04:6E04) and the credits
(17:72E7's scroll up a waterfall strip, 1C:78DC's shaft after the "created by"
picture) span the whole map; and levels such as the water tower and the maps
78:BE, 5E:8F, 5B:CA and 4D:D4 take the whole 8192-pixel map. Such bounds put
the map's other pictures (other scenes, boss arenas reached through a door,
credits art) around the one the scene shows, at a zoom worked out for the whole
map. The room is now the bounds narrowed to the pictures the camera's screen
is in: groups of drawn sectors (8-connected; a sector whose directory entry
has page 0 draws nothing), found once per map, less the bands of a single
metatile along their sides (levels end in up to a sector of blank metatile 0,
and the credits' shaft is 160 wide in a 256-wide column of filler), or, where
two pictures touch, a short list: the bridge and the house's column in the
opening map (4F:51), the game over's column and the two credits strips in the
map of one-screen scenes (73:59). A side of the bounds that reaches past the
pictures or into one of those bands is narrowed; the camera's own screen is
always kept, so a camera that goes into a band takes the room with it. The
second arena of debug scene B2 (map 78:BE) needed the band: while its boss
rises, 25:46A8 holds the camera at 796 with bounds reaching 88 pixels into
the blank sector after the arena, which put the arena against the picture's
left side with a black bar on the right. Its room is now 796-1023, 228 wide
like the first arena's, so the cut between the two arenas' copies keeps the
same framing. Of the ROM's room loads, only the intro's, the game over's, the
two credits', 25:46A8 and the whole-map levels' change (the latter also on a
band along the map's left or top edge, which the earlier rule missed). Sectors
of other groups inside the room's box are drawn black.

The room the picture shows eases like the zoom when the bounds change in play
(16 guest frames, smoothstep), so the picture pans to the new room instead of
jumping; a new map, a cut or a dark frame snaps. When B2's boss has risen,
25:46DC widens the arena to 752-1023 and the view pans left over the zoom-out
instead of recentering 64 pixels in one frame. The native picture stops at the
room's edges like the background and the sprites: the rumble while the first
arena's stone sinks shakes the camera 2 pixels past the room's least x, and
that strip of the native picture flashed in the black margin every fourth
frame. (The shake is not shown in a room narrower than the view: the room
stays centered.) `shantae_view_info` reports `shown`, the room the last
picture showed.
Activation, the one-screen test and room zoom all use the narrowed room, so the
house pans in its 256-wide column at 7.5× (Fill, Aspect Fit, 1080p), the game
over and the credits fill the height like the original screen, and a picture
that is one screen shows the original view. A cut (the camera moving more than
64 pixels in a frame, like the intro's cut from the house to the bridge) snaps
the zoom like a dark frame. `shantae_view_info` reports `room` (x0, y0, x1, y1:
the camera's range plus the screen) and `room_masked`.

**Bottom strip.** A window along the bottom of the screen (from line 128 to 143)
stays at the bottom of the view like the status bar, and its background (the
color covering most of it) continues across the view's width: the intro's
strip from line 136 (the dialogue box's top before it slides up) is a
letterbox bar the width of the view, with whatever crosses it in the middle,
instead of stopping at the original screen's edge. The status bar is mostly
black, so it keeps black beside it.

Rooms are the practical limit: a view larger than its room shows the room
centered (the opening bridge is about 3070×600, so 3840×1080 shows all of it).
The compositor's cost grows with the pixel count. It looks each 8×8 map cell
up once a frame and expands a row of its pattern 8 pixels at a time; sprites
read the background under them from the same cells, and only the native
picture's top lines keep a palette-entry map (for the fade correction below).
A whole frame at the water tower's waterfall (the game, the compositor and
the runtime, headless, before the upload) takes about 1.0 ms at 426×240,
2.9 ms at 1920×1080, 4.3 ms at 2560×1440, 8.1 ms at 3840×2160 and 34 ms at
8192×8192; the compositor that looked a cell up again for every line and
kept four values per pixel took 1.7, 13.6, 22.8, 48 and 226 ms. With the
window's upload and present, 1920×1080 runs at about 3.5× speed drawing every
frame; Fast-Forward Frame Skip (on by default) draws only what the display
shows, and the 1000% Max Speed holds there.
Wider views also activate more enemies. Remove slowdown has to absorb them:
with the view on it holds a frame for up to eight frames of CPU instead of two
(the water tower at 1920×1080 takes three to eight a tick). The game's object
table grows from 32 slots to 157 to hold them, and the pool of collision nodes
that platforms and hazards take from grows with it (see Object slots below).
157 is as many as 16-bit addresses leave room for. In the water tower's densest
room about 108 spawn records are within reach at 1920×1080 (about 130 objects
with the ones they spawn), 185 at 2560×1440 and 282 at 3840×2160; when slots
run short the game's own spawns come first, so some distant objects come a
little late.

The runtime compositor supports independent width/height. It leaves the PPU and
serialized hardware state at 160×144. Gameplay is expanded; menus and dialogue
use the native picture (the original view above). The status bar stays at the bottom. Room edges
clamp the expanded camera, and a room smaller than the view is centered; brief
shake beyond native camera bounds is clipped
safely and does not disable the expanded view.

## Game bindings (USA ROM, CRC32 E994B59B)

- Map directory: C9F8–C9FA; metatile bank base: FFDF. Decode follows 00:26FD and
  00:2CCF. The presentation camera is the value the VBlank handler (00:0884)
  copies from C9FB–C9FE to SCX/SCY at 00:0A57/00:0A5C (generated PCs 0A5A/0A5F),
  not the next simulation camera in FFE1–FFE4. The handler commits only when the
  main loop has finished its frame (00:0851 sets FF8F and waits). On a lag frame
  the main loop has already moved C9FB–C9FE on while the screen keeps the old
  scroll, so reading C9FB–C9FE at line 0 put the surround one frame ahead of the
  native picture: a seam at the original screen edge whenever the game slowed
  down. The committed value always lands in VBlank (LY 145–146 measured). Until
  the first commit after a reset or state load, C9FB–C9FE are used. Map validity
  is checked directly, without comparing static map tiles against
  animated/destructible background objects.
- Background offset: the camera routine (03:722F → 03:724D, and 03:72EB from
  banks 05/1C/1F) builds the scroll as the camera FFE1–FFE4 plus C9D2/C9D4, while
  sprites are placed with the camera alone. Bosses drawn in the background move
  their body with it: the first boss (1B:4E88) sets C9D2 from its x and swings
  the scroll about ±60 pixels while the camera stays at 944. The offset read at
  03:725A/03:726A (03:72F0/03:7300) is kept with the committed scroll. The
  native picture and the sprites are placed by the camera; the background is
  drawn at the scroll, clipped to the room at its displayed position. Placing
  everything by the scroll put a second, shifted copy of the picture over the
  surround: two Shantaes, two eyes, and pillars in the wrong places. Risky's
  ship in the opening (the hold below its deck) bobs C9D4 by ±4 in a room taller
  than the view; the surround there lines up with the native picture at every
  offset. The boss moves C9D4 after the tick's build (03:726B reads it right
  after the VBlank), so Reduce input lag takes the offset the committed scroll
  was built with (noted at 03:730E), not C9D4 at the VBlank: in a tick where
  the camera moved +1 and the offset −1, the latter put the copied sprites a
  pixel off the committed camera (the opening-boss replay's frames 656 and
  1139).
- Small rooms: a room narrower or shorter than the view is centered along that
  axis (the ship is 192 wide, camera x 32–63). The origin used to clamp to the
  room's left/top edge, leaving the room in a corner. Activation shares the
  origin but stops at the room plus the original's own margins: 01:506A
  spawns records within 8 pixels of the camera's screen and 01:502F keeps
  objects within 80 across and 72 down. The original's camera stays in the
  room, so it never wakes anything farther outside it than that; the view
  draws nothing outside the room either (sprites are clipped to it like the
  background). Without that limit a view wider than its room, or the first
  frames after a state load (activation uses the size last presented, before
  the room zoom is worked out again), woke the objects of the map's other
  rooms: the caves are one map of twelve rooms. Retention (00:11C5) compares
  bounds by a signed 16-bit difference, so a negative left bound is fine, as
  it already is natively near x = 0.
- One-screen rooms: camera bounds pinned to a single screen (the first boss's
  arena, 944,368 to 944,368) have nothing outside the native picture to show,
  so the picture stays native (the original view) and activation stays native,
  even if the same map was presented expanded before. Rooms are loaded by far
  calls (`CALL 0540`, then lo, hi, bank) to 03:7920 with 17 inline bytes: the
  camera's least x and y, a span for each (03:79E4 makes the greatest x and y
  the least plus the span minus 1), the map directory for C9F8–C9FA, FFDD–FFDF
  and C399–C39B. 03:7A3D and 03:7AFE take 8 bytes and change the bounds only.
  Of the ROM's 112 room loads, 31 have spans of at most 1 both ways (one
  screen); 48 more show at most 960×532 (of them five 319×128 corridors and
  five 511×255 rooms above the status bar), 4 are smaller than 1920×1064 both
  ways, 19 one way only (corridors up to 8191 wide), and 10 are at least that
  large. Room zoom (above) scales all of those that are smaller than the view.
  The test is on the room kept to its pictures (above), so a one-screen
  picture under wider bounds counts too.
- Scenes: 0A:4919 dispatches scene CBFD (66 of them, 00–43) when CBFC is set,
  from WayForward's debug scene grid (the code Left ×2, Right ×8, Left ×6,
  Right ×2, Left ×7, Right ×6, Left ×8 at "Press Start", then Start Debug Game;
  Select on the inventory opens the grid again while CC04 is set).
  `logs/states/debug-grid.state` is the grid; poking CBFD and CBFC=1 there loads
  a scene, and A held picks another entrance for some (3F: the credits' shaft).
  From a cold boot the code works at "Press Start" by frame 300, then Start,
  Down and A (Start Debug Game) and A (File Select's New 1) reach the grid
  (`tools/check_budgets.py` makes it so). In a scene, Select+A starts the
  debug flight, 4 pixels a frame in all four directions; Select again ends it.
  Flying does not take a room's exits: end it and walk. Dialogue scrolls fast
  with B held; B again goes on. The flight asks for debug mode only at
  06:472E (in 06:46E2, which the player's movement routines call in normal
  control): with CC04 set and Select+A held, 06:473D sets the player's
  script to 06:71B5 (+16 = $0080, +5 = $FF), which installs the flight
  callback 06:71D4 and clears C30D and CB78; Select there (06:7246) goes
  back to script 06:49F2 without asking. The debug command `shantae_flight`
  writes what 06:473D writes, so any state can fly without debug mode,
  which would also open the grid from the inventory (05:5A43) and have
  0A:4468 skip the save file (04:4D55). In the debug server's `set_input`,
  T is Select and S is Start.
- Metasprites: capture descriptor and world position at 00:1DC6, before native
  culling. Preserve the D700/D800 draw lists when 01:667C swaps shadow OAM pages.
  The frame descriptor contains piece layout; the animation points to the ROM
  graphics block. This avoids the native OAM capacity and viewport restrictions
  in the added area.
- Background rectangles: capture objects at 01:4F16's FFBD read (generated PC
  4F19), before the 01:4F12 cull. Object offsets 2E/2F supply picture bank/pointer;
  5B/5F give absolute tile coordinates. Pictures contain width, height, then
  tile/attribute pairs, matching 00:0F15 and 00:034E. Rebuild the overlay each
  frame; never accumulate camera-relative VRAM tiles. Native background culling
  remains unchanged, avoiding corruption from wrapping its 32×32 tilemap.
- Activation: provide wider bounds only to 00:0FC6–1058 and wider sector indices
  to 00:1114–11BE. The native scan handles a 2×2 sector batch; larger rectangles
  cycle batches, with an activation guard outside the visible area. Views up to
  704×480 take at most four passes and keep the 32/16-pixel guard (8 pixels a
  frame across, 4 down). Beyond that the guard is 8/4 pixels per pass, and
  since a wider guard spans more batches, the pass count is the smallest that
  covers the batches of its own guarded rectangle (853×480 takes six, 48/24;
  the whole map at most 17×17). Sizing the guard from the view plus the
  32/16-pixel guard alone left sectors unscanned for views a few thousand
  pixels wide. Activation uses the presented size, so it follows an adaptive
  view. The cycle keeps up with a camera moving 8 pixels a frame across and 4
  down; an area that appears faster took a whole cycle to wake (15 frames at
  1920×1080), and its objects appeared inside the picture: a room's first
  widened frames, a cut (a warp inside a level moves the camera hundreds of
  pixels in a frame), the picture growing as the room zoom eases out or the
  window is resized. Such an area is filled in the same call: when a side of
  the spawn rectangle moves out faster than that (or on the first widened
  call in a room), every other sector of it is queued, and where 00:1114
  comes back from its first sector (00:1143, after the CALL $0FA9 at 00:1140)
  each is scanned through 00:0FA9 as 00:1122–1140 would set it up (HL = the
  sector's entry in the record directory, page C39A + y/2, (y & 1) × 128 +
  x × 4, bank C39B), coming back to 00:1143. `fills` in `shantae_view_info`
  counts them. While the room zoom eases out, activation takes the size of
  the picture it eases to, so what the growing picture shows is awake first
  and the area is filled once. Retention reads in 00:11C5–1289 use expanded
  bounds: the spawn rectangle (the view and its guard, stopping at the room
  plus 8) with the original's slack past it, 72 across and 64 down (its
  spawn margin of 8 to its 80 and 72). They were the view plus 80 and 72,
  inside the guard once it passes 80 (120 across at 1920×1080): what spawned
  by the guard was released the next frame and spawned again a cycle later,
  and a child made at its parent's spawn went for good while the parent
  stayed (Sky's crow, below). The original
  allocator, spawn flags, scripts and cleanup still run. Physics and camera
  consumers see their original values. Earlier enemy activation is intentional.
- Object slots: the game has 32 (D000 in WRAM bank 3). 01:4CB6 builds the
  free list on every map load, and 01:4DB0 takes its head (FFB3, linked
  through slot+7C). The spawner sets a record's flag (00:1061) before it asks
  for a slot, so a record that gets no slot stays flagged with no object until
  the map reloads. The original's bounds almost never fill the slots, but the
  widened ones did. At 1920×527 in a labyrinth every slot held an object the
  view had activated, so the door warps between rooms (object script 0A:4048;
  the record's arguments are the destination) were lost and their doorways
  became dead ends. At 1920×1080 the water tower's room wanted about 130, so
  the drop that becomes its mini-boss (object 0A:4070) found none. With the
  view on, the table has 157 slots (`object_slots.c`), as many as 16-bit
  addresses leave room for; towns keep the original 32 (see Towns below). While SVBK selects bank 3, two windows are memory
  of the table's own (the runtime's WRAM extension):
  - E000–FDC5 instead of the echo of C000, which only the map decoder reads,
    with SVBK 1: slots 32–92 continue from D000 (slot 32 starts at DFC0, a
    part of bank 3 the game never uses; slot 92 ends just below OAM);
  - A000–BF7F, slots 93–156, instead of cartridge RAM, which only the four
    save routines in bank 4 use. They switch it on (`LD A,$0A` at 04:4CA8,
    4CF6, 4D30, 4D5A) and off (`LD A,$00` at 04:4CDF, 4CE8, 4D24, 4D4E,
    4D90, 4D99) with interrupts disabled, and the window steps aside between.
    It does not follow the RAM enable itself: an allocation that finds no slot
    writes a whole object to 0000+ (09:55C8 does not check), which the MBC
    takes for RAM enables.

  01:4CB6 counts 93 slots from D000 (01:4CD1) and gets slot 92 linked on to
  A000 (01:4CFE); the object scripts count 157 (00:1312) and step from slot
  92 to A000 (00:1384). Two passes are unrolled for 32 slots, and the slots
  past 31 join them as though the code went on. The movement pass (00:0C45)
  stops at 00:0C4B and calls their routines from the top before slot 31's,
  each returning to 00:0C4D. Draw layering (13:4000) lists them after the
  others in lists kept outside bank 7 (a list there holds 32), and the
  consumer (01:4E90) reads the longer counts and the entries past a list's own
  from them, at most 127 in all. Each costs the guest time the original code
  takes for a live slot, one slot at a time. A third unrolled pass, the y-sort
  at 13:63BB, is never installed by anything in the ROM and keeps its 32. The
  inventory's copy of bank 3 (05:6D08, 05:6D20) takes the extra slots along.

  Collision: platforms and hazards each take a node from a pool that 01:7120
  builds in bank 7 right after the table (both from 01:4CA3, on every map
  load): 12 nodes of $13 bytes at DD40, free list FFEA through +10, the ones in
  use at FFEC. The player's checks (01:7266, 72BC, 7312, 7368, through 00:2AF2
  onwards) walk those and jump to a handler in each (+2, +5, +8, +B); a
  platform's landing check is 01:73BE at +B. The allocator (01:7192) hands out
  none when the pool is empty, and the platform or hazard asking collides
  with nothing (09:643C, the platforms' setup, then writes its handlers to
  0000+). 12 nodes do for 32 objects, not for 157: in the water tower seven
  rising platforms far off to the left held seven of them, and the electric
  platform over the shaft and the rising platforms Shantae jumps to got none,
  so she passed through them. With the table, the pool grows by one node for
  each slot at E000 while SVBK selects bank 7 (the echo there is not read
  either), on the end of the free list (01:7186). Its handler jumps dispatch
  like the pool's own (`ram_native.c`). The free routine (01:71FF) asks
  whether a node has a neighbour on each side by the low byte alone (LD
  A,(HL+) / LD E,A / LD D,(HL) / OR E at 01:720B and 01:7224). None of the
  pool's own 12 has a low byte of 0; the first extra node, E000, does, and
  freeing a node beside it kept E000's link to the freed node (a freed node
  still in use, the in-use list running on into the free list) or its link
  back, with which freeing E000 later cut a live node out of the list: a worm
  in the water tower's shaft that Shantae jumped through. The read tap puts
  the neighbour's high byte in A before that OR E (generated PCs 720E and
  7227), so the test takes the whole address. Before each movement pass the
  pool's lists are checked: a node on neither list goes back in use when its
  object is alive and names it at +1D (else on the free list), and the prev
  links and CA03 follow the lists; that mends states saved with the damage.
  09:480D, which gives an object (DE) its node, then clears the object's +6B
  through BC, which 01:7192 has made the node, so the byte at node + 6B in
  bank 3 goes instead: one of slots 27–29 for the pool's own nodes, of slots
  33–56 for the extra ones. The original's few objects hardly reach those
  slots; the grown table fills them, and for node DD79 the byte is the high
  one of slot 28's movement JP (DDE4). The next movement pass jumped to 00EE,
  the ROM's $FF filler, where RST $38 recursed until the stack ran down
  through WRAM and over the VBlank vector at C374, and the runtime aborted at
  the uncompiled jump there. A hook on the LD A,$00 before the store
  (09:4880) points HL at the object instead, with the grown table only.

  Gates with a padlock (object 0A:4044) leave a part behind (script 09:56E2)
  that waits for the gate's signal, which not every way a gate goes sends; the
  wider view releases and respawns gates far more often, and the parts piled
  up. One that no gate links (+61) is freed at the start of the movement pass.

  Object 0A:42F8 (one, in the caves' first room, x 478; script 11:77E7)
  spawns a child that watches for the player, and 11:7820 links the two both
  ways at +63. When retention releases it, its callback (11:78B2) frees the
  child through 00:12D7, but 11:78B6 loads the link the wrong way round
  (`LD A,(HL+)` / `LD C,(HL)` / `LD B,A`): with the child at D372 it frees
  72D3, and 00:12E7 puts 72D3 at the head of the free list. Every slot then
  free is lost (the next object taken is written over ROM; from a child at an
  E000 slot, over the middle of another slot), and records outside the
  original bounds wait for slots that never come, so enemies appear only
  once the original bounds reach them, in the middle of the view. The
  original reaches this too: floating to the right end of that room (camera
  x 585) releases the object, and afterwards nothing respawns there, the
  room's ladders included. The view reached it from the next room (the first
  frames after a state load there woke the object, and the zoomed view then
  released it). A read override has 11:78B6 take the link the right way round
  (with the grown table only; `children_freed` counts it).

  The free list holds exactly the slots whose status (+0) is FF: 00:12F7 sets
  it as 00:12E7 links a slot, 01:4DFD sets 80 as 01:4DB0 takes one, and no
  other code or object script writes either. Before each movement pass the
  list is checked: one that leaves the table, meets a slot again or reaches a
  live slot is cut there, and the dead slots it does not hold go on its end
  in table order (C397, a count nothing reads, is counted again;
  `slot_repairs` counts the mends). That mends states saved with such damage.
  This check, the collision pool's and the reapers wait while the inventory
  has the level's table in bank 5 (from 05:6D08 to 05:6D20). Leaving the
  inventory through the debug grid (Select, 05:5A43) never restores it, and
  the waits then lasted into every room after; a room load (01:4CA3, the only
  caller of the pool build 01:7120; the inventory builds its own table
  through 00:0C26) now drops the copy.

  The game's own objects come first when slots run short
  (`expanded_background.inc`): a record whose flag is read (00:1054,
  generated PC 1055) while no slot is free reads as already spawned, so it is
  tried again instead of lost; a record outside the original's spawn bounds
  (FFC5–FFCC) also waits while 12 or fewer slots are free; and while fewer
  than 12 are free, retention hands the original bounds (FFB5–FFBC) to
  objects off screen that those bounds release, farthest first and only as
  many as it takes (below 4, to such objects on screen too). Encounters keep
  the original distances: the water tower's mini-boss (object 0A:4070) is a
  drop that falls the moment it exists and then heads for Shantae's x
  wherever she is (09:7442 through 00:2BD4, x alone), so it spawns and is
  retained with the original bounds. The encounters are a list of objects,
  read from their scripts (object table 0A:4000, script VM 00:1342 with its
  opcode table at 00:0600): of the objects placed at most twice in a map the
  view widens, the rest are NPCs and their houses, collectibles, story set
  pieces, props and triggers that wait for the player's box (00:38BE). The
  fight scripts that lock the camera (around 1A:54FD, 25:4692, 27:6A68,
  07:5587) are started by native code; no record or other script names them.
  The first boss's arena is a one-screen room. The rule used to take
  every object placed only once in its map, and
  Mimic, his door and his porch in Scuttle Town (0A:42D4, 0A:42D0, 0A:42CC)
  popped in and out in the middle of a 1920-wide view. A door warp flagged
  while no slot holds it (a state saved with such a dead end) spawns again:
  doors are never defeated or collected.

  The table's size, the extra draw lists, the inventory's copy and the extra
  collision nodes live in the WRAM extension past the windows, which save
  states and rollback carry. A state saved before the table grew gets the
  extra slots at the end of its free list when loaded (one from the 93-slot
  build keeps its 93 and gets the rest); one saved before the pool grew gets
  the extra nodes at the end of the pool's. An object that found no node in
  such a state keeps none until it spawns again (a room reload, or retention
  releasing it). `shantae_view_info` reports `slots`, `free_slots`,
  `spawn_waits`, `encounter_waits`, `early_releases`, `doors_restored`, how
  often slots past 31 were moved, layered and drawn (`extra_moves`,
  `extra_layered`, `extra_drawn`), `slot_upgrades`, `orphans_freed`,
  `node_pools` (pools built with the extra nodes), `node_upgrades`,
  `node_repairs` (nodes the pool check put back on a list; 0 in play, 1 for
  the worm-shaft state below), `slot_repairs`, `children_freed` and `fills`
  (areas the spawner filled at once; see Activation). The debug
  command `shantae_slots` gives the table at a glance: the free list's length
  and where it breaks (`cut`: 0 whole, 1 leaves the table, 2 meets a slot
  again, 3 reaches a live slot), `lost` (dead slots off the list), the camera
  and each live slot as [slot, status, record bank, record, x, y, width,
  height].
- Activation widens only in a room (map directory C9F8–C9FA) the view has
  already presented expanded. A dialogue box or the inventory in that room keeps
  the wider activation, so nothing despawns while talking; a room that is never
  presented expanded keeps the original activation throughout. New camera bounds
  within the same map do not reset it.
- Towns keep the original picture, activation and object table. The town
  player (04:62CC) stays at screen x 84, and the camera wraps at 640
  (04:641E/04:6474) past the room's own maximum (C9DD = 480). Objects near the
  seam have spawn-record duplicates at +640 (the Firefly Shrine door at 56 and
  695). The clamped wide window missed the duplicates, so the Firefly Shrine
  door never existed after the wrap. The door handler (04:60B8) also compares
  only the low byte of the player/door distance (04:60E0 `CP $10` then
  `SBC A,D` with A = D): within 16 pixels the door's label shows (CBD6) and Up
  enters (the player's routine becomes 04:6584). The original 320-pixel
  retention never keeps a door 256±16 pixels away alive, but the wide one did,
  and such a door took over the label and the Up entrance. That is an
  original-game quirk, left as is.

  All five towns are one map, 67:9B, strips 640 wide at y 0, 768, 1120, 1376
  and 1632, loaded by 18:5C3D, 18:5FEA, 18:638B, 18:6738 and 18:6AD5 with the
  camera's x from 0 to 480 and its y pinned; no other of the ROM's 112 room
  loads or 21 bounds changes gives those bounds. A town is that room
  (`town_room`), not the building-name panel: the panel is not up on every
  frame (a state load shows two frames with only the status bar), and one frame
  presented expanded marked the whole map widened. The shops and houses share
  the map (18:71F2, 1F:4303, 1F:5111 and others, x from 248 and y 336-624), so
  leaving one that had been presented expanded widened the town too; with
  that, the user's state1 in Water Town (2026-09-26) found the Firefly Shrine's
  label empty and Up did nothing. A town is never presented expanded, never
  activated wider, and its map load builds the original 32 slots and 12
  collision nodes (object_slots.c): the grown table's own work (the scripts
  pass counting 157, the build counting 93) takes guest time, and the main
  loop spends what is left of each frame calling the random numbers (00:0852
  calls 00:0B13 into C381/C382 until the VBlank), so the townsfolk chose
  differently than in the original. A town state saved with the grown table
  goes back to it on load while nothing lives past slot 31 (`town_tables` in
  `shantae_view_info`, `town` for the room).

- Totems: the labyrinth (map 50:4A, records in bank 4D) has four totem
  puzzles, at 488,1880 (three stones), 1960,3176 (three), 2496,2608 (two) and
  3104,1360 (four). A stone (object 0A:4054, script 0C:4340) takes its totem
  and place (0 the top) from its record (+20, +21), keeps its face (+65,
  1-3) beside the record's flag (the byte after it in WRAM bank 4, 00:0F5C),
  and notes itself at C004 + 2 × place
  (0C:43C8) as it spawns and after every flip, which starts its script
  again (0C:442C). A whip that reaches it (callback 0C:4465) flips it. The
  totem's pedestal (0A:4058, callback 0C:4C25) reads the stones noted at
  C004–C013 every frame and, once their faces are the totem's solution
  (C035 + 8 × totem), sets CA93 + its +22 >> 4 and becomes the orb
  (0C:41E1); a crouching whip frees the key and walking into it takes it
  (CA85). The notes are one list for all four totems, and no two are ever
  within the original's reach of each other. The view kept the stones by
  Shantae alive while it woke another totem's (the one at 2496,2608 is 536
  pixels right of and 568 above the one at 1960,3176, inside a 1920×1080
  view), whose stones noted themselves over
  them and, once released, left their slots to fireballs. The user's state2
  (2026-09-26, 1920×1080) had the top two notes on slots F07C and F5E6:
  whipping the top stone to its face solved the puzzle and the orb never
  came. The pedestal's reads (0C:4C4F and 0C:4C51, then 48 bytes on for
  each place; generated PCs 4C50 and 4C52) now take the live stone of its
  own totem at that place: its record's totem and place, and its script
  position in 0C:4340–4464 (a flip moves its callback to 00:0C41, and a slot
  keeps a released stone's record and arguments, so a fireball there still
  reads as that stone), the noted one first, and the note itself when there
  is none. Where the original can solve a puzzle at all its notes are those
  stones, so this reads what the original would; it applies whenever the
  view is on, which also mends states saved with the damage.

- Budgets: some spawners share a count of what they have made (a WRAM byte
  raised as each is made) and make no more at its limit; what they made
  lowers it when retention releases it (a callback that asks 00:12B2 and then
  counts down, or one that calls such a callback first) or when it dies. The
  tinkerbats (CC2E, above) are one. An audit of the ROM for the pattern (a
  compare of a WRAM byte with a small limit followed by its increment, in
  object scripts through VM op 98 and 96 or natively) found four more whose
  count the view changes:

  | Spawner | Map (debug scene) | Count | Compare |
  |---|---|---|---|
  | swamp creatures 0A:4204, 12 placed | 40:9F (0D) | C000 < 1 | VM op 98 at 21:5BED |
  | 0A:41F8, every 60 frames | 67:85 rooms B/C (09) | C000 < 3 | 21:4D80 |
  | 0A:4220, near Shantae | 67:85 room A (36) | C000 < 2 | 0B:6852 |
  | 0A:41AC and 0A:41B0, near Shantae | 40:58 (02) | C080 < 2 | 0B:4698, 0B:4BB0 |

  A swamp creature surfaces once Shantae is about 112 pixels past its place
  and swims after her, one at a time; the view kept the one following her,
  and flying right through the level at 1920×1080 three of the twelve never
  surfaced and three surfaced 300-390 pixels late. Hovering by 0A:41F8's
  three spawners at 419-516,1390-1446, the original's nine creatures came
  from them and the view's six all from the one at 954,1196, 500 pixels off.
  Along their rows 0A:4220 made 17 and 15, and map 40:58's pair 14 and 6.

  Each compare now reads, for a spawner the original's retention bounds
  (FFB5-FFBC) keep, the count less what the original would already have
  released: what wears one of the budget's release callbacks and is outside
  those bounds, at most the budget's allowance of them (its limit; twelve
  for the swamp creatures, each of which surfaces once as she passes). What
  is between states (just made, or dying: 0B:47D8, 0B:4829, 0B:6C4C and
  21:4FA2 do not count down) stays counted. With no allowance 0A:41F8, which
  makes one every 60 frames whether Shantae is near or not, had 52 alive
  after 1000 frames by the cluster: its creatures flew out of the original's
  reach and were replaced without end. A spawner only the view runs reads the
  count as it is, so it only makes one while the budget is free. Either reads
  no more than the limit, since most of these compares test for the limit
  itself (`CP $03` / `JR Z` after 21:4D80) and the view's count can pass it.
  The count stays the game's, up for each one made and down for each one
  released, so at most the limit plus the allowance are alive at once.
  `budgets` in `shantae_view_info` counts the compares answered other than
  the count.

  Not affected: 0A:412C (C01F < 2, 14 placed in map 5E:8F) makes one only
  within 48 pixels of Shantae and made 6 and 5. The other objects that note
  their own address in a fixed byte either read it back in the same routine
  (98 sites, which make a child and place it at themselves: CBFA, C020 and
  the like), write it only when Shantae touches them (0A:431C, 37 placed;
  0A:4354), index it by their own argument (0A:4014), sit in rooms of their
  own (0A:4280 and 0A:4288 in map 62:BB), are placed once, or belong to
  scene code, menus, the text engine, the credits or boss arenas.

- Sky's crow: the desert labyrinth's door (map 74:AB, object 0A:4238,
  script 15:5804, record AD:70F7, box 880-911 × 880-919) picks its step by
  CA9C as it spawns (15:582A): at 1 it makes the crow (15:591F: a slot
  through 00:0C2D, script 15:5A37, at 890,890, 12×16 from its script, no
  record) and waits at 15:5873 for CA9C to reach 4; at 2-5 it makes the
  crow's later forms (15:5961, 15:59A3). Nothing else makes the crow. It
  perches on the door, and walking into it (its callback 15:5A66: Shantae's
  box FFEE-FFF5 over its own, B newly pressed) starts its dialogue, which
  takes CA9C to 2, 3 and 4 (15:5B5B, 15:5C87, 15:5CDA); the door then opens,
  and Up held on it for 10 frames (the helper 06:7143 sets CB7C) enters the
  labyrinth (map 78:BE). Retention keeps the door while its record box is in
  its bounds and the crow while its own box is, so an edge of the bounds
  between the two (nine pixels on the left, ten on the right) keeps the door
  and releases the crow; the original can do this too, but its bounds are 72
  pixels past where the door spawns. With the view's bounds inside the guard
  (Activation above), the door spawned, was released with its crow, and
  spawned again every cycle until the bounds reached it, and whether the
  first spawn within them fell in that band came down to the cycle's phase
  and Shantae's pace: flying back to the door
  at 4 pixels every third frame at 1920×1080, 3 of 6 approaches lost the
  crow and none of 3 in the original. The user's state1 on 2026-09-27
  (1920×1080, beside the door, `logs/states/crow-door.state`) was saved
  with the door waiting and no crow. With the slack, 16 of 16 approaches (4
  pixels every frame, every third frame and every fourth) keep it, the door
  spawning once. A state file loaded with the view on whose door waits at
  15:5873-587F while CA9C is 1 and no crow runs its script (15:5A37-5B67)
  has the door go back to 15:585E on its next script pass (+16 = $0080), so
  its call to 15:591F makes the crow again (`crows` in `shantae_view_info`).

- Scene gate: menus reuse the map engine state. The inventory keeps C9F8–C9FA
  and the room bounds, zeroes both cameras, and replaces the tilemap and tile
  data. The view expands only while at least half of the fully visible native
  tilemap matches the ROM map plus this frame's background rectangles. Gameplay
  replays measure 100%; the inventory measures 0–3%. Otherwise, the native picture
  is centered, as for dialogue.
- Palettes: when FF90 is set, the LYC=0 STAT handler (00:0ABC → 00:1D4D →
  01:5E0B) uploads the fade palettes from C440 (BG) and C400 (OBJ). It writes one
  8-byte palette per HBlank-gated block, so the top ~16 native lines mix old and
  new colors. Pausing blanks palettes during VBlank. The surround uses the
  palettes read by the runtime's `gb_custom_frame_end` hook when line 143
  completes. Each native pixel in the top 24 lines has a predicted palette entry
  from the compositor's own BG/sprite render. If that pixel still shows the
  entry's line-0 color, the settled color replaces it. Line-0 palettes alone
  left a darker rectangle on every fade frame. Frame-end palettes alone left a
  1–2 line strip at the top of the native picture.

The central native picture is retained for exact hardware effects. Snapshot
diagnostics compare captured metasprites with OAM, live background rectangles
with the native tilemap, and the map-gate tiles (`map_checked`/`map_matched`).
`shantae_view_info` returns these counts followed by the captured metasprite records.

## Regression checks

With MinGW on PATH:

```powershell
cmake -G Ninja -S generated -B generated/build
ninja -C generated/build expanded_view_check object_slots_check
generated/build/expanded_view_check.exe
generated/build/object_slots_check.exe roms/shantae.gbc
# Rooms without live background objects, at the user's 426x240.
python tools/view_state_repro.py logs/view-pitjump/frame-11000.state --route R:120,L:120 --width 426 --height 240 --out logs/view-pitjump-426 --port 14371
python tools/check_expanded_replays.py logs/view-pitjump-426 --width 426 --height 240 --no-background-objects
# Live background objects at the default size.
python tools/view_state_repro.py logs/view-expanded/frame-7000.state --route R:180,L:180 --out logs/view-background --port 14372
python tools/check_expanded_replays.py logs/view-background
# First boss (the user's state2 on 2026-09-22): native picture and activation,
# sprites matching OAM while the boss swings the background offset. The
# original view is presented on its own, so these captures are 160x144.
python tools/view_state_repro.py logs/states/boss-arena.state --route=-:900 --interval 5 --width 426 --height 240 --out logs/view-boss --port 14373
python tools/check_expanded_replays.py logs/view-boss --width 426 --height 240 --one-screen --offset
# Opening boss (the user's state3): holding Down and tapping B every other
# frame drops Shantae into the ship's hold, where the background bobs.
python tools/view_state_repro.py logs/states/opening-boss.state --route=D:1600:B --interval 4 --width 426 --height 240 --out logs/view-opening-boss --port 14375
python tools/check_expanded_replays.py logs/view-opening-boss --width 426 --height 240 --no-background-objects --offset
# Inventory: the gate must close while paused and reopen after unpausing.
python tools/view_state_repro.py logs/view-expanded/frame-7000.state --route=-:12,S:3,-:120,S:3,-:120 --interval 3 --out logs/view-pause --port 14374
# Labyrinth doorways at 1920x527 (the user's state4 on 2026-09-23, saved with
# 32 slots and the door warps already lost): the table grows to 157 on load,
# the 8 doors spawn again, Right goes through the doorway into the tower
# (camera x 2182 -> 2298 at about frame 80) and Left comes back (-> 2171 by
# frame 170). spawn_waits stays 0. All 12 of the collision pool's own nodes
# are in use by frame 80; the next one would come from E000.
python tools/view_state_repro.py logs/states/labyrinth-door.state --route R:80,-:30,L:150 --interval 2 --width 1920 --height 527 --out logs/view-labyrinth --port 14376
python tools/check_expanded_replays.py logs/view-labyrinth --width 1920 --height 527
# The waterfall tower at 1920x1080 (the user's state3 on 2026-09-23, saved
# with 32 slots): 50-60 objects in slots past 31, down to a few free.
python tools/view_state_repro.py logs/states/popin-state3.state --route R:240 --interval 4 --width 1920 --height 1080 --out logs/view-tower --port 14377
python tools/check_expanded_replays.py logs/view-tower --width 1920 --height 1080
# Scuttle Town at 1920x1080 (the user's state3 on 2026-09-24, saved before
# Mimic's house had spawned): Mimic, his door and his porch (0A:42D4, 42D0,
# 42CC) spawn once, on the load, and stay through 400 frames of Left and 400
# of Right; encounter_waits stays 0. With every object placed once in its map
# held to the original bounds they popped in and out mid-view.
python tools/view_state_repro.py logs/states/mimic-unload.state --route L:400,R:400 --interval 10 --width 1920 --height 1080 --room-zoom 1 --window 1920x1080 --out logs/view-mimic --port 14382
python tools/check_expanded_replays.py logs/view-mimic --width 1920 --height 1080 --room-zoom
# Room zoom at 1920x1080 in a 1920x1080 window with Pixel Perfect (the routes
# above keep room_zoom=0): Risky's ship hold at 7x (274x154) with the
# background offset, the opening area at 2x (960x540), a 4847-wide corridor
# at 2x and, with Whole room, a 3871-wide one at 2x.
python tools/view_state_repro.py logs/states/opening-boss.state --route=D:1600:B --interval 4 --width 1920 --height 1080 --room-zoom 1 --window 1920x1080 --out logs/view-zoom-opening --port 14378
python tools/check_expanded_replays.py logs/view-zoom-opening --width 1920 --height 1080 --no-background-objects --offset --room-zoom
python tools/view_state_repro.py logs/view-expanded/frame-7000.state --route R:180,L:180 --width 1920 --height 1080 --room-zoom 1 --window 1920x1080 --out logs/view-zoom-bridge --port 14379
python tools/check_expanded_replays.py logs/view-zoom-bridge --width 1920 --height 1080 --room-zoom
python tools/view_state_repro.py logs/states/jump-latency.state --route R:150,L:200 --width 1920 --height 1080 --room-zoom 1 --window 1920x1080 --out logs/view-zoom-corridor --port 14380
python tools/check_expanded_replays.py logs/view-zoom-corridor --width 1920 --height 1080 --room-zoom --no-background-objects
python tools/view_state_repro.py logs/states/monkey-dance.state --route R:240,L:120 --width 1920 --height 1080 --room-zoom 2 --window 1920x1080 --out logs/view-zoom-whole --port 14381
python tools/check_expanded_replays.py logs/view-zoom-whole --width 1920 --height 1080 --room-zoom --no-background-objects
# Debug scene B2 at 1920x1080 in a 1920x1080 Aspect Fit window (the user's
# state1 on 2026-09-24 is the scene grid on B2; these are saved in the scene).
# The stone's rumble shakes the camera to 282 in a room from 284: the 2-pixel
# column at x 12-13 stays black. From b2-activation.state the cut to the
# second arena (about frame 83) keeps 256x144 at 7.5x, `room` 796-1024 while
# the boss rises, and from about frame 323 `shown` eases 796 -> 752 with the
# zoom to 271x152.
python tools/view_state_repro.py logs/states/b2-shake.state --route=-:60 --interval 1 --width 1920 --height 1080 --room-zoom 1 --window 1920x1080:1 --out logs/view-b2-shake --port 14383
python tools/check_expanded_replays.py logs/view-b2-shake --width 1920 --height 1080 --room-zoom --no-background-objects
python tools/view_state_repro.py logs/states/b2-activation.state --route=-:400 --interval 1 --width 1920 --height 1080 --room-zoom 1 --window 1920x1080:1 --out logs/view-b2 --port 14384
python tools/check_expanded_replays.py logs/view-b2 --width 1920 --height 1080 --room-zoom
# The labyrinth's totem at 1960,3176 (the user's state2 on 2026-09-26,
# 1920x1080, saved with the top two stone notes on other objects' slots):
# one whip at the top stone at the height of a jump gives the orb (the
# pedestal leaves 0C:4BFB), a crouching whip frees the key, and walking left
# takes it (CA85 0 -> 1). --native plays it in the original view with the
# notes put right by hand, for the original's outcome; both pass, and at
# 426x240 and 3840x2160 too.
python tools/check_totem.py
python tools/check_totem.py --native
# Spawner budgets (about 5 minutes): the debug grid made from a cold boot,
# four routes flown in both views. The swamp creatures surface in both,
# within 40 pixels of the original's lead; by 0A:41F8's cluster at least
# three are made by Shantae and never more than six counted; 0A:4220's and
# map 40:58's make at least two thirds of the original's number (their random
# waits differ between the views), never more than four counted.
python tools/check_budgets.py
# Sky's crow at the desert labyrinth (the user's state1 on 2026-09-27,
# 1920x1080, saved beside the door with the crow lost): loaded, the door makes
# the crow again; B on it through the dialogue takes CA9C to 4, and Up held
# on the open door loads the labyrinth (map 78:BE). Then six approaches: fly
# right (shantae_flight) until the door has been released for 40 frames,
# back at 4 pixels every third frame, a frame later each time; the crow must
# be on the door after each. --native flies them in the original view.
python tools/check_crow.py
python tools/check_crow.py --native --phases 3
```

`--room-zoom` writes `room_zoom` to the isolated `shantae.ini` and `--window`
sets the window the zoom is worked out for; the checker then takes each
capture's size from `shantae_view_info`. (Holding Right longer from
`jump-latency.state` leaves the corridor for the one-screen temple entrance at
about frame 280.)

Use `--route=` when a route starts with `-` (no buttons). A third field in a
route part (`D:1600:B`) presses those buttons every other frame on top of the
held ones; a slower tap does not trigger the hold. The replay tool writes
only the view settings, so slowdown removal is at its default (on).
`logs/states/temple-entrance.state` (the user's state1 on 2026-09-22) is another
one-screen room, without a boss; Up at its door loads the next map at about
frame 110, where `node_pools` goes up by one (01:7120 builds the pool again,
extra nodes included; the count starts at launch, and booting builds one).
`logs/states/electric-platform.state` and
`rising-platform.state` (the user's state3 and state4 on 2026-09-23, at the
bottom of the water tower's shaft and among its rising platforms, 1920×1080)
were saved while the 12 collision nodes were all taken: loading them adds the
extra nodes, but the platforms that had found none (slots 80 and 81; 80 and
105) only get one when they spawn again. Released through the game's own
path (as 00:12BD does) and spawned again, the electric platform catches
Shantae dropped onto it and shocks her while it is electrified; given its
node, the rising platform below her catches her after a jump.
`electric-platform-fixed.state` and `rising-platform-fixed.state` are those
repaired copies. The electric platform is solid only from above (its node's
only handler is the landing check, 01:73BE at +B): jumping into it from the
floor passes through, as in the original.
`logs/states/worm-platform.state` (the user's state3 later on 2026-09-23, in
the water tower's worm shaft at 1920×1080) was saved with the worm above
Shantae (slot 23, DB52) owning node E013 on neither list; holding A from the
load she lands on it (FFA0 = DB52 from about frame 44) and `node_repairs` is 1
after the first tick. Random play from `rising-platform-fixed.state` damaged
the lists within about 1000 frames before 01:71FF's test took the high byte.
`logs/states/hold-left-crash.state` (the user's state3 late on 2026-09-23,
1920×1080, saved as a room fades in): holding Left, the build before the
09:4880 hook exited with code 3 at about frame 87 (`Uncompiled
writable-memory execution 000:C374`); with it, 1800 frames of Left and four
2400-frame random-input runs play on.
`logs/states/bat-popin.state` (the user's state2 on 2026-09-24, the caves'
second room at 1920×1080 with room zoom Fill) was saved with the free list cut
at 72D3 by 11:78B6: 3 slots on it and 145 lost, so one of the room's two bats
waited (`spawn_waits` climbing) until the original bounds reached it in the
middle of the view. Loaded, the first movement pass mends the list
(`slot_repairs` 1, 149 free) and both bats (0A:41B4 at x 955 and 1012) are
there from the first frames, with no waits. The debug grid's scene 24 is the
caves' first room: floated (Select+A) to its right end with the view off, the
original loses its free list at camera x 585.
`logs/states/cave-room2.state` (made from scene 25, 1920×1080, Fill) is the
caves' second room with a whole free list: loaded, the first frames no longer
wake 0A:42F8 from the first room (it did, and its release then broke the
list). `logs/states/warp-cut.state` (scene 0A, map 5E8F, floated) is about
90 frames of Right before a warp inside the level moves the camera from
4220,4920 to 4852,4664: before filling, seven of the new area's objects
(one of them drawn) appeared inside the picture over the next frames; now
`fills` goes up by one there and none do.
The user's earlier slots are gone: the
platform room that lagged at 426 wide (`--enemy` needs it) needs a new state.

Towns (`logs/states/debug-grid.state` is the user's state2 on 2026-09-26: the
debug scene grid with the cursor on row N, whose first five columns are the
towns). `tools/check_towns.py` loads each, walks its loop without the view to
find the doors (where each label shows), and plays in an expanded and an
original instance side by side the whole loop both ways, walking and running,
and for every door a walk there, Up, 240 frames inside, and a walk left (the
way out of the Firefly Shrine; a building whose way out is elsewhere keeps
Shantae inside). Until the town is left, every frame's picture, OAM, HRAM,
WRAM C000-CEFF (the stack below CFFF holds stale frames) and live object slots
must be identical; the town must never be presented or activated expanded,
nor keep more than 32 slots past its map load. Both must leave the town on the
same frame. A shop or house is a room the view presents, with the grown
table, so its frames take other time and draw other random numbers: from there
the two must go through the same rooms at the same cameras (a load may end a
frame apart) and end in the same room, camera and door label. Remove slowdown
is off in both, since the view lets a screen load hold for up to eight frames
instead of two, which only moves the frame a load ends on. At 1920×1080
(Fill, in a 1920×1080 Aspect Fit window) all 44 routes pass: the 10 loops are
identical throughout, and each of the 34 doors (7 in Scuttle Town, Water
Town, Oasis Town and Bandit Town, 6 in the Zombie Caravan) is identical up to
the frame both leave the town, for the same room. Walking left brings Shantae
back out of 27 of them, to the same spot with the door's label; the other 7
are the five gates, which lead out of town, and two buildings whose way out
is elsewhere. Values held for less than three frames are left out of the
rooms compared: a frame that ends in the middle of a load, or of the camera
routine in a lag frame, shows a camera half set (the gate out of Water Town
reads 4252 for one frame in the level while the picture stays at 4191).

```powershell
python tools/check_towns.py                      # all five, 1920x1080; --towns 1 for Water Town
```

`logs/states/water-town.state` (the user's state1 on 2026-09-26, in Water Town
at camera x 235, saved with the grown table and the map marked widened) is
the Firefly Shrine report: the shrine's door is at x 40-70 (its duplicate at
+640 past the wrap), 270 frames of Left from the load. Loaded, it gets the
original table back (`town_tables` 1); with Up the two enter the shrine on
frame 460, and walking left takes both back to camera 606 with the shrine's
label. Before, the label stayed empty and Up did nothing.

The replay tool uses isolated settings/saves and never modifies the input states.
It captures PNGs, memory snapshots and a timeline. `--width 426 --height 270`
checks another size (pass the same options to `check_expanded_replays.py`);
`--native` captures the original rendering. The checks require constant
expanded presentation, the requested PNG dimensions (with `--room-zoom`, the
zoomed picture's, within them; 160×144 for `--one-screen`), a surround camera equal to
the scroll the native frame started with (`scx`/`scy` in `shantae_view_info`:
no seam), and exact background-tile agreement in the native overlap.

The debug server's `window` command resizes the window (or, under
`--benchmark`, the windowed size the view resolves against) and sets the
scaling mode, so an adaptive view can be checked headlessly: write
`view_adaptive=1` to the isolated `shantae.ini`, send `window` with a size,
`step`, then compare `window`'s `view_width`/`view_height` and the screenshot.
Its `native_presented` says whether the last frame was the original view on
its own (the screenshot is then 160×144) and `game_x`/`game_y`/`game_width`/
`game_height` give its rect in the window, so the original view's scaling can
be checked the same way: at 1920×1080 with Pixel Perfect the one-screen room
of the user's state3 (`logs/states/waterfall-perf.state`) is 400,36 1120×1008
until about frame 77 of holding Right, when the room fades to black and the
next one presents expanded.

The standalone C check covers aspect presets at every height, adaptive sizes
(whole-pixel and exact-height fits, short and absent windows, limits), every
sector in activation rectangles across many camera positions at 13 widths and
8 heights up to 8192×8192 (with the guard covering the pass count), background
coordinates across tilemap wrapping, cache reset, out-of-bounds shake clipping,
frame bounds and status-bar placement at sizes from 160×144 to 3840×2160 and
8192×1024, the map gate (matching tilemap, tiles
covered by background objects, replaced tilemap, zeroed camera), activation
that widens only after a room is presented (kept under a dialogue box, reset by
a new room; never in a town, with or without its name panel, even after a
shop in the same map was presented, and not in another room of the town map
pinned otherwise), the committed camera on a lag
frame, a background offset (sprites at the camera, background identical to the
same scroll without an offset), one-screen rooms (native picture and
activation), small rooms centered with sprites clipped to the room, top-line
fade palette correction, filling an area the view has just revealed (the
first widened scan and a cut queue the area's other sectors, 00:1143 scans
them with the entry and bank 00:1122–1140 would use; moving at the guard's
pace does not; activation takes the size an easing-out zoom heads for),
retention keeping the spawn rectangle with the original's slack past it (a
door the rectangle just reaches and the crow it makes),
a totem's pedestal reading its own stones (a flipping one too; not another
totem's noted over them, nor a fireball in a released stone's slot; the note
when it has none), spawner budgets (the count less what the original would
have released, up to the allowance and in an A000 slot too, one between
states still counted, no more than the limit, a spawner the original would
not run reading the count, the swamp creatures' op 98 by its DE, and other
reads, banks and unwidened rooms left alone),
object slots (with 32, 93 and 157 slots: records
wait with no free slot and, outside the original bounds, while 12 or fewer
are free; retention releases off-screen objects the original bounds release,
farthest first, then on-screen ones below 4 free; a record of a listed
encounter keeps the original bounds and one of an object placed once but not
listed (Mimic) does not; lost door warps spawn again unless a slot past DFFF or at
A000 holds them; the free list runs across DFC0 and on to A000; and captures
read objects past DFFF and at A000 from the table only while SVBK selects
bank 3) and centered 256×240 fallback. It does not read user saves. The regular
build/whole-ROM/RAM-native checks still apply.

`object_slots_check` runs the game's own routines on the compiled dispatcher,
one instruction at a time: 01:4CB6 builds 32 slots without the view and 157
with it, E000 and A000 are the table's only in bank 3, slot jumps past DFFF,
at A000 (and slot 32's in bank 3's tail) dispatch, the movement pass calls
the slots past 31 from the top before slot 31 and restores SVBK and the bank,
the object scripts step from slot 92 to A000, draw layering and its consumer
draw each list's own entries and then those of the slots past 31 (never a $80
one), the inventory's copy brings them back after the inventory's own table,
a save and a load through cartridge RAM (04:4CF1, 04:4D55) leave the slots at
A000 alone, gate parts no gate links are freed, a free list cut as the caves'
state was (or looping, or reaching a live slot) is mended with its lost slots
on the end, 11:78B6 frees 0A:42F8's child at its own address, 01:7120 builds the collision
pool with the extra nodes after its 12 (bank 7 only), 01:7192 hands them out
once the 12 are taken, the electric platform's setup (09:643C) gets one and
the player's landing check (01:72BC) lands on it through its handler, 01:71EB
gives it back, freeing the nodes on either side of E000 and then E000 keeps
the lists whole, the pool check puts a live object's lost node back in use
and a nameless one on the free list (and waits while the inventory has the
table), and states saved before the table or the pool grew get the extra
slots and nodes at the end of their free lists (or as all of them). A town's
map load builds 32 slots and 12 nodes with the view on (a shop in the same
map the grown ones), and a town state saved with the grown table gets them
back, its free lists in their order, unless a slot past 31 is live or a node
past the 12 in use.

The replay paths above refer to the user's local regression states. They are
not distributed fixtures. These checks do not establish correctness in every
room, boss scene, transformation or menu.
