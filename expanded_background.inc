/* Shantae background rectangles (00:0F15 / 00:034E) and object activation.
 * Included after the backing-memory readers in expanded_view.c. */
typedef struct { int x, y; uint8_t tile, attr; } BackgroundTile;
#define MAX_BG_TILES 16384
typedef struct { BackgroundTile tiles[MAX_BG_TILES]; int count; } Background;
static Background bg_pending, bg_latched, bg_visible;
static unsigned spawn_phase;
static int spawn_bounds[4], spawn_sectors[4];

static int clamp(int value, int lo, int hi) {
    return value < lo ? lo : value > hi ? hi : value;
}

/* Activation widens only in a room (map directory C9F8-C9FA) the view has
 * presented expanded, so enemies never start early where nothing shows them.
 * Towns keep their building-name panel up and stay native: their camera also
 * wraps at 640 (04:6469, 04:64A3) past the room bounds, with spawn-record
 * duplicates at +640 near the seam, and the door test (04:60E0) compares only
 * the low byte of the distance, so wider retention there lets a door 256 pixels
 * away take over the label and the entrance. */
static int widened;
static uint8_t widened_room[3];

/* Rooms. A scene scrolls the camera through C9DB-C9E2 (03:7920, and the
 * bounds-only 03:7A3D / 03:7AFE; greatest = least + span - 1); with the
 * 160x144 screen that is its room. Some scenes set more than they show and
 * move the camera with an object instead: the intro keeps the whole opening
 * map (07:405C) until 07:4079 narrows it to the bridge, the game over spans
 * the whole map (04:6E04), and levels such as the water tower take the whole
 * 8192-pixel map. A map holds several pictures, most of them kept apart by
 * sectors that draw nothing (00:26FD's directory entry has page 0), so a room
 * keeps to the pictures the camera's screen is in: 8-connected groups of
 * drawn sectors less the bands of one metatile along their sides, or the
 * pictures listed below where two touch. Boxes are in world pixels, ends
 * exclusive; a room's box is the screen's range (the camera's least and
 * greatest positions plus 160 x 144). */
typedef struct { int x0, y0, x1, y1; } Box;
typedef struct {
    Box box;
    int masked;              /* sectors lists the pictures kept */
    uint32_t sectors[32];    /* bit x of row y: sector x,y is drawn */
} Room;

/* Pictures that touch another in their map. The intro's view of Shantae's
 * house is the column under the bridge, which the camera pans up before the
 * cut to the bridge; the game over's picture stands beside the two strips of
 * 17:72E7's scene in the map of one-screen scenes. */
static const struct { uint16_t map; Box box; } pictures[] = {
    {0x4f51, {0, 0, 3584, 512}},        /* the bridge */
    {0x4f51, {0, 512, 256, 1024}},      /* the intro's house */
    {0x7359, {4096, 0, 4256, 472}},     /* game over */
    {0x7359, {3616, 0, 3808, 8192}},
    {0x7359, {3872, 0, 4064, 8192}},
};

/* A map's pictures: sectors numbered by 8-connected group of drawn ones. Two
 * maps are kept, the view's snapshot and the live one differing for a frame
 * when a scene changes. */
#define MAX_PICTURES 255
typedef struct {
    unsigned key;                      /* C9F9 << 8 | C9FA, plus 1; 0 unused */
    uint8_t id[32][32];                /* 0 draws nothing */
    Box box[MAX_PICTURES + 1];         /* the sectors' box less bands of one metatile */
} MapPictures;
static MapPictures map_pictures[2];
static int map_pictures_next;

/* The metatile a drawn sector's cell at x,y uses (as 00:2CCF reads it). */
static unsigned map_cell(const GBContext *ctx, unsigned page, unsigned bank, int x, int y) {
    const unsigned entry = (page << 8) + (y >> 8) * 64 + (x >> 8) * 2;
    const unsigned cells = rom8(ctx, bank, entry) << 8, cell_bank = rom8(ctx, bank, entry + 1);
    const unsigned c = cells + ((y & 255) >> 4) * 32 + ((x & 255) >> 4) * 2;
    return rom8(ctx, cell_bank, c) | rom8(ctx, cell_bank, c + 1) << 8;
}
static int sector_drawn(const GBContext *ctx, unsigned page, unsigned bank, int x, int y) {
    unsigned entry = (page << 8) + y * 64 + x * 2;
    if (entry < 0x4000 || entry + 1 >= 0x8000) return 0;
    unsigned cells = rom8(ctx, bank, entry), cell_bank = rom8(ctx, bank, entry + 1);
    return cells >= 0x40 && cells < 0x80 && cell_bank * 0x4000u < ctx->rom_size;
}
/* Whether picture id's cells along n steps of dx,dy from x,y all use the
 * metatile *tile (the first one found sets it). */
static int uniform_line(const GBContext *ctx, const MapPictures *m, unsigned page, unsigned bank, int id,
                        int x, int y, int dx, int dy, int n, int *tile) {
    for (int i = 0; i < n; ++i, x += dx, y += dy) {
        if (m->id[y >> 8][x >> 8] != id) continue;
        const int t = (int)map_cell(ctx, page, bank, x, y);
        if (*tile < 0) *tile = t;
        else if (t != *tile) return 0;
    }
    return 1;
}
/* A picture's sides often run on in one metatile past what it shows: the
 * credits' shaft (1C:78DC) is 160 wide in a 256-wide column of filler, and
 * levels end in bands of blank metatile 0 up to a sector wide. Its box stops
 * where such a band begins. */
static void trim_picture(const GBContext *ctx, const MapPictures *m, unsigned page, unsigned bank, int id,
                         Box *b) {
    int tile = -1;
    while (b->x1 - b->x0 > 16 &&
           uniform_line(ctx, m, page, bank, id, b->x1 - 16, b->y0, 0, 16, (b->y1 - b->y0) / 16, &tile))
        b->x1 -= 16;
    tile = -1;
    while (b->x1 - b->x0 > 16 &&
           uniform_line(ctx, m, page, bank, id, b->x0, b->y0, 0, 16, (b->y1 - b->y0) / 16, &tile))
        b->x0 += 16;
    tile = -1;
    while (b->y1 - b->y0 > 16 &&
           uniform_line(ctx, m, page, bank, id, b->x0, b->y1 - 16, 16, 0, (b->x1 - b->x0) / 16, &tile))
        b->y1 -= 16;
    tile = -1;
    while (b->y1 - b->y0 > 16 &&
           uniform_line(ctx, m, page, bank, id, b->x0, b->y0, 16, 0, (b->x1 - b->x0) / 16, &tile))
        b->y0 += 16;
}
static const MapPictures *pictures_of(const GBContext *ctx, unsigned page, unsigned bank) {
    const unsigned key = (page << 8 | bank) + 1;
    for (int i = 0; i < 2; ++i)
        if (map_pictures[i].key == key) return &map_pictures[i];
    MapPictures *m = &map_pictures[map_pictures_next];
    map_pictures_next ^= 1;
    m->key = key;
    uint8_t drawn[32][32];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) drawn[y][x] = sector_drawn(ctx, page, bank, x, y);
    memset(m->id, 0, sizeof(m->id));
    int count = 0;
    static uint8_t stack[32 * 32][2];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            if (!drawn[y][x] || m->id[y][x]) continue;
            int id = MAX_PICTURES;   /* more than that share the last number */
            if (count < MAX_PICTURES) {
                id = ++count;
                m->box[id] = (Box){x * 256, y * 256, x * 256 + 256, y * 256 + 256};
            }
            int top = 0;
            m->id[y][x] = id;
            stack[top][0] = x; stack[top++][1] = y;
            while (top) {
                const int sx = stack[--top][0], sy = stack[top][1];
                Box *b = &m->box[id];
                if (sx * 256 < b->x0) b->x0 = sx * 256;
                if (sy * 256 < b->y0) b->y0 = sy * 256;
                if (sx * 256 + 256 > b->x1) b->x1 = sx * 256 + 256;
                if (sy * 256 + 256 > b->y1) b->y1 = sy * 256 + 256;
                for (int ny = sy - 1; ny <= sy + 1; ++ny)
                    for (int nx = sx - 1; nx <= sx + 1; ++nx) {
                        if (nx < 0 || ny < 0 || nx > 31 || ny > 31 || !drawn[ny][nx] || m->id[ny][nx]) continue;
                        m->id[ny][nx] = id;
                        stack[top][0] = nx; stack[top++][1] = ny;
                    }
            }
        }
    for (int id = 1; id <= count; ++id) trim_picture(ctx, m, page, bank, id, &m->box[id]);
    return m;
}

static int overlaps(const Box *a, const Box *b) {
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}
static void box_add(Box *a, const Box *b) {
    if (b->x0 < a->x0) a->x0 = b->x0;
    if (b->y0 < a->y0) a->y0 = b->y0;
    if (b->x1 > a->x1) a->x1 = b->x1;
    if (b->y1 > a->y1) a->y1 = b->y1;
}

/* The room around camera cx,cy, from `ram` (C000-CFFF: the view's snapshot or
 * live WRAM) with a status bar `hud` lines tall. A side of the camera's range
 * that reaches past the pictures, or into their bands of one metatile, stops
 * where the pictures do: 25:46A8 holds the camera still at 796 while the
 * second arena's boss rises, with bounds reaching 88 pixels into the blank
 * sector after the arena, which put the arena against one side of the picture
 * with a black bar on the other. The camera's own screen is always kept.
 * `sectors` also lists the sectors of the pictures kept. */
static Room room_at(const GBContext *ctx, const uint8_t *ram, int cx, int cy, int hud, int sectors) {
    Room room;
    room.box = (Box){(int)word(ram + 0x9db), (int)word(ram + 0x9df), (int)word(ram + 0x9dd) + 160,
                     (int)word(ram + 0x9e1) + 144};
    room.masked = 0;
    const unsigned map = ram[0x9f9] << 8 | ram[0x9fa];
    const Box screen = {cx, cy, cx + 160, cy + 144 - hud};
    Box pictured = screen;
    int found = 0;
    for (size_t i = 0; i < sizeof(pictures) / sizeof(pictures[0]); ++i)
        if (pictures[i].map == map && overlaps(&pictures[i].box, &screen)) {
            if (!found++) pictured = pictures[i].box;
            else box_add(&pictured, &pictures[i].box);
        }
    if (!found && ram[0x9f9] >= 0x40 && ram[0x9f9] < 0x80) {
        const MapPictures *m = pictures_of(ctx, ram[0x9f9], ram[0x9fa]);
        uint8_t in[MAX_PICTURES + 1] = {0};
        for (int y = clamp(screen.y0 >> 8, 0, 31); y <= clamp((screen.y1 - 1) >> 8, 0, 31); ++y)
            for (int x = clamp(screen.x0 >> 8, 0, 31); x <= clamp((screen.x1 - 1) >> 8, 0, 31); ++x) {
                const int id = m->id[y][x];
                if (!id || in[id]) continue;
                in[id] = 1;
                if (!found++) pictured = m->box[id];
                else box_add(&pictured, &m->box[id]);
            }
        if (found && sectors) {
            room.masked = 1;
            for (int y = 0; y < 32; ++y) {
                room.sectors[y] = 0;
                for (int x = 0; x < 32; ++x) room.sectors[y] |= (uint32_t)in[m->id[y][x]] << x;
            }
        }
    }
    if (!found) return room;
    /* The camera's greatest y shows a picture's bottom above the status bar. */
    pictured.y1 += hud;
    Box b = room.box;
    if (b.x0 < pictured.x0) b.x0 = pictured.x0;
    if (b.y0 < pictured.y0) b.y0 = pictured.y0;
    if (b.x1 > pictured.x1) b.x1 = pictured.x1;
    if (b.y1 > pictured.y1) b.y1 = pictured.y1;
    const Box camera_screen = {cx, cy, cx + 160, cy + 144};
    box_add(&b, &camera_screen);
    if (b.x0 < room.box.x0) b.x0 = room.box.x0;
    if (b.y0 < room.box.y0) b.y0 = room.box.y0;
    if (b.x1 > room.box.x1) b.x1 = room.box.x1;
    if (b.y1 > room.box.y1) b.y1 = room.box.y1;
    if (b.x0 < b.x1 && b.y0 < b.y1) room.box = b;
    return room;
}
/* Lines of a window along the bottom of the screen: the status bar from line
 * 128, the intro's strip from 136 (the dialogue box before it slides up). */
static int status_bar(int lcdc_value, int wx, int wy) {
    return (lcdc_value & 0x20) && wx == 0 && wy >= 128 && wy < 144 ? 144 - wy : 0;
}
/* The room the live camera is in, for activation. */
static Room live_room(const GBContext *ctx) {
    const GBPPU *ppu = (const GBPPU *)ctx->ppu;
    const int hud = ppu ? status_bar(ppu->lcdc, (int)ppu->wx - 7, ppu->wy) : 0;
    return room_at(ctx, ctx->wram, word(ctx->hram + 0x61), word(ctx->hram + 0x63), hud, 0);
}

/* A room pinned to one screen (the first boss's arena is 944,368 to 944,368,
 * a one-screen scene in a larger map is its picture) leaves nothing outside
 * the native picture to show. Such rooms keep the original picture and
 * activation. */
static int room_fits_screen(const Room *room) {
    return room->box.x1 - room->box.x0 <= 160 && room->box.y1 - room->box.y0 <= 144;
}
static int activation_widened(const GBContext *ctx) {
    if (!widened || memcmp(ctx->wram + 0x9f8, widened_room, sizeof(widened_room))) return 0;
    const Room room = live_room(ctx);
    return !room_fits_screen(&room);
}
/* Called at each snapshot with whether this frame is presented expanded. */
static void note_presented(int presented) {
    if (memcmp(wram + 0x9f8, widened_room, sizeof(widened_room))) {
        memcpy(widened_room, wram + 0x9f8, sizeof(widened_room));
        widened = 0;
    }
    widened |= presented;
}

/* Share one origin for presentation and activation, at the presented size
 * (gb_custom_width/height: it follows the window when adaptive, and a size
 * changed in the Esc menu applies from the next frame). Shake can cross room
 * limits for a frame without invalidating the scene. A room narrower or
 * shorter than the view (Risky's ship in the opening is 192 wide) is centered. */
static void view_origin(const Box *room, int cx, int cy, int width, int height, int *x, int *y) {
    int minx = room->x0, miny = room->y0;
    int maxx = room->x1 - width;
    int maxy = room->y1 - height;
    *x = maxx < minx ? minx + (maxx - minx) / 2 : clamp(cx - (width - 160) / 2, minx, maxx);
    *y = maxy < miny ? miny + (maxy - miny) / 2 : clamp(cy - (height - 144) / 2, miny, maxy);
}

/* The original's margins around its camera's screen: 01:506A spawns records
 * within 8 pixels of it and 01:502F keeps objects within 80 across and 72
 * down. Its camera stays in the room, so it never wakes anything farther
 * outside the room than that, and neither does the view, which draws nothing
 * outside the room (sprites are clipped to it). A view wider than its room
 * (centered) or a picture not yet zoomed to a new room (the first frames after
 * a state load activate at the size presented before it) would otherwise run
 * the objects of the map's other rooms: the caves are one map of twelve rooms,
 * and 0A:42F8, in the first, woke from the second and was released as the
 * view narrowed, which reaches a bug in its release (object_slots.c). */
#define SPAWN_MARGIN 8
#define KEEP_ACROSS 80
#define KEEP_DOWN 72
static int at_least(int value, int lo) { return value < lo ? lo : value; }
static int at_most(int value, int hi) { return value > hi ? hi : value; }

/* Filling. The cycle below keeps up with a camera moving 8 pixels a frame
 * across and 4 down; an area that appears faster (a room's first widened
 * frames, a cut such as a warp inside a level, the picture growing as the room
 * zoom eases out or the window is resized) took a whole cycle to wake, up to
 * 15 frames at 1920x1080, and its objects appeared inside the picture. Its
 * sectors are scanned in the same call instead (fill_dispatch). */
#define FILL_ACROSS 8
#define FILL_DOWN 4
static uint8_t fill_sectors[32 * 32][2];
static int fill_count, fill_next, fill_valid, fill_last[4];
static unsigned fills;
/* While the room zoom eases out, the size of the picture it eases to (fit,
 * expanded_view.c; 0 otherwise): activation takes it at once, so what the
 * growing picture shows is awake first, and the area is filled once. */
static int ease_width, ease_height;
static void activation_size(int *width, int *height) {
    *width = gb_custom_width > ease_width ? gb_custom_width : ease_width;
    *height = gb_custom_height > ease_height ? gb_custom_height : ease_height;
}

/* The guest scans one 2x2 batch of 256-pixel sectors per call, so a larger
 * rectangle takes several calls. The guard around the view must cover a camera
 * moving 8 pixels a frame across and 4 down for a whole cycle of the batches
 * the guarded rectangle spans: 32/16 up to four batches, wider beyond. A wider
 * guard spans more batches, so grow both together. Spawn bounds stay on the
 * 8192-pixel map, which caps the cycle at 17 x 17 batches. */
static int sector_batches(int span) {
    if (span > 8192) span = 8192;
    return ((span + 255) / 256 + 2) / 2;
}
static void guard_for(int passes, int *across, int *down) {
    *across = passes > 4 ? 8 * passes : 32;
    *down = passes > 4 ? 4 * passes : 16;
}
static int activation_passes(int width, int height) {
    int passes = 1, across, down;
    for (;; ++passes) {
        guard_for(passes, &across, &down);
        if (sector_batches(width + 2 * across) * sector_batches(height + 2 * down) <= passes)
            return passes;
    }
}
static void activation_guard(int width, int height, int *across, int *down) {
    guard_for(activation_passes(width, height), across, down);
}


/* Object slots (object_slots.c). 01:4DB0 takes the head of the free list at
 * FFB3, linked through slot+$7C, and finds none when it is empty. The spawner
 * (00:0FC6) sets a record's flag at 00:1061 before it asks for a slot, and a
 * record whose slot never came stays flagged with no object until the map is
 * reloaded; objects that spawn others (shots, effects, the drop that becomes
 * the water tower's mini-boss) simply get none. With the view on the table
 * has 157 slots, as many as fit; the densest room seen at 1920x1080 wants
 * about 130, and larger views can want more than any table holds. The game's
 * own objects come first:
 * - no record spawns while no slot is free (its flag, read at 00:1054, reads
 *   as set), so it is tried again on a later pass instead of being lost;
 * - a record outside the original's spawn bounds (FFC5-FFCC) also waits while
 *   SLOT_RESERVE or fewer slots are free, which keeps them for what the
 *   original would spawn and for the objects others spawn;
 * - while fewer than SLOT_RESERVE are free, retention hands the original
 *   bounds (FFB5-FFBC) to objects off screen that those bounds release,
 *   farthest first and only as many as it takes; below SLOT_FLOOR, to such
 *   objects on screen too. The game then releases them its usual way; 00:12B2
 *   clears their flags, so they come back once slots are free. Only objects
 *   that ran retention this frame or the frame before count;
 * - a door warp flagged with no object (a state saved with such a dead end)
 *   spawns again. Doors are never defeated or collected.
 * spawn_waits, early_releases and doors_restored count them for
 * shantae_view_info. */
#define SLOT_RESERVE 12
#define SLOT_FLOOR 4
/* Slots that ran retention (00:11C5) this frame and the frame before, and the
 * frame they were counted in; each slot's early-release answer this frame
 * (0 unknown, 1 no, 2 yes), and its frame. */
static uint8_t retention_seen[2][SHANTAE_MAX_SLOTS];
static uint64_t retention_frame;
static uint8_t early_answer[SHANTAE_MAX_SLOTS];
static uint64_t early_frame;
static unsigned spawn_waits, early_releases, doors_restored;

static unsigned slot_word(GBContext *ctx, unsigned addr) {
    const uint8_t *lo = shantae_slot_memory(ctx, addr), *hi = shantae_slot_memory(ctx, addr + 1);
    return lo && hi ? *lo | *hi << 8 : 0;
}
static unsigned slot_byte(GBContext *ctx, unsigned addr) {
    const uint8_t *p = shantae_slot_memory(ctx, addr);
    return p ? *p : 0;
}
/* Marks the free list's slots in `marks` and returns its length. */
static int free_slots(GBContext *ctx, uint8_t marks[SHANTAE_MAX_SLOTS]) {
    int count = 0;
    memset(marks, 0, SHANTAE_MAX_SLOTS);
    for (int slot = shantae_slot_index(ctx, word(ctx->hram + 0x33)); slot >= 0 && !marks[slot];
         slot = shantae_slot_index(ctx, slot_word(ctx, shantae_slot_addr(slot) + 0x7c))) {
        marks[slot] = 1;
        ++count;
    }
    return count;
}
/* A door warp record: object script 0A:4048 at record+10, which DE points to. */
static int door_record(const GBContext *ctx) {
    return rom8(ctx, ctx->rom_bank, ctx->de) == 0x48 && rom8(ctx, ctx->rom_bank, ctx->de + 1) == 0x40 &&
           rom8(ctx, ctx->rom_bank, ctx->de + 2) == 0x0a;
}
/* Whether an object holds the record whose flag is at HL (its +$27). */
static int record_held(GBContext *ctx, const uint8_t *free_marks) {
    for (int slot = 0; slot < shantae_slot_count(ctx); ++slot)
        if (!free_marks[slot] && slot_word(ctx, shantae_slot_addr(slot) + 0x27) == ctx->hl) return 1;
    return 0;
}
/* The guest's 16-bit comparisons test bit 7 of the difference's high byte. */
static int negative(unsigned difference) { return (difference & 0x8000) != 0; }

/* At 00:1054 the record's box is in FF9B-FFA2; 00:0FE7 tests it against the
 * spawn bounds FFC5-FFCC on the axes C39C selects. HRAM keeps the original's
 * bounds: only the spawner's reads of them are widened. */
static int record_in_native_bounds(const GBContext *ctx) {
    const uint8_t *h = ctx->hram;
    unsigned axes = ctx->wram[0x39c];
    if ((axes & 1) && (negative(word(h + 0x1f) - word(h + 0x45)) || !negative(word(h + 0x1b) - word(h + 0x49))))
        return 0;
    if ((axes & 2) && (negative(word(h + 0x21) - word(h + 0x47)) || !negative(word(h + 0x1d) - word(h + 0x4b))))
        return 0;
    return 1;
}
/* Encounters. Most objects do the same thing wherever they are, so starting
 * them early does no harm: touch triggers wait for the player's box
 * (00:38BE), collectibles for her, story set pieces play at the view's edge as
 * they would at the screen's, and enemies are meant to start early. An
 * encounter starts a fight that relies on the original's short distances. The
 * water tower's mini-boss (object 0A:4070, script 09:720C) is a drop that
 * falls from the ceiling the moment it exists and, once it lands, heads for
 * Shantae's x wherever she is (09:7442, x alone through 00:2BD4); spawned
 * from the corridor above, it fell to nobody, wandered off under her, and
 * left its arena empty. An encounter's record keeps
 * the original bounds, for spawning and for retention; one started early (a
 * state saved so) is released the usual way and comes back when those bounds
 * reach it.
 *
 * The list is by object (bank and address at record+10), read from the
 * scripts of the objects placed at most twice in a map the view widens. The
 * others are NPCs and their houses, collectibles, story set pieces, props and
 * triggers (Mimic and his door in Scuttle Town, 0A:42D4 and 0A:42D0, popped
 * in and out in the middle of the view when every object placed once in its
 * map was held to the original bounds). The fight scripts that lock the
 * camera (around 1A:54FD, 25:4692, 27:6A68, 07:5587) are started by native
 * code; no record or other script names them. The first boss's arena is a
 * one-screen room, whose activation stays native. */
static const uint32_t encounter_types[] = {
    0x0a4070,   /* the water tower's mini-boss drop */
};
static unsigned encounter_waits;

static uint32_t record_type(const GBContext *ctx, unsigned bank, unsigned record) {
    return rom8(ctx, bank, record + 12) << 16 | rom8(ctx, bank, record + 11) << 8 | rom8(ctx, bank, record + 10);
}
static int encounter_record(const GBContext *ctx, unsigned bank, unsigned record) {
    const uint32_t type = record_type(ctx, bank, record);
    for (size_t i = 0; i < sizeof(encounter_types) / sizeof(encounter_types[0]); ++i)
        if (encounter_types[i] == type) return 1;
    return 0;
}
/* Whether the object in `slot` came from such a record (bank +$24, record +$25). */
static int encounter(GBContext *ctx, int slot) {
    const unsigned o = shantae_slot_addr(slot), bank = slot_byte(ctx, o + 0x24);
    return bank && encounter_record(ctx, bank, slot_word(ctx, o + 0x25));
}

/* The flag 00:1054 reads for a record the widened bounds reached. */
static uint8_t spawn_flag(GBContext *ctx, uint8_t flag) {
    if (flag > 1 || (flag == 1 && !door_record(ctx))) return flag;
    if (!flag && !record_in_native_bounds(ctx) && encounter_record(ctx, ctx->rom_bank, ctx->de - 10)) {
        encounter_waits++;
        return 1;
    }
    uint8_t free_marks[SHANTAE_MAX_SLOTS];
    int free = free_slots(ctx, free_marks);
    if (flag == 1 && record_held(ctx, free_marks)) return flag;
    if (!free || (free <= SLOT_RESERVE && !record_in_native_bounds(ctx))) {
        spawn_waits++;
        return 1;
    }
    doors_restored += flag;
    return 0;
}

/* Whether the original retention bounds (FFB5-FFBC) release the object in
 * `slot`: 00:11C5 keeps it while its box is inside them on the axes C39C
 * selects, 00:1229 while its spawn record's box (bank +$24, record +$25) is. */
static int released_natively(GBContext *ctx, int slot) {
    const unsigned o = shantae_slot_addr(slot);
    const uint8_t *h = ctx->hram;
    unsigned min_x = word(h + 0x35), max_x = word(h + 0x37), min_y = word(h + 0x39), max_y = word(h + 0x3b);
    unsigned axes = ctx->wram[0x39c], x = slot_word(ctx, o + 0x34), y = slot_word(ctx, o + 0x37);
    if ((!(axes & 1) || (!negative(max_x - x) && negative(min_x - (x + slot_byte(ctx, o + 0x57))))) &&
        (!(axes & 2) || (!negative(max_y - y) && negative(min_y - (y + slot_byte(ctx, o + 0x58))))))
        return 0;
    unsigned bank = slot_byte(ctx, o + 0x24), record = slot_word(ctx, o + 0x25);
    return !bank || negative(rom16(ctx, bank, record) - min_x) || !negative(rom16(ctx, bank, record + 2) - max_x) ||
           negative(rom16(ctx, bank, record + 4) - min_y) || !negative(rom16(ctx, bank, record + 6) - max_y);
}
/* Whether the object's box meets the picture the view presents (the room's
 * part of it). */
static int on_screen(GBContext *ctx, int slot) {
    const unsigned o = shantae_slot_addr(slot);
    int x, y, width, height;
    activation_size(&width, &height);
    const Room room = live_room(ctx);
    view_origin(&room.box, word(ctx->hram + 0x61), word(ctx->hram + 0x63), width, height, &x, &y);
    const int left = at_least(x, room.box.x0), right = at_most(x + width, room.box.x1);
    const int top = at_least(y, room.box.y0), bottom = at_most(y + height, room.box.y1) - 16;
    int ox = (int16_t)slot_word(ctx, o + 0x34), oy = (int16_t)slot_word(ctx, o + 0x37);
    return ox + (int)slot_byte(ctx, o + 0x57) >= left && ox < right &&
           oy + (int)slot_byte(ctx, o + 0x58) >= top && oy < bottom;
}
static int camera_distance(GBContext *ctx, int slot) {
    const unsigned o = shantae_slot_addr(slot);
    int dx = abs((int16_t)(slot_word(ctx, o + 0x34) - word(ctx->hram + 0x61) - 80));
    int dy = abs((int16_t)(slot_word(ctx, o + 0x37) - word(ctx->hram + 0x63) - 72));
    return dx > dy ? dx : dy;
}
/* With fewer than SLOT_RESERVE slots free, the original bounds go to as many
 * of the farthest objects off screen that they release as it takes to free
 * that many; with fewer than SLOT_FLOOR, to as many on screen as that still
 * leaves short. Asked once per object and frame. */
static int release_early(GBContext *ctx, int slot) {
    if (early_frame != ctx->completed_frames) {
        early_frame = ctx->completed_frames;
        memset(early_answer, 0, sizeof(early_answer));
    }
    if (early_answer[slot]) return early_answer[slot] == 2;
    early_answer[slot] = 1;
    uint8_t free_marks[SHANTAE_MAX_SLOTS];
    int free = free_slots(ctx, free_marks);
    if (free >= SLOT_RESERVE || !released_natively(ctx, slot)) return 0;
    int shown = on_screen(ctx, slot);
    if (shown && free >= SLOT_FLOOR) return 0;
    int distance = camera_distance(ctx, slot), farther = 0, hidden = 0;
    for (int other = 0; other < shantae_slot_count(ctx); ++other) {
        if (other == slot || free_marks[other] || !(retention_seen[0][other] | retention_seen[1][other]) ||
            !released_natively(ctx, other))
            continue;
        int other_shown = on_screen(ctx, other);
        hidden += !other_shown;
        if (other_shown != shown) continue;
        int d = camera_distance(ctx, other);
        farther += d > distance || (d == distance && other > slot);
    }
    int release = shown ? farther < SLOT_FLOOR - free - hidden : farther < SLOT_RESERVE - free;
    early_answer[slot] = release ? 2 : 1;
    return release;
}

/* Tinkerbats. Three scripts start one (1D:5A08 out of a barrel, 1D:5A6F and
 * 1D:67E4) and each frees itself at once unless fewer than two are about: it
 * compares CC2E with 2 (VM op 98, 00:18FA, reading it at 00:1904), raises it,
 * and its callback 1D:5A96 lowers it again when retention releases it
 * (1D:59F9) or it dies (1D:61AE). On the original's screen that is two at a
 * time; the view keeps the ones left behind alive, so the barrels further on
 * broke empty. The compare reads instead how many live ones (callback
 * 1D:5A96) the original's retention would keep around the new one: within 160
 * across and 144 down (01:502F's bounds, as if its screen were centered on
 * it). A spawner that makes one every 240 frames (1D:4AB6) still stops at two
 * around it. */
#define TINKERBAT_COUNT 0xcc2e
#define TINKERBAT_COMPARE_PC 0x1905   /* 00:1904 LD A,(HL), generated PC */
static uint8_t tinkerbats_near(GBContext *ctx, uint8_t value) {
    const int self = shantae_slot_index(ctx, memory16(ctx, ctx->sp));   /* 00:1900 pushed BC */
    if (self < 0) return value;
    const unsigned s = shantae_slot_addr(self);
    const int cx = (int16_t)slot_word(ctx, s + 0x34) + (int)slot_byte(ctx, s + 0x57) / 2;
    const int cy = (int16_t)slot_word(ctx, s + 0x37) + (int)slot_byte(ctx, s + 0x58) / 2;
    unsigned count = 0;
    for (int i = 0; i < shantae_slot_count(ctx); ++i) {
        const unsigned o = shantae_slot_addr(i);
        if (slot_byte(ctx, o) == 0xFF || slot_byte(ctx, o + 0x19) != 0x1d || slot_word(ctx, o + 0x1b) != 0x5a96)
            continue;
        const int x = (int16_t)slot_word(ctx, o + 0x34), y = (int16_t)slot_word(ctx, o + 0x37);
        count += x + (int)slot_byte(ctx, o + 0x57) > cx - 160 && x < cx + 160 &&
                 y + (int)slot_byte(ctx, o + 0x58) > cy - 144 && y < cy + 144;
    }
    return (uint8_t)(count < 0xFF ? count : 0xFF);
}

static uint8_t read_override(GBContext *ctx, uint16_t addr, uint8_t value) {
    value = shantae_slots_read_override(ctx, addr, value);
    /* 00:1054 LD A,(HL): the record's flag (generated PC 1055). */
    if (ctx->pc == 0x1055 && addr == ctx->hl)
        return activation_widened(ctx) ? spawn_flag(ctx, value) : value;
    if (addr == TINKERBAT_COUNT && ctx->pc == TINKERBAT_COMPARE_PC)
        return activation_widened(ctx) ? tinkerbats_near(ctx, value) : value;
    if (addr < 0xffb5 || addr > 0xffcc || !activation_widened(ctx)) return value;
    if (addr >= 0xffc5 && addr <= 0xffcc && ctx->pc >= 0x0fc6 && ctx->pc <= 0x1058) {
        unsigned i = (addr - 0xffc5) / 2;
        return (uint8_t)(spawn_bounds[i] >> ((addr & 1) ? 0 : 8));
    }
    if (ctx->pc >= 0x1122 && ctx->pc <= 0x11be && addr >= 0xffc6 && addr <= 0xffcc && !(addr & 1))
        return (uint8_t)spawn_sectors[(addr - 0xffc6) / 2];
    if (ctx->pc >= 0x11c5 && ctx->pc <= 0x1289 && addr >= 0xffb5 && addr <= 0xffbc) {
        /* 00:1229 has pushed BC (the object), DE and AF. */
        int slot = shantae_slot_index(ctx, ctx->pc <= 0x1228 ? ctx->bc : memory16(ctx, ctx->sp + 4));
        if (slot >= 0 && encounter(ctx, slot)) return value;
        if (slot >= 0 && release_early(ctx, slot)) {
            early_releases += ctx->pc == 0x1248;   /* 00:1229's first bounds read */
            return value;
        }
        int x, y, width, height;
        activation_size(&width, &height);
        const Room room = live_room(ctx);
        view_origin(&room.box, word(ctx->hram + 0x61), word(ctx->hram + 0x63), width, height, &x, &y);
        const int bounds[4] = {
            at_least(x, room.box.x0) - KEEP_ACROSS, at_most(x + width, room.box.x1) + KEEP_ACROSS,
            at_least(y, room.box.y0) - KEEP_DOWN, at_most(y + height, room.box.y1) + KEEP_DOWN,
        };
        return (uint8_t)((unsigned)bounds[(addr - 0xffb5) / 2] >> ((addr & 1) ? 0 : 8));
    }
    return value;
}

static void world_read_tap(GBContext *ctx, uint16_t addr) {
    shantae_slots_read_tap(ctx, addr);
    if (ctx->pc >= 0x11c5 && ctx->pc <= 0x1228 && addr >= 0xffb5 && addr <= 0xffbc) {
        int slot = shantae_slot_index(ctx, ctx->bc);
        if (ctx->completed_frames != retention_frame) {
            retention_frame = ctx->completed_frames;
            memcpy(retention_seen[1], retention_seen[0], sizeof(retention_seen[0]));
            memset(retention_seen[0], 0, sizeof(retention_seen[0]));
        }
        if (slot >= 0) retention_seen[0][slot] = 1;
    }
    if (ctx->rom_bank == 1 && ctx->pc == 0x4f19 && addr == 0xffbd) {
        /* 01:4F12 hasn't culled the object yet. Its packed picture starts with
         * width/height followed by interleaved tile/attribute pairs. Capture
         * before clipping but keep the native 32x32 tilemap writes unchanged. */
        unsigned obj = ctx->bc;
        unsigned bank = memory8(ctx, obj + 0x2e), ptr = memory16(ctx, obj + 0x2f);
        unsigned cols = rom8(ctx, bank, ptr), rows = rom8(ctx, bank, ptr + 1);
        int x = (int16_t)memory16(ctx, obj + 0x5b), y = (int16_t)memory16(ctx, obj + 0x5f);
        if (cols && rows && cols <= 128 && rows <= 64 && ptr >= 0x4000 && ptr + 2 + cols * rows * 2 <= 0x8000) {
            for (unsigned row = 0; row < rows; ++row) for (unsigned col = 0; col < cols; ++col) {
                if (bg_pending.count == MAX_BG_TILES) break;
                unsigned data = ptr + 2 + (row * cols + col) * 2;
                BackgroundTile *t = &bg_pending.tiles[bg_pending.count++];
                t->x = x + col; t->y = y + row;
                t->tile = rom8(ctx, bank, data); t->attr = rom8(ctx, bank, data + 1);
            }
        }
    }
    if (ctx->pc == 0x111b && addr == 0xc9f8) {
        int x, y, across, down, width, height;
        activation_size(&width, &height);
        const Room room = live_room(ctx);
        view_origin(&room.box, word(ctx->hram + 0x61), word(ctx->hram + 0x63), width, height, &x, &y);
        activation_guard(width, height, &across, &down);
        spawn_bounds[0] = clamp(at_least(x - across, room.box.x0 - SPAWN_MARGIN), 0, 8191);
        spawn_bounds[1] = clamp(at_least(y - down, room.box.y0 - SPAWN_MARGIN), 0, 8191);
        spawn_bounds[2] = clamp(at_most(x + width + across, room.box.x1 + SPAWN_MARGIN), 0, 8191);
        spawn_bounds[3] = clamp(at_most(y + height + down, room.box.y1 + SPAWN_MARGIN), 0, 8191);
        /* The guest visits a 2x2 sector batch. Cycle batches when the larger
         * rectangle crosses more sectors. The guard covers the short latency. */
        int nx = ((spawn_bounds[2] >> 8) - (spawn_bounds[0] >> 8) + 2) / 2;
        int ny = ((spawn_bounds[3] >> 8) - (spawn_bounds[1] >> 8) + 2) / 2;
        unsigned phase = spawn_phase++ % (nx * ny);
        spawn_sectors[0] = (spawn_bounds[0] >> 8) + 2 * (phase % nx);
        spawn_sectors[1] = (spawn_bounds[1] >> 8) + 2 * (phase / nx);
        spawn_sectors[2] = clamp(spawn_sectors[0] + 1, 0, spawn_bounds[2] >> 8);
        spawn_sectors[3] = clamp(spawn_sectors[1] + 1, 0, spawn_bounds[3] >> 8);
        /* An area the cycle has not been keeping up with (the first widened
         * scan in a room, or a side moving out faster than the guard allows)
         * is filled at once from 00:1143 (fill_dispatch). */
        const int widened_now = activation_widened(ctx);
        fill_count = fill_next = 0;
        if (widened_now && (!fill_valid || spawn_bounds[0] < fill_last[0] - FILL_ACROSS ||
                            spawn_bounds[2] > fill_last[2] + FILL_ACROSS ||
                            spawn_bounds[1] < fill_last[1] - FILL_DOWN || spawn_bounds[3] > fill_last[3] + FILL_DOWN)) {
            for (int sy = spawn_bounds[1] >> 8; sy <= spawn_bounds[3] >> 8; ++sy)
                for (int sx = spawn_bounds[0] >> 8; sx <= spawn_bounds[2] >> 8; ++sx) {
                    if (sx >= spawn_sectors[0] && sx <= spawn_sectors[2] && sy >= spawn_sectors[1] &&
                        sy <= spawn_sectors[3])
                        continue;   /* this call's own batch */
                    fill_sectors[fill_count][0] = (uint8_t)sx;
                    fill_sectors[fill_count++][1] = (uint8_t)sy;
                }
            fills++;
        }
        memcpy(fill_last, spawn_bounds, sizeof(fill_last));
        fill_valid = widened_now;
    }
}

/* 00:1143, where 00:1114 comes back from the first sector of its batch (the
 * CALL $0FA9 at 00:1140 that every call makes): the sectors queued to fill are
 * scanned there the same way, each coming back to 00:1143, before the batch's
 * other three. 00:0FA9 takes HL = the sector's entry in the map's record
 * directory (page C39A plus y/2, (y & 1) * 128 + x * 4) with bank C39B
 * selected, as 00:1122-1140 set them up. */
static int fill_dispatch(GBContext *ctx, uint16_t addr) {
    if (addr != 0x1143 || fill_next >= fill_count) return 0;
    const unsigned x = fill_sectors[fill_next][0], y = fill_sectors[fill_next][1];
    ++fill_next;
    const uint8_t bank = ctx->wram[0x39b];
    gb_push16(ctx, 0x1143);
    ctx->hram[0x11] = bank;
    gb_write8(ctx, 0x2000, bank);
    ctx->hl = (uint16_t)(((ctx->wram[0x39a] + (y >> 1)) & 0xFF) << 8 | (((y & 1) << 7) + x * 4));
    ctx->pc = 0x0FA9;
    gb_tick(ctx, 184);   /* 00:1122-1140 for one sector, the CALL included */
    return 1;
}
