/*
 * object_slots.c -- Shantae's object table grown from 32 slots to 157.
 *
 * Objects live in 32 slots of $7E bytes at D000 in WRAM bank 3, taken from a
 * free list (FFB3, linked through slot+$7C) that 01:4CB6 builds on every map
 * load. The expanded view activates objects over a much larger area: at
 * 1920x527 a labyrinth filled every slot, so its door warps (0A:4048) found
 * none, and at 1920x1080 the water tower's room wants about 130, so the drop
 * that becomes its mini-boss found none. With the view on, the table has 157
 * slots, as many as 16-bit addresses leave room for. The game reaches objects
 * with SVBK selecting bank 3, and while it does, two windows are memory of
 * the table's own (gb_context_map_wram_extension):
 * - E000-FDC5, instead of the echo of C000, which only the map decoder reads,
 *   with SVBK 1. Slots 32-92 continue from D000 there; slot 32 starts at
 *   DFC0, a part of bank 3 the game never uses, and slot 92 ends below OAM.
 * - A000-BF7F, slots 93-156, instead of cartridge RAM, which only the four
 *   save routines in bank 4 use: they switch it on (LD A,$0A at 04:4CA8,
 *   4CF6, 4D30, 4D5A) and off again (LD A,$00 at 04:4CDF, 4CE8, 4D24, 4D4E,
 *   4D90, 4D99) with interrupts disabled, and the window steps aside between.
 *   It does not follow the RAM enable itself: an allocation that finds no
 *   slot writes a whole object to 0000+ (09:55C8 does not check), where the
 *   MBC takes it for RAM enables.
 *
 * The code that walks the table takes the new size. 01:4CB6 counts 93 slots
 * from D000 (01:4CD1) and gets slot 92 linked on to A000 (01:4CFE); the
 * object scripts count 157 (00:1312) and step from slot 92 to A000 (00:1384).
 * Two passes are unrolled for 32 slots, and the extra slots join them as
 * though the code went on:
 * - movement (00:0C45, slots 31 to 0): before slot 31, each live extra slot's
 *   routine is called from 156 down, returning to 00:0C4D;
 * - draw layering (13:4000, slots 0 to 31, into the eight lists of bank 7 at
 *   D340-D54F): the extra slots' entries follow each list's own, in lists
 *   kept here. A list holds 32 entries; the consumer (01:4E90) reads the count
 *   and the entries past it from these lists, at most 127 in all (its count
 *   is 2n-1 in a byte).
 * Each extra slot takes the guest time the original code takes for a live
 * slot, one slot per step so interrupts come between them as they would; free
 * extra slots take none. A third unrolled pass, the y-sort at 13:63BB, is never
 * installed by anything in the ROM and keeps its 32. The inventory saves bank
 * 3 to bank 5 and back (05:6D08, 05:6D20); the extra slots go with it.
 *
 * Gates with a padlock (object 0A:4044) leave a part behind: the gate spawns
 * it (09:55C8) and links it at +$61, and tells it to go (+$18 = 1, 09:56BA)
 * when retention releases the gate, but not on every way a gate goes, and a
 * part nobody links waits at 09:56F8 until the map reloads. The wider view
 * releases and respawns such gates far more often, and the parts pile up; one
 * nothing links is freed at the start of the movement pass.
 *
 * Objects the player collides with (platforms, hazards) each take a node from
 * a pool 01:711E builds in bank 7: 12 of $13 bytes at DD40, their free list
 * at FFEA linked through +$10, the ones in use at FFEC, which the player's
 * checks (01:7266, 72BC, 7312, 7368) walk. 12 is enough for 32 objects, not
 * for 157: 01:7192 then hands out none, and the platform or hazard asking
 * collides with nothing, writing its handlers to 0000+ (09:6440 does not
 * check either). With the grown table the pool grows too, one node for each
 * slot at E000 while SVBK selects bank 7 (the echo there is never read
 * either; gb_context_map_wram_extension_bank), on the end of the free list.
 * 01:71FF, which frees a node, asks whether it has a neighbour on each side
 * with LD A,(HL+) / LD E,A / LD D,(HL) / OR E: the low byte alone. None of the
 * pool's own nodes has a low byte of 0, but the first extra one, E000, does,
 * and freeing a node beside it kept E000's link to it (a freed node still in
 * use) or its link back, with which freeing E000 later took a live node off
 * the list (a worm in the water tower's shaft nobody could land on). The read
 * tap has the test take the high byte too. The lists are checked before each
 * movement pass (check_nodes), which mends states saved with that damage.
 * 09:480D, which gives the object at DE its node, then clears the object's
 * +$6B through BC, where 01:7192 has left the node: in bank 3 that is the
 * node's address + $6B, a byte of slots 27-29 for the pool's own nodes and of
 * slots 33-56 for the extra ones, slots the original's few objects hardly
 * reach. With the grown table they are live; for node DD79 the byte is the
 * high one of slot 28's movement JP (DDE4), and the next movement pass jumped
 * into the $FF at 00EE, where RST $38 ran the stack down through WRAM and the
 * VBlank vector at C374. The hook has the clear take the object (DE).
 *
 * Object 0A:42F8 (one in the caves' first room, script 11:77E7) spawns a
 * child that watches for the player (11:7820 links them both ways at +$63).
 * When retention releases it, its callback frees the child through 00:12D7,
 * but 11:78B6 loads the link the wrong way round (LD A,(HL+) / LD C,(HL) /
 * LD B,A): with the child at D372 it frees 72D3, which 00:12E7 puts at the
 * head of the free list. Every slot then free is lost, and the next object
 * taken is written over ROM (or, from an E000 slot, over the middle of
 * another slot). The original reaches it too, floating to the right end of
 * that room; the view reaches it from the next room. The hook reads the link
 * the right way round, with the grown table only.
 *
 * The free list holds exactly the slots whose status (+0) is $FF: 00:12F7
 * sets it as 00:12E7 links a slot, 01:4DFD sets $80 as 01:4DB0 takes one, and
 * no other code or object script writes either. Before each movement pass the
 * list is checked (check_slots): one that leaves the table, loops, or holds a
 * live slot is cut there, and the dead slots it does not hold go on its end,
 * which mends states saved with such damage.
 *
 * What must survive a save state is kept in the extension's memory past the
 * windows, which save states include. A state from before the table grew
 * gets the extra slots at the end of its free list, and one from before the
 * pool grew the extra nodes at the end of the pool's.
 */
#include "object_slots.h"
#include "gbrt.h"
#include "gb_custom_view.h"

#include <string.h>

#define SLOT_ADDR(i) shantae_slot_addr(i)
#define EXTRA_SLOTS (SHANTAE_MAX_SLOTS - SHANTAE_ORIGINAL_SLOTS)
#define CART_SLOTS (SHANTAE_MAX_SLOTS - SHANTAE_ECHO_SLOTS)
#define TAIL (SHANTAE_SLOT_BASE + SHANTAE_ORIGINAL_SLOTS * SHANTAE_SLOT_SIZE)       /* DFC0 */
#define ECHO_MAPPED (SHANTAE_SLOT_BASE + SHANTAE_ECHO_SLOTS * SHANTAE_SLOT_SIZE - 0xE000)
#define CART_MAPPED (CART_SLOTS * SHANTAE_SLOT_SIZE)
#define CART_OFFSET 0x1E00                                  /* in wram_ext */
#define PRIVATE_OFFSET (CART_OFFSET + CART_MAPPED)
#define PRIVATE_MAGIC 0x32534C53u                           /* "SLS2": this layout */
#define PRIVATE_MAGIC_V1 0x31534C53u                        /* 93 slots, private at 0x1E00 */

#define MOVE_RESUME_PC 0x0C4D    /* LDH ($70),A after LD A,$03 in 00:0C45 */
#define MOVE_MARKER 0x5AA7       /* never a pushed AF: its low nibble is set */
#define DRAW_LAST_PC 0x4484      /* after LD A,($DF42), slot 31, in 13:4000 */
#define BACKUP_PC 0x6D13         /* after 05:6D08's first LD A,(HL) */
#define RESTORE_PC 0x6D2B        /* after 05:6D20's first LD A,(HL) */
#define FREE_PREV_TEST_PC 0x720E /* OR E after 01:720D's LD D,(HL), the node's prev */
#define FREE_NEXT_TEST_PC 0x7227 /* OR E after 01:7226's LD D,(HL), the node's next */
#define NODE_SETUP_CLEAR_PC 0x4880   /* LD A,$00 before 09:480D's LD (HL),A to +$6B */
#define PLATFORM_SPAWN_PC 0x495C    /* 09:495A's LD A,$01, before setting C02D+group */
#define PLATFORM_FREE_PC 0x499F     /* 09:499D's LD A,$00, before clearing it */
#define CHILD_LINK_FIRST_PC 0x78BA  /* 11:78BA LD A,(HL+) (+$63, taken for B) */
#define CHILD_LINK_LAST_PC 0x78BC   /* after 11:78BB's LD C,(HL) (+$64, taken for C) */
#define CHILD_LINK 0x63
#define DRAW_LISTS 8
#define DRAW_LIST_BASE 0xD340
#define DRAW_LIST_SIZE 0x42
#define DRAW_LIST_MAX 127
#define NODE_BASE 0xDD40         /* bank 7 */
#define NODE_SIZE 0x13
#define ORIGINAL_NODES 12
#define NODE_LAST_NEXT (NODE_BASE + (ORIGINAL_NODES - 1) * NODE_SIZE + 0x10)
#define EXTRA_NODES SHANTAE_MAX_SLOTS
#define NODE_EXT_BASE 0xE000
#define NODE_MAPPED (EXTRA_NODES * NODE_SIZE)
#define NODE_OFFSET (PRIVATE_OFFSET + sizeof(SlotState))   /* in wram_ext */

typedef struct {
    uint32_t magic;
    uint8_t slots;            /* in the table the game built last */
    uint8_t backup_slots;     /* in the table the inventory saved, 0 = none */
    uint8_t draw_next;        /* next extra slot to layer, 0 = none */
    uint8_t consumer_list;    /* list 01:4E90 is drawing, 0xFF = none */
    uint8_t consumer_real;    /* its entries in bank 7 */
    uint8_t cart_yielded;     /* a save routine has cartridge RAM on */
    uint8_t overflow_count[DRAW_LISTS];
    uint8_t overflow[DRAW_LISTS][EXTRA_SLOTS];   /* slots, in layering order */
    uint8_t backup[ECHO_MAPPED + CART_MAPPED];
    uint8_t nodes;            /* extra collision nodes in the pool, 0 or EXTRA_NODES */
} SlotState;

/* The 93-slot build's layout, at 0x1E00: states it saved are upgraded. */
typedef struct {
    uint32_t magic;
    uint8_t slots, backup_slots, draw_next, consumer_list, consumer_real;
    uint8_t overflow_count[DRAW_LISTS];
    uint16_t overflow[DRAW_LISTS][61];
    uint8_t backup[ECHO_MAPPED];
} SlotStateV1;

static int s_extended;
static ShantaeSlotCounts s_counts;

void shantae_slots_counts(ShantaeSlotCounts *out) { *out = s_counts; }

static SlotState *slot_state(const GBContext *ctx) {
    if (!ctx->wram_ext || ctx->wram_ext_size < PRIVATE_OFFSET + sizeof(SlotState)) return NULL;
    return (SlotState *)(ctx->wram_ext + PRIVATE_OFFSET);
}

int shantae_slot_count(const GBContext *ctx) {
    const SlotState *s = slot_state(ctx);
    return s && (s->slots == SHANTAE_ECHO_SLOTS || s->slots == SHANTAE_MAX_SLOTS) ? s->slots
                                                                                   : SHANTAE_ORIGINAL_SLOTS;
}

int shantae_slot_field(const GBContext *ctx, unsigned addr) {
    const int count = shantae_slot_count(ctx);
    const int echo = count < SHANTAE_ECHO_SLOTS ? count : SHANTAE_ECHO_SLOTS;
    if (addr >= SHANTAE_SLOT_BASE && addr < SHANTAE_SLOT_BASE + echo * SHANTAE_SLOT_SIZE)
        return (int)((addr - SHANTAE_SLOT_BASE) % SHANTAE_SLOT_SIZE);
    if (count > SHANTAE_ECHO_SLOTS && addr >= SHANTAE_CART_BASE &&
        addr < SHANTAE_CART_BASE + (count - SHANTAE_ECHO_SLOTS) * SHANTAE_SLOT_SIZE)
        return (int)((addr - SHANTAE_CART_BASE) % SHANTAE_SLOT_SIZE);
    return -1;
}

int shantae_slot_index(const GBContext *ctx, unsigned addr) {
    if (shantae_slot_field(ctx, addr) != 0) return -1;
    return addr >= SHANTAE_SLOT_BASE ? (int)(addr - SHANTAE_SLOT_BASE) / SHANTAE_SLOT_SIZE
                                     : SHANTAE_ECHO_SLOTS + (int)(addr - SHANTAE_CART_BASE) / SHANTAE_SLOT_SIZE;
}

uint8_t *shantae_slot_memory(GBContext *ctx, unsigned addr) {
    if (shantae_slot_field(ctx, addr) < 0) return NULL;
    if (addr < 0xC000) return ctx->wram_ext + CART_OFFSET + (addr - SHANTAE_CART_BASE);
    if (addr < 0xE000) return ctx->wram + 3 * 0x1000 + (addr - 0xD000);
    return ctx->wram_ext + (addr - 0xE000);
}

static int extra_nodes(const GBContext *ctx) {
    const SlotState *s = slot_state(ctx);
    return s && s->nodes == EXTRA_NODES ? EXTRA_NODES : 0;
}

int shantae_node_field(const GBContext *ctx, unsigned addr) {
    if (ctx->wram_bank != 7) return -1;
    if (addr >= NODE_BASE && addr < NODE_BASE + ORIGINAL_NODES * NODE_SIZE)
        return (int)((addr - NODE_BASE) % NODE_SIZE);
    if (addr >= NODE_EXT_BASE && addr < NODE_EXT_BASE + extra_nodes(ctx) * NODE_SIZE)
        return (int)((addr - NODE_EXT_BASE) % NODE_SIZE);
    return -1;
}

/* Bank 7 memory at a node address, the pool's own or the extra nodes'. */
static uint8_t *node_memory(GBContext *ctx, unsigned addr) {
    if (addr >= NODE_EXT_BASE) return ctx->wram_ext + NODE_OFFSET + (addr - NODE_EXT_BASE);
    return ctx->wram + 7 * 0x1000 + (addr - 0xD000);
}
static unsigned node_word(GBContext *ctx, unsigned addr) {
    return node_memory(ctx, addr)[0] | node_memory(ctx, addr)[1] << 8;
}
static void set_node_word(GBContext *ctx, unsigned addr, unsigned value) {
    node_memory(ctx, addr)[0] = (uint8_t)value;
    node_memory(ctx, addr)[1] = (uint8_t)(value >> 8);
}

static uint8_t *slot_byte(GBContext *ctx, int slot, unsigned offset) {
    return shantae_slot_memory(ctx, SLOT_ADDR(slot) + offset);
}
static unsigned slot_word(GBContext *ctx, int slot, unsigned offset) {
    return *slot_byte(ctx, slot, offset) | *slot_byte(ctx, slot, offset + 1) << 8;
}
static void set_slot_word(GBContext *ctx, int slot, unsigned offset, unsigned value) {
    *slot_byte(ctx, slot, offset) = (uint8_t)value;
    *slot_byte(ctx, slot, offset + 1) = (uint8_t)(value >> 8);
}

/* The windows answer only while the table reaches them, the one at A000 not
 * while a save routine has cartridge RAM on. The taps are needed with a grown
 * table even without the expanded view (a state saved with it). */
static void sync(GBContext *ctx) {
    SlotState *s = slot_state(ctx);
    const int slots = shantae_slot_count(ctx);
    gb_context_map_wram_extension(ctx, slots > SHANTAE_ORIGINAL_SLOTS ? ECHO_MAPPED : 0,
                                  slots > SHANTAE_ECHO_SLOTS && s && !s->cart_yielded ? CART_MAPPED : 0,
                                  CART_OFFSET);
    gb_context_map_wram_extension_bank(ctx, 7, (uint16_t)(extra_nodes(ctx) * NODE_SIZE), NODE_OFFSET);
    if (slots > SHANTAE_ORIGINAL_SLOTS) {
        if (!gb_custom_read_tap) gb_custom_read_tap = shantae_slots_read_tap;
        if (!gb_custom_read_override) gb_custom_read_override = shantae_slots_read_override;
    } else {
        if (gb_custom_read_tap == shantae_slots_read_tap) gb_custom_read_tap = NULL;
        if (gb_custom_read_override == shantae_slots_read_override) gb_custom_read_override = NULL;
    }
}

/* Everything but the inventory's copy, which a table built while the
 * inventory is open must not lose. */
static void reset_passes(SlotState *s, int slots) {
    s->magic = PRIVATE_MAGIC;
    s->slots = (uint8_t)slots;
    s->draw_next = 0;
    s->consumer_list = 0xFF;
    memset(s->overflow_count, 0, sizeof(s->overflow_count));
}

void shantae_slots_init(GBContext *ctx, int extended) {
    s_extended = extended != 0;
    if (!gb_context_set_wram_extension(ctx, 3, 0, NODE_OFFSET + NODE_MAPPED)) return;
    reset_passes(slot_state(ctx), SHANTAE_ORIGINAL_SLOTS);
    sync(ctx);
}

/* Slots `from` to the last, zeroed and free: status $FF, their JPs, each
 * linked to the next. The table must already count them. */
static void add_free_slots(GBContext *ctx, int from) {
    for (int i = from; i < SHANTAE_MAX_SLOTS; ++i) {
        for (unsigned offset = 0; offset < SHANTAE_SLOT_SIZE; ++offset) *slot_byte(ctx, i, offset) = 0;
        *slot_byte(ctx, i, 0) = 0xFF;
        *slot_byte(ctx, i, 0x1A) = 0xC3;
        *slot_byte(ctx, i, 0x29) = 0xC3;
        set_slot_word(ctx, i, 0x7C, i + 1 < SHANTAE_MAX_SLOTS ? SLOT_ADDR(i + 1) : 0);
    }
}

/* 01:4CD1, LD A,$20: 01:4CB6 has cleared D000-DFBF and builds the free list
 * over this many slots from D000, the rest of a grown table waiting at A000
 * for 01:4CFE. */
static uint8_t build_table(GBContext *ctx, SlotState *s) {
    const int was_extended = s->slots > SHANTAE_ORIGINAL_SLOTS;
    reset_passes(s, s_extended ? SHANTAE_MAX_SLOTS : SHANTAE_ORIGINAL_SLOTS);
    memset(ctx->wram_ext, 0, PRIVATE_OFFSET);
    if (s_extended || was_extended) memset(ctx->wram + 0x3000 + (TAIL - 0xD000), 0, 0xE000 - TAIL);
    if (s_extended) add_free_slots(ctx, SHANTAE_ECHO_SLOTS);
    sync(ctx);
    return s_extended ? SHANTAE_ECHO_SLOTS : SHANTAE_ORIGINAL_SLOTS;
}

/* A table built before this layout (32 slots, or 93 by the build before):
 * the extra slots go on the end of its free list, so they are used only once
 * the others are. */
static void upgrade(GBContext *ctx, SlotState *s) {
    SlotStateV1 v1;
    memcpy(&v1, ctx->wram_ext + 0x1E00, sizeof(v1));
    const int from = v1.magic == PRIVATE_MAGIC_V1 && v1.slots == SHANTAE_ECHO_SLOTS ? SHANTAE_ECHO_SLOTS
                                                                                    : SHANTAE_ORIGINAL_SLOTS;
    memset(ctx->wram_ext + CART_OFFSET, 0, ctx->wram_ext_size - CART_OFFSET);
    if (from == SHANTAE_ORIGINAL_SLOTS) memset(ctx->wram_ext, 0, ECHO_MAPPED);
    reset_passes(s, from);
    if (v1.magic == PRIVATE_MAGIC_V1 && v1.backup_slots) {
        /* Saved while the inventory was open: its copy of the 93 slots. */
        s->backup_slots = v1.backup_slots;
        memcpy(s->backup, v1.backup, sizeof(v1.backup));
    }
    /* 01:4CB6 puts a JP in every slot; before it has run there is no table. */
    if (!s_extended || ctx->wram[0x3000 + 0x1A] != 0xC3) return;
    unsigned last = 0, next = ctx->hram[0x33] | ctx->hram[0x34] << 8;
    for (int steps = 0; next; ++steps) {
        if (steps == from || shantae_slot_index(ctx, next) < 0) return;   /* not a list of its slots */
        last = next;
        next = slot_word(ctx, shantae_slot_index(ctx, next), 0x7C);
    }
    s->slots = SHANTAE_MAX_SLOTS;
    if (from == SHANTAE_ORIGINAL_SLOTS) memset(ctx->wram + 0x3000 + (TAIL - 0xD000), 0, 0xE000 - TAIL);
    add_free_slots(ctx, from);
    if (last) {
        set_slot_word(ctx, shantae_slot_index(ctx, last), 0x7C, SLOT_ADDR(from));
    } else {
        ctx->hram[0x33] = (uint8_t)SLOT_ADDR(from);
        ctx->hram[0x34] = (uint8_t)(SLOT_ADDR(from) >> 8);
    }
}

/* The extra collision nodes as 01:711E leaves each of its own, free: +0
 * cleared, the four handlers JP $0C41 (a RET), each linked to the next. */
static void add_free_nodes(GBContext *ctx, SlotState *s) {
    memset(ctx->wram_ext + NODE_OFFSET, 0, NODE_MAPPED);
    for (int i = 0; i < EXTRA_NODES; ++i) {
        const unsigned node = NODE_EXT_BASE + i * NODE_SIZE;
        for (unsigned handler = 2; handler < 0x0E; handler += 3) {
            node_memory(ctx, node + handler)[0] = 0xC3;
            set_node_word(ctx, node + handler + 1, 0x0C41);
        }
        set_node_word(ctx, node + 0x10, i + 1 < EXTRA_NODES ? node + NODE_SIZE : 0);
    }
    s->nodes = EXTRA_NODES;
}

/* 01:7186, LD A,$40: 01:711E has built the pool's free list and ended it at
 * its last node; with the view it goes on to the extra nodes. Only a room
 * load (01:4CA3) builds a pool; the inventory builds its table alone
 * (00:0C26). One while the inventory's copy is kept means the inventory was
 * left without 05:6D20, through the debug grid (05:5A43): the copy never
 * comes back, and the checks that wait for it (check_slots, check_nodes, the
 * reapers) would stay off in every room after. */
static void build_nodes(GBContext *ctx, SlotState *s) {
    s->backup_slots = 0;
    s->nodes = 0;
    if (s_extended) {
        add_free_nodes(ctx, s);
        set_node_word(ctx, NODE_LAST_NEXT, NODE_EXT_BASE);
        s_counts.node_pools++;
    }
    sync(ctx);
}

/* A state saved with a grown table but the pool of 12: the extra nodes go on
 * the end of its free list. Objects that found no node keep none until they
 * spawn again. */
static void upgrade_nodes(GBContext *ctx, SlotState *s) {
    /* 01:711E puts a JP in every node; before it has run there is no pool. */
    if (node_memory(ctx, NODE_BASE + 2)[0] != 0xC3) return;
    unsigned last = 0, next = ctx->hram[0x6A] | ctx->hram[0x6B] << 8;   /* FFEA */
    for (int steps = 0; next; ++steps) {
        if (steps == ORIGINAL_NODES || next < NODE_BASE || next >= NODE_BASE + ORIGINAL_NODES * NODE_SIZE ||
            (next - NODE_BASE) % NODE_SIZE)
            return;   /* not a list of its nodes */
        last = next;
        next = node_word(ctx, next + 0x10);
    }
    add_free_nodes(ctx, s);
    if (last) {
        set_node_word(ctx, last + 0x10, NODE_EXT_BASE);
    } else {
        ctx->hram[0x6A] = (uint8_t)NODE_EXT_BASE;
        ctx->hram[0x6B] = (uint8_t)(NODE_EXT_BASE >> 8);
    }
}

void shantae_slots_state_loaded(GBContext *ctx) {
    SlotState *s = slot_state(ctx);
    if (!s) return;
    if (s->magic != PRIVATE_MAGIC) {
        upgrade(ctx, s);
        s_counts.upgrades += s->slots == SHANTAE_MAX_SLOTS;
    }
    if (s->nodes != EXTRA_NODES) {
        s->nodes = 0;
        if (s->slots > SHANTAE_ORIGINAL_SLOTS) upgrade_nodes(ctx, s);
        s_counts.node_upgrades += s->nodes == EXTRA_NODES;
    }
    sync(ctx);
}

/* The free list against the slots' status: cut where it leaves the table,
 * meets a slot twice or reaches a live one, then the dead slots it does not
 * hold on its end in table order. C397 (a count nothing reads) follows. Left
 * alone while the inventory has the table in bank 5. */
static void check_slots(GBContext *ctx, SlotState *s) {
    if (s->backup_slots) return;
    const int count = shantae_slot_count(ctx);
    uint8_t listed[SHANTAE_MAX_SLOTS] = {0};
    int last = -1, cut = 0;
    for (unsigned a = ctx->hram[0x33] | ctx->hram[0x34] << 8; a;) {
        const int i = shantae_slot_index(ctx, a);
        if (i < 0 || listed[i] || *slot_byte(ctx, i, 0) != 0xFF) {
            cut = 1;
            break;
        }
        listed[i] = 1;
        last = i;
        a = slot_word(ctx, i, 0x7C);
    }
    int first = -1, prev = -1, live = 0;
    for (int i = 0; i < count; ++i) {
        if (*slot_byte(ctx, i, 0) != 0xFF) {
            ++live;
            continue;
        }
        if (listed[i]) continue;
        if (prev < 0) first = i;
        else set_slot_word(ctx, prev, 0x7C, SLOT_ADDR(i));
        prev = i;
    }
    if (!cut && first < 0) return;
    if (prev >= 0) set_slot_word(ctx, prev, 0x7C, 0);
    const unsigned rest = first >= 0 ? SLOT_ADDR(first) : 0;
    if (last >= 0) {
        set_slot_word(ctx, last, 0x7C, rest);
    } else {
        ctx->hram[0x33] = (uint8_t)rest;
        ctx->hram[0x34] = (uint8_t)(rest >> 8);
    }
    ctx->wram[0x397] = (uint8_t)live;   /* C397, the live-object count */
    s_counts.slot_repairs++;
}

/* Node `i` of the pool: its own 12 at DD40, then the extra ones at E000. */
static unsigned node_addr(int i) {
    return i < ORIGINAL_NODES ? NODE_BASE + i * NODE_SIZE : NODE_EXT_BASE + (i - ORIGINAL_NODES) * NODE_SIZE;
}
static int node_index(unsigned addr) {
    if (addr >= NODE_BASE && addr < NODE_BASE + ORIGINAL_NODES * NODE_SIZE && (addr - NODE_BASE) % NODE_SIZE == 0)
        return (int)(addr - NODE_BASE) / NODE_SIZE;
    if (addr >= NODE_EXT_BASE && addr < NODE_EXT_BASE + EXTRA_NODES * NODE_SIZE && (addr - NODE_EXT_BASE) % NODE_SIZE == 0)
        return ORIGINAL_NODES + (int)(addr - NODE_EXT_BASE) / NODE_SIZE;
    return -1;
}

/* Marks the list from `head` (linked through +$10) with `mark` and returns its
 * length, or -1 if it leaves the pool or meets a node already marked. */
static int mark_nodes(GBContext *ctx, unsigned head, uint8_t *marks, uint8_t mark) {
    int count = 0;
    for (unsigned n = head; n; n = node_word(ctx, n + 0x10), ++count) {
        const int i = node_index(n);
        if (i < 0 || marks[i]) return -1;
        marks[i] = mark;
    }
    return count;
}

/* Every node is on one of the pool's lists whenever no pool routine is
 * running, as at the start of the movement pass. States saved before 01:71FF's
 * test took the high byte can have one on neither (the worm shaft's: a worm's
 * node, the worm alive and naming it at +$1D, CA03 counting it in use) or
 * stale prev links (+$0E) waiting to do that. A node on neither list goes
 * back at the head of the nodes in use when its object is still alive and
 * still names it, as 01:7192 would have put it, and on the free list
 * otherwise; the prev links and CA03 follow the lists. Lists that leave the
 * pool or share a node are left alone (a room load builds them again), and
 * so is the pool while the inventory has the table in bank 5 (its objects are
 * not the nodes' owners then). */
static void check_nodes(GBContext *ctx, SlotState *s) {
    if (extra_nodes(ctx) != EXTRA_NODES || s->backup_slots || node_memory(ctx, NODE_BASE + 2)[0] != 0xC3) return;
    enum { FREE = 1, USED = 2, TOTAL = ORIGINAL_NODES + EXTRA_NODES };
    uint8_t marks[TOTAL] = {0};
    unsigned used_head = ctx->hram[0x6C] | ctx->hram[0x6D] << 8;   /* FFEC */
    unsigned free_head = ctx->hram[0x6A] | ctx->hram[0x6B] << 8;   /* FFEA */
    if (mark_nodes(ctx, used_head, marks, USED) < 0 || mark_nodes(ctx, free_head, marks, FREE) < 0) return;
    for (int i = 0; i < TOTAL; ++i) {
        if (marks[i]) continue;
        const unsigned n = node_addr(i), owner = node_word(ctx, n);
        const uint8_t *st = shantae_slot_field(ctx, owner) == 0 ? shantae_slot_memory(ctx, owner) : NULL;
        if (st && *st != 0xFF && (shantae_slot_memory(ctx, owner + 0x1D)[0] | shantae_slot_memory(ctx, owner + 0x1E)[0] << 8) == n) {
            set_node_word(ctx, n + 0x10, used_head);
            used_head = n;
        } else {
            set_node_word(ctx, n + 0x10, free_head);
            free_head = n;
        }
        s_counts.node_repairs++;
    }
    ctx->hram[0x6C] = (uint8_t)used_head;
    ctx->hram[0x6D] = (uint8_t)(used_head >> 8);
    ctx->hram[0x6A] = (uint8_t)free_head;
    ctx->hram[0x6B] = (uint8_t)(free_head >> 8);
    unsigned prev = 0, used = 0;
    for (unsigned n = used_head; n; prev = n, n = node_word(ctx, n + 0x10), ++used)
        if (node_word(ctx, n + 0x0E) != prev) set_node_word(ctx, n + 0x0E, prev);
    ctx->wram[0xA03] = (uint8_t)used;   /* CA03, the nodes in use */
}

static int extra_live(GBContext *ctx) {
    for (int i = SHANTAE_ORIGINAL_SLOTS; i < shantae_slot_count(ctx); ++i)
        if (*slot_byte(ctx, i, 0) != 0xFF) return 1;
    return 0;
}

/* 09:4953 owns one controller (C01F) and seven platforms (bank 7's DE24,
 * count CC06). The widened 0A:40A8 trigger can create them before a doorway
 * calls 09:4953 unconditionally. Overwriting those handles leaks the previous
 * eight objects and their collision nodes. Keep creation/removal idempotent,
 * including calls made by the doorway rather than the trigger's flag test.
 * These scripts are private to 09:4953/49ED; regular moving platforms have
 * different scripts and spawn records. */
static int platform_part(GBContext *ctx, int slot) {
    if (*slot_byte(ctx, slot, 0) == 0xFF || *slot_byte(ctx, slot, 4) != 9 ||
        *slot_byte(ctx, slot, 0x24) || slot_word(ctx, slot, 0x27)) return 0;
    const unsigned script = slot_word(ctx, slot, 2);
    if (script >= 0x47D5 && script <= 0x47E5) return 1; /* controller */
    if ((script >= 0x46EA && script <= 0x4710) || (script >= 0x47E6 && script <= 0x480C)) return 2;
    return 0;
}

static int platform_group_live(GBContext *ctx) {
    const unsigned owner = ctx->wram[0x1F] | ctx->wram[0x20] << 8;
    const int slot = shantae_slot_index(ctx, owner);
    return slot >= 0 && platform_part(ctx, slot) == 1;
}

static int platform_group_has_space(GBContext *ctx) {
    unsigned next = ctx->hram[0x33] | ctx->hram[0x34] << 8;
    uint8_t seen[SHANTAE_MAX_SLOTS] = {0};
    for (int n = 0; n < 8; ++n) {
        const int slot = shantae_slot_index(ctx, next);
        if (slot < 0 || seen[slot]) return 0;
        seen[slot] = 1;
        next = slot_word(ctx, slot, 0x7C);
    }
    return 1;
}

/* Return a known orphan's node and slot just as 01:71FF and 00:12D7 do.
 * check_nodes has already repaired the node lists. */
static void free_platform_part(GBContext *ctx, int slot, const uint8_t *used) {
    const unsigned n = slot_word(ctx, slot, 0x1D);
    const int index = node_index(n);
    if (index >= 0 && used[index] == 1 && node_word(ctx, n) == SLOT_ADDR(slot)) {
        const unsigned prev = node_word(ctx, n + 0x0E), next = node_word(ctx, n + 0x10);
        if (prev) set_node_word(ctx, prev + 0x10, next);
        else {
            ctx->hram[0x6C] = (uint8_t)next;
            ctx->hram[0x6D] = (uint8_t)(next >> 8);
        }
        if (next) set_node_word(ctx, next + 0x0E, prev);
        set_node_word(ctx, n + 0x10, ctx->hram[0x6A] | ctx->hram[0x6B] << 8);
        ctx->hram[0x6A] = (uint8_t)n;
        ctx->hram[0x6B] = (uint8_t)(n >> 8);
        ctx->wram[0xA03]--;
    }
    set_slot_word(ctx, slot, 0x7C, ctx->hram[0x33] | ctx->hram[0x34] << 8);
    ctx->hram[0x33] = (uint8_t)SLOT_ADDR(slot);
    ctx->hram[0x34] = (uint8_t)(SLOT_ADDR(slot) >> 8);
    *slot_byte(ctx, slot, 0) = 0xFF;
    ctx->wram[0x397]--;
    s_counts.reaped++;
}

/* Repair clones in older states: only the current group's handles can ever
 * be released by 09:4996. Do not inspect the inventory's replacement table. */
static void reap_platforms(GBContext *ctx, const SlotState *s) {
    if (s->backup_slots) return;
    uint8_t held[SHANTAE_MAX_SLOTS] = {0};
    if (platform_group_live(ctx)) {
        if (ctx->wram[0xC06] != 7) return;
        held[shantae_slot_index(ctx, ctx->wram[0x1F] | ctx->wram[0x20] << 8)] = 1;
        for (int i = 0; i < 7; ++i) {
            const unsigned p = 0x7E24 + 2 * i;
            const int slot = shantae_slot_index(ctx, ctx->wram[p] | ctx->wram[p + 1] << 8);
            if (slot < 0 || held[slot] || platform_part(ctx, slot) != 2) return;
            held[slot] = 1;
        }
    }
    uint8_t used[ORIGINAL_NODES + EXTRA_NODES] = {0};
    if (mark_nodes(ctx, ctx->hram[0x6C] | ctx->hram[0x6D] << 8, used, 1) < 0 ||
        mark_nodes(ctx, ctx->hram[0x6A] | ctx->hram[0x6B] << 8, used, 2) < 0) return;
    for (int i = 0; i < shantae_slot_count(ctx); ++i)
        if (!held[i] && platform_part(ctx, i)) free_platform_part(ctx, i, used);
}

/* A gate's part (script 09:56E2) waiting at 09:56F2-56FE for +$18, with no
 * object linking it at +$61: nothing will ever tell it to go. It is freed as
 * 00:12E7 frees an object; it has no record and no linked object (+$1D). */
static void reap_orphans(GBContext *ctx) {
    const int count = shantae_slot_count(ctx);
    for (int i = 0; i < count; ++i) {
        const unsigned script = slot_word(ctx, i, 2);
        if (*slot_byte(ctx, i, 0) >= 0x80 || *slot_byte(ctx, i, 4) != 0x09 || script < 0x56F2 ||
            script > 0x56FE || *slot_byte(ctx, i, 0x18) || *slot_byte(ctx, i, 0x19) ||
            slot_word(ctx, i, 0x1B) != 0x0C41 || slot_word(ctx, i, 0x1D) || *slot_byte(ctx, i, 0x24))
            continue;
        int linked = 0;
        for (int j = 0; j < count && !linked; ++j)
            linked = *slot_byte(ctx, j, 0) != 0xFF && slot_word(ctx, j, 0x61) == SLOT_ADDR(i);
        if (linked) continue;
        set_slot_word(ctx, i, 0x7C, ctx->hram[0x33] | ctx->hram[0x34] << 8);
        ctx->hram[0x33] = (uint8_t)SLOT_ADDR(i);
        ctx->hram[0x34] = (uint8_t)(SLOT_ADDR(i) >> 8);
        *slot_byte(ctx, i, 0) = 0xFF;
        ctx->wram[0x397]--;   /* C397, the live-object count */
        s_counts.reaped++;
    }
}

uint8_t shantae_slots_imm(GBContext *ctx, uint8_t bank, uint16_t pc, uint8_t orig) {
    SlotState *s = slot_state(ctx);
    if (!s) return orig;
    if (bank == 1 && pc == 0x4CD1 && orig == 0x20) return build_table(ctx, s);
    if (bank == 1 && pc == 0x4CFE && orig == 0x00 && s->slots == SHANTAE_MAX_SLOTS) {
        /* The loop has ended the list at slot 92: on to A000. */
        set_slot_word(ctx, SHANTAE_ECHO_SLOTS - 1, 0x7C, SLOT_ADDR(SHANTAE_ECHO_SLOTS));
    } else if (bank == 1 && pc == 0x7186 && orig == 0x40) {
        build_nodes(ctx, s);
    } else if (bank == 9 && pc == NODE_SETUP_CLEAR_PC && orig == 0x00 && s->slots > SHANTAE_ORIGINAL_SLOTS &&
               ctx->hl == (uint16_t)(ctx->bc + 0x6B)) {
        /* HL = BC + $6B, BC the node 01:7192 handed out: the object is DE. */
        ctx->hl = (uint16_t)(ctx->de + 0x6B);
    } else if (bank == 9 && (pc == PLATFORM_SPAWN_PC - 2 || pc == PLATFORM_FREE_PC - 2) &&
               s->slots > SHANTAE_ORIGINAL_SLOTS) {
        /* Resume in shantae_slots_dispatch before the flag/handles change. */
        ctx->stopped = 1;
    } else if (bank == 0 && pc == 0x1312 && orig == 0x20) {
        return (uint8_t)shantae_slot_count(ctx);
    } else if (bank == 0 && pc == 0x1384 && orig == 0x7E && ctx->bc == SLOT_ADDR(SHANTAE_ECHO_SLOTS - 1) &&
               shantae_slot_count(ctx) > SHANTAE_ECHO_SLOTS) {
        /* ADD A,$7E with A = C, then LD C,A and INC B on a carry: from slot
         * 92 the scripts go on at A000 instead of FDC6. */
        ctx->b = (uint8_t)((SHANTAE_CART_BASE >> 8) - 1);
        return (uint8_t)(0x100 - ctx->a);
    } else if (bank == 0 && pc == 0x0C4B && orig == 0x03 && s->slots > SHANTAE_ORIGINAL_SLOTS) {
        /* LD A,$03 in 00:0C45, before any object moves: check the free list
         * and the collision pool, free orphaned parts, and return to the
         * scheduler at 00:0C4D, where the extra slots move first (move_extra). */
        check_slots(ctx, s);
        check_nodes(ctx, s);
        reap_platforms(ctx, s);
        reap_orphans(ctx);
        if (extra_live(ctx)) ctx->stopped = 1;
    } else if (bank == 4 && (orig == 0x0A || orig == 0x00) &&
               (pc == 0x4CA8 || pc == 0x4CF6 || pc == 0x4D30 || pc == 0x4D5A || pc == 0x4CDF ||
                pc == 0x4CE8 || pc == 0x4D24 || pc == 0x4D4E || pc == 0x4D90 || pc == 0x4D99)) {
        /* A save routine turns cartridge RAM on ($0A) or off ($00). */
        s->cart_yielded = orig == 0x0A;
        sync(ctx);
    }
    return orig;
}

/* At 00:0C4D: call the next live extra slot's movement routine the way 00:0C45
 * calls slot 31's, returning here with the slot's index under MOVE_MARKER on
 * the stack. 00:0C45 pushed AF last, so a first arrival never finds the
 * marker. Returns 1 when it called one. */
static int move_extra(GBContext *ctx) {
    int below;
    if (gb_read16(ctx, ctx->sp) == MOVE_MARKER) {
        gb_pop16(ctx);
        below = gb_pop16(ctx);
    } else {
        below = shantae_slot_count(ctx);
    }
    for (int i = below - 1; i >= SHANTAE_ORIGINAL_SLOTS && i < shantae_slot_count(ctx); --i) {
        if (*slot_byte(ctx, i, 0) == 0xFF) continue;
        const uint8_t bank = *slot_byte(ctx, i, 0x19);
        gb_push16(ctx, (uint16_t)i);
        gb_push16(ctx, MOVE_MARKER);
        gb_push16(ctx, MOVE_RESUME_PC);
        gb_write8(ctx, 0xFF70, 3);
        ctx->hram[0x11] = bank;
        gb_write8(ctx, 0x2000, bank);
        ctx->a = bank;
        ctx->bc = (uint16_t)SLOT_ADDR(i);
        ctx->pc = (uint16_t)(SLOT_ADDR(i) + 0x1A);
        gb_tick(ctx, 112);   /* 00:0C4F-0C61 for one slot */
        s_counts.moves++;
        return 1;
    }
    ctx->a = 3;   /* what 00:0C4B loaded */
    return 0;
}

static int draw_list_index(unsigned addr) {
    if (addr < DRAW_LIST_BASE || (addr - DRAW_LIST_BASE) % DRAW_LIST_SIZE) return -1;
    const unsigned index = (addr - DRAW_LIST_BASE) / DRAW_LIST_SIZE;
    return index < DRAW_LISTS ? (int)index : -1;
}

/* At 13:4484, about to test slot 31: list the next live extra slot's draw
 * entry as 13:4000 lists each slot's, into the list its +$2C names (bank 7).
 * Returns 0 once all are listed. */
static int layer_extra(GBContext *ctx, SlotState *s) {
    while (s->draw_next && s->draw_next < shantae_slot_count(ctx)) {
        const int i = s->draw_next++;
        if (*slot_byte(ctx, i, 0) >= 0x80) continue;
        const unsigned list = slot_word(ctx, i, 0x2C);
        const int index = draw_list_index(list);
        if (index >= 0 && s->overflow_count[index] < EXTRA_SLOTS) {
            s->overflow[index][s->overflow_count[index]++] = (uint8_t)i;
        } else if (list >= 0xD000 && list < 0xE000) {
            /* Not one of the eight lists: write it as the original would. */
            uint8_t *bank7 = ctx->wram + 7 * 0x1000 - 0xD000;
            const uint8_t count = (uint8_t)(bank7[list] + 2);
            bank7[list] = count;
            const unsigned entry = list + count;
            if (entry + 1 < 0xE000) {
                bank7[entry] = (uint8_t)SLOT_ADDR(i);
                bank7[entry + 1] = (uint8_t)(SLOT_ADDR(i) >> 8);
            }
        }
        gb_tick(ctx, 196);   /* 13:4025-4047 for one slot */
        s_counts.layered++;
        return 1;
    }
    s->draw_next = 0;
    return 0;
}

int shantae_slots_dispatch(GBContext *ctx, uint16_t addr) {
    if (addr != MOVE_RESUME_PC && addr != DRAW_LAST_PC && addr != PLATFORM_SPAWN_PC && addr != PLATFORM_FREE_PC) return 0;
    SlotState *s = slot_state(ctx);
    if (!s) return 0;
    if (addr == PLATFORM_SPAWN_PC || addr == PLATFORM_FREE_PC) {
        if (ctx->rom_bank != 9 || s->slots <= SHANTAE_ORIGINAL_SLOTS) return 0;
        const int live = platform_group_live(ctx);
        if (addr == PLATFORM_SPAWN_PC) {
            if (!live && platform_group_has_space(ctx)) return 0;
            /* No allocation may partially build a group: its routines do not
             * check for a null slot. An active trigger retries next tick. */
        } else {
            if (live && ctx->wram[0xC06]) return 0;
            if (ctx->hl >= 0xC02D && ctx->hl < 0xC035) ctx->wram[ctx->hl - 0xC000] = 0;
        }
        gb_ret_timed(ctx, 16);
        return 1;
    }
    if (addr == MOVE_RESUME_PC) return move_extra(ctx);
    /* pc stays at 13:4484 until the last extra slot is listed. */
    return ctx->rom_bank == 0x13 && s->draw_next ? layer_extra(ctx, s) : 0;
}

void shantae_slots_read_tap(GBContext *ctx, uint16_t addr) {
    const uint16_t pc = ctx->pc;
    if ((pc == FREE_PREV_TEST_PC || pc == FREE_NEXT_TEST_PC) && ctx->rom_bank == 1 && ctx->a == 0 &&
        shantae_node_field(ctx, addr) >= 0) {
        /* LD D,(HL): the neighbour's high byte, about to be tested with its
         * low byte (A) by OR E. Node E000 is no neighbour to that test; with
         * the high byte in A it tests the whole address. */
        ctx->a = node_memory(ctx, addr)[0];
        return;
    }
    if (pc != DRAW_LAST_PC && pc != BACKUP_PC && pc != RESTORE_PC) return;
    SlotState *s = slot_state(ctx);
    if (!s) return;
    if (ctx->rom_bank == 5 && addr == 0xD000) {
        /* The first reads of 05:6D08, which saves bank 3 to bank 5, and of
         * 05:6D20, which restores it. */
        if (pc == BACKUP_PC) {
            memcpy(s->backup, ctx->wram_ext, ECHO_MAPPED);
            memcpy(s->backup + ECHO_MAPPED, ctx->wram_ext + CART_OFFSET, CART_MAPPED);
            s->backup_slots = s->slots;
        } else if (pc == RESTORE_PC && s->backup_slots) {
            if (s->backup_slots > SHANTAE_ORIGINAL_SLOTS) memcpy(ctx->wram_ext, s->backup, ECHO_MAPPED);
            if (s->backup_slots > SHANTAE_ECHO_SLOTS)
                memcpy(ctx->wram_ext + CART_OFFSET, s->backup + ECHO_MAPPED, CART_MAPPED);
            s->slots = s->backup_slots;
            s->backup_slots = 0;
            sync(ctx);
        }
        return;
    }
    if (ctx->rom_bank == 0x13 && pc == DRAW_LAST_PC && addr == 0xDF42 && s->slots > SHANTAE_ORIGINAL_SLOTS) {
        memset(s->overflow_count, 0, sizeof(s->overflow_count));
        s->draw_next = SHANTAE_ORIGINAL_SLOTS;
        ctx->stopped = 1;
    }
}

uint8_t shantae_slots_read_override(GBContext *ctx, uint16_t addr, uint8_t value) {
    const uint16_t pc = ctx->pc;
    if (pc >= CHILD_LINK_FIRST_PC && pc <= CHILD_LINK_LAST_PC && ctx->rom_bank == 0x11) {
        /* 11:78B6 takes +$63 for B and +$64 for C: give each the other. */
        const SlotState *s = slot_state(ctx);
        const int field = shantae_slot_field(ctx, addr);
        if (!s || s->slots <= SHANTAE_ORIGINAL_SLOTS || (field != CHILD_LINK && field != CHILD_LINK + 1))
            return value;
        s_counts.children_freed += field == CHILD_LINK;
        return *shantae_slot_memory(ctx, field == CHILD_LINK ? addr + 1u : addr - 1u);
    }
    if (pc < 0x4E72 || pc > 0x4F03 || ctx->rom_bank != 1) return value;
    SlotState *s = slot_state(ctx);
    if (!s || s->slots <= SHANTAE_ORIGINAL_SLOTS) return value;
    /* 01:4E90 reads each list's count (2n-1, $FF for none) with LD A,(HL+) at
     * 4E9A + 15k, then 01:4E6D its entries at 4E72/4E74. */
    if (pc >= 0x4E9A && (pc - 0x4E9A) % 15 == 0) {
        const int index = draw_list_index(addr);
        if (index < 0) return value;
        const unsigned real = (uint8_t)(value + 1) >> 1;
        s->consumer_list = (uint8_t)index;
        s->consumer_real = (uint8_t)real;
        unsigned total = real + s->overflow_count[index];
        if (total > DRAW_LIST_MAX) total = DRAW_LIST_MAX;
        return total ? (uint8_t)(total * 2 - 1) : value;
    }
    if ((pc != 0x4E72 && pc != 0x4E74) || s->consumer_list >= DRAW_LISTS) return value;
    const unsigned first = DRAW_LIST_BASE + s->consumer_list * DRAW_LIST_SIZE + 1;
    if (addr < first || (addr - first) / 2 < s->consumer_real) return value;
    const unsigned extra = (addr - first) / 2 - s->consumer_real;
    if (extra >= s->overflow_count[s->consumer_list]) return value;
    const unsigned slot = SLOT_ADDR(s->overflow[s->consumer_list][extra]);
    const int high = (addr - first) & 1;
    s_counts.drawn += !high;
    return (uint8_t)(high ? slot >> 8 : slot);
}
