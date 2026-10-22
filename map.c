/*
 * map.c -- the map screen: Shantae's world as sector cells, laid out as
 * qwert_vs_qwerty's World Map on vgmaps.de lays it out, and the area she is
 * in as its terrain; heart holders, fireflies, warp squids, talismans, saves,
 * caves and labyrinth doors marked; explored as she goes; and the time to
 * nightfall. tools/map_extract.py is its research side and its check.
 *
 * Rooms. 03:7920 loads a room from a 17-byte header inline after a far call
 * to it (CD 40 05, then 20 79 03): the camera's least x and y (C9E9-C9EA,
 * C9EC-C9ED), the camera range's width and height (C9F4-C9F7; the greatest
 * x and y, C9DD-C9DE and C9E1-C9E2, are least + size - 1), C9F8, the map
 * directory's page and bank (C9F9-C9FA), FFDD, FFDE, the metatile bank
 * FFDF, C399 and the spawn record directory's page and bank (C39A-C39B).
 * What the room shows is the camera's range plus the screen above the status
 * bar, 160 x 128. An area here is a room, found by the address of its call;
 * its cells are the 256 x 256 sectors that room shows, but for slivers under
 * 64 pixels at its edges (qwert counts Spider Forest's 19 x 2 sectors, not
 * the 16 pixels of a twentieth). A labyrinth is a map of its own, every drawn
 * sector of it.
 *
 * Maps. 32 x 32 sectors; the directory has two bytes a sector, a page of
 * cells and its bank; each cell two bytes, a metatile bank offset from FFDF
 * in the low nibble and the metatile's address; a metatile 16 bytes, the
 * collision bytes of its four 8 x 8 quarters from +8 (00:3814 reads them).
 * The terrain is those bytes, a map pixel for each quarter: $00 open, $02
 * and $62 wall (00:3297), $04-$12 and $36-$3E slopes, $2C and $2E ledges
 * one can stand on, and the rest water, hazards and climbable things.
 *
 * Markers. The spawn record directory (C39A) has a pointer and a bank for
 * each sector; each list is a count and 17-byte records: the box's right,
 * left, bottom and top, a flag, the object (+10, +12: its stub in the table
 * at 0A:4000) and four parameters. The objects that matter, checked against
 * qwert's World Map and Cleeem's area maps:
 * - heart holders 0A:41A4 (seven), fireflies 0A:41A0 (thirteen, at night:
 *   11:72F3 tests the clock), warp squids 0A:4098 (five in each labyrinth),
 *   the four talismans 0A:418C-0A:4198 (Harpy Talon, Monkey Claw, Elephant
 *   Tote, Spider Venom), and the save statues 0A:41C0;
 * - cave mouths, an object of each area's own bank (0A:41DC the forests,
 *   0A:420C the wastelands, 0A:41F0 the waterfalls, 0A:4234 the desert,
 *   0A:425C the bog) whose first two parameters are a cave and its end, as
 *   the cave's own ladder (0A:41BC) has them.
 * A collectible's script points CA6D at a table in bank 11 (op 9A) and calls
 * 04:477A to test, 04:4726 to set, its flag: the object's number (its slot
 * +20, from a parameter) indexes two bytes, an offset from CB1B and a mask.
 * Heart holders 11:6EFF, fireflies 11:6F4F and talismans 11:6EEF take the
 * first parameter, warp squids 11:6F0F the fourth.
 *
 * Exploring. Each frame the game runs, the cells the camera shows are marked
 * explored, in the WRAM extension (shantae_map_memory), which save states,
 * state files and rollback carry. A save file keeps them in cartridge RAM the
 * game never uses: its three files are 206 bytes each at A000, A400 and A800
 * (04:4CF1 copies CA70-CB3B there and a checksum; 04:4D55 copies it back;
 * nothing else turns cartridge RAM on), and from AC00 each file has $600
 * bytes of the map's. The ends of those routines are hooks (extras.c): a file
 * saved copies the map's bytes out, one loaded copies them in, one found
 * empty (a new game: 0A:4474 then sets up and saves it) starts with nothing
 * explored, and one erased (05:7359) has its map bytes cleared. CD1D is the
 * file.
 *
 * The clock. 01:6039 adds 4 to C3E9-C3EA each frame unless C3D5 is set; when
 * bit 15 changes (the high byte's bit 7) it sets C3FC, and C3FD when that
 * made it night. So night is the top half, 8192 frames each: 2:17 of the
 * Game Boy's 59.73 frames a second.
 *
 * The Zombie Caravan. It stops at night between two areas, at one of four
 * borders: walking off an area's edge onto one (CBF3 1 left, 2 right), the
 * area's exit asks 12:427C with A the stop whether the caravan is there (E 0:
 * CAB9 is that stop and it is night) and goes to its gates (18:50CA-18:5174,
 * which set CC20 to the stop and CB71 to the side she came from) instead of
 * the next area. The stops are 1 Mens Desert and Naga Wasteland (15:43BD,
 * 23:46DA), 2 Eagle Wasteland and Mud Bog (23:4736, 21:64AC), 3 Rain Storm
 * and Snail Wasteland (21:5B2D, 23:4703) and 4 the Ghost and Archer Forests
 * (19:4464, 19:4487), where a new game starts it (04:4884) and qwert's map
 * has it. 12:427C first calls 12:425D: once the story counter CA9D is 6, the
 * first time it is called after a night began (C3FD) it clears C3FD and rolls
 * CAB9 anew, any stop but the one it was at (00:0B13's random number). So
 * from then on its stop is known only once a border it can stop at has been
 * crossed after nightfall. "The Zombie Caravan stays" (extras.c's hook on the
 * CP $06 at 12:4260) keeps it at stop 4: no roll, and CAB9 4.
 */
#include "map.h"
#include "hud.h"
#include "gbrt.h"
#include "game_extras.h"
#include "object_slots.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- The world's layout ---- */

enum {
    KIND_DESERT, KIND_WASTELAND, KIND_FIELD, KIND_FOREST, KIND_WATER, KIND_STORM, KIND_SWAMP,
    KIND_MOUNTAIN, KIND_CAVE, KIND_TOWN, KIND_LABYRINTH, KIND_COUNT
};
#define WHOLE 1    /* every drawn sector of the room's map */
#define ALIAS 2    /* only where she is: a town's street, at its gate's cell */
#define CARAVAN 4  /* only where she is: the Zombie Caravan, at its stop */

typedef struct {
    uint8_t bank;
    uint16_t at;     /* the room's far call to 03:7920 */
    uint8_t page;    /* 0 the world; 1 and on a labyrinth's own */
    int16_t cx, cy;  /* its top left sector on the page */
    uint8_t kind, flags;
} AreaDef;

/* As qwert's World Map: the overworld left to right on its ground line, the
 * towns at their gates, the caves under their mouths. */
static const AreaDef s_area_defs[] = {
    {0x15, 0x4406, 0, 0, 0, KIND_DESERT, 0},          /* Mens Desert, 17 x 3 */
    {0x23, 0x4765, 0, 17, 1, KIND_WASTELAND, 0},      /* Naga Wasteland, 16 x 2 */
    {0x18, 0x403C, 0, 33, 2, KIND_TOWN, 0},           /* Scuttle Town's gate */
    {0x0B, 0x4041, 0, 34, 1, KIND_FIELD, 0},          /* Scarecrow Field, 15 x 2 */
    {0x19, 0x44C1, 0, 49, 1, KIND_FOREST, 0},         /* Spider Forest, 19 x 2 */
    {0x0C, 0x5468, 0, 68, 2, KIND_TOWN, 0},           /* Dribble Fountain's square */
    {0x21, 0x43D5, 0, 69, 2, KIND_WATER, 0},          /* Waterfall, 16 x 3 */
    {0x18, 0x46AB, 0, 85, 4, KIND_TOWN, 0},           /* Water Town's gate */
    {0x21, 0x57C4, 0, 86, 3, KIND_STORM, 0},          /* Rain Storm, 14 x 2 */
    {0x23, 0x477D, 0, 100, 3, KIND_WASTELAND, 0},     /* Snail Wasteland, 14 x 2 */
    {0x0B, 0x6333, 0, 114, 4, KIND_SWAMP, 0},         /* Swamp, 14 x 1 */
    {0x19, 0x4509, 0, 128, 2, KIND_FOREST, 0},        /* Ghost Forest, 13 x 3 */
    {0x19, 0x4521, 0, 142, 1, KIND_FOREST, 0},        /* Archer Forest, 13 x 2 */
    {0x11, 0x405B, 0, 155, -3, KIND_MOUNTAIN, 0},     /* Mount Pointy, 16 x 6 */
    {0x23, 0x4795, 0, 171, 1, KIND_WASTELAND, 0},     /* Eagle Wasteland, 6 x 4 */
    {0x18, 0x571A, 0, 170, 4, KIND_TOWN, 0},          /* Bandit Town's gate */
    {0x21, 0x612D, 0, 177, 2, KIND_SWAMP, 0},         /* Mud Bog, 18 x 3 */
    {0x18, 0x4BBF, 0, 195, 4, KIND_TOWN, 0},          /* Oasis Town's gate */
    {0x15, 0x441E, 0, 196, 2, KIND_DESERT, 0},        /* Ladies Desert, 12 x 3 */
    {0x19, 0x44D9, 0, 60, 5, KIND_FOREST, 0},         /* Spider Forest's lower path, 8 x 1 */
    {0x19, 0x44F1, 0, 56, 3, KIND_FOREST, 0},         /* Spider Forest's pit, 2 x 3 */
    {0x21, 0x43ED, 0, 87, 5, KIND_WATER, 0},          /* Hidden Waterfall, 2 x 3 */
    {0x11, 0x5D0D, 0, 29, 3, KIND_CAVE, 0},           /* cave 1, under Naga Wasteland */
    {0x11, 0x5D25, 0, 21, 3, KIND_CAVE, 0},           /* cave 2, Naga Wasteland to Ladies Desert */
    {0x11, 0x5D3D, 0, 192, 5, KIND_CAVE, 0},          /* cave 3, under Mud Bog */
    {0x11, 0x5D55, 0, 54, 3, KIND_CAVE, 0},           /* cave 4, Spider Forest down */
    {0x11, 0x5D6D, 0, 68, 5, KIND_CAVE, 0},           /* cave 5, to the Waterfall */
    {0x11, 0x5D85, 0, 51, 5, KIND_CAVE, 0},           /* cave 6, the Monkey Claw */
    {0x11, 0x5D9D, 0, 84, 5, KIND_CAVE, 0},           /* cave 7, the Elephant Tote */
    {0x11, 0x5DB5, 0, 116, 6, KIND_CAVE, 0},          /* cave 8, Snail Wasteland to Ghost Forest */
    {0x11, 0x5DCD, 0, 148, 3, KIND_CAVE, 0},          /* cave 9, under Archer Forest */
    {0x11, 0x5DE5, 0, 87, 8, KIND_CAVE, 0},           /* cave 11, Hidden Waterfall to Snail Wasteland */
    {0x11, 0x5DFD, 0, 58, 5, KIND_CAVE, 0},           /* cave 12, from the pit */
    /* The labyrinths, numbered as their warp squids are (1-5, 6-10, 11-15,
     * 16-20), each a map of its own, and the Spy Scope (Risky's, none). */
    {0x09, 0x401F, 1, 0, 0, KIND_LABYRINTH, WHOLE},   /* Dribble Fountain, map 50:4A */
    {0x1E, 0x401F, 2, 0, 0, KIND_LABYRINTH, WHOLE},   /* Golem Mine, 78:BE */
    {0x22, 0x401F, 3, 0, 0, KIND_LABYRINTH, WHOLE},   /* Cackle Mound, 5E:8F */
    {0x26, 0x401F, 4, 0, 0, KIND_LABYRINTH, WHOLE},   /* Twinkle Palace, 5B:CA */
    {0x16, 0x40AB, 5, 0, 0, KIND_LABYRINTH, WHOLE},   /* the Spy Scope, 4D:D4 */
    {0x1D, 0x402E, 6, 0, 0, KIND_LABYRINTH, 0},       /* 4B:72, 30 x 3 (debug grid I1) */
    /* The towns' streets (map 67:9B), each at the gate it leads out to
     * (walked from debug row N). */
    {0x18, 0x5C3D, 0, 33, 2, KIND_TOWN, ALIAS},       /* Scuttle Town */
    {0x18, 0x5FEA, 0, 85, 4, KIND_TOWN, ALIAS},       /* Water Town */
    {0x18, 0x638B, 0, 195, 4, KIND_TOWN, ALIAS},      /* Oasis Town */
    {0x18, 0x6AD5, 0, 170, 4, KIND_TOWN, ALIAS},      /* Bandit Town */
    /* The Zombie Caravan's gates (map 73:59) and its street, at the stop it
     * was entered at. */
    {0x18, 0x51A1, 0, 0, 0, KIND_TOWN, CARAVAN},
    {0x18, 0x6738, 0, 0, 0, KIND_TOWN, CARAVAN},
};

/* The caravan's stops, 1-4 (CAB9), as page pixels: on the border between its
 * two areas (in the column qwert leaves for it between the forests), on the
 * row both have at their edges. */
static const struct { int x, y; } s_stops[4] = {
    {17 * 256, 2 * 256 + 128},         /* Mens Desert | Naga Wasteland */
    {177 * 256, 4 * 256 + 128},        /* Eagle Wasteland | Mud Bog */
    {100 * 256, 4 * 256 + 128},        /* Rain Storm | Snail Wasteland */
    {141 * 256 + 128, 2 * 256 + 128},  /* Ghost Forest | Archer Forest, the first */
};
#define FIRST_STOP 4
#define CARAVAN_STOP 0xCAB9
#define CARAVAN_ENTERED 0xCC20
#define STORY 0xCA9D
#define CARAVAN_MOVES 6         /* CA9D: it moves on each night */
#define NIGHT_BEGAN 0xC3FD
#define AREA_DEFS (int)(sizeof(s_area_defs) / sizeof(s_area_defs[0]))
#define PAGES 7
#define MIN_COVER 64

/* ---- Markers ---- */

enum { MARK_HEART, MARK_FIREFLY, MARK_SQUID, MARK_TALISMAN, MARK_SAVE, MARK_CAVE, MARK_DOOR, MARK_ENEMY, MARK_KINDS };

typedef struct {
    uint16_t stub;     /* in bank 0A */
    uint8_t kind;
    uint16_t table;    /* in bank 11: the flag table, 0 none */
    uint8_t param;     /* which parameter numbers it */
} MarkerDef;

static const MarkerDef s_marker_defs[] = {
    {0x41A4, MARK_HEART, 0x6EFF, 0},
    {0x41A0, MARK_FIREFLY, 0x6F4F, 0},
    {0x4098, MARK_SQUID, 0x6F0F, 3},
    {0x4190, MARK_TALISMAN, 0x6EEF, 0},
    {0x4194, MARK_TALISMAN, 0x6EEF, 0},
    {0x4198, MARK_TALISMAN, 0x6EEF, 0},
    {0x418C, MARK_TALISMAN, 0x6EEF, 0},
    {0x41C0, MARK_SAVE, 0, 0},
    {0x41DC, MARK_CAVE, 0, 0},
    {0x420C, MARK_CAVE, 0, 0},
    {0x41F0, MARK_CAVE, 0, 0},
    {0x4234, MARK_CAVE, 0, 0},
    {0x425C, MARK_CAVE, 0, 0},
    {0x41BC, MARK_CAVE, 0, 0},
    {0x4238, MARK_DOOR, 0, 0},     /* Golem Mine, Mens Desert */
    {0x40B0, MARK_DOOR, 0, 0},     /* Dribble Fountain, its square */
    {0x4228, MARK_DOOR, 0, 0},     /* Cackle Mound, the Swamp */
    {0x426C, MARK_DOOR, 0, 0},     /* Twinkle Palace, Mount Pointy's top */
    {0x4248, MARK_DOOR, 0, 0},     /* the Spy Scope, Ladies Desert */
};
#define MARKER_DEFS (int)(sizeof(s_marker_defs) / sizeof(s_marker_defs[0]))

/* Enemies (the map's enemy layer), by their stubs in bank 0A: the objects
 * whose first frame, spawned in a debug scene and read from their slots
 * (+2E-+30), is a creature; not the jars, platforms, gears and blocks, the
 * caged genies, nor those that drew nothing there. */
static const uint16_t s_enemies[] = {
    0x4064, 0x40C0, 0x40CC, 0x40D0, 0x40DC, 0x40E8, 0x4124, 0x4128, 0x4134, 0x4160, 0x41B4, 0x41C4,
    0x41C8, 0x41CC, 0x41D0, 0x41D8, 0x41E4, 0x41E8, 0x41EC, 0x41F4, 0x4208, 0x4214, 0x4218, 0x421C,
    0x422C, 0x4230, 0x4240, 0x4244, 0x4250, 0x4254, 0x4258, 0x4260, 0x4264, 0x42A4, 0x42B0, 0x42BC,
    0x42C8, 0x42DC, 0x4300, 0x431C, 0x4330, 0x4344, 0x4348, 0x434C, 0x4350, 0x435C,
};
#define ENEMIES (int)(sizeof(s_enemies) / sizeof(s_enemies[0]))

/* ---- What was found in the ROM ---- */

typedef struct {
    const AreaDef *def;
    uint8_t map_page, map_bank, ffdf, rec_page, rec_bank;
    int x0, y0, x1, y1;      /* world pixels it shows */
    int sx0, sy0, w, h;      /* its sectors */
    uint8_t *cells;          /* w x h: 0 none, 1 rock, 2 open */
    int first_bit;           /* its cells in the explored bits */
} Area;

typedef struct {
    uint8_t kind, area;
    int x, y;                /* world pixels */
    uint8_t flag_offset, flag_mask;   /* collected: CB1B + offset & mask; mask 0 none */
} Marker;

static Area s_areas[AREA_DEFS];
static int s_area_count;
static Marker *s_markers;
static int s_marker_count;
static int s_bits;           /* explored bits in all */
static uint16_t s_hash;
static int s_loaded;         /* 0 not yet, 1 done */
/* Where she was last found in an area, for a town's street or a house,
 * which are not on the map: the map opens at the gate she came in by. */
static int s_last_area = -1, s_last_x, s_last_y;

static unsigned rom8(const GBContext *ctx, unsigned bank, unsigned addr) {
    const size_t o = addr < 0x4000 ? addr : bank * 0x4000u + (addr - 0x4000u);
    return o < ctx->rom_size ? ctx->rom[o] : 0xFF;
}
static unsigned rom16(const GBContext *ctx, unsigned bank, unsigned addr) {
    return rom8(ctx, bank, addr) | rom8(ctx, bank, addr + 1) << 8;
}
static unsigned ram8(const GBContext *ctx, unsigned addr) { return ctx->wram[addr - 0xC000]; }
static unsigned ram16(const GBContext *ctx, unsigned addr) { return ram8(ctx, addr) | ram8(ctx, addr + 1) << 8; }

/* The collision byte at world x,y of a map, as 00:3814 reads it, or -1
 * where no sector is drawn. */
static int collision(const GBContext *ctx, const Area *a, int x, int y) {
    if (x < 0 || y < 0 || x >= 8192 || y >= 8192) return -1;
    const unsigned entry = ((unsigned)a->map_page << 8) + (unsigned)(y >> 8) * 64 + (unsigned)(x >> 8) * 2;
    const unsigned cells = rom8(ctx, a->map_bank, entry), cell_bank = rom8(ctx, a->map_bank, entry + 1);
    if (cells < 0x40 || cells >= 0x80 || (cell_bank + 1) * 0x4000u > ctx->rom_size) return -1;
    const unsigned c = (cells << 8) + ((unsigned)(y & 255) >> 4) * 32 + ((unsigned)(x & 255) >> 4) * 2;
    const unsigned lo = rom8(ctx, cell_bank, c), hi = rom8(ctx, cell_bank, c + 1);
    const unsigned bank = a->ffdf + (lo & 15), metatile = hi << 8 | (lo & 0xF0);
    if (metatile < 0x4000 || metatile + 16 > 0x8000) return -1;
    return (int)rom8(ctx, bank, metatile + 8 + ((y & 8) >> 2) + ((x & 8) >> 3));
}

static int wall(int c) { return c == 0x02 || c == 0x62; }

/* Sectors from `from` to `to` (world pixels, to exclusive) that cover
 * MIN_COVER pixels or more: the first and how many. */
static void sector_span(int from, int to, int *first, int *count) {
    int s = from >> 8, last = (to - 1) >> 8;
    while (s <= last && (s + 1) * 256 - from < MIN_COVER) s++;
    while (last >= s && to - last * 256 < MIN_COVER) last--;
    *first = s;
    *count = last >= s ? last - s + 1 : 0;
}

static void load_area(const GBContext *ctx, const AreaDef *d) {
    if (rom8(ctx, d->bank, d->at) != 0xCD || rom16(ctx, d->bank, d->at + 1) != 0x0540 ||
        rom16(ctx, d->bank, d->at + 3) != 0x7920 || rom8(ctx, d->bank, d->at + 5) != 0x03)
        return;
    const unsigned h = d->at + 6;
    Area *a = &s_areas[s_area_count];
    memset(a, 0, sizeof(*a));
    a->def = d;
    a->map_page = (uint8_t)rom8(ctx, d->bank, h + 9);
    a->map_bank = (uint8_t)rom8(ctx, d->bank, h + 10);
    a->ffdf = (uint8_t)rom8(ctx, d->bank, h + 13);
    a->rec_page = (uint8_t)rom8(ctx, d->bank, h + 15);
    a->rec_bank = (uint8_t)rom8(ctx, d->bank, h + 16);
    if (d->flags & WHOLE) {
        a->x0 = a->y0 = 0;
        a->x1 = a->y1 = 8192;
        a->sx0 = a->sy0 = 0;
        a->w = a->h = 32;
    } else {
        const int cx = (int)rom16(ctx, d->bank, h), cy = (int)rom16(ctx, d->bank, h + 2);
        int cw = (int)rom16(ctx, d->bank, h + 4), ch = (int)rom16(ctx, d->bank, h + 6);
        if (cw >= 0x8000) cw = 0;
        if (ch >= 0x8000) ch = 0;
        a->x0 = cx;
        a->y0 = cy;
        a->x1 = cx + (cw ? cw : 1) - 1 + 160;
        a->y1 = cy + (ch ? ch : 1) - 1 + 128;
        if (d->flags & (ALIAS | CARAVAN)) {   /* only where she is: no cells of its own */
            s_area_count++;
            return;
        }
        sector_span(a->x0, a->x1, &a->sx0, &a->w);
        sector_span(a->y0, a->y1, &a->sy0, &a->h);
        if (!a->w || !a->h) return;
    }
    a->cells = calloc((size_t)a->w * a->h, 1);
    if (!a->cells) return;
    for (int j = 0; j < a->h; ++j)
        for (int i = 0; i < a->w; ++i) {
            const int sx = (a->sx0 + i) * 256, sy = (a->sy0 + j) * 256;
            const int x0 = sx > a->x0 ? sx : a->x0, x1 = sx + 256 < a->x1 ? sx + 256 : a->x1;
            const int y0 = sy > a->y0 ? sy : a->y0, y1 = sy + 256 < a->y1 ? sy + 256 : a->y1;
            uint8_t state = 0;
            for (int y = y0 & ~7; y < y1 && state < 2; y += 8)
                for (int x = x0 & ~7; x < x1; x += 8) {
                    const int c = collision(ctx, a, x, y);
                    if (c < 0) continue;
                    if (!wall(c)) {
                        state = 2;
                        break;
                    }
                    state = 1;
                }
            a->cells[j * a->w + i] = state;
        }
    a->first_bit = s_bits;
    s_bits += a->w * a->h;
    s_hash = (uint16_t)(s_hash * 31 + d->page * 7 + d->cx * 5 + d->cy * 3 + a->w * 17 + a->h);
    s_area_count++;
}

static const MarkerDef *marker_def(unsigned bank, unsigned stub) {
    if (bank != 0x0A) return NULL;
    for (int i = 0; i < MARKER_DEFS; ++i)
        if (s_marker_defs[i].stub == stub) return &s_marker_defs[i];
    return NULL;
}
static int enemy(unsigned bank, unsigned stub) {
    if (bank != 0x0A) return 0;
    for (int i = 0; i < ENEMIES; ++i)
        if (s_enemies[i] == stub) return 1;
    return 0;
}

static int area_cell(const Area *a, int x, int y, int *i, int *j) {
    if (x < a->x0 || y < a->y0 || x >= a->x1 || y >= a->y1) return 0;
    const int ci = (x >> 8) - a->sx0, cj = (y >> 8) - a->sy0;
    if (ci < 0 || cj < 0 || ci >= a->w || cj >= a->h || !a->cells[cj * a->w + ci]) return 0;
    *i = ci;
    *j = cj;
    return 1;
}

static void add_marker(const Marker *m) {
    for (int k = 0; k < s_marker_count; ++k)   /* a record listed twice, or a part of the same thing */
        if (s_markers[k].kind == m->kind && abs(s_markers[k].x - m->x) < 24 && abs(s_markers[k].y - m->y) < 24)
            return;
    Marker *grown = realloc(s_markers, (size_t)(s_marker_count + 1) * sizeof(Marker));
    if (!grown) return;
    s_markers = grown;
    s_markers[s_marker_count++] = *m;
}

static void load_markers(const GBContext *ctx, int ai) {
    const Area *a = &s_areas[ai];
    for (int sy = a->sy0; sy < a->sy0 + a->h; ++sy)
        for (int sx = a->sx0; sx < a->sx0 + a->w; ++sx) {
            if (sx >= 32 || sy >= 32) continue;
            const unsigned e = ((a->rec_page + (sy >> 1)) & 0xFF) << 8 | ((sy & 1) << 7) | sx * 4;
            const unsigned list = rom16(ctx, a->rec_bank, e), bank = rom8(ctx, a->rec_bank, e + 2);
            if (list < 0x4000 || list >= 0x8000) continue;
            const unsigned n = rom8(ctx, bank, list);
            for (unsigned r = 0; r < n && r < 64; ++r) {
                const unsigned at = list + 1 + r * 17;
                const unsigned obj = rom16(ctx, bank, at + 10), obj_bank = rom8(ctx, bank, at + 12);
                const MarkerDef *d = marker_def(obj_bank, obj);
                if (!d && !enemy(obj_bank, obj)) continue;
                const int x = (int)(rom16(ctx, bank, at + 2) + rom16(ctx, bank, at)) / 2;
                const int y = (int)(rom16(ctx, bank, at + 6) + rom16(ctx, bank, at + 4)) / 2;
                int i, j;
                if (!area_cell(a, x, y, &i, &j)) continue;
                Marker m = {d ? d->kind : MARK_ENEMY, (uint8_t)ai, x, y, 0, 0};
                if (d && d->table) {
                    const unsigned number = rom8(ctx, bank, at + 13 + d->param);
                    m.flag_offset = (uint8_t)rom8(ctx, 0x11, d->table + number * 2);
                    m.flag_mask = (uint8_t)rom8(ctx, 0x11, d->table + number * 2 + 1);
                }
                add_marker(&m);
            }
        }
}

static void load(const GBContext *ctx) {
    s_loaded = 1;
    s_area_count = s_bits = 0;
    s_hash = 0x5AA7;
    if (!ctx->rom || ctx->rom_size < 0x80 * 0x4000u) return;
    for (int i = 0; i < AREA_DEFS; ++i) load_area(ctx, &s_area_defs[i]);
    for (int i = 0; i < s_area_count; ++i) load_markers(ctx, i);
}

/* ---- Exploring ---- */

#define MAGIC 0x50414D53u       /* "SMAP" */
#define HEADER 8                /* magic, the layout's hash, the bits' count */
#define FILE_BASE 0x0C00        /* AC00 in cartridge RAM */
#define BITS_MAX ((SHANTAE_MAP_BYTES - HEADER) * 8)

static int header_ok(const uint8_t *p) {
    return p && (p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) == MAGIC &&
           (p[4] | p[5] << 8) == s_hash && (p[6] | p[7] << 8) == s_bits;
}
static void header_set(uint8_t *p) {
    memset(p, 0, SHANTAE_MAP_BYTES);
    p[0] = (uint8_t)MAGIC;
    p[1] = (uint8_t)(MAGIC >> 8);
    p[2] = (uint8_t)(MAGIC >> 16);
    p[3] = (uint8_t)(MAGIC >> 24);
    p[4] = (uint8_t)s_hash;
    p[5] = (uint8_t)(s_hash >> 8);
    p[6] = (uint8_t)s_bits;
    p[7] = (uint8_t)(s_bits >> 8);
}

/* The explored bits, set up (nothing explored) when not yet. */
static uint8_t *explored_bits(GBContext *ctx) {
    if (!s_loaded) load(ctx);
    uint8_t *p = shantae_map_memory(ctx);
    if (!p || s_bits > BITS_MAX) return NULL;
    if (!header_ok(p)) header_set(p);
    return p + HEADER;
}

static int bit(const uint8_t *bits, int n) { return bits[n >> 3] >> (n & 7) & 1; }

static uint8_t *file_area(GBContext *ctx) {
    const unsigned file = ram8(ctx, 0xCD1D);
    if (file > 2 || !ctx->eram || ctx->eram_size < FILE_BASE + 3u * SHANTAE_MAP_BYTES) return NULL;
    return ctx->eram + FILE_BASE + file * SHANTAE_MAP_BYTES;
}

void shantae_map_file_saved(GBContext *ctx) {
    uint8_t *to = file_area(ctx), *from = shantae_map_memory(ctx);
    if (!to || !from || !explored_bits(ctx)) return;
    memcpy(to, from, SHANTAE_MAP_BYTES);
    ctx->eram_dirty = 1;
}

void shantae_map_file_loaded(GBContext *ctx, int ok) {
    uint8_t *from = file_area(ctx), *to = shantae_map_memory(ctx);
    if (!to) return;
    if (!s_loaded) load(ctx);
    if (ok && header_ok(from)) memcpy(to, from, SHANTAE_MAP_BYTES);
    else header_set(to);
    s_last_area = -1;
}

void shantae_map_file_erased(GBContext *ctx) {
    uint8_t *p = file_area(ctx);
    if (!p) return;
    memset(p, 0, SHANTAE_MAP_BYTES);
    ctx->eram_dirty = 1;
}

void shantae_map_state_file_loaded(GBContext *ctx) {
    uint8_t *p = shantae_map_memory(ctx), *from = file_area(ctx);
    if (!p) return;
    if (!s_loaded) load(ctx);
    if (!header_ok(p) && header_ok(from)) memcpy(p, from, SHANTAE_MAP_BYTES);
    s_last_area = -1;
}

/* Where she is: the area (its index), and her world position. */
static int locate_now(const GBContext *ctx, int *x, int *y) {
    if (!ctx->wram) return -1;
    const unsigned p = ram16(ctx, 0xCA13);
    if (shantae_slot_index(ctx, p) < 0) return -1;
    const uint8_t *s = shantae_slot_memory((GBContext *)ctx, p);
    if (!s || s[0] == 0xFF) return -1;
    *x = (int16_t)(s[0x34] | s[0x35] << 8);
    *y = (int16_t)(s[0x37] | s[0x38] << 8);
    const unsigned page = ram8(ctx, 0xC9F9), bank = ram8(ctx, 0xC9FA);
    int best = -1;
    long best_size = 0;
    for (int i = 0; i < s_area_count; ++i) {
        const Area *a = &s_areas[i];
        if (a->map_page != page || a->map_bank != bank) continue;
        if (*x < a->x0 || *y < a->y0 || *x >= a->x1 || *y >= a->y1) continue;
        const long size = (long)(a->x1 - a->x0) * (a->y1 - a->y0);
        if (best < 0 || size < best_size) {
            best = i;
            best_size = size;
        }
    }
    if (best >= 0 && (s_areas[best].def->flags & CARAVAN)) {   /* the caravan: at the stop it was entered at */
        int stop = ram8(ctx, CARAVAN_ENTERED);
        if (stop < 1 || stop > 4) stop = ram8(ctx, CARAVAN_STOP);
        if (stop < 1 || stop > 4) stop = FIRST_STOP;
        *x = s_stops[stop - 1].x;
        *y = s_stops[stop - 1].y;
        return best;
    }
    if (best >= 0 && (s_areas[best].def->flags & ALIAS)) {   /* a town's street: at its gate */
        const AreaDef *d = s_areas[best].def;
        for (int i = 0; i < s_area_count; ++i) {
            const Area *a = &s_areas[i];
            if (a->cells && a->def->page == d->page && a->def->cx == d->cx && a->def->cy == d->cy) {
                *x = (a->x0 + a->x1) / 2;
                *y = (a->y0 + a->y1) / 2;
                return i;
            }
        }
        return -1;
    }
    return best;
}

/* Where she is, or was last found: for the map. */
static int locate(const GBContext *ctx, int *x, int *y) {
    const int now = locate_now(ctx, x, y);
    if (now >= 0) {
        s_last_area = now;
        s_last_x = *x;
        s_last_y = *y;
        return now;
    }
    *x = s_last_x;
    *y = s_last_y;
    return s_last_area;
}

void shantae_map_frame(GBContext *ctx) {
    if (!ctx->wram || shantae_slot_index(ctx, ram16(ctx, 0xCA13)) < 0) return;   /* no player yet */
    uint8_t *bits = explored_bits(ctx);
    int x, y;
    const int ai = bits ? locate(ctx, &x, &y) : -1;
    if (ai < 0 || ai != locate_now(ctx, &x, &y)) return;
    const Area *a = &s_areas[ai];
    if (a->map_page != ram8(ctx, 0xC9F9) || a->map_bank != ram8(ctx, 0xC9FA)) {   /* in a town: its gate */
        for (int n = a->first_bit; n < a->first_bit + a->w * a->h; ++n) bits[n >> 3] |= (uint8_t)(1 << (n & 7));
        return;
    }
    const int cam_x = (int)(ctx->hram[0x61] | ctx->hram[0x62] << 8), cam_y = (int)(ctx->hram[0x63] | ctx->hram[0x64] << 8);
    for (int sy = cam_y >> 8; sy <= (cam_y + 127) >> 8; ++sy)
        for (int sx = cam_x >> 8; sx <= (cam_x + 159) >> 8; ++sx) {
            const int i = sx - a->sx0, j = sy - a->sy0;
            if (i < 0 || j < 0 || i >= a->w || j >= a->h) continue;
            const int n = a->first_bit + j * a->w + i;
            bits[n >> 3] |= (uint8_t)(1 << (n & 7));
        }
}

/* ---- The clock ---- */

void shantae_map_clock(const GBContext *ctx, int *night, int *seconds, int *stopped) {
    const unsigned clock = ram16(ctx, 0xC3E9);
    *night = clock >> 15;
    const unsigned frames = (0x8000 - (clock & 0x7FFF) + 3) / 4;
    *seconds = (int)((frames * 10000u + 597274u) / 597275u);   /* 59.7275 frames a second, rounded up */
    *stopped = ram8(ctx, 0xC3D5) != 0;
}

/* ---- Drawing ---- */

typedef struct {
    uint32_t *out;
    int w, h;
} Canvas;

static void put(Canvas *c, int x, int y, uint32_t color) {
    if (x >= 0 && y >= 0 && x < c->w && y < c->h) c->out[y * c->w + x] = color;
}
static void fill(Canvas *c, int x0, int y0, int w, int h, uint32_t color) {
    if (x0 < 0) w += x0, x0 = 0;
    if (y0 < 0) h += y0, y0 = 0;
    if (x0 + w > c->w) w = c->w - x0;
    if (y0 + h > c->h) h = c->h - y0;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) c->out[y * c->w + x] = color;
}

static uint32_t shade(uint32_t c, int num, int den) {
    const uint32_t r = ((c >> 16) & 255) * num / den, g = ((c >> 8) & 255) * num / den, b = (c & 255) * num / den;
    return (c & 0xFF000000u) | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
}

/* qwert's colors for each kind of area. */
static const uint32_t s_kind_color[KIND_COUNT] = {
    0xFFF8F0A0, 0xFFF8C898, 0xFFF8C898, 0xFFC8F8C0, 0xFFC8F8F8, 0xFFF8A8D0, 0xFFF8A8D0,
    0xFFC0E0F0, 0xFFC0A0F8, 0xFFB0F0B0, 0xFFF8D0A8,
};
#define BACKGROUND 0xF0101028u
#define ROCK 0xFF707078u

/* Icons, 7 x 7: '.' clear, 'o' outline, 'x' fill, 'w' light. */
static const char *const s_icons[MARK_KINDS][7] = {
    {".oo.oo.", "oxxoxxo", "oxwxxxo", "oxxxxxo", ".oxxxo.", "..oxo..", "...o..."},   /* heart */
    {"...o...", ".o.x.o.", "..xwx..", "oxwwwxo", "..xwx..", ".o.x.o.", "...o..."},   /* firefly */
    {"..ooo..", ".oxxxo.", "oxwxwxo", "oxxxxxo", "oxoxoxo", "ox.x.xo", "o..o..o"},   /* warp squid */
    {"...o...", "..oxo..", "ooxxxoo", "oxxwxxo", ".oxxxo.", "oxxoxxo", "oo...oo"},   /* talisman */
    {"..ooo..", ".oxxxo.", "oxoxoxo", "oxxxxxo", "oxoooxo", ".oxxxo.", "..ooo.."},   /* save */
    {"...o...", "..oxo..", "..oxo..", ".oxxxo.", ".oxwxo.", "oxxxxxo", "ooooooo"},   /* cave */
    {"...o...", "..oxo..", ".oxxxo.", "oxxwxxo", "oxxxxxo", ".oxxxo.", ".ooooo."},   /* door */
    {".......", ".......", "..ooo..", "..oxo..", "..ooo..", ".......", "......."},   /* enemy */
};
static const uint32_t s_icon_fill[MARK_KINDS] = {
    0xFFF02838, 0xFFF8E040, 0xFFF08830, 0xFFE02828, 0xFFB8B8C0, 0xFFF8F8F8, 0xFF9030B0, 0xFFF04040,
};

static void icon(Canvas *c, int kind, int x, int y, int dim) {
    const uint32_t fill_color = dim ? 0xFF606068u : s_icon_fill[kind];
    for (int j = 0; j < 7; ++j)
        for (int i = 0; i < 7; ++i) {
            const char p = s_icons[kind][j][i];
            if (p == '.') continue;
            put(c, x - 3 + i, y - 3 + j, p == 'o' ? 0xFF000000u : p == 'w' ? 0xFFFFFFFFu : fill_color);
        }
}

/* The day and night clock (hud.c), at the top right. */
static void draw_clock(Canvas *c, GBContext *ctx) {
    uint32_t clock[SHANTAE_CLOCK_W * SHANTAE_CLOCK_H];
    int w, h;
    if (!shantae_clock_draw(ctx, clock, &w, &h)) return;
    const int x0 = c->w - w - 2, y0 = 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (clock[y * w + x] >> 24) put(c, x0 + x, y0 + y, clock[y * w + x]);
}

/* ---- The screen ---- */

enum { ZOOM_CELLS, ZOOM_TERRAIN };
static const int s_cell_px[2] = {8, 32};   /* map pixels a sector */

static struct {
    int open, page, zoom;
    int center_x, center_y;   /* page pixels, 256 a sector */
    int held;                 /* buttons held last time (edges) */
    unsigned ticks;
} s_view;

static int s_debug_whole = -1;   /* shantae_map_debug_view: the whole map and enemies, or not */

int shantae_map_is_open(void) { return s_view.open; }

/* A world position in an area as page pixels. */
static void page_pos(const Area *a, int x, int y, int *px, int *py) {
    *px = (a->def->cx - a->sx0) * 256 + x;
    *py = (a->def->cy - a->sy0) * 256 + y;
}

static void center_on_her(GBContext *ctx) {
    int x, y;
    const int ai = locate(ctx, &x, &y);
    if (ai < 0) return;
    s_view.page = s_areas[ai].def->page;
    page_pos(&s_areas[ai], x, y, &s_view.center_x, &s_view.center_y);
}

int shantae_map_toggle(GBContext *ctx) {
    if (!s_loaded) load(ctx);
    if (s_view.open) {
        s_view.open = 0;
        gb_platform_set_game_screen(0);
        return 0;
    }
    int x, y;
    if (!shantae_map_on() || locate(ctx, &x, &y) < 0) return 0;
    s_view.open = 1;
    s_view.zoom = ZOOM_TERRAIN;
    s_view.held = 0xFF;   /* the buttons held now are not presses */
    center_on_her(ctx);
    gb_platform_set_game_screen(1);
    return 1;
}

void shantae_map_input(GBContext *ctx, int buttons) {
    if (!s_view.open) return;
    const int pressed = buttons & ~s_view.held;
    s_view.held = buttons;
    if (pressed & (0x20 | 0x80)) {   /* B, Start */
        shantae_map_toggle(ctx);
        return;
    }
    if (pressed & 0x10) s_view.zoom ^= 1;   /* A */
    if (pressed & 0x40) {                    /* Select: the world, or back to where she is */
        if (s_view.page) {
            s_view.page = 0;
            for (int i = 0; i < s_area_count; ++i)
                if (s_areas[i].def->page == 0) {
                    page_pos(&s_areas[i], s_areas[i].x0, s_areas[i].y0, &s_view.center_x, &s_view.center_y);
                    break;
                }
        } else {
            center_on_her(ctx);
        }
    }
    const int step = s_view.zoom == ZOOM_CELLS ? 64 : 16;   /* page pixels a frame: 2 and 4 map pixels */
    if (buttons & 0x01) s_view.center_x += step;
    if (buttons & 0x02) s_view.center_x -= step;
    if (buttons & 0x04) s_view.center_y -= step;
    if (buttons & 0x08) s_view.center_y += step;
}

/* The terrain's colors for a collision byte, in an area of `color`. */
static uint32_t terrain_color(int c, uint32_t color) {
    if (c < 0) return 0;
    if (c == 0) return shade(color, 1, 4) | 0xFF000000u;
    if (wall(c)) return color;
    if ((c >= 0x04 && c <= 0x12) || (c >= 0x36 && c <= 0x3E)) return shade(color, 3, 4);
    if (c == 0x2C || c == 0x2E) return 0xFFF8F8E8u;
    return 0xFF5890E0u;
}

static void draw_area(Canvas *c, const GBContext *ctx, const Area *a, const uint8_t *bits, int whole) {
    const int S = s_cell_px[s_view.zoom];
    const uint32_t color = s_kind_color[a->def->kind];
    for (int j = 0; j < a->h; ++j)
        for (int i = 0; i < a->w; ++i) {
            const int state = a->cells[j * a->w + i];
            if (!state || (!whole && !bit(bits, a->first_bit + j * a->w + i))) continue;
            const int X = ((a->def->cx + i) * 256 - s_view.center_x) * S / 256 + c->w / 2;
            const int Y = ((a->def->cy + j) * 256 - s_view.center_y) * S / 256 + c->h / 2;
            if (X >= c->w || Y >= c->h || X + S <= 0 || Y + S <= 0) continue;
            if (s_view.zoom == ZOOM_CELLS) {
                fill(c, X, Y, S, S, state == 2 ? color : ROCK);
                fill(c, X + S - 1, Y, 1, S, shade(color, 3, 5));
                fill(c, X, Y + S - 1, S, 1, shade(color, 3, 5));
                /* The area's edge, where the next cell is not its own. */
                if (i == 0 || !a->cells[j * a->w + i - 1]) fill(c, X, Y, 1, S, 0xFF000000u);
                if (i == a->w - 1 || !a->cells[j * a->w + i + 1]) fill(c, X + S - 1, Y, 1, S, 0xFF000000u);
                if (j == 0 || !a->cells[(j - 1) * a->w + i]) fill(c, X, Y, S, 1, 0xFF000000u);
                if (j == a->h - 1 || !a->cells[(j + 1) * a->w + i]) fill(c, X, Y + S - 1, S, 1, 0xFF000000u);
                continue;
            }
            const int sx = (a->sx0 + i) * 256, sy = (a->sy0 + j) * 256;
            for (int ty = 0; ty < 32; ++ty) {
                const int y = Y + ty, wy = sy + ty * 8;
                if (y < 0 || y >= c->h || wy < a->y0 || wy >= a->y1) continue;
                for (int tx = 0; tx < 32; ++tx) {
                    const int x = X + tx, wx = sx + tx * 8;
                    if (x < 0 || x >= c->w || wx < a->x0 || wx >= a->x1) continue;
                    const uint32_t t = terrain_color(collision(ctx, a, wx, wy), color);
                    if (t) c->out[y * c->w + x] = t;
                }
            }
        }
}

static int collected(const GBContext *ctx, const Marker *m) {
    return m->flag_mask && (ram8(ctx, 0xCB1B + m->flag_offset) & m->flag_mask);
}

static void draw_markers(Canvas *c, const GBContext *ctx, const uint8_t *bits, int whole) {
    const int S = s_cell_px[s_view.zoom];
    int night, seconds, stopped;
    shantae_map_clock(ctx, &night, &seconds, &stopped);
    const int enemies = s_debug_whole >= 0 ? s_debug_whole : shantae_map_enemies();
    for (int pass = 0; pass < 2; ++pass)   /* enemies under the rest */
        for (int k = 0; k < s_marker_count; ++k) {
            const Marker *m = &s_markers[k];
            const Area *a = &s_areas[m->area];
            if (a->def->page != s_view.page || (m->kind == MARK_ENEMY) != (pass == 0)) continue;
            if (m->kind == MARK_ENEMY && !enemies) continue;
            if (m->kind == MARK_FIREFLY && !night && s_debug_whole != 1) continue;
            int i, j;
            if (!area_cell(a, m->x, m->y, &i, &j)) continue;
            if (!whole && !bit(bits, a->first_bit + j * a->w + i)) continue;
            int px, py;
            page_pos(a, m->x, m->y, &px, &py);
            const int X = (px - s_view.center_x) * S / 256 + c->w / 2;
            const int Y = (py - s_view.center_y) * S / 256 + c->h / 2;
            icon(c, m->kind, X, Y, collected(ctx, m));
        }
}

/* ---- The Zombie Caravan ---- */

uint8_t shantae_map_caravan_roll(GBContext *ctx, uint8_t orig) {
    if (!shantae_caravan_stays() || !ctx->wram) return orig;
    ctx->wram[CARAVAN_STOP - 0xC000] = FIRST_STOP;
    return 0xFF;   /* CA9D is never $FF: no roll */
}

/* Where the caravan is: its stop (1-4) and whether it is there now (at
 * night), or comes there at nightfall (by day, where it never moves on). 0
 * when that is not known: by day once it moves on each night, and at night
 * until a border it can stop at is crossed (12:425D rolls it then). */
static int caravan_stop(const GBContext *ctx, int *now) {
    int night, seconds, stopped;
    shantae_map_clock(ctx, &night, &seconds, &stopped);
    *now = night;
    if (shantae_caravan_stays()) return FIRST_STOP;
    const int stop = ram8(ctx, CARAVAN_STOP);
    if (stop < 1 || stop > 4) return 0;
    if (ram8(ctx, STORY) != CARAVAN_MOVES) return stop;
    return night && !ram8(ctx, NIGHT_BEGAN) ? stop : 0;
}

/* Whether a cell of the world page at column i, row j is shown. */
static int shown_cell(const uint8_t *bits, int whole, int i, int j) {
    for (int k = 0; k < s_area_count; ++k) {
        const Area *a = &s_areas[k];
        if (a->def->page != 0 || !a->cells) continue;
        const int ci = i - a->def->cx, cj = j - a->def->cy;
        if (ci < 0 || cj < 0 || ci >= a->w || cj >= a->h || !a->cells[cj * a->w + ci]) continue;
        if (whole || bit(bits, a->first_bit + cj * a->w + ci)) return 1;
    }
    return 0;
}

/* A covered wagon, 7 x 7: '.' clear, 'o' outline, 'x' its cover, 'w' its
 * lantern-lit windows. */
static const char *const s_wagon[7] = {".ooooo.", "oxxxxxo", "oxwxwxo", "oxxxxxo", "ooooooo", ".oo.oo.", ".oo.oo."};

/* The caravan at its stop on the world page, once a cell beside it is shown:
 * bright while it is there, dimmed by day where it comes at nightfall. */
static void draw_caravan(Canvas *c, const GBContext *ctx, const uint8_t *bits, int whole) {
    int now;
    const int stop = caravan_stop(ctx, &now);
    if (!stop || s_view.page != 0) return;
    const int px = s_stops[stop - 1].x, py = s_stops[stop - 1].y;
    if (!shown_cell(bits, whole, (px - 129) >> 8, py >> 8) && !shown_cell(bits, whole, (px + 128) >> 8, py >> 8))
        return;
    const int S = s_cell_px[s_view.zoom];
    const int X = (px - s_view.center_x) * S / 256 + c->w / 2, Y = (py - s_view.center_y) * S / 256 + c->h / 2;
    for (int j = 0; j < 7; ++j)
        for (int i = 0; i < 7; ++i) {
            const char p = s_wagon[j][i];
            if (p == '.') continue;
            const uint32_t color = p == 'o' ? 0xFF000000u : p == 'w' ? 0xFFF8E070u : 0xFF78C078u;
            put(c, X - 3 + i, Y - 3 + j, now || p == 'o' ? color : shade(color, 1, 2) | 0xFF000000u);
        }
}

static void draw_her(Canvas *c, GBContext *ctx) {
    int x, y;
    const int ai = locate(ctx, &x, &y);
    if (ai < 0 || s_areas[ai].def->page != s_view.page || (s_view.ticks / 16) % 4 == 3) return;
    const int S = s_cell_px[s_view.zoom];
    int px, py;
    page_pos(&s_areas[ai], x, y, &px, &py);
    const int X = (px - s_view.center_x) * S / 256 + c->w / 2, Y = (py - s_view.center_y) * S / 256 + c->h / 2;
    fill(c, X - 3, Y - 3, 7, 7, 0xFFFFFFFFu);
    fill(c, X - 2, Y - 2, 5, 5, 0xFFE040A0u);
}

void shantae_map_debug_view(GBContext *ctx, int zoom, int page, int x, int y, int whole) {
    if (!s_loaded) load(ctx);
    s_view.zoom = zoom ? ZOOM_TERRAIN : ZOOM_CELLS;
    s_debug_whole = whole;
    if (page < 0) {
        center_on_her(ctx);
        return;
    }
    s_view.page = page;
    s_view.center_x = x;
    s_view.center_y = y;
}

int shantae_map_draw(GBContext *ctx, uint32_t *out, int width, int height) {
    if (!ctx || !ctx->wram || width <= 0 || height <= 0) return 0;
    const uint8_t *bits = explored_bits(ctx);
    if (!bits || !s_area_count) return 0;
    Canvas c = {out, width, height};
    for (int i = 0; i < width * height; ++i) out[i] = BACKGROUND;
    const int whole = s_debug_whole >= 0 ? s_debug_whole : shantae_map_whole();
    for (int i = 0; i < s_area_count; ++i)
        if (s_areas[i].def->page == s_view.page) draw_area(&c, ctx, &s_areas[i], bits, whole);
    draw_markers(&c, ctx, bits, whole);
    draw_caravan(&c, ctx, bits, whole);
    draw_her(&c, ctx);
    draw_clock(&c, ctx);
    s_view.ticks++;
    return 1;
}

void shantae_map_init(GBContext *ctx) {
    (void)ctx;
    memset(&s_view, 0, sizeof(s_view));
}

int shantae_map_describe(GBContext *ctx, const char *path) {
    const uint8_t *bits = explored_bits(ctx);
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f) return 0;
    static const char *const kinds[MARK_KINDS] = {"heart", "firefly", "squid", "talisman", "save", "cave", "door", "enemy"};
    fprintf(f, "{\"areas\":[");
    for (int i = 0; i < s_area_count; ++i) {
        const Area *a = &s_areas[i];
        fprintf(f, "%s\n{\"room\":\"%02X:%04X\",\"page\":%d,\"cx\":%d,\"cy\":%d,\"w\":%d,\"h\":%d,\"alias\":%d,"
                   "\"map\":\"%02X%02X\",\"box\":[%d,%d,%d,%d],\"sx0\":%d,\"sy0\":%d,\"cells\":\"",
                i ? "," : "", a->def->bank, a->def->at, a->def->page, a->def->cx, a->def->cy, a->w, a->h,
                (a->def->flags & ALIAS) != 0, a->map_page, a->map_bank, a->x0, a->y0, a->x1, a->y1, a->sx0, a->sy0);
        for (int k = 0; k < a->w * a->h; ++k) {   /* . none, r rock, o open; R and O explored */
            const int seen = bits && bit(bits, a->first_bit + k);
            fputc(a->cells[k] == 0 ? '.' : a->cells[k] == 1 ? (seen ? 'R' : 'r') : (seen ? 'O' : 'o'), f);
        }
        fprintf(f, "\"}");
    }
    fprintf(f, "],\n\"markers\":[");
    for (int k = 0; k < s_marker_count; ++k) {
        const Marker *m = &s_markers[k];
        fprintf(f, "%s\n{\"kind\":\"%s\",\"area\":%d,\"x\":%d,\"y\":%d,\"flag\":[%d,%d],\"collected\":%d}",
                k ? "," : "", kinds[m->kind], m->area, m->x, m->y, m->flag_offset, m->flag_mask, collected(ctx, m));
    }
    fprintf(f, "]}\n");
    fclose(f);
    return 1;
}

void shantae_map_counts(GBContext *ctx, int *areas, int *cells, int *explored, int *markers) {
    const uint8_t *bits = explored_bits(ctx);
    *areas = s_area_count;
    *cells = *explored = 0;
    for (int i = 0; i < s_area_count; ++i)
        for (int k = 0; k < s_areas[i].w * s_areas[i].h; ++k) {
            if (!s_areas[i].cells[k]) continue;
            ++*cells;
            if (bits && bit(bits, s_areas[i].first_bit + k)) ++*explored;
        }
    *markers = s_marker_count;
}
