/*
 * hud.c -- the transformation buttons' HUD: an icon for each form and for the
 * heal, in the Dance Menu's look, in a top corner of the picture.
 *
 * The icons are the Dance Menu's (bank 5 opens it): 05:6547 and 05:6552 call
 * the graphics decompressor 00:0803 with their arguments inline (source,
 * bank, VRAM address, VRAM bank, length): 7B:54D5 into VRAM bank 0 and 7B:4CBD
 * into bank 1, $1800 bytes each from 8000. 00:0803 is LZSS as Okumura wrote
 * it (01:41BD): a 4 KB window at D000 filled with spaces and written from
 * $FEE, a flag byte per eight items with the low bit first, a set bit a
 * literal and a clear one a reference of two bytes, the position in the first
 * and the high nibble of the second and the length in its low nibble plus 3.
 * Right after the bank 0 stream come the menu's background palettes (the GBC
 * colors, then the GBA's at +$40, which the palette loader takes on a GBA,
 * 00:1CB6) and its tile map: 20 x 18, then a tile and an attribute per cell.
 * The monkey, elephant, spider, harpy and the heal dance's heart are the 3 x 3
 * cells at its left, from rows 2, 5, 8, 11 and 14 (the warps are on the
 * right). The tinkerbat's dance (GBA only) has no entry in the menu; hers is
 * the head of her idle frame, as the others are heads: her form's start
 * script (the dance table's entry, 0E:43FB, goes on to 1C:4BAA) sets it with
 * op 0E, a frame whose first word is its sprite's layout (a count of 8 x 16
 * pieces at +32, then y, x and attributes from +35) and whose +8 its tiles,
 * in the menu's colors. Its first 12 rows are her head, from the top of her
 * headband to the fringe of her jaw; her neck begins on the 13th.
 *
 * No key or button is written by an icon: the icons say what they do, and
 * the buttons are the player's to bind (Esc -> Controls), so the HUD is the
 * same for every keyboard, controller and language.
 *
 * Nothing is shown on a town's street: its walker (bank 4) has no dance, so
 * the buttons do nothing there either (transform.c). Inside a building she
 * walks with her own idle routine (06:4AA7), which dances, so it is shown.
 *
 * The day and night clock is in the status bar's look: the sun or the moon,
 * and the minutes and seconds to the next change (map.c's shantae_map_clock)
 * in the status bar's own numerals, outlined in its black (no box: a shader
 * preset shades the clock on its own, at its own scale, so a box of black
 * would not match the picture's black). Every room loads them with
 * the rest of its font: 04:4DFB, a far call each room makes, calls 00:0803
 * with E5:64A3 into VRAM bank 1 from 8000, $1800 bytes; the digits and the
 * colon are tiles $30-$3A, in color 1 on color 3 of BG palette 7. That
 * palette is each area's (its color 0 differs, a labyrinth's yellow is a
 * little darker, a GBA's brighter), so the clock takes palette 7 as the
 * status bar has it now, fades included. The numerals are dimmed while the
 * clock stands still (C3D5: a scene, a door's transition).
 *
 * Everything comes from the ROM the game runs, at the first frame drawn.
 */
#include "hud.h"
#include "transform.h"
#include "map.h"
#include "expanded_view.h"
#include "gbrt.h"
#include "ppu.h"
#include "object_slots.h"
#include <string.h>

int shantae_original_colors(void);

#define SLOT 26          /* a slot's box */
#define GAP 1
#define ICON 24          /* the menu's 3 x 3 cells */
#define FRAME 32         /* a sprite frame, drawn whole */
#define HEAD_ROWS 12     /* the tinkerbat's head in her frame */
#define TRANSPARENT 0xFF
#define ICONS (SHANTAE_HEAL + 1)

/* The buttons in the order of the game's actions (extras.c). */
static const int s_order[] = {SHANTAE_FORM_MONKEY, SHANTAE_FORM_ELEPHANT, SHANTAE_FORM_SPIDER,
                              SHANTAE_FORM_HARPY, SHANTAE_FORM_TINKERBAT, SHANTAE_HEAL};
#define BUTTONS (int)(sizeof(s_order) / sizeof(s_order[0]))

static int s_loaded;   /* 0 not yet, 1 icons, -1 none (not this ROM's layout) */
/* Each icon as menu palette entries (palette * 4 + color), cropped. */
static uint8_t s_icon[ICONS][ICON * ICON];
static int s_icon_w[ICONS], s_icon_h[ICONS];
static uint8_t s_palettes[2][64];   /* the menu's BG palettes: GBC, GBA */

static const uint8_t *rom_at(const GBContext *ctx, unsigned bank, unsigned addr) {
    const size_t o = addr < 0x4000 ? addr : bank * 0x4000u + (addr - 0x4000u);
    return o < ctx->rom_size ? ctx->rom + o : NULL;
}
static unsigned rom8(const GBContext *ctx, unsigned bank, unsigned addr) {
    const uint8_t *p = rom_at(ctx, bank, addr);
    return p ? *p : 0xFF;
}
static unsigned rom16(const GBContext *ctx, unsigned bank, unsigned addr) {
    return rom8(ctx, bank, addr) | rom8(ctx, bank, addr + 1) << 8;
}

/* 00:0803's LZSS into out (len bytes). Returns the address after the stream. */
static unsigned lzss(const GBContext *ctx, unsigned bank, unsigned src, uint8_t *out, unsigned len) {
    uint8_t ring[4096];
    memset(ring, 0x20, sizeof(ring));
    unsigned r = 0xFEE, n = 0;
    while (n < len) {
        unsigned flags = rom8(ctx, bank, src++);
        for (int bit = 0; bit < 8 && n < len; ++bit, flags >>= 1) {
            if (flags & 1) {
                const uint8_t c = (uint8_t)rom8(ctx, bank, src++);
                out[n++] = c;
                ring[r] = c;
                r = (r + 1) & 0xFFF;
            } else {
                const unsigned i = rom8(ctx, bank, src), j = rom8(ctx, bank, src + 1);
                src += 2;
                const unsigned pos = i | (j & 0xF0) << 4, count = (j & 0x0F) + 3;
                for (unsigned k = 0; k < count && n < len; ++k) {
                    const uint8_t c = ring[(pos + k) & 0xFFF];
                    out[n++] = c;
                    ring[r] = c;
                    r = (r + 1) & 0xFFF;
                }
            }
        }
    }
    return src;
}

/* The decompressor's inline arguments at a CALL $0803. */
static int load_call(const GBContext *ctx, unsigned bank, unsigned at, unsigned *src_bank, unsigned *src,
                     unsigned *vbk) {
    if (rom8(ctx, bank, at) != 0xCD || rom16(ctx, bank, at + 1) != 0x0803) return 0;
    *src = rom16(ctx, bank, at + 3);
    *src_bank = rom8(ctx, bank, at + 5);
    *vbk = rom8(ctx, bank, at + 8);
    return rom16(ctx, bank, at + 6) == 0x8000 && rom16(ctx, bank, at + 9) == 0x1800 && *src >= 0x4000 &&
           *src < 0x8000;
}

/* A pixel of a tile in the decoded VRAM ($1800 bytes a bank from 8000), with
 * 8800 addressing (the menu's LCDC). */
static unsigned tile_pixel(const uint8_t vram[2][0x1800], unsigned tile, unsigned attr, int x, int y) {
    const unsigned at = tile < 128 ? 0x1000 + tile * 16 : 0x0800 + (tile - 128) * 16;
    if (attr & 0x40) y = 7 - y;
    const int bit = (attr & 0x20) ? x : 7 - x;
    const uint8_t *row = vram[(attr >> 3) & 1] + at + y * 2;
    return ((row[0] >> bit) & 1) | (((row[1] >> bit) & 1) << 1);
}

/* Crop an icon (size x size pixels) to what is not `background` connected to
 * its edges, and not a ring of `ring` pixels along them (the menu slot's
 * frame lines). */
static void crop_icon(int icon, uint8_t *pixels, int size, uint8_t background, int ring) {
    uint8_t clear[FRAME * FRAME] = {0};
    int stack[FRAME * FRAME], top = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
            if (x < ring || y < ring || x >= size - ring || y >= size - ring ||
                (edge && pixels[y * size + x] == background)) {
                clear[y * size + x] = 1;
                stack[top++] = y * size + x;
            }
        }
    while (top) {
        const int i = stack[--top], x = i % size, y = i / size;
        static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + d[k][0], ny = y + d[k][1];
            if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
            const int j = ny * size + nx;
            if (clear[j] || pixels[j] != background) continue;
            clear[j] = 1;
            stack[top++] = j;
        }
    }
    /* Specks go too, only parts of eight pixels or more are the icon's: the
     * rivets the menu's frame leaves inside the ring, and the tip of the
     * tinkerbat's arm beside her jaw. */
    uint8_t seen[FRAME * FRAME] = {0};
    for (int start = 0; start < size * size; ++start) {
        if (clear[start] || seen[start]) continue;
        int part[FRAME * FRAME], n = 0;
        top = 0;
        stack[top++] = start;
        seen[start] = 1;
        while (top) {
            const int i = stack[--top], x = i % size, y = i / size;
            part[n++] = i;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
                    const int j = ny * size + nx;
                    if (clear[j] || seen[j]) continue;
                    seen[j] = 1;
                    stack[top++] = j;
                }
        }
        if (n < 8)
            for (int k = 0; k < n; ++k) clear[part[k]] = 1;
    }
    int x0 = size, y0 = size, x1 = -1, y1 = -1;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            if (clear[y * size + x]) continue;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    if (x1 < 0) return;
    s_icon_w[icon] = x1 - x0 + 1 < ICON ? x1 - x0 + 1 : ICON;
    s_icon_h[icon] = y1 - y0 + 1 < ICON ? y1 - y0 + 1 : ICON;
    for (int y = 0; y < s_icon_h[icon]; ++y)
        for (int x = 0; x < s_icon_w[icon]; ++x) {
            const int from = (y + y0) * size + x + x0;
            s_icon[icon][y * ICON + x] = clear[from] ? TRANSPARENT : pixels[from];
        }
}

/* Script op lengths for the few ops a form's start script opens with. */
static int op_length(unsigned op) {
    switch (op) {
    case 0x30: return 2;
    case 0x1C: case 0x4A: case 0x4C: case 0x52: return 3;
    case 0x2E: case 0x32: case 0x40: case 0x94: case 0x98: return 4;
    case 0x0C: case 0x0E: return 6;
    }
    return 0;
}

/* The tinkerbat's head, from her idle frame, as the menu's palette 2: her
 * sprite's colors 1, 2 and 3 as its light, shadow and outline. */
static void load_tinkerbat(const GBContext *ctx) {
    const int form = SHANTAE_FORM_TINKERBAT;
    for (int i = 0; i < 16; ++i) {   /* the dance table, 0E:4342 */
        const unsigned e = 0x4342 + i * 8;
        if (!rom16(ctx, 0x0E, e)) return;
        if (rom8(ctx, 0x0E, e + 7) != (unsigned)form) continue;
        unsigned bank = rom8(ctx, 0x0E, e + 6), at = rom16(ctx, 0x0E, e + 4);
        for (int ops = 0; ops < 24; ++ops) {
            const unsigned op = rom8(ctx, bank, at);
            if (op == 0x40) {   /* goto: the form's start */
                const unsigned to = rom16(ctx, bank, at + 1);
                bank = rom8(ctx, bank, at + 3);
                at = to;
                continue;
            }
            if (op == 0x0C || op == 0x0E) {
                const unsigned fbank = rom8(ctx, bank, at + 1), frame = rom16(ctx, bank, at + 2);
                const unsigned layout = rom16(ctx, fbank, frame), tiles = rom16(ctx, fbank, frame + 8);
                const unsigned count = rom8(ctx, fbank, layout + 32);
                if (count == 0 || count > 16) return;
                uint8_t pixels[FRAME * FRAME];
                memset(pixels, 0, sizeof(pixels));   /* 0: nothing */
                int min_x = 999, min_y = 999;
                for (unsigned p = 0; p < count; ++p) {
                    const int y = (int8_t)rom8(ctx, fbank, layout + 35 + p * 3);
                    const int x = (int8_t)rom8(ctx, fbank, layout + 36 + p * 3);
                    if (y < min_y) min_y = y;
                    if (x < min_x) min_x = x;
                }
                static const uint8_t tone[4] = {0, 2 * 4 + 0, 2 * 4 + 2, 2 * 4 + 3};
                for (unsigned p = 0; p < count; ++p) {
                    const int py = (int8_t)rom8(ctx, fbank, layout + 35 + p * 3) - min_y;
                    const int px = (int8_t)rom8(ctx, fbank, layout + 36 + p * 3) - min_x;
                    const unsigned attr = rom8(ctx, fbank, layout + 37 + p * 3);
                    for (int y = 0; y < 16; ++y)
                        for (int x = 0; x < 8; ++x) {
                            const int sy = (attr & 0x40) ? 15 - y : y, bit = (attr & 0x20) ? x : 7 - x;
                            const unsigned g = tiles + 1 + p * 32 + sy * 2;
                            const unsigned c = ((rom8(ctx, fbank, g) >> bit) & 1) |
                                               (((rom8(ctx, fbank, g + 1) >> bit) & 1) << 1);
                            const int dx = px + x, dy = py + y;
                            if (c && dx >= 0 && dy >= 0 && dx < FRAME && dy < HEAD_ROWS)
                                pixels[dy * FRAME + dx] = tone[c];
                        }
                }
                crop_icon(form, pixels, FRAME, 0, 0);
                return;
            }
            const int n = op_length(op);
            if (!n) return;
            at += n;
        }
        return;
    }
}

static void load_icons(const GBContext *ctx) {
    s_loaded = -1;
    static uint8_t vram[2][0x1800];
    unsigned b0, s0, v0, b1, s1, v1;
    if (!load_call(ctx, 0x05, 0x6547, &b0, &s0, &v0) || !load_call(ctx, 0x05, 0x6552, &b1, &s1, &v1) || v0 != 0 ||
        v1 != 1)
        return;
    const unsigned end = lzss(ctx, b0, s0, vram[0], 0x1800);
    lzss(ctx, b1, s1, vram[1], 0x1800);
    const unsigned map = end + 0x80;
    if (rom8(ctx, b0, map) != 20 || rom8(ctx, b0, map + 1) != 18) return;
    for (int i = 0; i < 64; ++i) {
        s_palettes[0][i] = (uint8_t)rom8(ctx, b0, end + i);
        s_palettes[1][i] = (uint8_t)rom8(ctx, b0, end + 0x40 + i);
    }
    static const struct { int icon, row; } menu[] = {
        {SHANTAE_FORM_MONKEY, 2}, {SHANTAE_FORM_ELEPHANT, 5}, {SHANTAE_FORM_SPIDER, 8}, {SHANTAE_FORM_HARPY, 11},
        {SHANTAE_HEAL, 14},
    };
    for (size_t m = 0; m < sizeof(menu) / sizeof(menu[0]); ++m) {
        uint8_t pixels[ICON * ICON];
        for (int ty = 0; ty < 3; ++ty)
            for (int tx = 0; tx < 3; ++tx) {
                const unsigned cell = map + 2 + ((menu[m].row + ty) * 20 + tx) * 2;
                const unsigned tile = rom8(ctx, b0, cell), attr = rom8(ctx, b0, cell + 1);
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        pixels[(ty * 8 + y) * ICON + tx * 8 + x] =
                            (uint8_t)((attr & 7) * 4 + tile_pixel((const uint8_t(*)[0x1800])vram, tile, attr, x, y));
            }
        crop_icon(menu[m].icon, pixels, ICON, 2 * 4 + 1, 2);
    }
    load_tinkerbat(ctx);
    s_loaded = 1;
}

/* ---- Drawing ---- */

typedef struct {
    uint32_t *out;
    int w, h;
    uint32_t colors[64];   /* the menu's palettes */
} Canvas;

static uint32_t rgb555(const uint8_t *p) {
    const unsigned c = p[0] | p[1] << 8;
    return 0xFF000000u | (((c & 31) * 255 / 31) << 16) | ((((c >> 5) & 31) * 255 / 31) << 8) |
           (((c >> 10) & 31) * 255 / 31);
}

static void put(Canvas *c, int x, int y, uint32_t color) {
    if (x >= 0 && y >= 0 && x < c->w && y < c->h) c->out[y * c->w + x] = color;
}

static void fill(Canvas *c, int x0, int y0, int w, int h, uint32_t color) {
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) put(c, x, y, color);
}

/* LOCKED: a dance not learned, or the heal when it would heal nothing; its
 * icon is dark. CURRENT: the form she is, framed in the menu cursor's yellow. */
typedef enum { STATE_LOCKED, STATE_READY, STATE_CURRENT } SlotState;

/* A slot at x,y: its box and its icon. */
static void slot(Canvas *c, int x, int y, int icon, SlotState state) {
    const uint32_t *p = c->colors + 2 * 4;   /* the menu's palette 2 */
    const uint32_t light = p[0], panel = p[1], shadow = p[2], dark = p[3];
    const uint32_t frame = state == STATE_CURRENT ? c->colors[3 * 4 + 0] : dark;   /* palette 3: the cursor's yellow */
    fill(c, x, y, SLOT, SLOT, frame);
    fill(c, x + 1, y + 1, SLOT - 2, SLOT - 2, panel);
    if (state != STATE_CURRENT) {
        fill(c, x + 1, y + 1, SLOT - 2, 1, light);
        fill(c, x + 1, y + 1, 1, SLOT - 2, light);
    } else {
        fill(c, x + 1, y + 1, SLOT - 2, 1, frame);
        fill(c, x + 1, y + 1, 1, SLOT - 2, frame);
        fill(c, x + SLOT - 2, y + 1, 1, SLOT - 2, frame);
        fill(c, x + 1, y + SLOT - 2, SLOT - 2, 1, frame);
    }
    if (!s_icon_w[icon]) return;
    const int ix = x + (SLOT - s_icon_w[icon]) / 2, iy = y + (SLOT - s_icon_h[icon]) / 2;
    for (int yy = 0; yy < s_icon_h[icon]; ++yy)
        for (int xx = 0; xx < s_icon_w[icon]; ++xx) {
            const uint8_t e = s_icon[icon][yy * ICON + xx];
            if (e == TRANSPARENT || e == 0) continue;
            put(c, ix + xx, iy + yy, state == STATE_LOCKED ? shadow : c->colors[e & 63]);
        }
}

/* Whether there is a player in the game's hands: her slot is live. */
static int player(const GBContext *ctx) {
    if (!ctx || !ctx->wram || !ctx->rom) return 0;
    const unsigned p = ctx->wram[0xA13] | ctx->wram[0xA14] << 8;
    return shantae_slot_index(ctx, p) >= 0 && *shantae_slot_memory((GBContext *)ctx, p) != 0xFF;
}

int shantae_hud_draw(GBContext *ctx, uint32_t *out, int *width, int *height) {
    *width = *height = 0;
    if (shantae_hud_mode() == SHANTAE_HUD_OFF || !shantae_transform_buttons() || !player(ctx) ||
        shantae_view_town(ctx))
        return 0;
    int learned = 0;
    for (int i = 0; i < BUTTONS; ++i) learned |= shantae_form_learned(ctx, s_order[i]);
    if (!learned) return 0;
    if (!s_loaded) load_icons(ctx);
    if (s_loaded < 0) return 0;

    const int gba = ctx->hram[0x7E] == 3 && !shantae_original_colors();
    Canvas c = {out, SHANTAE_HUD_MAX_W, SHANTAE_HUD_MAX_H, {0}};
    for (int i = 0; i < 64; ++i) c.colors[i] = rgb555(s_palettes[gba] + (i & 31) * 2);
    for (int i = 0; i < c.w * c.h; ++i) out[i] = 0;

    /* In the actions' order, as many to a row as the columns chosen; the
     * tinkerbat (GBA only) once she has her dance. */
    const int columns = shantae_hud_columns();
    const int form_now = shantae_form(ctx);
    int n = 0;
    for (int i = 0; i < BUTTONS; ++i) {
        const int icon = s_order[i];
        const int known = shantae_form_learned(ctx, icon);
        if (icon == SHANTAE_FORM_TINKERBAT && !known) continue;
        SlotState st = !known ? STATE_LOCKED : icon == form_now ? STATE_CURRENT : STATE_READY;
        if (icon == SHANTAE_HEAL && !shantae_heal_possible(ctx)) st = STATE_LOCKED;
        slot(&c, (n % columns) * (SLOT + GAP), (n / columns) * (SLOT + GAP), icon, st);
        n++;
    }
    const int used_w = (n < columns ? n : columns) * (SLOT + GAP) - GAP;
    const int used_h = (n + columns - 1) / columns * (SLOT + GAP) - GAP;
    /* Pack the rows used to the image's own width. */
    for (int y = 0; y < used_h; ++y) memmove(out + y * used_w, out + y * c.w, (size_t)used_w * sizeof(uint32_t));
    *width = used_w;
    *height = used_h;
    return 1;
}

/* ---- The day and night clock ---- */

#define FONT_FIRST 0x30   /* tiles $30-$39 the digits, $3A the colon */
#define OUTLINE_MARK 0x01000000u   /* an outline pixel while the outline is found */
#define FONT_TILES 11
#define COLON 10
static int s_font_loaded;   /* 0 not yet, 1 the numerals, -1 none (not this ROM's layout) */
static uint8_t s_font[FONT_TILES][16];

static void load_font(const GBContext *ctx) {
    s_font_loaded = -1;
    unsigned bank, src, vbk;
    if (!load_call(ctx, 0x04, 0x4DFB, &bank, &src, &vbk) || vbk != 1) return;
    static uint8_t vram[0x1800];
    lzss(ctx, bank, src, vram, sizeof(vram));
    for (int i = 0; i < FONT_TILES; ++i) memcpy(s_font[i], vram + 0x1000 + (FONT_FIRST + i) * 16, 16);
    s_font_loaded = 1;
}

/* The sun and the moon, 7 x 7, in their own colors. */
static const char *const s_sun[7] = {"o..o..o", ".oxxxo.", ".xwwwx.", "oxwwwxo", ".xwwwx.", ".oxxxo.", "o..o..o"};
static const char *const s_moon[7] = {"..ooo..", ".owwo..", "owwo...", "owxo...", "owxo...", ".owwo..", "..ooo.."};

static uint32_t half(uint32_t a, uint32_t b) {
    return 0xFF000000u | ((a >> 1 & 0x7F7F7F) + (b >> 1 & 0x7F7F7F));
}

int shantae_clock_draw(GBContext *ctx, uint32_t *out, int *width, int *height) {
    *width = *height = 0;
    if (!player(ctx) || !ctx->ppu) return 0;
    if (!s_font_loaded) load_font(ctx);
    if (s_font_loaded < 0) return 0;
    int night, seconds, stopped;
    shantae_map_clock(ctx, &night, &seconds, &stopped);

    const uint8_t *palette = ((const GBPPU *)ctx->ppu)->bg_palette_ram + 7 * 8;
    const uint32_t back = rgb555(palette + 6), ink = rgb555(palette + 2);
    const uint32_t text = stopped ? half(ink, back) : ink;
    Canvas c = {out, SHANTAE_CLOCK_W, SHANTAE_CLOCK_H, {0}};
    fill(&c, 0, 0, c.w, c.h, 0);

    /* The sun or the moon, then M:SS a tile each as the status bar has them
     * (a glyph starts on its tile's second row, a digit's on its first column
     * with the last one clear), but the colon, whose glyph is its tile's
     * second and third columns, with a column clear on each side. */
    const char *const *art = night ? s_moon : s_sun;
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) {
            const char p = art[y][x];
            if (p == '.') continue;
            const uint32_t color = night ? (p == 'o' ? 0xFF8890C8u : p == 'w' ? 0xFFF8F0C8u : 0xFFD8D0A0u)
                                         : (p == 'o' ? 0xFFF89820u : p == 'w' ? 0xFFFFF8C0u : 0xFFF8D040u);
            put(&c, 2 + x, 2 + y, color);
        }
    const int minutes = seconds / 60 > 9 ? 9 : seconds / 60;
    const int text_tiles[4] = {minutes, COLON, seconds % 60 / 10, seconds % 10};
    static const int tile_x[4] = {11, 18, 22, 30};
    for (int k = 0; k < 4; ++k) {
        const uint8_t *tile = s_font[text_tiles[k]];
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                const int color = ((tile[y * 2] >> bit) & 1) | (((tile[y * 2 + 1] >> bit) & 1) << 1);
                if (color == 1) put(&c, tile_x[k] + x, 1 + y, text);
            }
    }
    /* No box: around everything drawn, an outline of the status bar's black,
     * so it reads over a bright sky as on the bar. */
    for (int y = 0; y < c.h; ++y)
        for (int x = 0; x < c.w; ++x) {
            if (out[y * c.w + x]) continue;
            int near = 0;
            for (int dy = -1; dy <= 1 && !near; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    const uint32_t n = nx >= 0 && ny >= 0 && nx < c.w && ny < c.h ? out[ny * c.w + nx] : 0;
                    if (n && n != OUTLINE_MARK) near = 1;
                }
            if (near) out[y * c.w + x] = OUTLINE_MARK;
        }
    for (int i = 0; i < c.w * c.h; ++i)
        if (out[i] == OUTLINE_MARK) out[i] = back;
    *width = c.w;
    *height = c.h;
    return 1;
}
