/* The object table grown to 157 slots (object_slots.c), through the game's own
 * routines on the compiled dispatcher: the free list 01:4CB6 builds, the
 * movement pass 00:0C45, the object scripts 00:1305, draw layering 13:4000
 * and its consumer 01:4E90, the inventory's copy of bank 3 (05:6D08, 05:6D20),
 * a save and a load through cartridge RAM (04:4CF1, 04:4D55), slot jumps past
 * DFFF and at A000, states from before the table grew, orphaned gate parts,
 * and the collision node pool past its 12 (01:7120, 01:7192, 09:643C, 01:72BC,
 * 01:71EB and its 01:71FF beside node E000, 09:480D's clear of +$6B) and its
 * check before the movement pass. */
#include "shantae.h"
#include "../object_slots.h"
#include "gb_custom_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int game_dispatch_override(GBContext*, uint16_t);

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define SLOT(i) shantae_slot_addr(i)
#define SENTINEL 0x0150      /* a return address none of these routines reach */
#define DRAW_RETURN 0x0F07   /* LD A,$01 ... JP NZ,$4E6D: draw handlers' way back */
#define ECHO_MAPPED (SLOT(SHANTAE_ECHO_SLOTS - 1) + SHANTAE_SLOT_SIZE - 0xE000)
#define CART_MAPPED ((SHANTAE_MAX_SLOTS - SHANTAE_ECHO_SLOTS) * SHANTAE_SLOT_SIZE)
#define CART_OFFSET 0x1E00

static GBContext *ctx;
static unsigned drawn[160], drawn_count;

static void set_svbk(unsigned bank) { gb_write8(ctx, 0xFF70, (uint8_t)bank); }
static unsigned slot8(int slot, unsigned offset) {
    return *shantae_slot_memory(ctx, SLOT(slot) + offset);
}
static void set_slot8(int slot, unsigned offset, unsigned value) {
    *shantae_slot_memory(ctx, SLOT(slot) + offset) = (uint8_t)value;
}
static unsigned slot16(int slot, unsigned offset) {
    return slot8(slot, offset) | slot8(slot, offset + 1) << 8;
}
static void set_slot16(int slot, unsigned offset, unsigned value) {
    set_slot8(slot, offset, value & 255);
    set_slot8(slot, offset + 1, value >> 8);
}
static unsigned bank7(unsigned addr) { return ctx->wram[7 * 0x1000 + addr - 0xD000]; }

/* CALL bank:entry from SENTINEL and step until it returns there, noting the
 * slot (BC) each time a draw handler returns through DRAW_RETURN. */
static void call(unsigned bank, unsigned entry) {
    gb_write8(ctx, 0x2000, (uint8_t)bank);
    ctx->hram[0x11] = (uint8_t)bank;
    gb_push16(ctx, SENTINEL);
    const unsigned sp = ctx->sp + 2;
    ctx->pc = (uint16_t)entry;
    for (unsigned long steps = 0; steps < 2000000; ++steps) {
        if (ctx->pc == SENTINEL && ctx->sp == sp) return;
        if (ctx->pc == DRAW_RETURN && drawn_count < 160) drawn[drawn_count++] = ctx->bc;
        gb_debug_step(ctx, GB_EXECUTION_GENERATED);
    }
    fprintf(stderr, "%02X:%04X did not return (pc %04X)\n", bank, entry, ctx->pc);
    exit(1);
}

static void fresh(int extended) {
    gb_context_reset(ctx, true);
    ctx->ime = 0;
    ctx->sp = 0xCFF0;
    gb_custom_read_tap = NULL;
    gb_custom_read_override = NULL;
    shantae_slots_init(ctx, extended);
    set_svbk(3);
}

static int free_count(void) {
    int n = 0;
    for (unsigned p = ctx->hram[0x33] | ctx->hram[0x34] << 8; p && n <= SHANTAE_MAX_SLOTS; ++n)
        p = gb_read16(ctx, (uint16_t)(p + 0x7C));
    return n;
}

/* The collision node pool's free list (FFEA, through +$10 in bank 7): its
 * length, and its last node in *last. */
static int free_nodes(unsigned *last) {
    const unsigned svbk = ctx->wram_bank;
    int n = 0;
    set_svbk(7);
    *last = 0;
    for (unsigned p = ctx->hram[0x6A] | ctx->hram[0x6B] << 8; p && n <= 200; ++n) {
        *last = p;
        p = gb_read16(ctx, (uint16_t)(p + 0x10));
    }
    set_svbk(svbk);
    return n;
}

static unsigned node8(unsigned addr) {
    const unsigned svbk = ctx->wram_bank;
    set_svbk(7);
    const unsigned value = gb_read8(ctx, (uint16_t)addr);
    set_svbk(svbk);
    return value;
}

static unsigned node16(unsigned addr) { return node8(addr) | node8(addr + 1) << 8; }
static void set_node16(unsigned addr, unsigned value) {
    const unsigned svbk = ctx->wram_bank;
    set_svbk(7);
    gb_write8(ctx, (uint16_t)addr, value & 255);
    gb_write8(ctx, (uint16_t)(addr + 1), value >> 8);
    set_svbk(svbk);
}

/* The nodes in use (FFEC, through +$10): how many, or -1 unless each prev link
 * (+$0E) names the node before it, CA03 counts them and the free list holds
 * all the others. */
static int used_nodes(void) {
    unsigned last, prev = 0;
    const int free = free_nodes(&last);
    int n = 0;
    for (unsigned p = ctx->hram[0x6C] | ctx->hram[0x6D] << 8; p; prev = p, p = node16(p + 0x10), ++n)
        if (n > 200 || node16(p + 0x0E) != prev) return -1;
    return n + free == 12 + SHANTAE_MAX_SLOTS && n == ctx->wram[0xA03] ? n : -1;
}

/* 01:7192 for the object at `slot` (DE), as 00:2AB5 calls it; BC = the node. */
static unsigned alloc_node(int slot) {
    ctx->de = (uint16_t)SLOT(slot);
    call(1, 0x7192);
    return ctx->bc;
}

/* A gate's part as 09:55C8 spawns it, waiting at 09:56F8 for +$18. */
static void gate_part(int slot) {
    set_slot8(slot, 0, 0x00);
    set_slot8(slot, 4, 0x09);
    set_slot16(slot, 2, 0x56F8);
    set_slot8(slot, 0x18, 0);
    set_slot8(slot, 0x19, 0);
    set_slot16(slot, 0x1B, 0x0C41);
    set_slot16(slot, 0x1D, 0);
    set_slot8(slot, 0x24, 0);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    unsigned char *rom = malloc(size);
    if (!rom || fread(rom, 1, size, f) != (size_t)size) return 2;
    fclose(f);
    ctx = gb_context_create(shantae_default_config());
    if (!ctx || !gb_context_load_rom(ctx, rom, size)) return 2;
    ctx->mbc_type = rom[0x147];
    gbrt_interp_fallback_logging = false;
    gbrt_imm_override_hook = shantae_slots_imm;
    ShantaeSlotCounts before, after;

    /* Without the view the table keeps 32 slots; E000 stays the echo and
     * A000 cartridge RAM. */
    fresh(0);
    call(1, 0x4CB6);
    CHECK(shantae_slot_count(ctx) == 32 && ctx->wram_ext_mapped == 0 && ctx->wram_ext_cart_mapped == 0);
    CHECK(free_count() == 32 && gb_read16(ctx, SLOT(31) + 0x7C) == 0);
    CHECK(shantae_slots_imm(ctx, 0, 0x1312, 0x20) == 0x20);
    ctx->wram[0x123] = 0x5A;
    CHECK(gb_read8(ctx, 0xE123) == 0x5A && gb_read8(ctx, 0xA123) == 0xFF);

    /* With it, 01:4CB6 builds 157 and the loops that count slots take 157. */
    fresh(1);
    call(1, 0x4CB6);
    CHECK(shantae_slot_count(ctx) == SHANTAE_MAX_SLOTS);
    CHECK(ctx->wram_ext_mapped == ECHO_MAPPED && ctx->wram_ext_cart_mapped == CART_MAPPED);
    CHECK(free_count() == SHANTAE_MAX_SLOTS && (ctx->hram[0x33] | ctx->hram[0x34] << 8) == SLOT(0));
    for (int i = 0; i < SHANTAE_MAX_SLOTS; ++i) {
        CHECK(gb_read8(ctx, SLOT(i)) == 0xFF);
        CHECK(gb_read8(ctx, SLOT(i) + 0x1A) == 0xC3 && gb_read8(ctx, SLOT(i) + 0x29) == 0xC3);
        CHECK(gb_read16(ctx, SLOT(i) + 0x7C) == (i + 1 < SHANTAE_MAX_SLOTS ? SLOT(i + 1) : 0));
        CHECK(shantae_slot_index(ctx, SLOT(i)) == i && shantae_slot_field(ctx, SLOT(i) + 0x29) == 0x29);
    }
    CHECK(SLOT(SHANTAE_ECHO_SLOTS) == 0xA000 && SLOT(SHANTAE_MAX_SLOTS - 1) + SHANTAE_SLOT_SIZE <= 0xC000);
    CHECK(shantae_slot_index(ctx, SLOT(40) + 1) < 0 && shantae_slot_field(ctx, 0xBF80) < 0);
    CHECK(shantae_slots_imm(ctx, 0, 0x1312, 0x20) == SHANTAE_MAX_SLOTS);
    CHECK(shantae_slots_imm(ctx, 0, 0x1312, 0x1E) == 0x1E);   /* a HALT-bug copy */
    /* E000 and A000 are the table's only while SVBK selects bank 3. */
    ctx->wram[0x123] = 0x5A;
    CHECK(gb_read8(ctx, 0xE123) == 0 && ctx->wram_ext[0x123] == 0);
    gb_write8(ctx, 0xE123, 0x77);
    gb_write8(ctx, 0xA123, 0x66);
    CHECK(ctx->wram_ext[0x123] == 0x77 && ctx->wram[0x123] == 0x5A);
    CHECK(ctx->wram_ext[CART_OFFSET + 0x123] == 0x66 && ctx->eram[0x123] != 0x66);
    set_svbk(1);
    CHECK(gb_read8(ctx, 0xE123) == 0x5A && gb_read8(ctx, 0xA123) == 0xFF);
    set_svbk(3);
    gb_write8(ctx, 0xE123, 0);
    gb_write8(ctx, 0xA123, 0);

    /* Slot jumps past DFFF, at A000, and slot 32's in bank 3's tail. */
    const int jumps[][2] = {{32, 0x1A}, {32, 0x29}, {40, 0x1A}, {92, 0x29}, {93, 0x1A}, {156, 0x29}};
    for (unsigned j = 0; j < 6; ++j) {
        const unsigned at = SLOT(jumps[j][0]) + jumps[j][1];
        gb_write8(ctx, 0x2000, 1);
        set_slot16(jumps[j][0], jumps[j][1] + 1, 0x4123 + j);
        ctx->halt_bug = 0;
        CHECK(game_dispatch_override(ctx, (uint16_t)at) && ctx->pc == 0x4123 + j);
        set_slot16(jumps[j][0], jumps[j][1] + 1, 0);
    }

    /* The movement pass: the extra slots first, from the top, then 31 to 0,
     * each called with BC = its slot and SVBK 3. 00:0C40 is a RET. Run
     * freely, the generated code returns to the scheduler at 00:0C4D only when
     * 00:0C4B asks; stepping here returns after every instruction. */
    const int movers[] = {156, 100, 40, 5};
    for (unsigned m = 0; m < 4; ++m) {
        set_slot8(movers[m], 0, 0x00);
        set_slot8(movers[m], 0x19, 0x00);
        set_slot16(movers[m], 0x1B, 0x0C40);
    }
    ctx->stopped = 0;
    CHECK(shantae_slots_imm(ctx, 0, 0x0C4B, 0x03) == 0x03 && ctx->stopped);
    ctx->stopped = 0;
    CHECK(shantae_slots_imm(ctx, 0, 0x0C4B, 0x3E) == 0x3E && !ctx->stopped);
    shantae_slots_counts(&before);
    set_svbk(2);
    ctx->hram[0x11] = 0x07;
    const unsigned sp = ctx->sp;
    gb_write8(ctx, 0x2000, 0);
    gb_push16(ctx, SENTINEL);
    ctx->pc = 0x0C45;
    unsigned order[8], moved = 0;
    for (unsigned long steps = 0; ctx->pc != SENTINEL || ctx->sp != sp; ++steps) {
        CHECK(steps < 100000);
        if (ctx->pc == 0x0C40 && moved < 8) {
            CHECK(ctx->wram_bank == 3 && ctx->a == 0);
            order[moved++] = ctx->bc;
        }
        gb_debug_step(ctx, GB_EXECUTION_GENERATED);
    }
    shantae_slots_counts(&after);
    CHECK(moved == 4 && order[0] == SLOT(156) && order[1] == SLOT(100) && order[2] == SLOT(40) &&
          order[3] == SLOT(5));
    CHECK(after.moves == before.moves + 3);
    CHECK(ctx->wram_bank == 2 && ctx->hram[0x11] == 0x07);   /* 00:0C45 restores them */
    for (unsigned m = 0; m < 4; ++m) set_slot8(movers[m], 0, 0xFF);
    set_svbk(3);
    ctx->stopped = 0;
    CHECK(shantae_slots_imm(ctx, 0, 0x0C4B, 0x03) == 0x03 && !ctx->stopped);   /* none live past 31 */

    /* The object scripts visit every slot, stepping from slot 92 to A000: each
     * live one's wait (+$16) counts down by one. */
    const int scripted[] = {3, 92, 93, 156};
    for (unsigned k = 0; k < 4; ++k) {
        set_slot8(scripted[k], 0, 0x00);
        set_slot16(scripted[k], 0x16, 0x0500);
    }
    call(0, 0x1305);
    for (unsigned k = 0; k < 4; ++k) {
        CHECK(slot16(scripted[k], 0x16) == 0x0400);
        set_slot8(scripted[k], 0, 0xFF);
    }

    /* Draw layering and its consumer: each list's own entries, then the extra
     * slots' in slot order. Slot 50 ($80) is not drawn; FFB2 bit 0 against
     * +$32 ($7F) lets the others through. */
    const struct { int slot; unsigned status, list; } draws[] = {
        {3, 0x00, 0xD406}, {10, 0x00, 0xD340}, {33, 0x00, 0xD406}, {50, 0x80, 0xD340},
        {60, 0x00, 0xD340}, {92, 0x00, 0xD406}, {120, 0x00, 0xD406}, {140, 0x00, 0xD340}};
    for (unsigned d = 0; d < 8; ++d) {
        set_slot8(draws[d].slot, 0, draws[d].status);
        set_slot16(draws[d].slot, 0x2C, draws[d].list);
        set_slot16(draws[d].slot, 0x2A, DRAW_RETURN);
        set_slot8(draws[d].slot, 0x32, 0x7F);
    }
    ctx->hram[0x32] = 0;
    shantae_slots_counts(&before);
    call(0x13, 0x4000);
    shantae_slots_counts(&after);
    CHECK(after.layered == before.layered + 5);
    CHECK(bank7(0xD340) == 1 && (bank7(0xD341) | bank7(0xD342) << 8) == SLOT(10));
    CHECK(bank7(0xD406) == 1 && (bank7(0xD407) | bank7(0xD408) << 8) == SLOT(3));
    CHECK(bank7(0xD382) == 0xFF && bank7(0xD448) == 0xFF);
    drawn_count = 0;
    call(1, 0x4E90);
    shantae_slots_counts(&before);
    CHECK(before.drawn == after.drawn + 5);
    CHECK(drawn_count == 7 && drawn[0] == SLOT(10) && drawn[1] == SLOT(60) && drawn[2] == SLOT(140) &&
          drawn[3] == SLOT(3) && drawn[4] == SLOT(33) && drawn[5] == SLOT(92) && drawn[6] == SLOT(120));
    CHECK(bank7(0xD340) == 1 && bank7(0xD406) == 1);   /* the lists themselves are untouched */

    /* The inventory: 05:6D08 saves bank 3, the inventory builds its own table,
     * 05:6D20 puts the old one back, extra slots included. */
    set_slot16(60, 0x34, 0x1234);
    set_slot16(140, 0x34, 0x4321);
    uint8_t *saved = malloc(0x1E00 + CART_MAPPED);
    CHECK(saved);
    memcpy(saved, ctx->wram_ext, 0x1E00 + CART_MAPPED);
    call(5, 0x6D08);
    call(1, 0x4CB6);
    CHECK(slot16(60, 0x34) == 0 && slot8(60, 0) == 0xFF && slot16(140, 0x34) == 0);
    call(5, 0x6D20);
    CHECK(!memcmp(saved, ctx->wram_ext, ECHO_MAPPED));
    CHECK(!memcmp(saved + CART_OFFSET, ctx->wram_ext + CART_OFFSET, CART_MAPPED));
    CHECK(slot16(60, 0x34) == 0x1234 && slot16(140, 0x34) == 0x4321 && slot8(92, 0) == 0x00);
    CHECK(shantae_slot_count(ctx) == SHANTAE_MAX_SLOTS && ctx->wram_ext_cart_mapped == CART_MAPPED);

    /* A save and a load through cartridge RAM (file 2, A400) leave the slots
     * at A000 alone and put the window back. */
    ctx->wram[0xD1D] = 1;
    for (unsigned k = 0xA70; k < 0xB3C; ++k) ctx->wram[k] = (uint8_t)(k * 7 + 3);
    uint8_t game[0xB3C - 0xA70];
    memcpy(game, ctx->wram + 0xA70, sizeof(game));
    call(4, 0x4CF1);
    ctx->ime = ctx->ime_pending = 0;   /* both end with EI */
    CHECK(!memcmp(ctx->eram + 0x400, game, sizeof(game)));
    CHECK(ctx->wram_ext_cart_mapped == CART_MAPPED && slot16(140, 0x34) == 0x4321);
    memset(ctx->wram + 0xA70, 0, sizeof(game));
    call(4, 0x4D55);
    ctx->ime = ctx->ime_pending = 0;
    CHECK(!memcmp(ctx->wram + 0xA70, game, sizeof(game)) && ctx->e == 0);   /* checksum good */
    CHECK(ctx->wram_ext_cart_mapped == CART_MAPPED && slot16(140, 0x34) == 0x4321);
    free(saved);

    /* Gate parts nobody links are freed as the movement pass starts; a linked
     * one, a signalled one and other objects stay. */
    fresh(1);
    call(1, 0x4CB6);
    for (int i = 0; i < SHANTAE_MAX_SLOTS; ++i) set_slot8(i, 0, 0x80);   /* every slot taken */
    ctx->hram[0x33] = ctx->hram[0x34] = 0;
    gate_part(60); gate_part(61); gate_part(120); gate_part(121);
    set_slot8(70, 0, 0x00); set_slot16(70, 0x61, SLOT(61));    /* its gate */
    set_slot8(121, 0x18, 1);                                     /* told to go */
    set_slot8(5, 0, 0x00); set_slot16(5, 0x1B, 0x0C41);          /* something else */
    ctx->wram[0x397] = 7;
    shantae_slots_counts(&before);
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    shantae_slots_counts(&after);
    CHECK(after.reaped == before.reaped + 2 && free_count() == 2 && ctx->wram[0x397] == 5);
    CHECK(slot8(60, 0) == 0xFF && slot8(120, 0) == 0xFF);
    CHECK((ctx->hram[0x33] | ctx->hram[0x34] << 8) == SLOT(120) && slot16(120, 0x7C) == SLOT(60));
    CHECK(slot8(61, 0) == 0x00 && slot8(121, 0) == 0x00 && slot8(5, 0) == 0x00 && slot8(70, 0) == 0x00);

    /* The free list against the statuses, as the movement pass starts. The
     * caves' damage (bat-popin.state): 0A:42F8 in slot 6 released while slot
     * 10 was the head, then its child freed as 72D3, then slots 5, 9 and 2.
     * The list keeps its own slots and gets the lost ones in table order; C397
     * counts the live ones. A whole list is left alone. */
    fresh(1);
    call(1, 0x4CB6);
    for (int i = 0; i < 10; ++i) set_slot8(i, 0, i == 2 || i == 5 || i == 6 || i == 9 ? 0xFF : 0x00);
    ctx->hram[0x33] = SLOT(2) & 255;
    ctx->hram[0x34] = SLOT(2) >> 8;
    set_slot16(2, 0x7C, SLOT(9)); set_slot16(9, 0x7C, SLOT(5)); set_slot16(5, 0x7C, 0x72D3);
    set_slot16(6, 0x7C, SLOT(10));
    ctx->wram[0x397] = 5;
    shantae_slots_counts(&before);
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    shantae_slots_counts(&after);
    CHECK(after.slot_repairs == before.slot_repairs + 1 && free_count() == SHANTAE_MAX_SLOTS - 6);
    CHECK((ctx->hram[0x33] | ctx->hram[0x34] << 8) == SLOT(2) && slot16(5, 0x7C) == SLOT(6) &&
          slot16(6, 0x7C) == SLOT(10) && slot16(SHANTAE_MAX_SLOTS - 1, 0x7C) == 0 && ctx->wram[0x397] == 6);
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    shantae_slots_counts(&before);
    CHECK(before.slot_repairs == after.slot_repairs && free_count() == SHANTAE_MAX_SLOTS - 6);
    /* A list that loops is cut where it meets a slot again; one that reaches
     * a live slot is cut there and the dead slots after it go on its end. */
    set_slot16(SHANTAE_MAX_SLOTS - 1, 0x7C, SLOT(9));
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    CHECK(free_count() == SHANTAE_MAX_SLOTS - 6 && slot16(SHANTAE_MAX_SLOTS - 1, 0x7C) == 0);
    set_slot8(10, 0, 0x00);
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    shantae_slots_counts(&after);
    CHECK(after.slot_repairs == before.slot_repairs + 2 && free_count() == SHANTAE_MAX_SLOTS - 7);
    CHECK(slot16(6, 0x7C) == SLOT(11) && ctx->wram[0x397] == 7);

    /* Left through the debug grid, the inventory never restores its copy:
     * the next room load (01:4CA3) drops it, so the checks run again. While
     * the inventory's own table is up (00:0C26 builds it) they wait. */
    fresh(1);
    call(1, 0x4CB6);
    call(5, 0x6D08);
    call(1, 0x4CB6);
    ctx->hram[0x33] = 0xD3; ctx->hram[0x34] = 0x72;
    shantae_slots_counts(&before);
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    shantae_slots_counts(&after);
    CHECK(after.slot_repairs == before.slot_repairs);
    call(1, 0x4CA3);
    ctx->hram[0x33] = 0xD3; ctx->hram[0x34] = 0x72;
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    shantae_slots_counts(&before);
    set_svbk(3);
    CHECK(before.slot_repairs == after.slot_repairs + 1 && free_count() == SHANTAE_MAX_SLOTS);

    /* 0A:42F8's release (11:78B6, once 00:12B2 has freed it) frees its child
     * at +$63, not that address with its bytes swapped. */
    fresh(1);
    call(1, 0x4CB6);
    set_slot8(0, 0, 0x00); set_slot8(1, 0, 0x00); set_slot8(2, 0, 0x00);
    ctx->hram[0x33] = SLOT(3) & 255;
    ctx->hram[0x34] = SLOT(3) >> 8;
    set_slot16(1, 0x63, SLOT(2));
    set_slot16(2, 0x63, SLOT(1));
    ctx->wram[0x397] = 3;
    shantae_slots_counts(&before);
    ctx->bc = (uint16_t)SLOT(1);
    call(0x11, 0x78B6);
    shantae_slots_counts(&after);
    CHECK(slot8(2, 0) == 0xFF && (ctx->hram[0x33] | ctx->hram[0x34] << 8) == SLOT(2) &&
          slot16(2, 0x7C) == SLOT(3) && ctx->wram[0x397] == 2 && slot8(1, 0) == 0x00);
    CHECK(after.children_freed == before.children_freed + 1 && free_count() == SHANTAE_MAX_SLOTS - 2);

    /* The collision node pool (01:7120, as 00:2AAE calls it after 01:4CB6 on
     * a map load): 12 nodes at DD40 without the view, and with it the extra
     * nodes at E000 (bank 7 only) after them. */
    unsigned last;
    fresh(0);
    call(1, 0x4CB6);
    call(1, 0x7120);
    CHECK(free_nodes(&last) == 12 && last == 0xDD40 + 11 * 0x13 && ctx->wram_ext_mapped2 == 0);
    CHECK(shantae_node_field(ctx, 0xE00B) < 0 && (set_svbk(7), shantae_node_field(ctx, 0xDD4B) == 0x0B));
    set_svbk(3);
    fresh(1);
    call(1, 0x4CB6);
    shantae_slots_counts(&before);
    call(1, 0x7120);
    shantae_slots_counts(&after);
    CHECK(after.node_pools == before.node_pools + 1);
    CHECK(free_nodes(&last) == 12 + SHANTAE_MAX_SLOTS && last == 0xE000 + (SHANTAE_MAX_SLOTS - 1) * 0x13);
    CHECK(ctx->wram_ext_bank2 == 7 && ctx->wram_ext_mapped2 == SHANTAE_MAX_SLOTS * 0x13);
    CHECK(node8(0xDD40 + 11 * 0x13 + 0x10) == 0x00 && node8(0xDD40 + 11 * 0x13 + 0x11) == 0xE0);
    for (int i = 0; i < SHANTAE_MAX_SLOTS; ++i)
        for (unsigned handler = 2; handler < 0x0E; handler += 3)
            CHECK(node8(0xE000 + i * 0x13 + handler) == 0xC3 && node8(0xE000 + i * 0x13 + handler + 1) == 0x41 &&
                  node8(0xE000 + i * 0x13 + handler + 2) == 0x0C);
    ctx->wram[0x123] = 0x5A;   /* C123: E123 is its echo in any other bank */
    CHECK(node8(0xE123) == 0x41 && gb_read8(ctx, 0xE123) == 0);   /* node 15's JP $0C41 at +5 */
    set_svbk(2);
    CHECK(gb_read8(ctx, 0xE123) == 0x5A);
    set_svbk(3);
    /* 01:7192 hands out the 12, then the extra ones; each object's +$1D gets
     * its node, and the ones in use are linked at FFEC. */
    for (int i = 1; i <= 12; ++i) CHECK(alloc_node(i) == 0xDD40 + (i - 1) * 0x13 && slot16(i, 0x1D) == ctx->bc);
    set_slot8(140, 0, 0x00);
    set_slot16(140, 0x34, 95);  set_slot16(140, 0x47, 0); set_slot16(140, 0x49, 31);
    set_slot16(140, 0x37, 215); set_slot16(140, 0x4B, 0); set_slot16(140, 0x4D, 8);
    set_slot8(140, 0x1F, 0);
    /* 09:643C, the electric platform's setup: its node gets JP $73BE, the
     * landing check, at +$0B and the object at +0. */
    ctx->bc = (uint16_t)SLOT(140);
    call(9, 0x643C);
    CHECK(slot16(140, 0x1D) == 0xE000 && ctx->wram[0xA03] == 13);
    CHECK(node8(0xE000) == (SLOT(140) & 255) && node8(0xE001) == SLOT(140) >> 8);
    CHECK(node8(0xE00B) == 0xC3 && node8(0xE00C) == 0xBE && node8(0xE00D) == 0x73);
    CHECK((ctx->hram[0x6C] | ctx->hram[0x6D] << 8) == 0xE000);
    /* The handler JPs of both pools dispatch, in bank 7 only. */
    set_svbk(7);
    gb_write8(ctx, 0x2000, 1);
    ctx->halt_bug = 0;
    CHECK(game_dispatch_override(ctx, 0xE00B) && ctx->pc == 0x73BE);
    CHECK(game_dispatch_override(ctx, 0xDD4B) && ctx->pc == 0x0C41);
    CHECK(!game_dispatch_override(ctx, 0xE000 + 0x13 * 5 + 0x10));
    set_svbk(3);
    CHECK(!game_dispatch_override(ctx, 0xE00B));
    /* 01:72BC, the player's landing check, walks the nodes in use and lands
     * the player (feet at 220, inside the platform's top band 215-222). */
    set_slot8(0, 0, 0x00);
    set_slot16(0, 0x34, 100); set_slot16(0, 0x39, 0); set_slot16(0, 0x47, 0); set_slot16(0, 0x49, 10);
    set_slot16(0, 0x37, 200); set_slot16(0, 0x3B, 0); set_slot16(0, 0x4B, 0); set_slot16(0, 0x4D, 20);
    ctx->bc = (uint16_t)SLOT(0);
    call(1, 0x72BC);
    CHECK((ctx->hram[0x20] | ctx->hram[0x21] << 8) == SLOT(140) && slot8(140, 0x1F) == 1);
    CHECK(slot16(0, 0x37) == 194 && ctx->wram_bank == 3);
    /* 01:71EB frees an object's node (as retention does): back on the free
     * list, +$1D cleared. */
    ctx->bc = (uint16_t)SLOT(140);
    call(1, 0x71EB);
    CHECK(slot16(140, 0x1D) == 0 && ctx->wram[0xA03] == 12 && (ctx->hram[0x6A] | ctx->hram[0x6B] << 8) == 0xE000);
    CHECK(free_nodes(&last) == SHANTAE_MAX_SLOTS);
    set_slot8(0, 0, 0xFF);
    set_slot8(140, 0, 0xFF);
    /* 01:71FF tests a node's neighbours by their low byte, and E000's is 0.
     * In use, newest first: E026, E013, E000, DE11, ... Freeing E013 (E000
     * after it), DE11 (E000 before it) and E000 keeps the lists whole. */
    fresh(1);
    call(1, 0x4CB6);
    call(1, 0x7120);
    for (int i = 1; i <= 12; ++i) alloc_node(i);
    CHECK(alloc_node(40) == 0xE000 && alloc_node(41) == 0xE013 && alloc_node(42) == 0xE026);
    CHECK(used_nodes() == 15);
    ctx->bc = (uint16_t)SLOT(41);
    call(1, 0x71EB);
    CHECK(used_nodes() == 14 && node16(0xE026 + 0x10) == 0xE000 && node16(0xE000 + 0x0E) == 0xE026);
    ctx->bc = (uint16_t)SLOT(12);
    call(1, 0x71EB);
    CHECK(used_nodes() == 13 && node16(0xE000 + 0x10) == 0xDDFE && node16(0xDDFE + 0x0E) == 0xE000);
    ctx->bc = (uint16_t)SLOT(40);
    call(1, 0x71EB);
    CHECK(used_nodes() == 12 && node16(0xE026 + 0x10) == 0xDDFE && node16(0xDDFE + 0x0E) == 0xE026);
    /* The damage those tests once left, as a state saved then holds it: a
     * live object's node on neither list, CA03 counting it. The pool check
     * before the movement pass puts it back in use; a node nobody names goes
     * on the free list. Not while the inventory has the table in bank 5. */
    set_slot8(42, 0, 0x00);
    set_node16(0xE026, SLOT(42));   /* its object, as the callers of 01:7192 write it */
    set_node16(0xE026 + 0x10, 0);   /* E026 was the head: FFEC moves on, the node is on neither list */
    ctx->hram[0x6C] = 0xFE;
    ctx->hram[0x6D] = 0xDD;
    set_node16(0xDDFE + 0x0E, 0);
    CHECK(used_nodes() < 0 && ctx->wram[0xA03] == 12);
    unsigned free_head = ctx->hram[0x6A] | ctx->hram[0x6B] << 8;
    ctx->hram[0x6A] = node16(free_head + 0x10) & 255;   /* and the free list's head, which nobody names */
    ctx->hram[0x6B] = node16(free_head + 0x10) >> 8;
    const uint32_t backup_at = CART_OFFSET + CART_MAPPED + 5;   /* SlotState.backup_slots */
    ctx->wram_ext[backup_at] = SHANTAE_MAX_SLOTS;
    shantae_slots_counts(&before);
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    ctx->stopped = 0;
    shantae_slots_counts(&after);
    CHECK(after.node_repairs == before.node_repairs && used_nodes() < 0);
    ctx->wram_ext[backup_at] = 0;
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    ctx->stopped = 0;
    shantae_slots_counts(&after);
    CHECK(after.node_repairs == before.node_repairs + 2 && used_nodes() == 12);
    CHECK((ctx->hram[0x6C] | ctx->hram[0x6D] << 8) == 0xE026 && (ctx->hram[0x6A] | ctx->hram[0x6B] << 8) == free_head);
    shantae_slots_imm(ctx, 0, 0x0C4B, 0x03);
    ctx->stopped = 0;
    shantae_slots_counts(&before);
    CHECK(before.node_repairs == after.node_repairs && used_nodes() == 12);
    for (int i = 1; i <= 42; ++i) set_slot8(i, 0, 0xFF);
    /* A state from before the pool grew, with the table grown: the extra
     * nodes go on the end of its free list, or are all of it when the 12 are
     * taken. Loading it again changes nothing. */
    for (int taken = 5; taken <= 12; taken += 7) {
        fresh(0);
        call(1, 0x4CB6);
        call(1, 0x7120);
        for (int i = 1; i <= taken; ++i) alloc_node(i);
        shantae_slots_init(ctx, 1);
        memset(ctx->wram_ext, 0, ctx->wram_ext_size);   /* a state without it: the table grows on load */
        shantae_slots_counts(&before);
        shantae_slots_state_loaded(ctx);
        shantae_slots_counts(&after);
        CHECK(after.node_upgrades == before.node_upgrades + 1 && ctx->wram_ext_mapped2 == SHANTAE_MAX_SLOTS * 0x13);
        CHECK(free_nodes(&last) == 12 - taken + SHANTAE_MAX_SLOTS && last == 0xE000 + (SHANTAE_MAX_SLOTS - 1) * 0x13);
        CHECK((ctx->hram[0x6A] | ctx->hram[0x6B] << 8) == (taken == 12 ? 0xE000 : 0xDD40 + taken * 0x13));
        gb_context_map_wram_extension_bank(ctx, 7, 0, 0);
        shantae_slots_state_loaded(ctx);
        shantae_slots_counts(&before);
        CHECK(before.node_upgrades == after.node_upgrades && ctx->wram_ext_mapped2 == SHANTAE_MAX_SLOTS * 0x13);
        CHECK(free_nodes(&last) == 12 - taken + SHANTAE_MAX_SLOTS);
    }
    /* 09:480D gives the object at BC a node, then clears the object's +$6B
     * through BC, which 01:7192 has made the node. Without the view the byte
     * at node + $6B goes, as in the original: for DD79, the high byte of slot
     * 28's movement JP. With it the object's goes, for an extra node too
     * (E000 + $6B is slot 33's +$2D). */
    for (int extended = 0; extended < 2; ++extended) {
        fresh(extended);
        call(1, 0x4CB6);
        call(1, 0x7120);
        for (int i = 1; i <= 3; ++i) alloc_node(i);
        set_slot8(28, 0, 0x00);
        set_slot16(28, 0x1B, 0x7AEE);
        set_slot8(20, 0x6B, 0x55);
        ctx->bc = (uint16_t)SLOT(20);
        call(9, 0x480D);
        CHECK(slot16(20, 0x1D) == 0xDD79 && ctx->bc == SLOT(20) && ctx->wram_bank == 3);
        CHECK(node8(0xDD79) == (SLOT(20) & 255) && node8(0xDD79 + 0x0C) == 0xBE);
        if (extended) CHECK(slot16(28, 0x1B) == 0x7AEE && slot8(20, 0x6B) == 0);
        else CHECK(slot16(28, 0x1B) == 0x00EE && slot8(20, 0x6B) == 0x55);
        if (!extended) continue;
        for (int i = 4; i <= 11; ++i) alloc_node(i);
        set_slot8(33, 0x2D, 0x77);
        set_slot8(40, 0x6B, 0x55);
        ctx->bc = (uint16_t)SLOT(40);
        call(9, 0x480D);
        CHECK(slot16(40, 0x1D) == 0xE000 && slot8(33, 0x2D) == 0x77 && slot8(40, 0x6B) == 0);
    }

    /* The trigger and the doorway both call 09:4953. Repeated forced spawns
     * must preserve the same eight objects and their seven collision nodes,
     * even when the group is allocated across DFFF or at A000. */
    for (int first = 0; first <= 145; first += 29) {
        fresh(1);
        call(1, 0x4CB6);
        call(1, 0x7120);
        for (int i = 0; i < first; ++i) call(1, 0x4DB0);
        ctx->a = 1;
        call(9, 0x4953);
        CHECK(free_count() == SHANTAE_MAX_SLOTS - first - 8 && ctx->wram[0xC06] == 7);
        unsigned handles[8];
        handles[0] = ctx->wram[0x1F] | ctx->wram[0x20] << 8;
        for (int i = 0; i < 7; ++i) {
            handles[i + 1] = bank7(0xDE24 + 2 * i) | bank7(0xDE25 + 2 * i) << 8;
            ctx->bc = handles[i + 1];
            call(9, i < 2 ? 0x4711 : 0x480D);
        }
        CHECK(used_nodes() == 7);
        for (int repeat = 0; repeat < 1000; ++repeat) {
            ctx->a = 1;
            call(9, 0x4953);
            CHECK(free_count() == SHANTAE_MAX_SLOTS - first - 8 && used_nodes() == 7);
            CHECK((ctx->wram[0x1F] | ctx->wram[0x20] << 8) == handles[0]);
            for (int i = 0; i < 7; ++i)
                CHECK((bank7(0xDE24 + 2 * i) | bank7(0xDE25 + 2 * i) << 8) == handles[i + 1]);
        }
        ctx->a = 1;
        call(9, 0x4996);
        CHECK(free_count() == SHANTAE_MAX_SLOTS - first && used_nodes() == 0);
        /* A trigger may already have removed the group before the doorway. */
        ctx->a = 1;
        call(9, 0x4996);
        CHECK(free_count() == SHANTAE_MAX_SLOTS - first && used_nodes() == 0);
        ctx->a = 1;
        call(9, 0x4953);
        CHECK(free_count() == SHANTAE_MAX_SLOTS - first - 8);
    }
    /* Refuse a partial group when fewer than eight slots remain; the trigger
     * can retry after a slot is freed. Zero would otherwise be written as an
     * object address and corrupt mapper state. */
    fresh(1);
    call(1, 0x4CB6);
    call(1, 0x7120);
    for (int i = 0; i < SHANTAE_MAX_SLOTS - 7; ++i) call(1, 0x4DB0);
    ctx->wram[0x2E] = 0;
    ctx->a = 1;
    call(9, 0x4953);
    CHECK(free_count() == 7 && ctx->wram[0x2E] == 0 && used_nodes() == 0);
    ctx->bc = SLOT(0);
    call(0, 0x12D7);
    ctx->a = 1;
    call(9, 0x4953);
    CHECK(free_count() == 0 && ctx->wram[0x2E] == 1);
    ctx->a = 1;
    call(9, 0x4996);
    CHECK(free_count() == 8);

    /* An older save can already contain a lost group. Clear its controller
     * handle to reproduce the old unconditional allocation, then repair the
     * resulting clone through the movement-pass hook. */
    fresh(1);
    call(1, 0x4CB6);
    call(1, 0x7120);
    for (int group = 0; group < 3; ++group) {
        ctx->wram[0x1F] = ctx->wram[0x20] = 0;
        ctx->a = 1;
        call(9, 0x4953);
        for (int i = 0; i < 7; ++i) {
            ctx->bc = bank7(0xDE24 + 2 * i) | bank7(0xDE25 + 2 * i) << 8;
            call(9, i < 2 ? 0x4711 : 0x480D);
        }
    }
    CHECK(free_count() == SHANTAE_MAX_SLOTS - 24 && used_nodes() == 21);
    shantae_slots_counts(&before);
    ctx->wram_ext[backup_at] = SHANTAE_MAX_SLOTS;
    shantae_slots_imm(ctx, 0, 0x0C4B, 3);
    ctx->stopped = 0;
    CHECK(free_count() == SHANTAE_MAX_SLOTS - 24);
    ctx->wram_ext[backup_at] = 0;
    shantae_slots_imm(ctx, 0, 0x0C4B, 3);
    ctx->stopped = 0;
    shantae_slots_counts(&after);
    CHECK(after.reaped == before.reaped + 16);
    CHECK(free_count() == SHANTAE_MAX_SLOTS - 8 && used_nodes() == 7);
    ctx->a = 1;
    call(9, 0x4996);
    CHECK(free_count() == SHANTAE_MAX_SLOTS && used_nodes() == 0);

    /* A state from before the table grew: the extension comes back empty, and
     * the extra slots go on the end of the free list. */
    for (int empty = 0; empty < 2; ++empty) {
        fresh(0);
        call(1, 0x4CB6);
        if (empty) {
            ctx->hram[0x33] = ctx->hram[0x34] = 0;
        } else {
            ctx->hram[0x33] = SLOT(5) & 255;
            ctx->hram[0x34] = SLOT(5) >> 8;
        }
        shantae_slots_init(ctx, 1);
        memset(ctx->wram_ext, 0, ctx->wram_ext_size);
        shantae_slots_counts(&before);
        shantae_slots_state_loaded(ctx);
        shantae_slots_counts(&after);
        CHECK(after.upgrades == before.upgrades + 1);
        CHECK(shantae_slot_count(ctx) == SHANTAE_MAX_SLOTS && ctx->wram_ext_cart_mapped == CART_MAPPED);
        CHECK(free_count() == (empty ? 0 : 27) + SHANTAE_MAX_SLOTS - 32);
        CHECK((ctx->hram[0x33] | ctx->hram[0x34] << 8) == (empty ? SLOT(32) : SLOT(5)));
        if (!empty) CHECK(gb_read16(ctx, SLOT(31) + 0x7C) == SLOT(32));
        CHECK(gb_read16(ctx, SLOT(92) + 0x7C) == SLOT(93));
        for (int i = 32; i < SHANTAE_MAX_SLOTS; ++i)
            CHECK(slot8(i, 0) == 0xFF && slot8(i, 0x1A) == 0xC3 && slot8(i, 0x29) == 0xC3);
        /* A state of this build keeps its table as it is. */
        gb_context_map_wram_extension(ctx, 0, 0, 0);
        shantae_slots_state_loaded(ctx);
        CHECK(ctx->wram_ext_cart_mapped == CART_MAPPED && free_count() == (empty ? 0 : 27) + SHANTAE_MAX_SLOTS - 32);
    }
    /* One from the 93-slot build: its layout at 0x1E00, the free list ending
     * at slot 92, which now goes on to A000. */
    fresh(1);
    call(1, 0x4CB6);
    set_slot16(92, 0x7C, 0);
    ctx->hram[0x33] = SLOT(50) & 255;
    ctx->hram[0x34] = SLOT(50) >> 8;
    set_slot16(60, 0x34, 0x2222);
    memset(ctx->wram_ext + CART_OFFSET, 0, ctx->wram_ext_size - CART_OFFSET);
    const uint32_t v1_magic = 0x31534C53u;
    memcpy(ctx->wram_ext + 0x1E00, &v1_magic, 4);
    ctx->wram_ext[0x1E04] = SHANTAE_ECHO_SLOTS;
    shantae_slots_state_loaded(ctx);
    CHECK(shantae_slot_count(ctx) == SHANTAE_MAX_SLOTS && ctx->wram_ext_cart_mapped == CART_MAPPED);
    CHECK(free_count() == SHANTAE_ECHO_SLOTS - 50 + SHANTAE_MAX_SLOTS - SHANTAE_ECHO_SLOTS);
    CHECK(gb_read16(ctx, SLOT(92) + 0x7C) == SLOT(93) && slot16(60, 0x34) == 0x2222);
    /* Before the first table there is nothing to add to. */
    fresh(1);
    memset(ctx->wram + 0x3000, 0, 0x1000);
    memset(ctx->wram_ext, 0, ctx->wram_ext_size);
    shantae_slots_state_loaded(ctx);
    CHECK(shantae_slot_count(ctx) == 32 && ctx->wram_ext_mapped == 0 && ctx->wram_ext_cart_mapped == 0);

    puts("Object slots: 32 without the view, 157 with it; E000 and A000 only in bank 3; slot jumps past DFFF "
         "and at A000; movement, scripts, draw layering and drawing of the extra slots; inventory copy; "
         "save and load through cartridge RAM; orphaned gate parts; damaged free lists mended; 0A:42F8's "
         "child freed at its own address; collision nodes past the 12 (built, "
         "handed out, landed on, freed, beside E000 too, 09:480D's clear on its object); damaged node lists "
         "mended; repeated platform spawns/removals, slot pressure and saved clones; older states passed.");
    return 0;
}
