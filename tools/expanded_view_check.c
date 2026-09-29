/* Host compositor regressions: no generated CPU or user save data needed. */
#include <stdlib.h>
#include <stdarg.h>
#include "../expanded_view.c"

int shantae_expanded_view(void) { return 1; }
int shantae_view_width(void) { return 256; }
int shantae_view_height(void) { return 240; }
int shantae_view_adaptive(void) { return 0; }
int shantae_native_scaling(void) { return GB_CUSTOM_NATIVE_SCALING_MODE; }
int shantae_native_scale(void) { return 1; }
static int s_room_zoom = SHANTAE_ROOM_ZOOM_FILL;
int shantae_room_zoom(void) { return s_room_zoom; }
void shantae_input_lag_counts(unsigned long long *fresh, unsigned long long *original,
                              unsigned long long *early_moves, unsigned long long *replays) {
    *fresh = *original = *early_moves = *replays = 0;
}
void gb_debug_server_send_fmt(const char *fmt, ...) { (void)fmt; }
/* What fill_dispatch asks of the runtime: the return it pushes, the ROM bank
 * it selects, the time it takes. */
static uint16_t pushed;
static int pushes;
static uint8_t bank_written;
static uint32_t ticked;
void gb_push16(GBContext *ctx, uint16_t value) { ctx->sp -= 2; pushed = value; ++pushes; }
void gb_write8(GBContext *ctx, uint16_t addr, uint8_t value) { (void)ctx; if (addr == 0x2000) bank_written = value; }
void gb_tick(GBContext *ctx, uint32_t cycles) { (void)ctx; ticked += cycles; }

/* object_slots.c runs on the generated CPU (tools/object_slots_check.c); here
 * a table of test_slots slots, those past DFFF in test_ext and those at A000
 * in test_cart. */
static int test_slots = SHANTAE_ORIGINAL_SLOTS;
static uint8_t test_ext[0x1e00], test_cart[0x2000];
int shantae_slot_count(const GBContext *ctx) { (void)ctx; return test_slots; }
int shantae_slot_field(const GBContext *ctx, unsigned addr) {
    (void)ctx;
    int echo = test_slots < SHANTAE_ECHO_SLOTS ? test_slots : SHANTAE_ECHO_SLOTS;
    if (addr >= SHANTAE_SLOT_BASE && addr < SHANTAE_SLOT_BASE + echo * SHANTAE_SLOT_SIZE)
        return (addr - SHANTAE_SLOT_BASE) % SHANTAE_SLOT_SIZE;
    if (addr >= SHANTAE_CART_BASE && addr < SHANTAE_CART_BASE + (test_slots - echo) * SHANTAE_SLOT_SIZE)
        return (addr - SHANTAE_CART_BASE) % SHANTAE_SLOT_SIZE;
    return -1;
}
int shantae_slot_index(const GBContext *ctx, unsigned addr) {
    if (shantae_slot_field(ctx, addr)) return -1;
    return addr >= SHANTAE_SLOT_BASE ? (int)(addr - SHANTAE_SLOT_BASE) / SHANTAE_SLOT_SIZE
                                     : SHANTAE_ECHO_SLOTS + (int)(addr - SHANTAE_CART_BASE) / SHANTAE_SLOT_SIZE;
}
uint8_t *shantae_slot_memory(GBContext *ctx, unsigned addr) {
    if (shantae_slot_field(ctx, addr) < 0) return NULL;
    if (addr < 0xc000) return test_cart + (addr - 0xa000);
    return addr < 0xe000 ? ctx->wram + 0x3000 + (addr - 0xd000) : test_ext + (addr - 0xe000);
}
void shantae_slots_read_tap(GBContext *ctx, uint16_t addr) { (void)ctx; (void)addr; }
uint8_t shantae_slots_read_override(GBContext *ctx, uint16_t addr, uint8_t value) {
    (void)ctx; (void)addr; return value;
}
void shantae_slots_counts(ShantaeSlotCounts *out) { memset(out, 0, sizeof(*out)); }

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
static void put16(uint8_t *p, unsigned value) { p[0] = value; p[1] = value >> 8; }
static void put_slot16(GBContext *ctx, int slot, unsigned offset, unsigned value) {
    unsigned addr = shantae_slot_addr(slot) + offset;
    *shantae_slot_memory(ctx, addr) = value;
    *shantae_slot_memory(ctx, addr + 1) = value >> 8;
}
/* The free list: the last `n` slots, in order; the rest are live. */
static void free_last(GBContext *ctx, int n) {
    put16(ctx->hram + 0x33, n ? shantae_slot_addr(test_slots - n) : 0);
    for (int i = 0; i < test_slots; ++i)
        put_slot16(ctx, i, 0x7c, i >= test_slots - n && i + 1 < test_slots ? shantae_slot_addr(i + 1) : 0);
}
/* The stone a totem's pedestal reads for `place`: 0C:4C4F LD C,(HL) and
 * 0C:4C51 LD B,(HL), 48 bytes on per place (generated PCs one past each). */
static unsigned totem_read(GBContext *ctx, int place) {
    unsigned stone = 0;
    for (int high = 0; high < 2; ++high) {
        const uint16_t addr = (uint16_t)(0xc004 + 2 * place + high);
        ctx->pc = (uint16_t)(0x4c50 + 0x30 * place + 2 * high);
        stone |= (unsigned)read_override(ctx, addr, ctx->wram[addr - 0xc000]) << (8 * high);
    }
    return stone;
}

int main(void) {
    GBContext *ctx = calloc(1, sizeof(*ctx));
    GBPPU *ppu = calloc(1, sizeof(*ppu));
    uint8_t *rom = calloc(1, 0x8000);
    CHECK(ctx && ppu && rom);
    ctx->wram = calloc(1, 0x8000); ctx->hram = calloc(1, 0x7f);
    CHECK(ctx->wram && ctx->hram);
    ctx->ppu = ppu; ctx->rom = rom; ctx->rom_size = 0x8000;
    put16(ctx->wram + 0x9dd, 8032); put16(ctx->wram + 0x9e1, 8048);

    /* Presets keep the height where the width limits allow, round the width
     * down, and are recognised again from the size alone. */
    int pw = 0, ph = 240;
    shantae_view_aspect_size(shantae_view_aspect_find("16:15"), &pw, &ph);
    CHECK(pw == 256 && ph == 240);
    shantae_view_aspect_size(shantae_view_aspect_find("16:9"), &pw, &ph);
    CHECK(pw == 426 && ph == 240 && shantae_view_aspect_of(426, 240) == shantae_view_aspect_find("16:9"));
    ph = 480; shantae_view_aspect_size(shantae_view_aspect_find("32:9"), &pw, &ph);
    CHECK(pw == 1706 && ph == 480);
    ph = 8192; shantae_view_aspect_size(shantae_view_aspect_find("32:9"), &pw, &ph);
    CHECK(pw == 8192 && ph == 2304);
    ph = 144; shantae_view_aspect_size(shantae_view_aspect_find("16:15"), &pw, &ph);
    CHECK(pw == 160 && ph == 150);
    CHECK(shantae_view_aspect_of(427, 240) < 0 && shantae_view_aspect_find("custom") < 0);
    for (int i = 0; i < shantae_view_aspect_count(); ++i)
        for (int h = 100; h <= 9000; h += h < 600 ? 1 : 97) {
            pw = 0; ph = h;
            shantae_view_aspect_size(i, &pw, &ph);
            CHECK(pw >= SHANTAE_VIEW_MIN_WIDTH && pw <= SHANTAE_VIEW_MAX_WIDTH);
            CHECK(ph >= SHANTAE_VIEW_MIN_HEIGHT && ph <= SHANTAE_VIEW_MAX_HEIGHT);
            CHECK(shantae_view_aspect_of(pw, ph) == i);
        }

    /* Adaptive views fill the window: with whole pixels at the largest scale
     * that keeps the least height, otherwise at exactly that height. */
    gb_custom_render = render;
    gb_custom_fill_size(1920, 1080, 240, 1, &pw, &ph); CHECK(pw == 480 && ph == 270);
    gb_custom_fill_size(2560, 1440, 240, 1, &pw, &ph); CHECK(pw == 426 && ph == 240);
    gb_custom_fill_size(3840, 2160, 240, 1, &pw, &ph); CHECK(pw == 426 && ph == 240);
    gb_custom_fill_size(3440, 1440, 360, 1, &pw, &ph); CHECK(pw == 860 && ph == 360);
    gb_custom_fill_size(1920, 1080, 240, 0, &pw, &ph); CHECK(pw == 427 && ph == 240);
    gb_custom_fill_size(2560, 1440, 2000, 1, &pw, &ph); CHECK(pw == 2560 && ph == 1440);
    gb_custom_fill_size(2560, 1440, 2000, 0, &pw, &ph); CHECK(pw == 3556 && ph == 2000);
    gb_custom_fill_size(100, 80, 240, 1, &pw, &ph); CHECK(pw == 160 && ph == 144);
    gb_custom_fill_size(0, 0, 240, 1, &pw, &ph); CHECK(pw == 426 && ph == 240);
    gb_custom_requested_width = -1; gb_custom_requested_height = 270;
    gb_custom_resolve_size(1920, 1080, 1, &pw, &ph); CHECK(pw == 1920 / 4 && ph == 270);
    gb_custom_requested_width = 20000; gb_custom_requested_height = 20000;
    gb_custom_resolve_size(1920, 1080, 1, &pw, &ph); CHECK(pw == 8192 && ph == 8192);
    gb_custom_requested_width = 0;
    gb_custom_resolve_size(1920, 1080, 1, &pw, &ph); CHECK(pw == 160 && ph == 144);
    gb_custom_render = NULL;

    /* Every sector intersecting the activation guard must be visited within
     * activation_passes, and the guard must cover that latency at 8 pixels a
     * frame across and 4 down. Views up to 704 x 480 keep four passes. */
    const int widths[] = {160, 256, 320, 426, 640, 704, 705, 853, 1024, 1920, 2560, 4096, 8192};
    const int heights[] = {144, 240, 270, 360, 480, 1080, 2160, 8192};
    for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w)
    for (unsigned h = 0; h < sizeof(heights) / sizeof(heights[0]); ++h) {
        gb_custom_width = widths[w];
        gb_custom_height = heights[h];
        int passes = activation_passes(widths[w], heights[h]), across, down;
        activation_guard(widths[w], heights[h], &across, &down);
        CHECK(across >= 8 * passes && down >= 4 * passes);
        CHECK(passes <= 4 || widths[w] > 704 || heights[h] > 480);
        CHECK(passes <= 17 * 17);
        for (int cx = 0; cx < 8192; cx += 13) {
            unsigned char visited[32][32] = {{0}};
            int cy = (cx * 7) % 8049;
            put16(ctx->hram + 0x61, cx); put16(ctx->hram + 0x63, cy);
            ctx->pc = 0x111b;
            for (int pass = 0; pass < passes; ++pass) {
                world_read_tap(ctx, 0xc9f8);
                visited[spawn_sectors[1]][spawn_sectors[0]] = 1;
                visited[spawn_sectors[1]][spawn_sectors[2]] = 1;
                visited[spawn_sectors[3]][spawn_sectors[0]] = 1;
                visited[spawn_sectors[3]][spawn_sectors[2]] = 1;
            }
            for (int y = spawn_bounds[1] / 256; y <= spawn_bounds[3] / 256; ++y)
                for (int x = spawn_bounds[0] / 256; x <= spawn_bounds[2] / 256; ++x) CHECK(visited[y][x]);
            /* Camera/physics readers must still see the real value. */
            ctx->pc = 0x4000;
            CHECK(read_override(ctx, 0xffc5, 0x57) == 0x57);
            CHECK(read_override(ctx, 0xffb5, 0x23) == 0x23);
            CHECK(word(ctx->hram + 0x61) == (unsigned)cx);
            CHECK(word(ctx->hram + 0x63) == (unsigned)cy);
        }
    }

    /* A background picture outside native view still has its absolute tile
     * coordinates, including both sides of the 32-tile wrapping boundary. */
    ctx->rom_bank = 1; ctx->wram_bank = 3; ctx->bc = 0xd000; ctx->pc = 0x4f19;
    ctx->wram[0x302e] = 1; put16(ctx->wram + 0x302f, 0x4100);
    put16(ctx->wram + 0x305b, 351); put16(ctx->wram + 0x305f, 55);
    rom[0x4100] = 3; rom[0x4101] = 2;
    for (int i = 0; i < 6; ++i) { rom[0x4102 + i*2] = 10+i; rom[0x4103+i*2] = 0x80+i; }
    bg_pending.count = 0;
    world_read_tap(ctx, 0xffbd);
    CHECK(bg_pending.count == 6);
    for (int i = 0; i < 6; ++i) {
        CHECK(bg_pending.tiles[i].x == 351+i%3 && bg_pending.tiles[i].y == 55+i/3);
        CHECK(bg_pending.tiles[i].tile == 10+i && bg_pending.tiles[i].attr == 0x80+i);
    }
    reset_view(ctx);
    CHECK(bg_pending.count == 0 && bg_visible.count == 0 && latched.count == 0);

    /* Shake just outside a room's camera bounds must keep composition active
     * and clip the native overlay without writing outside the destination. */
    memcpy(wram, ctx->wram, sizeof(wram));
    put16(wram + 0x9db, 100); put16(wram + 0x9dd, 1000);
    put16(wram + 0x9df, 100); put16(wram + 0x9e1, 1000);
    lcdc = ppu->lcdc = 0x81; ready = 1; gb_custom_height = 240;
    uint32_t native[160 * 144];
    for (int i = 0; i < 160*144; ++i) native[i] = 0xff123456;
    size_t count = 256 * 240;
    uint32_t *out = malloc((count + 2) * sizeof(*out));
    CHECK(out);
    out[0] = out[count+1] = 0xdecafbad;
    camera_x = camera_y = scroll_x = scroll_y = 96;
    CHECK(render(ctx, out+1, 256, native));
    CHECK(out[0] == 0xdecafbad && out[count+1] == 0xdecafbad);
    CHECK(out[1] == 0xff123456);

    /* A room smaller than the view is centered: Risky's ship in the opening
     * (camera x 32-63) is 192 wide; here 192x144 in 256x240. */
    put16(wram + 0x9dd, 132); put16(wram + 0x9e1, 100);
    camera_x = scroll_x = 132; camera_y = scroll_y = 100;
    CHECK(render(ctx, out+1, 256, native));
    CHECK(out[1 + 48*256 + 64] == 0xff123456 && out[1 + 48*256 + 63] != 0xff123456);
    CHECK(out[1 + 191*256 + 223] == 0xff123456 && out[1 + 191*256 + 224] != 0xff123456);
    CHECK(out[1 + 47*256 + 100] != 0xff123456 && out[1 + 192*256 + 100] != 0xff123456);
    /* A camera shaking 2 pixels past the room's least x (the rumble in the
     * first arena of 25:401F) keeps the native picture inside the room. */
    camera_x = scroll_x = 98;
    CHECK(render(ctx, out+1, 256, native));
    CHECK(out[1 + 100*256 + 31] == 0xff000000 && out[1 + 100*256 + 32] == 0xff123456);
    CHECK(out[1 + 100*256 + 189] == 0xff123456 && out[1 + 100*256 + 190] != 0xff123456);
    camera_x = scroll_x = 132;
    /* Sprites past the room's edge (output x 32) are off the original screen too. */
    rom[0x7000 + 32] = 1; rom[0x7000 + 35] = 16; rom[0x7000 + 36] = 8;
    memset(rom + 0x7101, 0xff, 32);
    put16(ppu->obj_palette_ram + 6, 0x03e0);
    frame_end(ctx);
    visible.count = 2;
    visible.draws[0] = (Draw){76, 112, 1, 0x7000, 0x7100};
    visible.draws[1] = (Draw){108, 112, 1, 0x7000, 0x7100};
    CHECK(render(ctx, out+1, 256, native));
    CHECK(out[1 + 60*256 + 8] != 0xff00ff00 && out[1 + 60*256 + 40] == 0xff00ff00);
    visible.count = 0;
    put16(wram + 0x9dd, 1000); put16(wram + 0x9e1, 1000);
    camera_x = camera_y = scroll_x = scroll_y = 96;

    /* Each size writes exactly its own frame, with the status bar centered on
     * the bottom rows and the world directly above it. */
    const int sizes[][2] = {{160, 144}, {256, 240}, {426, 270}, {1024, 288}, {853, 480}, {1024, 480}, {1706, 480}, {3840, 2160}, {8192, 1024}};
    for (int i = 128 * 160; i < 144 * 160; ++i) native[i] = 0xff00aa00;
    lcdc = ppu->lcdc = 0xa1; window_x = 0; window_y = 128;
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        int w = sizes[s][0], h = sizes[s][1];
        uint32_t *frame = malloc(((size_t)w * h + 2) * sizeof(*frame));
        CHECK(frame);
        frame[0] = frame[(size_t)w * h + 1] = 0xdecafbad;
        gb_custom_height = h;
        CHECK(render(ctx, frame + 1, w, native));
        CHECK(frame[0] == 0xdecafbad && frame[(size_t)w * h + 1] == 0xdecafbad);
        int hud_left = (w - 160) / 2;
        CHECK(frame[1 + (size_t)(h - 16) * w + hud_left] == 0xff00aa00);
        CHECK(frame[1 + (size_t)(h - 1) * w + hud_left + 159] == 0xff00aa00);
        CHECK(frame[1 + (size_t)(h - 17) * w + hud_left] != 0xff00aa00);
        free(frame);
    }
    /* The intro's strip (a window from line 136) stays at the bottom and its
     * background spans the view, with what crosses it kept in the middle;
     * without a color covering most of it, black is beside it. */
    window_y = 136; gb_custom_height = 240;
    for (int i = 136 * 160; i < 144 * 160; ++i) native[i] = 0xff000041;
    for (int x = 8; x < 24; ++x) native[139 * 160 + x] = 0xffffffff;
    CHECK(render(ctx, out + 1, 256, native));
    CHECK(out[1 + 232 * 256] == 0xff000041 && out[1 + 239 * 256 + 255] == 0xff000041);
    CHECK(out[1 + 235 * 256 + 48 + 8] == 0xffffffff && out[1 + 231 * 256 + 128] != 0xff000041);
    for (int i = 136 * 160; i < 144 * 160; ++i) native[i] = 0xff000000u | (i % 3) * 0x40;
    CHECK(render(ctx, out + 1, 256, native));
    CHECK(out[1 + 232 * 256] == 0xff000000 && out[1 + 232 * 256 + 48] == 0xff000040);
    for (int i = 128 * 160; i < 144 * 160; ++i) native[i] = 0xff123456;
    lcdc = ppu->lcdc = 0x81; window_x = window_y = 0; gb_custom_height = 240;

    /* A declined frame is drawn by nobody but the native fallback, which
     * centers the picture (the platform may present it on its own instead). */
    gb_custom_render = NULL;
    CHECK(!gb_custom_draw_frame(ctx, out + 1, 256, native));
    CHECK(out[0] == 0xdecafbad && out[count + 1] == 0xdecafbad);
    CHECK(!gb_custom_compose_frame(ctx, out+1, 256, native, 160));
    CHECK(out[1] == 0xff000000);
    CHECK(out[1 + 48*256 + 48] == 0xff123456);
    CHECK(out[1 + 191*256 + 207] == 0xff123456);
    CHECK(out[0] == 0xdecafbad && out[count+1] == 0xdecafbad);

    /* Menus keep the map pointers while replacing the tilemap (the inventory
     * also zeroes the camera). Expand only while hardware draws the map. */
    ctx->vram = calloc(1, 0x4000); ctx->oam = calloc(1, 160);
    CHECK(ctx->vram && ctx->oam);
    memset(rom + 0x4000, 0, 0x4000);
    for (int i = 0; i < 0x800; i += 2) { rom[0x4000 + i] = 0x50; rom[0x4001 + i] = 1; }
    for (int i = 0; i < 0x200; i += 2) { rom[0x5000 + i] = (i / 2 % 4) << 4; rom[0x5001 + i] = 0x60; }
    for (int k = 0; k < 4; ++k)
        for (int q = 0; q < 4; ++q) { rom[0x6000 + k*16 + q] = 0x10 + k*4 + q; rom[0x6004 + k*16 + q] = k; }
    ctx->wram[0x9f9] = 0x40; ctx->wram[0x9fa] = 1; ctx->hram[0x5f] = 1;
    put16(ctx->wram + 0x9fb, 516); put16(ctx->wram + 0x9fd, 264);
    memcpy(wram, ctx->wram, sizeof(wram)); memcpy(hram, ctx->hram, sizeof(hram));
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 96; ++x) {
        unsigned tile, attr, at = 0x1800 + (y & 31) * 32 + (x & 31);
        if (x * 8 < 512 || y * 8 < 256 || !world_tile(ctx, x * 8, y * 8, &tile, &attr)) continue;
        ctx->vram[at] = tile; ctx->vram[at + 0x2000] = attr;
    }
    ppu->lcdc = 0x81; ppu->wy = 144; ppu->wx = 7;
    reset_view(ctx);
    snapshot(ctx);
    CHECK(ready && map_checked == 19 * 18 && map_matched == map_checked);

    /* Activation widens only in a room already presented expanded. A window
     * above the status bar (a dialogue box from line 112) is not presented. */
    put16(ctx->hram + 0x61, 516); put16(ctx->hram + 0x63, 264);
    reset_view(ctx);
    lcdc = ppu->lcdc = 0xa1; ppu->wx = 7; ppu->wy = 112;
    snapshot(ctx);
    CHECK(ready && !widened);
    ctx->pc = 0x11d7; CHECK(read_override(ctx, 0xffb7, 0x12) == 0x12);
    ctx->pc = 0x0fd0; CHECK(read_override(ctx, 0xffc5, 0x34) == 0x34);
    ctx->pc = 0x1130; CHECK(read_override(ctx, 0xffc6, 0x02) == 0x02);
    ppu->lcdc = 0x81; ppu->wy = 144;
    snapshot(ctx);
    CHECK(ready && widened);
    ctx->pc = 0x11d7; CHECK(read_override(ctx, 0xffb7, 0x12) != 0x12);
    /* A dialogue box in the same room keeps the wider activation. */
    ppu->lcdc = 0xa1; ppu->wy = 112;
    snapshot(ctx);
    CHECK(widened && read_override(ctx, 0xffb7, 0x12) != 0x12);
    /* New camera bounds in the same map keep it; another map starts native
     * again, even before the next snapshot sees it. */
    put16(ctx->wram + 0x9dd, 8000);
    CHECK(read_override(ctx, 0xffb7, 0x12) != 0x12);
    put16(ctx->wram + 0x9dd, 8032);
    ctx->wram[0x9f8] = 0x10;
    CHECK(read_override(ctx, 0xffb7, 0x12) == 0x12);
    ctx->wram[0x9f8] = 0;
    ppu->lcdc = 0x81; ppu->wy = 144;

    /* A lag frame: the VBlank handler has not copied C9FB/C9FD to SCX/SCY
     * (00:0A57, 00:0A5C) while the main loop has already moved them on. The
     * surround keeps the camera the hardware is still drawing with. */
    ctx->pc = 0x0a5a; read_tap(ctx, 0xc9fb);
    ctx->pc = 0x0a5f; read_tap(ctx, 0xc9fd);
    put16(ctx->wram + 0x9fb, 517); put16(ctx->wram + 0x9fd, 265);
    snapshot(ctx);
    CHECK(committed && camera_x == 516 && camera_y == 264 && ready);
    reset_view(ctx);
    snapshot(ctx);
    CHECK(camera_x == 517 && camera_y == 265);
    put16(ctx->wram + 0x9fb, 516); put16(ctx->wram + 0x9fd, 264);

    /* Bosses drawn in the background move by C9D2/C9D4, which the camera
     * routine (03:724D) adds to the scroll only. Sprites and the native picture
     * follow the camera; the background composition depends only on the scroll. */
    uint32_t *plain = malloc(count * sizeof(*plain));
    CHECK(plain);
    rom[0x7000 + 32] = 1; rom[0x7000 + 35] = 16; rom[0x7000 + 36] = 8;
    memset(rom + 0x7101, 0xff, 32);
    frame_end(ctx);
    for (int offset = 0; offset < 2; ++offset) {
        put16(ctx->wram + 0x9d2, offset ? 24 : 0); put16(ctx->wram + 0x9d4, offset ? -8 : 0);
        put16(ctx->wram + 0x9fb, 540); put16(ctx->wram + 0x9fd, 256);
        ctx->rom_bank = 3;
        ctx->pc = 0x725b; read_tap(ctx, 0xc9d2);
        ctx->pc = 0x726b; read_tap(ctx, 0xc9d4);
        put16(ctx->wram + 0x9d2, 99);   /* later changes wait for the next scroll */
        ctx->pc = 0x0a5a; read_tap(ctx, 0xc9fb);
        ctx->pc = 0x0a5f; read_tap(ctx, 0xc9fd);
        latched.count = 1;
        latched.draws[0] = (Draw){500, 300, 1, 0x7000, 0x7100};
        snapshot(ctx);
        CHECK(ready && scroll_x == 540 && scroll_y == 256);
        CHECK(camera_x == (offset ? 516 : 540) && camera_y == (offset ? 264 : 256));
        CHECK(render(ctx, out + 1, 256, native));
        int sx = 500 - camera_x + 48, sy = 300 - camera_y + 48;
        CHECK(out[1 + sy * 256 + sx] == 0xff00ff00 && out[1 + (sy + 15) * 256 + sx + 7] == 0xff00ff00);
        CHECK(out[1 + 48 * 256 + 48] == 0xff123456 && out[1 + 191 * 256 + 207] == 0xff123456);
        visible.count = 0;
        CHECK(render(ctx, out + 1, 256, native));
        if (!offset) memcpy(plain, out + 1, count * sizeof(*plain));
        else CHECK(!memcmp(plain, out + 1, count * sizeof(*plain)));
    }
    free(plain);

    /* A room pinned to one screen (the first boss's arena) shows the native
     * picture and keeps native activation, although this map was expanded. */
    CHECK(widened);
    put16(ctx->wram + 0x9db, 540); put16(ctx->wram + 0x9dd, 540);
    put16(ctx->wram + 0x9df, 256); put16(ctx->wram + 0x9e1, 256);
    snapshot(ctx);
    CHECK(!ready && map_checked && map_matched == map_checked);
    ctx->pc = 0x11d7; CHECK(read_override(ctx, 0xffb7, 0x12) == 0x12);
    put16(ctx->wram + 0x9dd, 8032); put16(ctx->wram + 0x9e1, 8048);
    CHECK(read_override(ctx, 0xffb7, 0x12) != 0x12);

    /* Towns (map 67:9B, camera x 0-480, y pinned) keep the original picture
     * and activation, with no window up (the building-name panel is missing
     * for two frames after a state load) and after the map was presented
     * expanded (the shops share it). The shops' bounds widen as before. The
     * test map's directory is copied to 67:9B, so the town is one the view
     * would otherwise present. */
    rom = realloc(rom, 0x9c * 0x4000);
    CHECK(rom);
    memset(rom + 0x8000, 0, 0x9a * 0x4000);
    memcpy(rom + 0x9b * 0x4000 + 0x2700, rom + 0x4000, 0x800);
    ctx->rom = rom; ctx->rom_size = 0x9c * 0x4000;
    put16(ctx->wram + 0x9d2, 0); put16(ctx->wram + 0x9d4, 0);
    put16(ctx->wram + 0x9fb, 516); put16(ctx->wram + 0x9fd, 264);
    put16(ctx->hram + 0x61, 516); put16(ctx->hram + 0x63, 264);
    ppu->lcdc = 0x81; ppu->wy = 144;
    reset_view(ctx);
    ctx->wram[0x9f9] = 0x67; ctx->wram[0x9fa] = 0x9b;
    put16(ctx->wram + 0x9db, 400); put16(ctx->wram + 0x9dd, 831);
    put16(ctx->wram + 0x9df, 264); put16(ctx->wram + 0x9e1, 264);
    snapshot(ctx);
    CHECK(ready && widened);
    ctx->pc = 0x11d7; CHECK(read_override(ctx, 0xffb7, 0x12) != 0x12);
    /* Leaving the shop for the town keeps the map (and `widened`). */
    put16(ctx->wram + 0x9db, 0); put16(ctx->wram + 0x9dd, 480);
    CHECK(widened && !memcmp(widened_room, ctx->wram + 0x9f8, sizeof(widened_room)));
    ctx->pc = 0x11d7; CHECK(read_override(ctx, 0xffb7, 0x12) == 0x12);
    ctx->pc = 0x0fd0; CHECK(read_override(ctx, 0xffc5, 0x34) == 0x34);
    ctx->pc = 0x1130; CHECK(read_override(ctx, 0xffc6, 0x02) == 0x02);
    snapshot(ctx);
    CHECK(!ready && widened && map_checked == 0);
    CHECK(!render(ctx, out + 1, 256, native));
    /* The town engine's camera passes C9DD on its way round (604 in Water
     * Town, wrapping at 640): the same town. */
    put16(ctx->wram + 0x9fb, 604); put16(ctx->hram + 0x61, 604);
    snapshot(ctx);
    CHECK(!ready);
    ctx->pc = 0x11d7; CHECK(read_override(ctx, 0xffb7, 0x12) == 0x12);
    /* Only those bounds: a room of that map pinned otherwise is not a town. */
    put16(ctx->wram + 0x9fb, 516); put16(ctx->hram + 0x61, 516);
    put16(ctx->wram + 0x9dd, 481);
    snapshot(ctx);
    CHECK(ready);
    ctx->pc = 0x11d7; CHECK(read_override(ctx, 0xffb7, 0x12) != 0x12);
    ctx->rom_size = 0x8000;
    ctx->wram[0x9f9] = 0x40; ctx->wram[0x9fa] = 1;
    put16(ctx->wram + 0x9dd, 8032); put16(ctx->wram + 0x9e1, 8048);
    put16(ctx->wram + 0x9db, 0); put16(ctx->wram + 0x9df, 0);
    put16(ctx->wram + 0x9d2, 0); put16(ctx->wram + 0x9d4, 0);
    put16(ctx->wram + 0x9fb, 516); put16(ctx->wram + 0x9fd, 264);
    put16(ppu->obj_palette_ram + 6, 0);
    ctx->rom_bank = 1;
    reset_view(ctx);

    /* A fade's palettes reach the top native lines late (01:5E0B). Those rows
     * take the settled colors; later rows and other colors stay native. */
    for (int i = 0; i < 64; i += 2) put16(ppu->bg_palette_ram + i, 0x001f);
    snapshot(ctx);
    for (int i = 0; i < 64; i += 2) put16(ppu->bg_palette_ram + i, 0x7c00);
    frame_end(ctx);
    for (int i = 0; i < 160*144; ++i) native[i] = 0xffff0000;
    native[5] = 0xff00ff00;
    CHECK(render(ctx, out+1, 256, native));
    CHECK(out[1 + 48*256 + 48] == 0xff0000ff && out[1 + 71*256 + 60] == 0xff0000ff);
    CHECK(out[1 + 48*256 + 53] == 0xff00ff00);
    CHECK(out[1 + 72*256 + 48] == 0xffff0000 && out[1 + 191*256 + 207] == 0xffff0000);
    CHECK(out[1 + 30*256 + 48] == 0xff0000ff);
    /* Most of the view covered by live background objects still agrees. */
    for (int y = 33; y < 51; ++y) for (int x = 65; x < 80; ++x) {
        BackgroundTile *t = &bg_latched.tiles[bg_latched.count++];
        t->x = x; t->y = y; t->tile = 0xe0 + x % 8; t->attr = 7;
        ctx->vram[0x1800 + (y & 31) * 32 + (x & 31)] = t->tile; ctx->vram[0x3800 + (y & 31) * 32 + (x & 31)] = 7;
    }
    snapshot(ctx);
    CHECK(ready && map_matched == map_checked);
    for (int i = 0; i < 0x400; ++i) { ctx->vram[0x1800 + i] = 0x80 + i % 20; ctx->vram[0x3800 + i] = 0x0c; }
    snapshot(ctx);
    CHECK(!ready);
    put16(ctx->wram + 0x9fb, 0); put16(ctx->wram + 0x9fd, 0);
    snapshot(ctx);
    CHECK(!ready && map_checked == 20 * 18 && map_matched == 0);
    free(ctx->vram); free(ctx->oam);

    /* Object slots. With none free, a record waits instead of being lost, and
     * a door warp flagged with no object spawns again. The table has 32 slots
     * without the view and 157 with it (93 in states from the build before):
     * slot 32 starts in bank 3 at DFC0 and runs on past DFFF, slot 93 starts at
     * A000. */
    gb_custom_width = 1920; gb_custom_height = 527;
    widened = 1; memcpy(widened_room, ctx->wram + 0x9f8, sizeof(widened_room));
    uint8_t marks[SHANTAE_MAX_SLOTS];
    const int table_sizes[] = {SHANTAE_ORIGINAL_SLOTS, SHANTAE_ECHO_SLOTS, SHANTAE_MAX_SLOTS};
    for (int t = 0; t < 3; ++t) {
        test_slots = table_sizes[t];
        memset(ctx->wram + 0x3000, 0, 0x1000); memset(test_ext, 0, sizeof(test_ext)); memset(test_cart, 0, sizeof(test_cart));
        free_last(ctx, 0); CHECK(free_slots(ctx, marks) == 0);
        free_last(ctx, 7);
        CHECK(free_slots(ctx, marks) == 7 && marks[test_slots - 1] && marks[test_slots - 7] && !marks[test_slots - 8]);
        free_last(ctx, test_slots - 31);
        CHECK(free_slots(ctx, marks) == test_slots - 31 && marks[31] && !marks[30]);
        /* 00:1054 reads the flag (HL) of a record (DE at its script); with C39C
         * clear, every record is inside the original's bounds. */
        ctx->pc = 0x1055; ctx->hl = 0xd2fc; ctx->de = 0x7800; ctx->rom_bank = 1;
        rom[0x7800] = 0x24; rom[0x7801] = 0x40; rom[0x7802] = 0x0a;
        free_last(ctx, 0); CHECK(read_override(ctx, 0xd2fc, 0) == 1);   /* waits instead of being lost */
        free_last(ctx, 1); CHECK(read_override(ctx, 0xd2fc, 0) == 0);
        CHECK(read_override(ctx, 0xd2fc, 1) == 1 && read_override(ctx, 0xd2fc, 2) == 2);
        /* A door warp flagged with no object spawns again; one that is held does not. */
        rom[0x7800] = 0x48;
        unsigned restored = doors_restored;
        CHECK(read_override(ctx, 0xd2fc, 1) == 0 && doors_restored == restored + 1);
        CHECK(read_override(ctx, 0xd2fc, 2) == 2);
        const int holders[] = {test_slots - 2, test_slots > 32 ? 32 : 20, test_slots > SHANTAE_ECHO_SLOTS ? 100 : 5};
        for (int h = 0; h < 3; ++h) {
            put_slot16(ctx, holders[h], 0x27, 0xd2fc);
            CHECK(read_override(ctx, 0xd2fc, 1) == 1);
            put_slot16(ctx, holders[h], 0x27, 0);
        }
        free_last(ctx, 0); CHECK(read_override(ctx, 0xd2fc, 1) == 1);
        rom[0x7800] = 0x24;
        /* Native activation keeps the original behavior. */
        widened = 0; CHECK(read_override(ctx, 0xd2fc, 0) == 0); widened = 1;
    }
    CHECK(spawn_waits == 6 && doors_restored == 3);

    /* Filling: the first widened scan in a room queues every sector of the
     * area but the call's own batch, and 00:1143 scans them one by one as
     * 00:1122-1140 would (HL = the directory entry, bank C39B, back to 1143).
     * A camera moving at the guard's pace does not fill; a cut does; native
     * activation forgets the area, so widening again fills. */
    {
        uint8_t saved_map[3], saved_room[8], saved_cam[4], saved_39a[2];
        memcpy(saved_map, ctx->wram + 0x9f8, 3);
        memcpy(saved_room, ctx->wram + 0x9db, 8);
        memcpy(saved_cam, ctx->hram + 0x61, 4);
        memcpy(saved_39a, ctx->wram + 0x39a, 2);
        const int saved_w = gb_custom_width, saved_h = gb_custom_height;
        const uint16_t saved_hl = ctx->hl, saved_sp = ctx->sp;
        const uint8_t saved_ff91 = ctx->hram[0x11];
        ctx->wram[0x9f8] = 0; ctx->wram[0x9f9] = 0x12; ctx->wram[0x9fa] = 0x34;   /* no pictures to keep to */
        memcpy(widened_room, ctx->wram + 0x9f8, sizeof(widened_room));
        put16(ctx->wram + 0x9db, 0); put16(ctx->wram + 0x9dd, 4000);
        put16(ctx->wram + 0x9df, 0); put16(ctx->wram + 0x9e1, 1000);
        ctx->wram[0x39a] = 0x50; ctx->wram[0x39b] = 0x21;
        gb_custom_width = 1920; gb_custom_height = 1080;
        put16(ctx->hram + 0x61, 1000); put16(ctx->hram + 0x63, 300);
        fill_valid = 0;
        const unsigned before = fills;
        ctx->pc = 0x111b; world_read_tap(ctx, 0xc9f8);
        const int area = ((spawn_bounds[2] >> 8) - (spawn_bounds[0] >> 8) + 1) *
                         ((spawn_bounds[3] >> 8) - (spawn_bounds[1] >> 8) + 1);
        const int batch = (spawn_sectors[2] - spawn_sectors[0] + 1) * (spawn_sectors[3] - spawn_sectors[1] + 1);
        CHECK(activation_widened(ctx) && fill_count == area - batch && fills == before + 1 && area > 20);
        ctx->sp = 0xcff0; ticked = 0;
        for (int i = 0; i < area - batch; ++i) {
            const unsigned x = fill_sectors[i][0], y = fill_sectors[i][1];
            pushes = 0;
            CHECK(fill_dispatch(ctx, 0x1143) && ctx->pc == 0x0fa9 && pushes == 1 && pushed == 0x1143);
            CHECK(bank_written == 0x21 && ctx->hram[0x11] == 0x21);
            CHECK(ctx->hl == (((0x50 + (y >> 1)) << 8) | (((y & 1) << 7) + x * 4)));
            CHECK(!(x >= (unsigned)spawn_sectors[0] && x <= (unsigned)spawn_sectors[2] &&
                    y >= (unsigned)spawn_sectors[1] && y <= (unsigned)spawn_sectors[3]));
            ctx->sp += 2;   /* 00:0FA9's RET */
        }
        CHECK(!fill_dispatch(ctx, 0x1143) && ctx->sp == 0xcff0 && ticked == 184u * (unsigned)(area - batch));
        ctx->pc = 0x111b; world_read_tap(ctx, 0xc9f8);
        CHECK(fill_count == 0 && !fill_dispatch(ctx, 0x1143));   /* the same place */
        put16(ctx->hram + 0x61, 1008); put16(ctx->hram + 0x63, 304);
        ctx->pc = 0x111b; world_read_tap(ctx, 0xc9f8);
        CHECK(fill_count == 0);                                   /* at the guard's pace */
        put16(ctx->hram + 0x61, 1400);
        ctx->pc = 0x111b; world_read_tap(ctx, 0xc9f8);
        CHECK(fill_count > 0 && fills == before + 2);             /* a cut */
        widened = 0; ctx->pc = 0x111b; world_read_tap(ctx, 0xc9f8);
        CHECK(fill_count == 0 && !fill_valid);
        widened = 1; ctx->pc = 0x111b; world_read_tap(ctx, 0xc9f8);
        CHECK(fill_count > 0 && fills == before + 3);
        /* The picture easing out to a larger one: activation takes that at once. */
        gb_custom_width = 480; gb_custom_height = 270; ease_width = 1920; ease_height = 1080;
        int aw, ah;
        activation_size(&aw, &ah);
        CHECK(aw == 1920 && ah == 1080);
        ease_width = ease_height = 0;
        activation_size(&aw, &ah);
        CHECK(aw == 480 && ah == 270);
        fill_count = fill_next = 0;
        memcpy(ctx->wram + 0x9f8, saved_map, 3);
        memcpy(widened_room, saved_map, sizeof(widened_room));
        memcpy(ctx->wram + 0x9db, saved_room, 8);
        memcpy(ctx->hram + 0x61, saved_cam, 4);
        memcpy(ctx->wram + 0x39a, saved_39a, 2);
        gb_custom_width = saved_w; gb_custom_height = saved_h;
        ctx->hl = saved_hl; ctx->sp = saved_sp; ctx->hram[0x11] = saved_ff91;
        fill_valid = 0;
    }

    /* The reserve, with 157 slots. Camera 1000,1000 at 1920x527: the view
     * shows x 120-2039 and y 809-1319; the original spawn bounds are 968-1192
     * x 984-1160, retention 920-1240 x 928-1216. */
    memset(ctx->wram + 0x3000, 0, 0x1000); memset(test_ext, 0, sizeof(test_ext)); memset(test_cart, 0, sizeof(test_cart));
    ctx->wram[0x39c] = 3;
    put16(ctx->hram + 0x61, 1000); put16(ctx->hram + 0x63, 1000);
    put16(ctx->hram + 0x45, 968); put16(ctx->hram + 0x49, 1192);
    put16(ctx->hram + 0x47, 984); put16(ctx->hram + 0x4b, 1160);
    put16(ctx->hram + 0x35, 920); put16(ctx->hram + 0x37, 1240);
    put16(ctx->hram + 0x39, 928); put16(ctx->hram + 0x3b, 1216);
    #define RECORD(x, y) do { put16(ctx->hram + 0x1b, x); put16(ctx->hram + 0x1f, (x) + 15); \
                              put16(ctx->hram + 0x1d, y); put16(ctx->hram + 0x21, (y) + 40); } while (0)
    ctx->pc = 0x1055;
    RECORD(1100, 1000);
    free_last(ctx, 5); CHECK(read_override(ctx, 0xd2fc, 0) == 0);        /* the original's record spawns */
    RECORD(2000, 1000);
    CHECK(read_override(ctx, 0xd2fc, 0) == 1);                           /* one only the view reaches waits */
    free_last(ctx, SLOT_RESERVE); CHECK(read_override(ctx, 0xd2fc, 0) == 1);
    free_last(ctx, SLOT_RESERVE + 1); CHECK(read_override(ctx, 0xd2fc, 0) == 0);
    ctx->wram[0x39c] = 2; RECORD(3000, 1000);                            /* x untested in this map */
    free_last(ctx, 5); CHECK(read_override(ctx, 0xd2fc, 0) == 0);
    ctx->wram[0x39c] = 3;
    /* Encounters are listed by object: a record of the water tower's mini-boss
     * drop (0A:4070) keeps the original bounds, while Mimic (0A:42D4), the
     * only one in Scuttle Town, and an ordinary object (0A:4024) spawn with
     * the view. */
    const unsigned records[] = {0x7a01, 0x7a12, 0x7a23};
    const uint8_t types[][3] = {{0x24, 0x40, 0x0a}, {0xd4, 0x42, 0x0a}, {0x70, 0x40, 0x0a}};
    for (int r = 0; r < 3; ++r) { put16(rom + records[r] + 8, 0xd100 + 4 * r); memcpy(rom + records[r] + 10, types[r], 3); }
    free_last(ctx, 40); RECORD(2000, 1000);
    ctx->pc = 0x1055; ctx->de = records[0] + 10; CHECK(read_override(ctx, 0xd2fc, 0) == 0);
    ctx->de = records[1] + 10; CHECK(read_override(ctx, 0xd2fc, 0) == 0 && encounter_waits == 0);
    ctx->de = records[2] + 10; CHECK(read_override(ctx, 0xd2fc, 0) == 1 && encounter_waits == 1);
    RECORD(1100, 1000); CHECK(read_override(ctx, 0xd2fc, 0) == 0);
    /* Retention hands it the original bounds too, however many are free;
     * Mimic keeps the view's. */
    const int holders[] = {130, 131, 132};
    for (int r = 0; r < 3; ++r) {
        *shantae_slot_memory(ctx, shantae_slot_addr(holders[r]) + 0x24) = 1;
        put_slot16(ctx, holders[r], 0x25, records[r]);
    }
    ctx->pc = 0x11d8; ctx->bc = shantae_slot_addr(132); CHECK(read_override(ctx, 0xffb7, 0xd8) == 0xd8);
    ctx->bc = shantae_slot_addr(131); CHECK(read_override(ctx, 0xffb7, 0xd8) != 0xd8);
    ctx->bc = shantae_slot_addr(130); CHECK(read_override(ctx, 0xffb7, 0xd8) != 0xd8);
    ctx->de = 0x7800;
    /* Retention: objects kept by the original bounds (x 1100), released by
     * them on screen (1500, 1800) and off it (2100, 2400, 2700, and 2600 in a
     * slot at A000), and one that never ran retention (5000). */
    const int movers[] = {40, 41, 42, 43, 44, 45, 120, 46};
    const int xs[] = {1100, 1500, 1800, 2100, 2400, 2700, 2600, 5000};
    for (int i = 0; i < 8; ++i) {
        put_slot16(ctx, movers[i], 0x34, xs[i]); put_slot16(ctx, movers[i], 0x37, 1000);
        *shantae_slot_memory(ctx, shantae_slot_addr(movers[i]) + 0x57) = 16;
        *shantae_slot_memory(ctx, shantae_slot_addr(movers[i]) + 0x58) = 32;
    }
    #define NEXT_FRAME() do { ctx->completed_frames++; ctx->pc = 0x11d8; \
        for (int i = 0; i < 7; ++i) { ctx->bc = shantae_slot_addr(movers[i]); world_read_tap(ctx, 0xffb7); } } while (0)
    /* The view's right side, its guard (80 at 1920x527) and the original's 72. */
    #define WIDE_MAX_X_LO ((uint8_t)(1000 - (1920 - 160) / 2 + 1920 + 80 + 72))
    #define KEEPS_WIDE(i) (ctx->pc = 0x11d8, ctx->bc = shantae_slot_addr(movers[i]), \
                           read_override(ctx, 0xffb7, 0xd8) == WIDE_MAX_X_LO)
    CHECK(WIDE_MAX_X_LO != 0xd8);
    NEXT_FRAME(); free_last(ctx, SLOT_RESERVE);
    for (int i = 0; i < 8; ++i) CHECK(KEEPS_WIDE(i));
    /* Retention keeps what the spawner reaches (the view and its guard) with
     * the original's slack past it, from its 8-pixel spawn margin to 80
     * across and 72 down. Kept to the view plus 80 and 72, what spawned by the
     * guard went the next frame: Sky's crow, made by its door (record box
     * 880-911 x 880-919) as the door spawns at 890,890 12x16, went while the
     * door stayed. */
    {
        const Box spawn = spawn_box(ctx);
        int keep[4];
        for (int i = 0; i < 4; ++i) {
            ctx->pc = 0x11d8; ctx->bc = shantae_slot_addr(movers[0]);
            keep[i] = (int16_t)(read_override(ctx, 0xffb5 + 2 * i, 0) | read_override(ctx, 0xffb6 + 2 * i, 0) << 8);
        }
        CHECK(spawn.x0 == 1000 - (1920 - 160) / 2 - 80 && spawn.x1 == 1000 - (1920 - 160) / 2 + 1920 + 80);
        CHECK(keep[0] == spawn.x0 - 72 && keep[1] == spawn.x1 + 72);
        CHECK(keep[2] == spawn.y0 - 64 && keep[3] == spawn.y1 + 64);
        /* A door whose record box the spawn rectangle's left side just reaches,
         * and its crow: retention keeps both. */
        const int door_x0 = spawn.x0 - 31, crow_x0 = door_x0 + 10, crow_x1 = crow_x0 + 12;
        CHECK(door_x0 + 31 >= spawn.x0 && crow_x1 > keep[0]);
    }
    /* Three short of the reserve: the three farthest off screen go. */
    NEXT_FRAME(); free_last(ctx, SLOT_RESERVE - 3);
    CHECK(KEEPS_WIDE(0) && KEEPS_WIDE(1) && KEEPS_WIDE(2) && KEEPS_WIDE(3));
    CHECK(!KEEPS_WIDE(4) && !KEEPS_WIDE(5) && !KEEPS_WIDE(6));
    CHECK(!KEEPS_WIDE(7));   /* running retention itself, the farthest goes too */
    /* The answer holds for the frame. */
    free_last(ctx, SLOT_RESERVE); CHECK(!KEEPS_WIDE(5));
    /* Below the floor with enough off screen, nothing on screen goes. */
    NEXT_FRAME(); free_last(ctx, SLOT_FLOOR - 2);
    CHECK(KEEPS_WIDE(0) && KEEPS_WIDE(1) && KEEPS_WIDE(2));
    CHECK(!KEEPS_WIDE(3) && !KEEPS_WIDE(4) && !KEEPS_WIDE(5) && !KEEPS_WIDE(6));
    /* With nothing off screen to release, the farthest on screen go. */
    for (int i = 3; i < 7; ++i) put_slot16(ctx, movers[i], 0x34, 1100);
    NEXT_FRAME(); free_last(ctx, SLOT_FLOOR - 1);
    CHECK(KEEPS_WIDE(0) && KEEPS_WIDE(1) && !KEEPS_WIDE(2));
    NEXT_FRAME(); free_last(ctx, SLOT_FLOOR - 2);
    CHECK(!KEEPS_WIDE(1) && !KEEPS_WIDE(2));
    /* 00:1229 finds the object under its saved registers. */
    NEXT_FRAME(); free_last(ctx, SLOT_FLOOR - 2);
    ctx->sp = 0xc800; put16(ctx->wram + 0x804, shantae_slot_addr(movers[2])); ctx->bc = 0x4321; ctx->pc = 0x1248;
    unsigned released_before = early_releases;
    CHECK(read_override(ctx, 0xffb5, 0x98) == 0x98 && early_releases == released_before + 1);
    put16(ctx->wram + 0x804, shantae_slot_addr(movers[0]));
    CHECK(read_override(ctx, 0xffb5, 0x98) != 0x98);
    /* An object whose spawn record is still inside the original bounds stays. */
    NEXT_FRAME(); free_last(ctx, SLOT_FLOOR - 2);
    *shantae_slot_memory(ctx, shantae_slot_addr(movers[2]) + 0x24) = 1; put_slot16(ctx, movers[2], 0x25, 0x7810);
    put16(rom + 0x7810, 1115); put16(rom + 0x7812, 1100); put16(rom + 0x7814, 1040); put16(rom + 0x7816, 1000);
    CHECK(KEEPS_WIDE(2) && !KEEPS_WIDE(1));
    /* Two frames later, objects that stopped running retention no longer count. */
    for (int f = 0; f < 2; ++f) {
        ctx->completed_frames++; ctx->pc = 0x11d8; ctx->bc = shantae_slot_addr(movers[0]); world_read_tap(ctx, 0xffb7);
    }
    CHECK(retention_seen[0][movers[0]] && retention_seen[1][movers[0]] && !retention_seen[1][movers[1]]);
    /* A new tinkerbat's compare (00:1904, the object pushed at 00:1900) counts
     * the live ones (callback 1D:5A96) within 160 across and 144 down of it:
     * not those far across (in a slot at A000) or down, a dying one (callback
     * 1D:6285) or a freed slot. */
    {
        #define TINKERBAT(slot, x, y, callback, status) do { \
            const unsigned o_ = shantae_slot_addr(slot); \
            put_slot16(ctx, slot, 0x34, x); put_slot16(ctx, slot, 0x37, y); \
            *shantae_slot_memory(ctx, o_ + 0x57) = 24; *shantae_slot_memory(ctx, o_ + 0x58) = 36; \
            *shantae_slot_memory(ctx, o_) = status; *shantae_slot_memory(ctx, o_ + 0x19) = 0x1d; \
            put_slot16(ctx, slot, 0x1b, callback); } while (0)
        TINKERBAT(50, 3258, 1508, 0x0c41, 0x80);   /* the new one, its script not yet run */
        TINKERBAT(51, 3300, 1500, 0x5a96, 0);
        TINKERBAT(120, 3900, 1500, 0x5a96, 0);
        TINKERBAT(52, 3260, 1800, 0x5a96, 0);
        TINKERBAT(53, 3260, 1500, 0x6285, 0);
        TINKERBAT(54, 3260, 1500, 0x5a96, 0xff);
        ctx->sp = 0xc800; put16(ctx->wram + 0x800, shantae_slot_addr(50)); ctx->pc = 0x1905;
        CHECK(read_override(ctx, 0xcc2e, 5) == 1);
        TINKERBAT(55, 3100, 1420, 0x5a96, 0);
        CHECK(read_override(ctx, 0xcc2e, 5) == 2);
        ctx->pc = 0x1906; CHECK(read_override(ctx, 0xcc2e, 5) == 5);   /* other reads of it */
        ctx->pc = 0x1905; widened = 0; CHECK(read_override(ctx, 0xcc2e, 5) == 5); widened = 1;
        const int used[] = {50, 51, 120, 52, 53, 54, 55};
        for (int i = 0; i < 7; ++i) memset(shantae_slot_memory(ctx, shantae_slot_addr(used[i])), 0, SHANTAE_SLOT_SIZE);
        #undef TINKERBAT
    }
    /* Budgets. A spawner the original's retention bounds keep (x 920-1240,
     * y 928-1216 here) reads its count less what wears one of the budget's
     * release callbacks outside them, by at most the budget's allowance, and
     * no more than its limit; one outside them reads the count, no more than
     * its limit. What is between states stays counted. */
    {
        #define OBJECT(slot, x, y, bank, callback) do { \
            const unsigned o_ = shantae_slot_addr(slot); \
            put_slot16(ctx, slot, 0x34, x); put_slot16(ctx, slot, 0x37, y); \
            *shantae_slot_memory(ctx, o_ + 0x57) = 16; *shantae_slot_memory(ctx, o_ + 0x58) = 16; \
            *shantae_slot_memory(ctx, o_) = 0; *shantae_slot_memory(ctx, o_ + 0x24) = 0; \
            *shantae_slot_memory(ctx, o_ + 0x19) = bank; put_slot16(ctx, slot, 0x1b, callback); } while (0)
        #define GONE(slot) (*shantae_slot_memory(ctx, shantae_slot_addr(slot)) = 0xff)
        const unsigned saved_bc = ctx->bc, saved_de = ctx->de, saved_bank = ctx->rom_bank;
        /* 0A:41AC's compare of C080 with 2 (0B:4698), the spawner in BC. */
        OBJECT(60, 1000, 1000, 0x0b, 0x4648);   /* the spawner */
        OBJECT(61, 1010, 1000, 0x0b, 0x4971);   /* one it made, by Shantae */
        OBJECT(62, 2000, 1000, 0x0b, 0x4e6f);   /* one left behind */
        ctx->bc = shantae_slot_addr(60); ctx->rom_bank = 0x0b; ctx->pc = 0x4699;
        CHECK(read_override(ctx, 0xc080, 2) == 1);
        OBJECT(63, 2100, 1000, 0x0b, 0x4fff);
        CHECK(read_override(ctx, 0xc080, 3) == 1);
        OBJECT(121, 3000, 1100, 0x0b, 0x5882);  /* at A000: the allowance of 2 is used up */
        CHECK(read_override(ctx, 0xc080, 4) == 2);
        GONE(62); GONE(63); GONE(121);
        OBJECT(64, 2200, 1000, 0x0b, 0x47d8);   /* between states: counted */
        CHECK(read_override(ctx, 0xc080, 2) == 2);
        ctx->pc = 0x4bb1; CHECK(read_override(ctx, 0xc080, 2) == 2);   /* 0A:41B0's, 0B:4BB0 */
        OBJECT(62, 2000, 1000, 0x0b, 0x4e6f);
        CHECK(read_override(ctx, 0xc080, 3) == 2);
        /* A spawner only the view runs: the count, no more than the limit. */
        put_slot16(ctx, 60, 0x34, 3000);
        CHECK(read_override(ctx, 0xc080, 3) == 2 && read_override(ctx, 0xc080, 1) == 1);
        put_slot16(ctx, 60, 0x34, 1000);
        /* Other reads of the count, another bank, and a room the view does not widen. */
        ctx->pc = 0x469a; CHECK(read_override(ctx, 0xc080, 3) == 3);
        ctx->pc = 0x4699; ctx->rom_bank = 0x0c; CHECK(read_override(ctx, 0xc080, 3) == 3);
        ctx->rom_bank = 0x0b; widened = 0; CHECK(read_override(ctx, 0xc080, 3) == 3); widened = 1;
        /* The swamp creatures' op 98 (21:5BED, DE past its arguments), the new
         * one pushed at 00:1900: one following Shantae outside the bounds
         * lets it surface, one inside them does not. */
        OBJECT(70, 1000, 1000, 0x21, 0x5bfb);
        OBJECT(71, 700, 1000, 0x21, 0x5c44);
        ctx->sp = 0xc800; put16(ctx->wram + 0x800, shantae_slot_addr(70));
        ctx->rom_bank = 0x21; ctx->pc = 0x1905; ctx->de = 0x5bf1;
        CHECK(read_override(ctx, 0xc000, 1) == 0);
        OBJECT(72, 400, 1000, 0x21, 0x5c44);    /* no allowance to speak of: 12 */
        CHECK(read_override(ctx, 0xc000, 2) == 0);
        put_slot16(ctx, 71, 0x34, 1100);
        CHECK(read_override(ctx, 0xc000, 2) == 1);
        ctx->de = 0x5bf0; CHECK(read_override(ctx, 0xc000, 2) == 2);   /* another script's op 98 */
        const int used[] = {60, 61, 62, 63, 64, 121, 70, 71, 72};
        for (int i = 0; i < 9; ++i) memset(shantae_slot_memory(ctx, shantae_slot_addr(used[i])), 0, SHANTAE_SLOT_SIZE);
        CHECK(budget_reads > 0);
        ctx->bc = saved_bc; ctx->de = saved_de; ctx->rom_bank = saved_bank;
        #undef GONE
        #undef OBJECT
    }
    /* A totem's pedestal (0C:4C25, pushed at 0C:4C29) reads the stones noted
     * at C004 + 2 x place (0C:4C4F / 0C:4C51, 48 bytes on per place) as the
     * live stones of its own totem at those places, in their script 0C:4340
     * (a flip included): not another totem's stone noted over them, nor a
     * fireball whose slot kept a stone's arguments. A noted stone that is
     * its own stays, and with none the note stands. */
    {
        #define OBJECT(slot, status, bank, pc, totem, place) do { \
            const unsigned o_ = shantae_slot_addr(slot); \
            *shantae_slot_memory(ctx, o_) = status; *shantae_slot_memory(ctx, o_ + 4) = bank; \
            put_slot16(ctx, slot, 2, pc); \
            *shantae_slot_memory(ctx, o_ + 0x20) = totem; *shantae_slot_memory(ctx, o_ + 0x21) = place; } while (0)
        OBJECT(60, 0, 0x0c, 0x4bfb, 2, 3);     /* the pedestal: totem 2, three stones */
        OBJECT(61, 0, 0x0c, 0x439b, 2, 0);     /* its top stone */
        OBJECT(62, 0, 0x0c, 0x439b, 5, 0);     /* another totem's top stone */
        OBJECT(121, 0, 0x0c, 0x443b, 2, 1);    /* its middle stone, flipping, at A000 */
        OBJECT(63, 0, 0x09, 0x4cbd, 2, 1);     /* a fireball in a released stone's slot */
        OBJECT(64, 0xff, 0x0c, 0x4394, 2, 2);  /* a freed slot */
        put16(ctx->wram + 0x004, shantae_slot_addr(62));
        put16(ctx->wram + 0x006, shantae_slot_addr(63));
        put16(ctx->wram + 0x008, shantae_slot_addr(64));
        const unsigned saved_bank = ctx->rom_bank;
        ctx->sp = 0xc800; put16(ctx->wram + 0x800, shantae_slot_addr(60)); ctx->rom_bank = 0x0c;
        CHECK(totem_read(ctx, 0) == shantae_slot_addr(61));
        CHECK(totem_read(ctx, 1) == shantae_slot_addr(121));
        CHECK(totem_read(ctx, 2) == shantae_slot_addr(64));   /* none of its own: the note */
        OBJECT(65, 0, 0x0c, 0x43a2, 2, 0);
        put16(ctx->wram + 0x004, shantae_slot_addr(65));
        CHECK(totem_read(ctx, 0) == shantae_slot_addr(65));   /* the noted one, of two */
        put16(ctx->wram + 0x004, shantae_slot_addr(62));
        ctx->pc = 0x4c50; ctx->rom_bank = 0x0d; CHECK(read_override(ctx, 0xc004, 0x12) == 0x12);
        ctx->pc = 0x4c51; ctx->rom_bank = 0x0c; CHECK(read_override(ctx, 0xc004, 0x12) == 0x12);
        ctx->pc = 0x4c80; CHECK(read_override(ctx, 0xc004, 0x12) == 0x12);   /* place 1's read of place 0 */
        const int used[] = {60, 61, 62, 121, 63, 64, 65};
        for (int i = 0; i < 7; ++i) memset(shantae_slot_memory(ctx, shantae_slot_addr(used[i])), 0, SHANTAE_SLOT_SIZE);
        memset(ctx->wram + 0x004, 0, 6); ctx->rom_bank = saved_bank;
        #undef OBJECT
    }
    reset_view(ctx);
    CHECK(!retention_seen[0][movers[0]] && !spawn_waits && !early_releases && !doors_restored);
    test_slots = SHANTAE_ORIGINAL_SLOTS;

    /* Captures read objects past DFFF and at A000 from the table while SVBK
     * selects bank 3 (01:4F19 takes their pictures): the echo of C000 and
     * cartridge RAM (off) otherwise. */
    static uint8_t ext[0x3e00];
    ctx->wram_ext = ext; ctx->wram_ext_bank = 3; ctx->wram_ext_mapped = 0x1dc6;
    ctx->wram_ext_cart_mapped = 0x1f80; ctx->wram_ext_cart_offset = 0x1e00;
    ext[0x100] = 0x42; ctx->wram[0x100] = 0x24; ext[0x1e10] = 0x5a;
    ctx->wram_bank = 3; CHECK(memory8(ctx, 0xe100) == 0x42 && memory8(ctx, 0xa010) == 0x5a);
    ctx->wram_bank = 1; CHECK(memory8(ctx, 0xe100) == 0x24 && memory8(ctx, 0xa010) == 0xff);
    ctx->wram_ext_mapped = ctx->wram_ext_cart_mapped = 0; ctx->wram_bank = 3;
    CHECK(memory8(ctx, 0xe100) == 0x24 && memory8(ctx, 0xa010) == 0xff);
    ctx->wram_ext = NULL; ctx->wram_ext_bank = 0; ctx->wram_ext_cart_offset = 0;
    memset(ctx->wram, 0, 0x8000); memset(test_ext, 0, sizeof(test_ext)); memset(test_cart, 0, sizeof(test_cart));
    memset(ctx->hram, 0, 0x7f);
    ctx->completed_frames = 0; ctx->pc = 0;

    /* Room zoom: a 1920x1080 view in a 1920x1080 window. Bounds as the room
     * loader writes them (03:79E4: greatest = least + span - 1), with the
     * status bar. Pixel Perfect fills to the nearest whole scale, Whole room
     * rounds down and ignores a side longer than the view, never past 7x
     * (the original screen), never below the view's own 1x. */
    {
        struct { int span_x, span_y, mode, whole, w, h; double zoom; } cases[] = {
            {3000, 2000, SHANTAE_ROOM_ZOOM_FILL, 1, 1920, 1080, 0},    /* larger: as it is */
            {160, 0, SHANTAE_ROOM_ZOOM_FILL, 1, 274, 154, 7},          /* 319 x 128 corridor */
            {352, 128, SHANTAE_ROOM_ZOOM_FILL, 1, 480, 270, 4},        /* 511 x 255 */
            {352, 128, SHANTAE_ROOM_ZOOM_WHOLE, 1, 640, 360, 3},
            {1888, 128, SHANTAE_ROOM_ZOOM_FILL, 1, 480, 270, 4},       /* 2047 x 255: scrolls across */
            {1888, 128, SHANTAE_ROOM_ZOOM_WHOLE, 1, 640, 360, 3},
            {1, 280, SHANTAE_ROOM_ZOOM_FILL, 1, 274, 154, 7},          /* 160 x 407 shaft */
            {1, 280, SHANTAE_ROOM_ZOOM_WHOLE, 1, 960, 540, 2},
            {2912, 640, SHANTAE_ROOM_ZOOM_FILL, 1, 1920, 1080, 0},     /* 3071 x 767: 1.38 rounds to 1 */
            {2912, 640, SHANTAE_ROOM_ZOOM_FILL, 0, 1392, 783, 1080.0 / 783},
            {352, 128, SHANTAE_ROOM_ZOOM_FILL, 0, 481, 271, 1080.0 / 271},
            {352, 128, SHANTAE_ROOM_ZOOM_OFF, 1, 1920, 1080, 0},
        };
        memset(wram, 0, sizeof(wram));
        lcdc = ppu->lcdc = 0xa1; window_x = 0; window_y = 128; ready = 1;
        for (int i = 0; i < 160 * 144; ++i) ppu->rgb_framebuffer[i] = 0xff204060;
        for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
            put16(wram + 0x9db, 1000); put16(wram + 0x9dd, 1000 + cases[c].span_x - 1);
            put16(wram + 0x9df, 1000); put16(wram + 0x9e1, 1000 + cases[c].span_y - 1);
            s_room_zoom = cases[c].mode;
            zoom_snap = 1;
            int w = 1920, h = 1080;
            double s = 0;
            fit(ctx, 1920, 1080, cases[c].whole, &w, &h, &s);
            CHECK(w == cases[c].w && h == cases[c].h && fabs(s - cases[c].zoom) < 1e-9);
        }
        /* An arena locking in play (1A:54FD: span 32, 94) eases from 1x to 7x
         * over ZOOM_FRAMES guest frames; the camera unlocking behind a dark
         * frame snaps back; a dialogue box keeps the zoom it had. */
        s_room_zoom = SHANTAE_ROOM_ZOOM_FILL;
        put16(wram + 0x9dd, 1000 + 2999); put16(wram + 0x9e1, 1000 + 1999);
        zoom_snap = 1;
        int w = 1920, h = 1080;
        double s = 0;
        fit(ctx, 1920, 1080, 1, &w, &h, &s);
        CHECK(s == 0 && zoom == 1);
        put16(wram + 0x9dd, 1000 + 31); put16(wram + 0x9e1, 1000 + 93);
        ctx->completed_frames = 100;
        w = 1920; h = 1080; s = 0;
        fit(ctx, 1920, 1080, 1, &w, &h, &s);
        CHECK(s == 0 && zoom_target == 7);
        ctx->completed_frames = 100 + ZOOM_FRAMES / 2;
        w = 1920; h = 1080; s = 0;
        fit(ctx, 1920, 1080, 1, &w, &h, &s);
        CHECK(fabs(s - sqrt(7.0)) < 1e-9 && w == (int)(1920 / sqrt(7.0)) && h == (int)(1080 / sqrt(7.0)));
        ctx->completed_frames = 100 + ZOOM_FRAMES;
        w = 1920; h = 1080; s = 0;
        fit(ctx, 1920, 1080, 1, &w, &h, &s);
        CHECK(s == 7 && w == 274 && h == 154);
        window_y = 96;   /* a dialogue box: the frame is declined */
        put16(wram + 0x9dd, 1000 + 2999); put16(wram + 0x9e1, 1000 + 1999);
        w = 1920; h = 1080; s = 0;
        fit(ctx, 1920, 1080, 1, &w, &h, &s);
        CHECK(s == 7 && w == 274 && h == 154);
        window_y = 128;
        memset(ppu->rgb_framebuffer, 0, 160 * 144 * sizeof(uint32_t));
        w = 1920; h = 1080; s = 0;
        fit(ctx, 1920, 1080, 1, &w, &h, &s);
        CHECK(s == 0 && w == 1920 && h == 1080 && zoom == 1);
        ctx->completed_frames = 0; ready = 0; lcdc = ppu->lcdc = 0; window_x = window_y = 0;
        memset(wram, 0, sizeof(wram));
    }

    /* The room shown eases to a room that jumps in play, over the zoom's
     * frames: the second arena widening from 796-1023 to 752-1023 once its
     * boss has risen (25:46DC), the camera's screen shown throughout. A room
     * moving as fast as a camera follows at once; a cut snaps to the room. */
    {
        memset(wram, 0, sizeof(wram));
        for (int i = 0; i < 160 * 144; ++i) ppu->rgb_framebuffer[i] = 0xff204060;
        const Box rising = {796, 320, 1024, 519}, risen = {752, 320, 1023, 519}, other = {284, 320, 511, 519};
        camera_x = 796; camera_y = 336;
        room_snap = 1; ctx->completed_frames = 50;
        Box b = shown_room(ctx, &rising);
        CHECK(b.x0 == 796 && b.x1 == 1024 && b.y0 == 320 && b.y1 == 519);
        ctx->completed_frames = 51;
        b = shown_room(ctx, &risen);
        CHECK(b.x0 == 796 && b.x1 == 1024);
        ctx->completed_frames = 51 + ZOOM_FRAMES / 2;
        camera_x = 760;   /* the camera's screen (760-920) is shown although the room is at 774 */
        b = shown_room(ctx, &risen);
        CHECK(b.x0 == 760 && b.x1 == 1023 && b.y0 == 320 && b.y1 == 519);
        camera_x = 790;
        b = shown_room(ctx, &risen);   /* the same frame shown again */
        CHECK(b.x0 == 774);
        ctx->completed_frames = 51 + ZOOM_FRAMES;
        b = shown_room(ctx, &risen);
        CHECK(b.x0 == 752 && b.x1 == 1023);
        for (int step = 1; step <= 4; ++step) {
            const Box scrolling = {752 + 8 * step, 320, 1023 + 8 * step, 519};
            ++ctx->completed_frames;
            b = shown_room(ctx, &scrolling);
            CHECK(b.x0 == scrolling.x0 && b.x1 == scrolling.x1);
        }
        note_camera(790, 336);
        note_camera(284, 336);
        camera_x = 284;
        b = shown_room(ctx, &other);
        CHECK(b.x0 == 284 && b.x1 == 511);
        ctx->completed_frames = 0; camera_x = camera_y = 0;
        memset(ppu->rgb_framebuffer, 0, 160 * 144 * sizeof(uint32_t));
        memset(wram, 0, sizeof(wram));
    }

    /* Rooms keep to the pictures the camera is in. A map (directory 40 in
     * bank 1) holding a block of sectors 0-3 x 0-1 and a 256-wide shaft at
     * sector column 10, rows 0-9, under whole-map bounds (like the credits'
     * shaft, 1C:78DC). */
    {
        memset(rom + 0x4000, 0, 0x800);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                if ((x < 4 && y < 2) || (x == 10 && y < 10)) {
                    rom[0x4000 + y * 64 + x * 2] = 0x50; rom[0x4001 + y * 64 + x * 2] = 1;
                }
        /* Every cell of the sectors' page (50) a different metatile. */
        for (int i = 0; i < 0x200; i += 2) { rom[0x5000 + i] = (i / 2 & 15) << 4; rom[0x5001 + i] = 0x40 + (i / 2 >> 4); }
        map_pictures[0].key = map_pictures[1].key = 0;
        memset(wram, 0, sizeof(wram));
        wram[0x9f9] = 0x40; wram[0x9fa] = 1;
        put16(wram + 0x9dd, 8031); put16(wram + 0x9e1, 8063);
        Room r = room_at(ctx, wram, 2600, 1000, 16, 1);
        CHECK(r.box.x0 == 2560 && r.box.x1 == 2816 && r.box.y0 == 0 && r.box.y1 == 2560 + 16);
        CHECK(r.masked && (r.sectors[9] >> 10 & 1) && !(r.sectors[10] >> 10 & 1) && !(r.sectors[0] & 1));
        r = room_at(ctx, wram, 100, 100, 16, 0);
        CHECK(r.box.x0 == 0 && r.box.x1 == 1024 && r.box.y0 == 0 && r.box.y1 == 512 + 16 && !r.masked);
        /* Nothing drawn under the screen: the bounds as they are. */
        r = room_at(ctx, wram, 5000, 5000, 16, 1);
        CHECK(r.box.x0 == 0 && r.box.x1 == 8191 && r.box.y1 == 8207 && !r.masked);
        /* Bounds inside a picture stay the room. */
        put16(wram + 0x9db, 2600); put16(wram + 0x9dd, 2620);
        r = room_at(ctx, wram, 2610, 1000, 16, 0);
        CHECK(r.box.x0 == 2600 && r.box.x1 == 2780 && r.box.y1 == 2576);
        put16(wram + 0x9db, 0); put16(wram + 0x9dd, 8031);
        /* The shaft 160 wide in a column of filler (page 52: its cells from x
         * 160 on are one metatile, like the credits' shaft), and a band of
         * filler under the block's first row of sectors. */
        memcpy(rom + 0x5200, rom + 0x5000, 0x200);
        for (int row = 0; row < 16; ++row)
            for (int col = 10; col < 16; ++col) { rom[0x5200 + row * 32 + col * 2] = 0; rom[0x5201 + row * 32 + col * 2] = 0x7f; }
        for (int y = 0; y < 10; ++y) rom[0x4000 + y * 64 + 20] = 0x52;
        memcpy(rom + 0x5400, rom + 0x5000, 0x200);
        for (int i = 0x100; i < 0x200; i += 2) { rom[0x5400 + i] = 0; rom[0x5401 + i] = 0x7f; }
        for (int x = 0; x < 4; ++x) rom[0x4000 + 1 * 64 + x * 2] = 0x54;
        map_pictures[0].key = map_pictures[1].key = 0;
        r = room_at(ctx, wram, 2560, 1000, 16, 0);
        CHECK(r.box.x0 == 2560 && r.box.x1 == 2720 && r.box.y0 == 0 && r.box.y1 == 2576);
        r = room_at(ctx, wram, 100, 100, 16, 0);
        CHECK(r.box.x0 == 0 && r.box.x1 == 1024 && r.box.y1 == 384 + 16);
        /* Bounds that reach into the filler without leaving the sectors stop
         * where it starts too (25:46A8 holds the camera at the second arena's
         * least x with its greatest 88 pixels into a blank sector); a camera
         * that goes in keeps its screen. */
        put16(wram + 0x9db, 2560); put16(wram + 0x9dd, 2600);
        r = room_at(ctx, wram, 2560, 1000, 16, 0);
        CHECK(r.box.x0 == 2560 && r.box.x1 == 2720);
        r = room_at(ctx, wram, 2600, 1000, 16, 0);
        CHECK(r.box.x0 == 2560 && r.box.x1 == 2760);
        put16(wram + 0x9db, 0); put16(wram + 0x9dd, 8031);
        for (int y = 0; y < 10; ++y) rom[0x4000 + y * 64 + 20] = 0x50;
        for (int x = 0; x < 4; ++x) rom[0x4000 + 1 * 64 + x * 2] = 0x50;
        map_pictures[0].key = map_pictures[1].key = 0;

        /* Listed pictures: the intro's house (07:405C's bounds, camera 0,876),
         * the bridge (07:4079's bounds), the game over (04:6E04, whole map) and
         * the credits' strip (17:72E7). */
        wram[0x9f9] = 0x4f; wram[0x9fa] = 0x51;
        put16(wram + 0x9dd, 2911); put16(wram + 0x9e1, 879);
        r = room_at(ctx, wram, 0, 876, 0, 0);
        CHECK(r.box.x0 == 0 && r.box.x1 == 256 && r.box.y0 == 512 && r.box.y1 == 1023);
        r = room_at(ctx, wram, 0, 117, 0, 0);
        CHECK(r.box.x0 == 0 && r.box.x1 == 3071 && r.box.y0 == 0 && r.box.y1 == 512);
        put16(wram + 0x9e1, 343);
        r = room_at(ctx, wram, 11, 343, 16, 0);
        CHECK(r.box.x1 == 3071 && r.box.y1 == 487);
        wram[0x9f9] = 0x73; wram[0x9fa] = 0x59;
        put16(wram + 0x9dd, 8191); put16(wram + 0x9e1, 8191);
        r = room_at(ctx, wram, 4096, 327, 0, 0);
        CHECK(r.box.x0 == 4096 && r.box.x1 == 4256 && r.box.y0 == 0 && r.box.y1 == 472);
        put16(wram + 0x9dd, 8031); put16(wram + 0x9e1, 8047);
        r = room_at(ctx, wram, 3888, 3938, 0, 0);
        CHECK(r.box.x0 == 3872 && r.box.x1 == 4064 && r.box.y0 == 0 && r.box.y1 == 8191);

        /* The view zooms to the shaft (Fill: 256 wide fills the window at the
         * original screen's 7.5x) and draws nothing of the block beside it. */
        wram[0x9f9] = 0x40; wram[0x9fa] = 1;
        put16(wram + 0x9dd, 8031); put16(wram + 0x9e1, 8063);
        lcdc = ppu->lcdc = 0xa1; window_x = 0; window_y = 128; ready = 1;
        for (int i = 0; i < 160 * 144; ++i) ppu->rgb_framebuffer[i] = 0xff204060;
        camera_x = scroll_x = 2600; camera_y = scroll_y = 1000;
        zoom_snap = 1;
        int w = 1920, h = 1080;
        double s = 0;
        fit(ctx, 1920, 1080, 0, &w, &h, &s);
        CHECK(w == 256 && h == 144 && fabs(s - 7.5) < 1e-9);
        /* A cut to the block snaps to its zoom instead of easing. */
        note_camera(2600, 1000);
        zoom_snap = 0;
        note_camera(2600, 1064);
        CHECK(!zoom_snap);
        note_camera(100, 100);
        CHECK(zoom_snap);
        camera_x = scroll_x = 100; camera_y = scroll_y = 100;
        ctx->completed_frames = 10;
        w = 1920; h = 1080; s = 0;
        fit(ctx, 1920, 1080, 0, &w, &h, &s);   /* 1024 x 512 and the status bar */
        CHECK(zoom == zoom_target && fabs(s - 1080.0 / 528) < 1e-9);

        /* Activation keeps to the same room: at 256 x 144 the shaft's view
         * starts at x 2560 (the camera's own centering would give 2552), so
         * retention reaches 80 to the left of it, the room's side, and the
         * guard (16) and 64 above it. */
        memcpy(ctx->wram, wram, sizeof(wram));
        put16(ctx->hram + 0x61, 2600); put16(ctx->hram + 0x63, 1000);
        ppu->wx = 7; ppu->wy = 128;
        gb_custom_width = 256; gb_custom_height = 144;
        widened = 1; memcpy(widened_room, ctx->wram + 0x9f8, sizeof(widened_room));
        ctx->pc = 0x11d7; ctx->bc = 0;
        CHECK(read_override(ctx, 0xffb5, 0) == (uint8_t)(2560 - 80) && read_override(ctx, 0xffb6, 0) == (2560 - 80) >> 8);
        CHECK(read_override(ctx, 0xffb9, 0) == (uint8_t)(1000 - 16 - 64));

        ctx->completed_frames = 0; ctx->pc = 0; ready = 0; lcdc = ppu->lcdc = 0; window_x = window_y = 0;
        ppu->wx = ppu->wy = 0; widened = 0;
        memset(wram, 0, sizeof(wram)); memset(ctx->wram, 0, 0x8000); memset(ctx->hram, 0, 0x7f);
        map_pictures[0].key = map_pictures[1].key = 0;
    }

    /* Arming the view hands the original view's scaling to the platform;
     * without the view it stays centered in the (absent) view. */
    shantae_view_init(ctx);
    CHECK(gb_custom_render == render && gb_custom_fit == fit &&
          gb_custom_native_scaling == GB_CUSTOM_NATIVE_SCALING_MODE && gb_custom_native_scale == 1);

    free(out); free(rom); free(ppu); free(ctx->wram); free(ctx->hram); free(ctx);
    puts("Expanded view: aspect presets, adaptive sizes, sector coverage up to 8192x8192, background coordinates, reset, shake clipping, sizes 160x144 to 3840x2160 and 8192x1024, map gate, towns, committed camera, background offset, one-screen rooms, centered small rooms, fade palettes, object slots, the tinkerbat cap, spawner budgets, totem stones, retention past the spawn guard, filling revealed areas, 256x240 composition, the original view's scaling, room zoom, room easing, rooms kept to their pictures and the bottom strip passed.");
    return 0;
}
