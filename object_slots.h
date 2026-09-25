#pragma once
/* Shantae's object table, grown past 32 slots for the expanded view
 * (object_slots.c). */
#include <stdint.h>
struct GBContext;

#define SHANTAE_SLOT_BASE 0xD000
#define SHANTAE_SLOT_SIZE 0x7E
#define SHANTAE_ORIGINAL_SLOTS 32
/* D000 + 93 x $7E ends at FDC6, just below OAM; the rest start at A000. */
#define SHANTAE_ECHO_SLOTS 93
#define SHANTAE_CART_BASE 0xA000
#define SHANTAE_MAX_SLOTS 157

/* Where slot `slot` starts. */
static inline unsigned shantae_slot_addr(int slot) {
    return slot < SHANTAE_ECHO_SLOTS ? SHANTAE_SLOT_BASE + slot * SHANTAE_SLOT_SIZE
                                     : SHANTAE_CART_BASE + (slot - SHANTAE_ECHO_SLOTS) * SHANTAE_SLOT_SIZE;
}

/* Give the context the memory past bank 3, and choose the size of the tables
 * map loads build from now on: SHANTAE_MAX_SLOTS when `extended`, else 32. */
void shantae_slots_init(struct GBContext *ctx, int extended);
/* Slots in the table the game is using: 32, SHANTAE_MAX_SLOTS, or 93 in a
 * state from the build that stopped there, until the next map load. */
int shantae_slot_count(const struct GBContext *ctx);
/* Backing byte of a bank-3 address in the table (its slots at D000 and
 * A000), read or written without the game noticing; NULL outside it. */
uint8_t *shantae_slot_memory(struct GBContext *ctx, unsigned addr);
/* The slot starting at `addr`, or -1. */
int shantae_slot_index(const struct GBContext *ctx, unsigned addr);
/* How far into its slot `addr` is, or -1 outside the table. */
int shantae_slot_field(const struct GBContext *ctx, unsigned addr);
/* How far into its collision node `addr` is while SVBK selects bank 7 (the
 * pool at DD40, and its growth at E000), or -1. */
int shantae_node_field(const struct GBContext *ctx, unsigned addr);

/* Hooks, called from extras.c, ram_native.c and the expanded view's taps. */
uint8_t shantae_slots_imm(struct GBContext *ctx, uint8_t bank, uint16_t pc, uint8_t orig);
int shantae_slots_dispatch(struct GBContext *ctx, uint16_t addr);
void shantae_slots_read_tap(struct GBContext *ctx, uint16_t addr);
uint8_t shantae_slots_read_override(struct GBContext *ctx, uint16_t addr, uint8_t value);
/* After any state load: a state from before the table grew gets its extra
 * slots, one from before the collision pool grew its extra nodes, and the
 * mappings follow the table the state holds. */
void shantae_slots_state_loaded(struct GBContext *ctx);

/* Debug counts since launch (shantae_view_info): extra slots moved, layered
 * and drawn, states given the extra slots, orphaned gate parts freed, pools
 * built with the extra collision nodes, states given them, nodes the pool
 * check put back on a list, free lists the slot check mended, and children
 * 0A:42F8 freed at the right address. */
typedef struct {
    unsigned long long moves, layered, drawn, upgrades, reaped, node_pools, node_upgrades, node_repairs,
        slot_repairs, children_freed;
} ShantaeSlotCounts;
void shantae_slots_counts(ShantaeSlotCounts *out);
