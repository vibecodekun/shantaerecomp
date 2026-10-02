/* Shantae USA: render the world outside the 160x144 hardware viewport.
 * World-map bindings are derived from 00:26FD and 00:2CCF; sprite bindings
 * from 00:1DAD..1ECD. Presentation reads backing storage. Object activation
 * and retention use virtual bounds at their specific native consumers.
 */
#include "expanded_view.h"
#include "object_slots.h"
#include "gb_custom_view.h"
#include "gbrt.h"
#include "ppu.h"
#include "debug_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MAX_SPRITES 256
typedef struct {
    int x, y;
    unsigned bank, descriptor, graphics;
} Draw;
typedef struct { Draw draws[MAX_SPRITES]; int count; } DrawList;
static DrawList pending[2], latched, visible;
static uint8_t wram[0x1000], hram[0x7f], vram[0x4000], bg_palette[64], obj_palette[64];
static uint8_t line0_bg_palette[64], line0_obj_palette[64];
/* 01:5E0B, called from the LYC=0 STAT handler, uploads 16 palettes one
 * HBlank-gated block at a time, so it finishes by about line 16. */
#define PALETTE_UPLOAD_LINES 24
static int ready, lcdc, window_y, window_x;
/* The displayed frame's background scroll and the camera its sprites were
 * placed with. They differ by the background-only offset (see read_tap). */
static int scroll_x, scroll_y, camera_x, camera_y;
/* Scroll the VBlank handler last copied to SCX/SCY, and the line it did so. */
static int committed, committed_x, committed_y, commit_ly;
/* Offset in the latest scroll the camera routine built, and in the committed one. */
static int built_dx, built_dy, committed_dx, committed_dy;
static int line0_scx, line0_scy;   /* scroll the native frame started with */
static GBContext *s_ctx;           /* for the debug commands */
/* Room zoom (fit below): window pixels per game pixel now (0 = the view as
 * it is), easing from zoom_from to zoom_to since guest frame zoom_frame; the
 * map it was worked out for, and whether the next one snaps. Presentation
 * only: nothing the game reads, so not part of the rollback state. */
static double zoom, zoom_from, zoom_to, zoom_target;
static uint64_t zoom_frame;
static uint8_t zoom_room[3];
static int zoom_snap = 1, cut_x, cut_y;
static int room_snap = 1;   /* the room shown (shown_room) snaps next */
/* The camera jumping (a cut in a scene) snaps the zoom and the room like a
 * dark frame. */
static void note_camera(int x, int y) {
    if (abs(x - cut_x) > 64 || abs(y - cut_y) > 64) zoom_snap = room_snap = 1;
    cut_x = x;
    cut_y = y;
}
static unsigned oam_checked, oam_matched;
static unsigned background_checked, background_matched;
static unsigned map_checked, map_matched;

static unsigned word(const uint8_t *p) { return p[0] | (p[1] << 8); }
static unsigned rom8(const GBContext *ctx, unsigned bank, unsigned addr) {
    if (addr >= 0x8000) return 0xff;
    size_t offset = addr < 0x4000 ? addr : bank * 0x4000u + addr - 0x4000u;
    return offset < ctx->rom_size ? ctx->rom[offset] : 0xff;
}
static unsigned rom16(const GBContext *ctx, unsigned bank, unsigned addr) {
    return rom8(ctx, bank, addr) | (rom8(ctx, bank, addr + 1) << 8);
}
static unsigned memory8(const GBContext *ctx, unsigned addr) {
    if (addr >= 0xa000 && addr < 0xc000) {
        /* The object table's slots at A000 (object_slots.c). */
        if (ctx->wram_ext && ctx->wram_bank == ctx->wram_ext_bank && addr - 0xa000 < ctx->wram_ext_cart_mapped)
            return ctx->wram_ext[ctx->wram_ext_cart_offset + addr - 0xa000];
        return 0xff;
    }
    if (addr >= 0xc000 && addr < 0xd000) return ctx->wram[addr - 0xc000];
    if (addr >= 0xd000 && addr < 0xe000) return ctx->wram[ctx->wram_bank * 0x1000 + addr - 0xd000];
    if (addr >= 0xe000 && addr < 0xfe00) {
        /* The object table past DFFF (object_slots.c), else the echo. */
        if (ctx->wram_ext && ctx->wram_bank == ctx->wram_ext_bank && addr - 0xe000 < ctx->wram_ext_mapped)
            return ctx->wram_ext[addr - 0xe000];
        return memory8(ctx, addr - 0x2000);
    }
    if (addr >= 0xff80 && addr < 0xffff) return ctx->hram[addr - 0xff80];
    return rom8(ctx, ctx->rom_bank, addr);
}
static unsigned memory16(const GBContext *ctx, unsigned addr) {
    return memory8(ctx, addr) | (memory8(ctx, addr + 1) << 8);
}

#include "expanded_background.inc"

/* The room the picture shows (render), easing like the zoom from room_from
 * since guest frame room_frame when the room jumps in play; the room and map
 * it was last worked out for. Presentation only, like the zoom. */
static Box room_shown, room_from, room_target;
static uint64_t room_frame;
static uint8_t room_map[3];

static const ShantaeViewAspect aspects[] = {
    {"10:9", "Game Boy 10:9", 10, 9},
    {"16:15", "NES 16:15", 16, 15},
    {"4:3", "4:3", 4, 3},
    {"16:10", "16:10", 16, 10},
    {"16:9", "16:9", 16, 9},
    {"21:9", "21:9", 21, 9},
    {"32:9", "32:9", 32, 9},
};

int shantae_view_aspect_count(void) { return (int)(sizeof(aspects) / sizeof(aspects[0])); }
const ShantaeViewAspect *shantae_view_aspect_info(int index) {
    return index >= 0 && index < shantae_view_aspect_count() ? &aspects[index] : NULL;
}
int shantae_view_aspect_find(const char *id) {
    for (int i = 0; id && i < shantae_view_aspect_count(); ++i)
        if (!strcmp(id, aspects[i].id)) return i;
    return -1;
}
void shantae_view_aspect_size(int index, int *width, int *height) {
    const ShantaeViewAspect *a = shantae_view_aspect_info(index);
    if (!a) return;
    /* Only heights whose width at this ratio is within the width limits. */
    int lo = (SHANTAE_VIEW_MIN_WIDTH * a->den + a->num - 1) / a->num;
    int hi = SHANTAE_VIEW_MAX_WIDTH * a->den / a->num;
    *height = clamp(*height, lo > SHANTAE_VIEW_MIN_HEIGHT ? lo : SHANTAE_VIEW_MIN_HEIGHT,
                    hi < SHANTAE_VIEW_MAX_HEIGHT ? hi : SHANTAE_VIEW_MAX_HEIGHT);
    /* Round down: a view never wider than its ratio still scales to a whole
     * multiple on a screen of that ratio (16:9 at 240 is 426, 6 x 426 < 2560). */
    *width = *height * a->num / a->den;
}
int shantae_view_aspect_of(int width, int height) {
    for (int i = 0; i < shantae_view_aspect_count(); ++i) {
        int w, h = height;
        shantae_view_aspect_size(i, &w, &h);
        if (w == width && h == height) return i;
    }
    return -1;
}

static uint32_t color(const uint8_t *palette, int index) {
    unsigned c = word(palette + index * 2);
    return 0xff000000u | (((c & 31) * 255 / 31) << 16) |
           ((((c >> 5) & 31) * 255 / 31) << 8) | (((c >> 10) & 31) * 255 / 31);
}

static int world_tile(const GBContext *ctx, int x, int y, unsigned *tile, unsigned *attr) {
    if (x < 0 || y < 0 || x >= 8192 || y >= 8192) return 0;
    unsigned directory = (wram[0x9f9] << 8) + (y >> 8) * 64 + (x >> 8) * 2;
    if (directory < 0x4000 || directory + 1 >= 0x8000) return 0;
    unsigned page = rom8(ctx, wram[0x9fa], directory);
    unsigned bank = rom8(ctx, wram[0x9fa], directory + 1);
    unsigned cell = (page << 8) + ((y & 255) >> 4) * 32 + ((x & 255) >> 4) * 2;
    if (cell < 0x4000 || cell + 1 >= 0x8000) return 0;
    unsigned lo = rom8(ctx, bank, cell), hi = rom8(ctx, bank, cell + 1);
    bank = hram[0x5f] + (lo & 15);
    unsigned ptr = (hi << 8) | (lo & 240);
    if (ptr < 0x4000 || ptr + 7 >= 0x8000 || bank * 0x4000u >= ctx->rom_size) return 0;
    unsigned quadrant = ((y & 8) >> 2) + ((x & 8) >> 3);
    *tile = rom8(ctx, bank, ptr + quadrant);
    *attr = rom8(ctx, bank, ptr + 4 + quadrant);
    return 1;
}

/* Capture metasprites before the hardware cull. Two lists follow the game's
 * D700/D800 double-buffered shadow OAM pages. */
static void read_tap(GBContext *ctx, uint16_t addr) {
    world_read_tap(ctx, addr);
    /* 00:0A57/0A5C (generated PCs 0A5A/0A5F) copy C9FB/C9FD to SCX/SCY, but
     * only once the main loop has finished its frame (FF8F). During a lag frame
     * the main loop has already moved C9FB/C9FD on, while the screen still uses
     * the scroll committed here. */
    if (ctx->pc == 0x0a5a && addr == 0xc9fb) {
        committed_x = memory16(ctx, 0xc9fb);
        committed_dx = built_dx;
    }
    if (ctx->pc == 0x0a5f && addr == 0xc9fd) {
        committed_y = memory16(ctx, 0xc9fd);
        committed_dy = built_dy;
        committed = 1;
        commit_ly = ((GBPPU *)ctx->ppu)->ly;
    }
    /* 03:724D/03:72EB build the scroll (C9FF-CA02, copied to C9FB-C9FE at
     * 03:73B4) as the camera FFE1/FFE3 plus C9D2/C9D4 (generated PCs after the
     * ADD A,(HL) reads). Sprites are placed with the camera alone. Bosses drawn
     * in the background set this offset to move their body: 1B:4E88 derives
     * C9D2 from the boss's x, 17:4DA2 sets C9D4. */
    if (ctx->rom_bank == 3 && addr == 0xc9d2 && (ctx->pc == 0x725b || ctx->pc == 0x72f1))
        built_dx = (int16_t)memory16(ctx, 0xc9d2);
    if (ctx->rom_bank == 3 && addr == 0xc9d4 && (ctx->pc == 0x726b || ctx->pc == 0x7301))
        built_dy = (int16_t)memory16(ctx, 0xc9d4);
    if (ctx->rom_bank == 1 && addr == 0xffd7 && ctx->pc == 0x667e) {
        /* DMA has just consumed this page. Preserve its draw list before the
         * game starts reusing the same shadow page for a future frame. */
        unsigned displayed = ctx->io[0x46];
        if (displayed == 0xd7 || displayed == 0xd8) latched = pending[displayed - 0xd7];
        /* 667C toggles the draw page: new zero draws into D800, new eight D700. */
        unsigned next = ctx->hram[0x57] ^ 8;
        pending[next ? 0 : 1].count = 0;
        bg_latched = bg_pending;
        bg_pending.count = 0;
    }
    if (ctx->pc != 0x1dc6 || addr != ctx->hl) return;
    unsigned page = ctx->hram[0x59];
    if (page != 0xd7 && page != 0xd8) return;
    DrawList *list = &pending[page - 0xd7];
    if (list->count == MAX_SPRITES) return;
    unsigned base = memory16(ctx, ctx->sp);
    unsigned graphic = rom16(ctx, ctx->rom_bank, base + 8);
    unsigned count = rom8(ctx, ctx->rom_bank, ctx->hl + 32);
    if (count > 40 || graphic < 0x4000 || graphic + 1 + count * 32 > 0x8000) return;
    Draw *draw = &list->draws[list->count++];
    draw->x = (int16_t)word(ctx->hram + 0x12) + word(ctx->hram + 0x61);
    draw->y = (int16_t)word(ctx->hram + 0x14) + word(ctx->hram + 0x63);
    draw->bank = ctx->rom_bank;
    draw->descriptor = ctx->hl;
    draw->graphics = graphic;
}

static void reset_view(GBContext *ctx) {
    ready = 0;
    committed = 0;
    zoom_snap = room_snap = 1;
    /* A state saved after this frame's camera routine commits without
     * rerunning it; the stored offset is the one it used. */
    built_dx = ctx->wram ? (int16_t)memory16(ctx, 0xc9d2) : 0;
    built_dy = ctx->wram ? (int16_t)memory16(ctx, 0xc9d4) : 0;
    widened = 0;
    memset(widened_room, 0, sizeof(widened_room));
    memset(pending, 0, sizeof(pending));
    memset(&latched, 0, sizeof(latched));
    memset(&visible, 0, sizeof(visible));
    bg_pending.count = bg_latched.count = bg_visible.count = 0;
    spawn_phase = 0;
    memset(spawn_bounds, 0, sizeof(spawn_bounds));
    memset(spawn_sectors, 0, sizeof(spawn_sectors));
    memset(retention_seen, 0, sizeof(retention_seen));
    memset(early_answer, 0, sizeof(early_answer));
    retention_frame = early_frame = 0;
    spawn_waits = early_releases = doors_restored = encounter_waits = budget_reads = 0;
    eye_reads = eye_boxes = 0;
    fill_count = fill_next = fill_valid = 0;
    fills = 0;
}

/* Rollback for preemptive frames: everything later frames depend on. Object
 * activation reads the spawn bounds, the widened room, which objects ran
 * retention and this frame's early-release answers; the lists and snapshots
 * carry from one frame to the next. Lists are copied up to their counts. The
 * debug counters go too, so a replayed frame is not counted twice. The object
 * table's own state is in guest memory (object_slots.c). */
#define VIEW_FIXED_STATE(X) \
    X(wram) X(hram) X(vram) X(bg_palette) X(obj_palette) X(line0_bg_palette) \
    X(line0_obj_palette) X(ready) X(lcdc) X(window_y) X(window_x) X(scroll_x) \
    X(scroll_y) X(camera_x) X(camera_y) X(committed) X(committed_x) X(committed_y) \
    X(commit_ly) X(built_dx) X(built_dy) X(committed_dx) X(committed_dy) X(line0_scx) \
    X(line0_scy) X(oam_checked) X(oam_matched) X(background_checked) \
    X(background_matched) X(map_checked) X(map_matched) X(spawn_phase) \
    X(spawn_bounds) X(spawn_sectors) X(widened) X(widened_room) X(retention_seen) \
    X(retention_frame) X(early_answer) X(early_frame) X(spawn_waits) X(early_releases) X(doors_restored) \
    X(encounter_waits) X(fill_sectors) X(fill_count) X(fill_next) X(fill_valid) X(fill_last) X(fills) \
    X(budget_reads) X(eye_reads) X(eye_boxes)
static DrawList *const view_lists[] = {&pending[0], &pending[1], &latched, &visible};
static Background *const view_backgrounds[] = {&bg_pending, &bg_latched, &bg_visible};
#define VIEW_COUNT(a) (sizeof(a) / sizeof((a)[0]))

size_t shantae_view_state_size(void) {
    size_t size = 0;
#define VIEW_SIZE(v) size += sizeof(v);
    VIEW_FIXED_STATE(VIEW_SIZE)
#undef VIEW_SIZE
    return size + VIEW_COUNT(view_lists) * sizeof(DrawList) +
           VIEW_COUNT(view_backgrounds) * sizeof(Background);
}

void shantae_view_state_save(void *out) {
    uint8_t *p = out;
#define VIEW_SAVE(v) memcpy(p, &(v), sizeof(v)); p += sizeof(v);
    VIEW_FIXED_STATE(VIEW_SAVE)
#undef VIEW_SAVE
    for (size_t i = 0; i < VIEW_COUNT(view_lists); i++) {
        const DrawList *list = view_lists[i];
        memcpy(p, &list->count, sizeof(list->count));
        p += sizeof(list->count);
        memcpy(p, list->draws, list->count * sizeof(Draw));
        p += list->count * sizeof(Draw);
    }
    for (size_t i = 0; i < VIEW_COUNT(view_backgrounds); i++) {
        const Background *bg = view_backgrounds[i];
        memcpy(p, &bg->count, sizeof(bg->count));
        p += sizeof(bg->count);
        memcpy(p, bg->tiles, bg->count * sizeof(BackgroundTile));
        p += bg->count * sizeof(BackgroundTile);
    }
}

void shantae_view_state_load(const void *in) {
    const uint8_t *p = in;
#define VIEW_LOAD(v) memcpy(&(v), p, sizeof(v)); p += sizeof(v);
    VIEW_FIXED_STATE(VIEW_LOAD)
#undef VIEW_LOAD
    for (size_t i = 0; i < VIEW_COUNT(view_lists); i++) {
        DrawList *list = view_lists[i];
        memcpy(&list->count, p, sizeof(list->count));
        p += sizeof(list->count);
        memcpy(list->draws, p, list->count * sizeof(Draw));
        p += list->count * sizeof(Draw);
    }
    for (size_t i = 0; i < VIEW_COUNT(view_backgrounds); i++) {
        Background *bg = view_backgrounds[i];
        memcpy(&bg->count, p, sizeof(bg->count));
        p += sizeof(bg->count);
        memcpy(bg->tiles, p, bg->count * sizeof(BackgroundTile));
        p += bg->count * sizeof(BackgroundTile);
    }
}

/* Compare the fully visible native tilemap with the ROM map plus this frame's
 * background objects. Menus such as the inventory keep the map pointers but
 * zero the camera and replace the tilemap and tile data. */
static int map_agrees(const GBContext *ctx, int hud) {
    enum { COLS = 20, ROWS = 18 };
    int objects[COLS * ROWS];
    int x0 = (scroll_x + 7) / 8, x1 = (scroll_x + 160) / 8;
    int y0 = (scroll_y + 7) / 8, y1 = (scroll_y + 144 - hud) / 8;
    for (int i = 0; i < COLS * ROWS; ++i) objects[i] = -1;
    for (int i = 0; i < bg_visible.count; ++i) {
        const BackgroundTile *t = &bg_visible.tiles[i];
        if (t->x >= x0 && t->x < x1 && t->y >= y0 && t->y < y1)
            objects[(t->y - y0) * COLS + t->x - x0] = t->tile | (t->attr << 8);
    }
    unsigned base = (lcdc & 8) ? 0x1c00 : 0x1800;
    map_checked = map_matched = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            int expected = objects[(y - y0) * COLS + x - x0];
            unsigned tile, attr, offset = base + (y & 31) * 32 + (x & 31);
            if (expected < 0 && world_tile(ctx, x * 8, y * 8, &tile, &attr)) expected = tile | (attr << 8);
            map_checked++;
            map_matched += expected == (int)(vram[offset] | (vram[offset + 0x2000] << 8));
        }
    }
    return map_checked && map_matched * 2 >= map_checked;
}

/* Dialogue and other window scenes keep their original centered composition;
 * only a window along the bottom (status_bar) is moved to the view's bottom. */
static int window_blocks(void) {
    return (lcdc & 0x20) && window_x < 160 && window_y < 128;
}

static void snapshot(GBContext *ctx) {
    GBPPU *ppu = (GBPPU *)ctx->ppu;
    ready = 0;
    if (!ppu || !(ppu->lcdc & 0x80)) return;
    memcpy(wram, ctx->wram, sizeof(wram));
    memcpy(hram, ctx->hram, sizeof(hram));
    memcpy(vram, ctx->vram, sizeof(vram));
    memcpy(line0_bg_palette, ppu->bg_palette_ram, sizeof(line0_bg_palette));
    memcpy(line0_obj_palette, ppu->obj_palette_ram, sizeof(line0_obj_palette));
    /* Until the first commit after a reset, C9FB/C9FD are the best guess. */
    int dx = committed ? committed_dx : built_dx, dy = committed ? committed_dy : built_dy;
    camera_x = (uint16_t)((committed ? committed_x : word(wram + 0x9fb)) - dx);
    camera_y = (uint16_t)((committed ? committed_y : word(wram + 0x9fd)) - dy);
    scroll_x = camera_x + dx;
    scroll_y = camera_y + dy;
    lcdc = ppu->lcdc;
    line0_scx = ppu->latched_scx;
    line0_scy = ppu->latched_scy;
    window_y = ppu->wy;
    window_x = (int)ppu->wx - 7;
    visible = latched;
    bg_visible = bg_latched;
    background_checked = background_matched = 0;
    for (int i = 0; i < bg_visible.count; ++i) {
        BackgroundTile *t = &bg_visible.tiles[i];
        if (t->x * 8 < scroll_x || t->x * 8 + 8 > scroll_x + 160 ||
            t->y * 8 < scroll_y || t->y * 8 + 8 > scroll_y + 128) continue;
        unsigned offset = ((lcdc & 8) ? 0x1c00 : 0x1800) + (t->y & 31) * 32 + (t->x & 31);
        background_checked++;
        background_matched += vram[offset] == t->tile && vram[offset + 0x2000] == t->attr;
    }
    oam_checked = oam_matched = 0;
    for (int slot = 0; slot < 40; ++slot) {
        const uint8_t *o = ctx->oam + slot * 4;
        if (!o[0] || !o[1] || o[0] >= 160 || o[1] >= 168) continue;
        oam_checked++;
        int found = 0;
        for (int n = 0; n < visible.count && !found; ++n) {
            Draw *d = &visible.draws[n];
            unsigned count = rom8(ctx, d->bank, d->descriptor + 32);
            for (unsigned i = 0; i < count; ++i) {
                unsigned layout = d->descriptor + 35 + i * 3;
                unsigned y = (d->y - camera_y + rom8(ctx, d->bank, layout)) & 255;
                unsigned x = (d->x - camera_x + rom8(ctx, d->bank, layout + 1)) & 255;
                unsigned attr = rom8(ctx, d->bank, layout + 2);
                if (x != o[1] || y != o[0] || (attr & ~8u) != (o[3] & ~8u)) continue;
                int same = 1;
                unsigned voff = ((o[3] & 8) ? 0x2000 : 0) + (o[2] & 254) * 16;
                for (unsigned b = 0; b < 32; ++b)
                    if (vram[voff + b] != rom8(ctx, d->bank, d->graphics + 1 + i * 32 + b)) same = 0;
                if (same) { found = 1; break; }
            }
        }
        oam_matched += found;
    }
    /* Validate the world-map binding, then require the hardware to be drawing
     * it. A majority vote tolerates tiles streamed or changed between frames. */
    map_checked = map_matched = 0;
    if (wram[0x9fa] && (lcdc & 1) && !town_room(wram)) {
        unsigned tile, attr;
        const int hud = status_bar(lcdc, window_x, window_y);
        const Room room = room_at(ctx, wram, camera_x, camera_y, hud, 0);
        ready = world_tile(ctx, scroll_x, scroll_y, &tile, &attr) && map_agrees(ctx, hud) &&
                !room_fits_screen(&room);
    }
    note_camera(camera_x, camera_y);
    note_presented(ready && !window_blocks());
}

/* Fade palettes are uploaded at the top of the frame and pausing blanks them in
 * VBlank. Use the colors the rest of the native frame was drawn with. */
static void frame_end(GBContext *ctx) {
    GBPPU *ppu = (GBPPU *)ctx->ppu;
    memcpy(bg_palette, ppu->bg_palette_ram, sizeof(bg_palette));
    memcpy(obj_palette, ppu->obj_palette_ram, sizeof(obj_palette));
}

/* The background is drawn 8x8 map cell by cell: each cell's tile is looked up
 * once a frame, and a row of its pattern is expanded 8 pixels at a time. Byte i
 * of spread[b] is bit 7 - i of b (the leftmost pixel first); of spread[256 + b],
 * bit i (flipped horizontally). */
static uint64_t spread[512];
static void init_spread(void) {
    for (int b = 0; b < 256; ++b)
        for (int i = 0; i < 8; ++i) {
            spread[b] |= (uint64_t)((b >> (7 - i)) & 1) << (8 * i);
            spread[256 + b] |= (uint64_t)((b >> i) & 1) << (8 * i);
        }
}
/* A map cell's pattern data in VRAM (bank included), or -1 where nothing is
 * drawn, and its attributes. */
typedef struct { int address; unsigned attr; } Cell;
/* The drawn background: view columns x0..x1 and rows y0..y1 (inside the room,
 * above the status bar, at world coordinates >= 0), world position wx0/wy0 of
 * view pixel 0,0, and the cells covering it from world cell gx0,gy0. */
typedef struct {
    const Cell *cells;
    int cols, gx0, gy0, x0, x1, y0, y1, wx0, wy0;
} Grid;
static inline int filled(const Grid *g, int x, int y) {
    return x >= g->x0 && x < g->x1 && y >= g->y0 && y < g->y1;
}
static inline const Cell *cell_at(const Grid *g, int wx, int wy) {
    return &g->cells[((wy >> 3) - g->gy0) * g->cols + (wx >> 3) - g->gx0];
}
/* The background at view pixel x,y: palette entry (0-31) plus 0x100 for BG
 * priority, or -1 where none is drawn. */
static int background_at(const Grid *g, int x, int y) {
    if (!filled(g, x, y)) return -1;
    int wx = g->wx0 + x, wy = g->wy0 + y;
    const Cell *c = cell_at(g, wx, wy);
    if (c->address < 0) return -1;
    int py = (c->attr & 64) ? 7 - (wy & 7) : wy & 7;
    int bit = (c->attr & 32) ? wx & 7 : 7 - (wx & 7);
    const uint8_t *p = vram + c->address + py * 2;
    int pixel = ((p[0] >> bit) & 1) | (((p[1] >> bit) & 1) << 1);
    return ((c->attr & 7) * 4 + pixel) | ((c->attr & 128) << 1);
}
static void fill_black(uint32_t *p, int n) {
    for (int i = 0; i < n; ++i) p[i] = 0xff000000u;
}

/* The room zoom (fit below) and the room shown ease over ZOOM_FRAMES guest
 * frames; a dark native frame (a fade between rooms) snaps them. */
#define ZOOM_FRAMES 16
static int native_dark(const GBContext *ctx) {
    const uint32_t *p = ((GBPPU *)ctx->ppu)->rgb_framebuffer;
    for (int i = 0; i < 160 * 144; ++i)
        if (p[i] & 0xf8f8f8) return 0;
    return 1;
}
/* The room the picture shows. A room that jumps in play (bounds changed by a
 * script, or the camera's screen reaching another picture) eases there with
 * the zoom, so the picture pans to the new room instead of jumping: 25:46DC
 * gives the second arena its whole width (752-1023) once the boss has risen,
 * and the room centered in the picture moved 64 pixels in a frame. A room
 * that moves as fast as a camera (ROOM_JUMP a frame) follows at once, a new
 * map, a cut or a dark frame snaps to it, and frames shown again keep the ease
 * where it is. The camera's own screen in the room is always shown. */
#define ROOM_JUMP 8
static Box shown_room(const GBContext *ctx, const Box *target) {
    static int easing;
    const int jumped = abs(target->x0 - room_target.x0) > ROOM_JUMP || abs(target->y0 - room_target.y0) > ROOM_JUMP ||
                       abs(target->x1 - room_target.x1) > ROOM_JUMP || abs(target->y1 - room_target.y1) > ROOM_JUMP;
    if (room_snap || memcmp(wram + 0x9f8, room_map, sizeof(room_map)) || native_dark(ctx)) {
        easing = 0;
    } else if (jumped) {
        room_from = room_shown;
        room_frame = ctx->completed_frames;
        easing = 1;
    }
    room_snap = 0;
    memcpy(room_map, wram + 0x9f8, sizeof(room_map));
    room_target = *target;
    const uint64_t t = ctx->completed_frames - room_frame;
    if (t >= ZOOM_FRAMES) easing = 0;
    room_shown = *target;
    if (easing) {
        double u = (double)t / ZOOM_FRAMES;
        u = u * u * (3 - 2 * u);
        room_shown.x0 = room_from.x0 + (int)lround((target->x0 - room_from.x0) * u);
        room_shown.y0 = room_from.y0 + (int)lround((target->y0 - room_from.y0) * u);
        room_shown.x1 = room_from.x1 + (int)lround((target->x1 - room_from.x1) * u);
        room_shown.y1 = room_from.y1 + (int)lround((target->y1 - room_from.y1) * u);
        const Box screen = {clamp(camera_x, target->x0, target->x1), clamp(camera_y, target->y0, target->y1),
                            clamp(camera_x + 160, target->x0, target->x1),
                            clamp(camera_y + 144, target->y0, target->y1)};
        if (screen.x0 < screen.x1 && screen.y0 < screen.y1) box_add(&room_shown, &screen);
    }
    return room_shown;
}

static int render(GBContext *ctx, uint32_t *out, int width, const uint32_t *native) {
    if (!ready || !(((GBPPU *)ctx->ppu)->lcdc & 0x80)) return 0;
    int height = gb_custom_height;
    if (window_blocks()) return 0;
    int hud = status_bar(lcdc, window_x, window_y);
    int world_height = height - hud;
    const Room room = room_at(ctx, wram, camera_x, camera_y, hud, 1);
    const Box box = shown_room(ctx, &room.box);
    int min_x = box.x0, min_y = box.y0;
    int max_x = box.x1, max_y = box.y1 - hud;
    /* The native picture and sprites follow the camera; the background is
     * displaced by the offset in the scroll, as on hardware. Room bounds apply
     * to the displayed position, so a moving boss stays inside the room. */
    int origin_x, origin_y, dx = scroll_x - camera_x, dy = scroll_y - camera_y;
    view_origin(&box, camera_x, camera_y, width, height, &origin_x, &origin_y);
    int left = camera_x - origin_x, top = camera_y - origin_y;
    /* Sprites and the native picture stop at the room's edges like the
     * background: a camera shaking past them (the rumble while the first
     * arena's stone sinks runs 2 pixels past its least x) flashed that strip
     * of the native picture beside the room every fourth frame. */
    int room_left = clamp(min_x - origin_x, 0, width), room_right = clamp(max_x - origin_x, 0, width);
    int room_top = clamp(min_y - origin_y, 0, world_height), room_bottom = clamp(max_y - origin_y, 0, world_height);
    Grid g;
    g.wx0 = origin_x + dx;
    g.wy0 = origin_y + dy;
    g.x0 = clamp(min_x - origin_x > -g.wx0 ? min_x - origin_x : -g.wx0, 0, width);
    g.x1 = clamp(max_x - origin_x, g.x0, width);
    g.y0 = clamp(min_y - origin_y > -g.wy0 ? min_y - origin_y : -g.wy0, 0, world_height);
    g.y1 = clamp(max_y - origin_y, g.y0, world_height);
    g.gx0 = (g.wx0 + g.x0) >> 3;
    g.gy0 = (g.wy0 + g.y0) >> 3;
    g.cols = g.x1 > g.x0 ? ((g.wx0 + g.x1 - 1) >> 3) - g.gx0 + 1 : 0;
    int rows = g.y1 > g.y0 ? ((g.wy0 + g.y1 - 1) >> 3) - g.gy0 + 1 : 0;
    /* Background objects replace map cells, the last one listed on top, within
     * a window of cells from tile_x,tile_y. The buffers grow to the largest
     * view drawn (the size can change live). */
    static Cell *cells;
    static uint16_t *overlay;
    static size_t cell_capacity, overlay_capacity;
    const int overlay_w = width / 8 + 2, overlay_h = height / 8 + 2;
    size_t cell_count = (size_t)g.cols * rows, overlay_cells = (size_t)overlay_w * overlay_h;
    if (cell_count > cell_capacity) {
        free(cells);
        cells = malloc(cell_count * sizeof(*cells));
        cell_capacity = cells ? cell_count : 0;
        if (!cell_capacity) return 0;
    }
    if (overlay_cells > overlay_capacity) {
        free(overlay);
        overlay = malloc(overlay_cells * sizeof(*overlay));
        overlay_capacity = overlay ? overlay_cells : 0;
        if (!overlay_capacity) return 0;
    }
    g.cells = cells;
    if (!spread[1]) init_spread();
    uint32_t colors[64];
    for (int i = 0; i < 64; ++i) colors[i] = color(i < 32 ? bg_palette : obj_palette, i & 31);
    for (size_t i = 0; i < overlay_cells; ++i) overlay[i] = 0xffff;
    int tile_x = g.wx0 / 8, tile_y = g.wy0 / 8;
    for (int i = 0; i < bg_visible.count; ++i) {
        BackgroundTile *t = &bg_visible.tiles[i];
        int x = t->x - tile_x, y = t->y - tile_y;
        if (x >= 0 && x < overlay_w && y >= 0 && y < overlay_h) overlay[y * overlay_w + x] = t->tile | (t->attr << 8);
    }
    for (int cy = 0; cy < rows; ++cy) {
        for (int cx = 0; cx < g.cols; ++cx) {
            Cell *c = &cells[cy * g.cols + cx];
            unsigned tile, attr;
            const int wx = (g.gx0 + cx) * 8, wy = (g.gy0 + cy) * 8;
            c->address = -1;
            /* Another picture in the room's box is not the room's. */
            if (room.masked && (wx >> 8 > 31 || wy >> 8 > 31 || !(room.sectors[wy >> 8] >> (wx >> 8) & 1)))
                continue;
            if (!world_tile(ctx, wx, wy, &tile, &attr)) continue;
            int ox = g.gx0 + cx - tile_x, oy = g.gy0 + cy - tile_y;
            if (ox >= 0 && ox < overlay_w && oy >= 0 && oy < overlay_h && overlay[oy * overlay_w + ox] != 0xffff) {
                tile = overlay[oy * overlay_w + ox] & 255;
                attr = overlay[oy * overlay_w + ox] >> 8;
            }
            c->address = ((lcdc & 16) ? tile * 16 : 0x1000 + (int8_t)tile * 16) + ((attr & 8) ? 0x2000 : 0);
            c->attr = attr;
        }
    }
    for (int y = 0; y < height; ++y) {
        uint32_t *row = out + (size_t)y * width;
        if (y < g.y0 || y >= g.y1 || g.x0 >= g.x1) {
            fill_black(row, width);
            continue;
        }
        fill_black(row, g.x0);
        fill_black(row + g.x1, width - g.x1);
        int wy = g.wy0 + y, wx = g.wx0 + g.x0;
        const Cell *line = &cells[((wy >> 3) - g.gy0) * g.cols];
        for (int x = g.x0; x < g.x1;) {
            const Cell *c = &line[(wx >> 3) - g.gx0];
            int n = 8 - (wx & 7);
            if (n > g.x1 - x) n = g.x1 - x;
            if (c->address < 0) {
                fill_black(row + x, n);
            } else {
                const uint8_t *p = vram + c->address + ((c->attr & 64) ? 7 - (wy & 7) : wy & 7) * 2;
                const uint64_t *table = spread + ((c->attr & 32) ? 256 : 0);
                uint64_t pixels = (table[p[0]] | table[p[1]] << 1) >> 8 * (wx & 7);
                const uint32_t *palette = colors + (c->attr & 7) * 4;
                for (int i = 0; i < n; ++i, pixels >>= 8) row[x + i] = palette[pixels & 3];
            }
            x += n;
            wx += n;
        }
    }
    /* The native picture's top lines, for the fade correction below: the
     * palette entry drawn at each pixel (0-31 BG, 32-63 OBJ, 255 none). */
    int clip_x = room_left > left ? room_left - left : 0;
    int copy_width = clamp(room_right - left, 0, 160) - clip_x;
    uint8_t entry[PALETTE_UPLOAD_LINES][160];
    for (int y = 0; y < PALETTE_UPLOAD_LINES; ++y)
        for (int x = clip_x; x < clip_x + copy_width; ++x) {
            int bg = background_at(&g, left + x, top + y);
            entry[y][x] = bg < 0 ? 255 : bg & 31;
        }
    /* OAM pieces are triples (biased Y, biased X, attributes); the frame's
     * graphics block contains the corresponding 32-byte 8x16 tile pairs. */
    for (int n = visible.count - 1; n >= 0; --n) {
        const Draw *draw = &visible.draws[n];
        unsigned count = rom8(ctx, draw->bank, draw->descriptor + 32);
        for (int i = (int)count - 1; i >= 0; --i) {
            unsigned layout = draw->descriptor + 35 + i * 3;
            int sy = draw->y - camera_y + top + (int8_t)rom8(ctx, draw->bank, layout) - 16;
            int sx = draw->x - camera_x + left + (int8_t)rom8(ctx, draw->bank, layout + 1) - 8;
            unsigned attr = rom8(ctx, draw->bank, layout + 2);
            if (sx + 8 <= room_left || sx >= room_right || sy + 16 <= room_top || sy >= room_bottom) continue;
            for (int y = 0; y < 16; ++y) {
                int dy = sy + y;
                if (dy < room_top || dy >= room_bottom) continue;
                int py = (attr & 64) ? 15 - y : y;
                unsigned graphic = draw->graphics + 1 + i * 32 + py * 2;
                unsigned lo = rom8(ctx, draw->bank, graphic), hi = rom8(ctx, draw->bank, graphic + 1);
                for (int x = 0; x < 8; ++x) {
                    int dx = sx + x;
                    if (dx < room_left || dx >= room_right) continue;
                    int bit = (attr & 32) ? x : 7 - x;
                    int pixel = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                    if (!pixel) continue;
                    int bg = background_at(&g, dx, dy);
                    if (bg >= 0 && (bg & 3) && ((attr & 128) || (bg & 0x100))) continue;
                    int e = 32 + (attr & 7) * 4 + pixel;
                    out[(size_t)dy * width + dx] = colors[e];
                    int ny = dy - top, nx = dx - left;
                    if (ny >= 0 && ny < PALETTE_UPLOAD_LINES && nx >= clip_x && nx < clip_x + copy_width)
                        entry[ny][nx] = e;
                }
            }
        }
    }
    /* Preserve the hardware's exact central image, including raster effects and
     * dynamic background objects, and anchor the status bar to the new bottom.
     * During a fade the top native lines are drawn before their palettes are
     * uploaded. Where the predicted color still shows its line-0 value, use the
     * settled value that the rest of the frame uses. */
    uint32_t stale[64];
    for (int i = 0; i < 64; ++i) stale[i] = color(i < 32 ? line0_bg_palette : line0_obj_palette, i & 31);
    for (int y = 0; y < 144 - hud; ++y) {
        if (y + top < room_top || y + top >= room_bottom || copy_width <= 0) continue;
        uint32_t *at = out + (size_t)(y + top) * width + left + clip_x;
        memcpy(at, native + y * 160 + clip_x, copy_width * sizeof(uint32_t));
        for (int x = 0; y < PALETTE_UPLOAD_LINES && x < copy_width; ++x) {
            unsigned e = entry[y][clip_x + x];
            if (e < 64 && at[x] == stale[e]) at[x] = colors[e];
        }
    }
    /* The window's own background (the color of most of it) continues beside
     * it: the intro's strip spans the view like a letterbox, with whatever
     * crosses it kept in the middle; the status bar's is black. */
    const uint32_t *bar = native + (144 - hud) * 160;
    uint32_t fill = 0xff000000u;
    int votes = 0;
    for (int i = 0; i < hud * 160; ++i) {
        if (!votes) fill = bar[i];
        votes += bar[i] == fill ? 1 : -1;
    }
    int count = 0;
    for (int i = 0; i < hud * 160; ++i) count += bar[i] == fill;
    if (count * 2 <= hud * 160) fill = 0xff000000u;
    for (int y = 0; y < hud; ++y) {
        uint32_t *row = out + (size_t)(world_height + y) * width;
        for (int x = 0; x < width; ++x) row[x] = fill;
        memcpy(row + (width - 160) / 2, bar + y * 160, 160 * sizeof(uint32_t));
    }
    return 1;
}

/* Room zoom. A room smaller than the view (a few screens, or a camera locked
 * to an arena) would be drawn at the view's scale in a field of black. The
 * picture shrinks to it instead and the window scales it up. Fill zooms until
 * the room fills the window (to the nearest whole scale with Pixel Perfect),
 * scrolling along a side that no longer fits; Whole room zooms as far as it
 * can without cutting off a side that fits (whole scales round down). Never
 * past the original screen (160 x 144 at the least) nor below the view's own
 * scale. Rooms are entered through black (doors and deaths fade out) or a cut
 * (the intro's house to the bridge), so a new map, a dark frame or the camera
 * jumping snaps to the zoom; bounds that change in play (03:7A3D and 03:7AFE,
 * as an arena locks and unlocks) ease to it. */
static void fit(GBContext *ctx, int window_w, int window_h, int whole_pixels, int *width,
                int *height, double *scale) {
    const int view_w = *width, view_h = *height, mode = shantae_room_zoom();
    ease_width = ease_height = 0;
    if (mode == SHANTAE_ROOM_ZOOM_OFF || window_w < 160 || window_h < 144) {
        zoom = zoom_target = 0;
        return;
    }
    double base = fmin((double)window_w / view_w, (double)window_h / view_h);
    double most = fmin(window_w / 160.0, window_h / 144.0);
    if (whole_pixels) {
        base = base < 1 ? 1 : floor(base);
        most = floor(most);
    }
    /* While the game declines frames (menus, dialogue, the LCD off, one-screen
     * rooms) the picture stays as it was. */
    if (ready && !window_blocks() && (((GBPPU *)ctx->ppu)->lcdc & 0x80)) {
        const int hud = status_bar(lcdc, window_x, window_y);
        const Room room = room_at(ctx, wram, camera_x, camera_y, hud, 0);
        int room_w = room.box.x1 - room.box.x0;
        int room_h = room.box.y1 - hud - room.box.y0;
        if (room_w < 160) room_w = 160;
        if (room_h < 144 - hud) room_h = 144 - hud;
        room_h += hud;
        const double sx = (double)window_w / room_w, sy = (double)window_h / room_h;
        double target;
        if (mode == SHANTAE_ROOM_ZOOM_WHOLE) {
            target = room_w < view_w ? sx : INFINITY;
            if (room_h < view_h && sy < target) target = sy;
            if (whole_pixels && isfinite(target)) target = floor(target);
        } else {
            target = fmax(sx, sy);
            if (whole_pixels) target = floor(target + 0.5);
        }
        zoom_target = fmax(fmin(target, most), base);
        if (zoom <= 0 || zoom_snap || memcmp(wram + 0x9f8, zoom_room, sizeof(zoom_room)) || native_dark(ctx)) {
            zoom = zoom_from = zoom_to = zoom_target;
        } else {
            if (zoom_target != zoom_to) {
                zoom_from = zoom;
                zoom_to = zoom_target;
                zoom_frame = ctx->completed_frames;
            }
            const uint64_t t = ctx->completed_frames - zoom_frame;
            if (t >= ZOOM_FRAMES) {
                zoom = zoom_to;
            } else {
                double u = (double)t / ZOOM_FRAMES;
                u = u * u * (3 - 2 * u);
                zoom = zoom_from * pow(zoom_to / zoom_from, u);
            }
        }
        memcpy(zoom_room, wram + 0x9f8, sizeof(zoom_room));
        zoom_snap = 0;
    }
    if (zoom > 0 && zoom_to < zoom) {
        /* Easing out: activation takes the picture it eases to. */
        ease_width = zoom_to <= base ? view_w : clamp((int)(window_w / zoom_to + 1e-6), 160, view_w);
        ease_height = zoom_to <= base ? view_h : clamp((int)(window_h / zoom_to + 1e-6), 144, view_h);
    }
    if (zoom <= base) return;   /* the view as it is, by the scaling mode */
    *width = clamp((int)(window_w / zoom + 1e-6), 160, view_w);
    *height = clamp((int)(window_h / zoom + 1e-6), 144, view_h);
    *scale = zoom;
}

void shantae_view_init(GBContext *ctx) {
    s_ctx = ctx;
    gb_custom_render = NULL;
    gb_custom_fit = NULL;
    gb_custom_snapshot = gb_custom_frame_end = gb_custom_reset = NULL;
    gb_custom_read_tap = NULL;
    gb_custom_read_override = NULL;
    gb_custom_requested_width = 0;
    gb_custom_requested_height = 144;
    gb_custom_native_scaling = GB_CUSTOM_NATIVE_IN_VIEW;
    gb_custom_native_scale = 1;
    if (!shantae_expanded_view()) return;
    reset_view(ctx);
    gb_custom_render = render;
    gb_custom_fit = fit;
    gb_custom_snapshot = snapshot;
    gb_custom_frame_end = frame_end;
    gb_custom_reset = reset_view;
    gb_custom_read_tap = read_tap;
    gb_custom_read_override = read_override;
    shantae_view_apply_size();
    if (shantae_view_adaptive())
        fprintf(stderr, "[SHANTAE VIEW] Expanded view: adaptive, at least %d tall\n",
                gb_custom_requested_height);
    else
        fprintf(stderr, "[SHANTAE VIEW] Expanded view: %dx%d\n", gb_custom_requested_width,
                gb_custom_requested_height);
}

int shantae_view_dispatch(GBContext *ctx, uint16_t addr) { return fill_dispatch(ctx, addr); }

/* A save state file was loaded (extras.c): mend what the view lost in it. */
void shantae_view_state_file_loaded(GBContext *ctx) {
    if (gb_custom_render != render || !ctx->wram) return;
    restore_crow(ctx);
    restore_jars(ctx);
}

int shantae_view_town(const GBContext *ctx) { return ctx->wram && town_room(ctx->wram); }

/* The next presented frame resolves the new request (platform_sdl.cpp). */
void shantae_view_apply_size(void) {
    if (gb_custom_render != render) return;
    gb_custom_requested_width = shantae_view_adaptive() ? -1 : shantae_view_width();
    gb_custom_requested_height = shantae_view_height();
    gb_custom_native_scaling = shantae_native_scaling();
    gb_custom_native_scale = shantae_native_scale();
}

int game_handle_debug_cmd(const char *cmd, int id, const char *json) {
    (void)json;
    if (!strcmp(cmd, "shantae_hold_info") && s_ctx) {
        /* Also "Reduce input lag": VBlanks that copied the fresh sprite buffer
         * versus kept the original one, and player moves run early (extras.c). */
        unsigned long long fresh, original, early_moves, replays;
        shantae_input_lag_counts(&fresh, &original, &early_moves, &replays);
        gb_debug_server_send_fmt("{\"id\":%d,\"ok\":true,\"holds\":%llu,\"limit_hits\":%llu,"
                                 "\"fresh_sprites\":%llu,\"original_sprites\":%llu,"
                                 "\"early_moves\":%llu,\"preempt_replays\":%llu}", id,
                                 (unsigned long long)s_ctx->frame_hold.holds,
                                 (unsigned long long)s_ctx->frame_hold.limit_hits, fresh, original,
                                 early_moves, replays);
        return 1;
    }
    if (!strcmp(cmd, "shantae_flight") && s_ctx && s_ctx->wram) {
        /* The debug flight without debug mode. 06:46E2, which the player's
         * movement routines call in normal control, returns at 06:472E unless
         * CC04 is set; with it and Select+A held, 06:473D starts the flight
         * script 06:71B5 on the next script pass (+16 = $0080, script at
         * +2-+4, +5 = $FF). The script installs the callback 06:71D4: 4 pixels
         * a frame with the D-pad, through walls, no room exits; Select ends it
         * (06:7246, script 06:49F2) without asking for CC04. Setting CC04
         * would also open the scene grid from Select in the inventory
         * (05:5A43) and have 0A:4468 skip the save file (04:4D55, 04:4CF1),
         * so this writes what 06:473D writes and leaves CC04 as it is. */
        const unsigned player = word(s_ctx->wram + 0xa13);
        uint8_t *p = shantae_slot_index(s_ctx, player) >= 0 ? shantae_slot_memory(s_ctx, player) : NULL;
        if (!p || *p == 0xFF) {
            gb_debug_server_send_fmt("{\"id\":%d,\"ok\":false,\"error\":\"no player object at CA13 (%04X)\"}", id,
                                     player);
            return 1;
        }
        const unsigned bank = slot_byte(s_ctx, player + 4), pc = slot_word(s_ctx, player + 2);
        static const uint8_t fields[][2] = {{0x16, 0x80}, {0x17, 0x00}, {0x02, 0xb5}, {0x03, 0x71}, {0x04, 0x06},
                                            {0x05, 0xff}};
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
            *shantae_slot_memory(s_ctx, player + fields[i][0]) = fields[i][1];
        gb_debug_server_send_fmt("{\"id\":%d,\"ok\":true,\"player\":%u,\"was\":[%u,%u]}", id, player, bank, pc);
        return 1;
    }
    if (!strcmp(cmd, "shantae_slots") && s_ctx && s_ctx->wram) {
        /* The table at a glance: the free list's length and where it breaks
         * (0 whole, 1 leaves the table, 2 meets a slot again, 3 reaches a
         * live slot), dead slots it does not hold, and each live slot as
         * [slot, status, record bank, record, x, y, width, height]. */
        const int count = shantae_slot_count(s_ctx);
        uint8_t listed[SHANTAE_MAX_SLOTS] = {0};
        int free = 0, cut = 0, lost = 0;
        for (unsigned a = word(s_ctx->hram + 0x33); a;) {
            const int i = shantae_slot_index(s_ctx, a);
            cut = i < 0 ? 1 : listed[i] ? 2 : slot_byte(s_ctx, a) != 0xFF ? 3 : 0;
            if (cut) break;
            listed[i] = 1;
            ++free;
            a = slot_word(s_ctx, a + 0x7C);
        }
        char live[SHANTAE_MAX_SLOTS * 48] = "";
        size_t n = 0;
        for (int i = 0; i < count; ++i) {
            const unsigned o = shantae_slot_addr(i), status = slot_byte(s_ctx, o);
            if (status == 0xFF) {
                lost += !listed[i];
                continue;
            }
            n += (size_t)snprintf(live + n, sizeof(live) - n, "%s[%d,%u,%u,%u,%d,%d,%u,%u]", n ? "," : "", i,
                                  status, slot_byte(s_ctx, o + 0x24), slot_word(s_ctx, o + 0x25),
                                  (int16_t)slot_word(s_ctx, o + 0x34), (int16_t)slot_word(s_ctx, o + 0x37),
                                  slot_byte(s_ctx, o + 0x57), slot_byte(s_ctx, o + 0x58));
        }
        gb_debug_server_send_fmt("{\"id\":%d,\"ok\":true,\"slots\":%d,\"free\":%d,\"cut\":%d,\"lost\":%d,"
                                 "\"camera\":[%u,%u],\"live\":[%s]}",
                                 id, count, free, cut, lost, word(s_ctx->hram + 0x61), word(s_ctx->hram + 0x63),
                                 live);
        return 1;
    }
    if (strcmp(cmd, "shantae_view_info")) return 0;
    int expanded = ready && !window_blocks(), free = 0, slots = 0;
    uint8_t free_marks[SHANTAE_MAX_SLOTS];
    if (s_ctx && s_ctx->wram) {
        free = free_slots(s_ctx, free_marks);
        slots = shantae_slot_count(s_ctx);
    }
    ShantaeSlotCounts counts;
    shantae_slots_counts(&counts);
    /* The room the view presents (snapshot) as a box of camera screens, and
     * the one the last picture showed while easing to it. */
    Room room = {{0, 0, 0, 0}, 0, {0}};
    if (s_ctx && s_ctx->wram) room = room_at(s_ctx, wram, camera_x, camera_y, status_bar(lcdc, window_x, window_y), 1);
    gb_debug_server_send_fmt("{\"id\":%d,\"ok\":true,\"ready\":%d,\"expanded\":%d,\"camera_x\":%d,\"camera_y\":%d,\"scroll_x\":%d,\"scroll_y\":%d,\"committed\":%d,\"commit_ly\":%d,\"scx\":%d,\"scy\":%d,\"widened\":%d,\"count\":%d,\"oam_checked\":%u,\"oam_matched\":%u,\"background_tiles\":%d,\"background_checked\":%u,\"background_matched\":%u,\"map_checked\":%u,\"map_matched\":%u,\"slots\":%d,\"free_slots\":%d,\"spawn_waits\":%u,\"encounter_waits\":%u,\"early_releases\":%u,\"doors_restored\":%u,\"extra_moves\":%llu,\"extra_layered\":%llu,\"extra_drawn\":%llu,\"slot_upgrades\":%llu,\"orphans_freed\":%llu,\"node_pools\":%llu,\"node_upgrades\":%llu,\"node_repairs\":%llu,\"slot_repairs\":%llu,\"children_freed\":%llu,\"town_tables\":%llu,\"town\":%d,\"activated\":%d,\"fills\":%u,\"budgets\":%u,\"crows\":%u,\"eyes\":%u,\"eye_boxes\":%u,\"jars\":%u,\"width\":%d,\"height\":%d,\"zoom\":%.4f,\"zoom_target\":%.4f,\"room\":[%d,%d,%d,%d],\"room_masked\":%d,\"shown\":[%d,%d,%d,%d]}",
                             id, ready, expanded, camera_x, camera_y, scroll_x, scroll_y, committed, commit_ly, line0_scx, line0_scy, widened, visible.count, oam_checked, oam_matched,
                             bg_visible.count, background_checked, background_matched, map_checked, map_matched, slots, free, spawn_waits, encounter_waits, early_releases, doors_restored,
                             counts.moves, counts.layered, counts.drawn, counts.upgrades, counts.reaped,
                             counts.node_pools, counts.node_upgrades, counts.node_repairs, counts.slot_repairs,
                             counts.children_freed, counts.town_tables, s_ctx ? shantae_view_town(s_ctx) : 0,
                             s_ctx && s_ctx->wram ? activation_widened(s_ctx) : 0, fills, budget_reads, crows_restored, eye_reads, eye_boxes, jars_restored,
                             gb_custom_width, gb_custom_height, zoom, zoom_target, room.box.x0, room.box.y0,
                             room.box.x1, room.box.y1, room.masked, room_shown.x0, room_shown.y0,
                             room_shown.x1, room_shown.y1);
    for (int i = 0; i < visible.count; ++i) {
        Draw *d = &visible.draws[i];
        gb_debug_server_send_fmt("{\"id\":%d,\"ok\":true,\"index\":%d,\"x\":%d,\"y\":%d,\"bank\":%u,\"descriptor\":%u,\"graphics\":%u}",
                                 id, i, d->x, d->y, d->bank, d->descriptor, d->graphics);
    }
    return 1;
}
