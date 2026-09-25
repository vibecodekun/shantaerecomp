/* Native translations of Shantae's writable code. ROM writer evidence and
 * disassembly: tools/audit_ram_coverage.py, docs/whole-rom-coverage.md.
 * Dispatch is by program location; there is no runtime opcode decoder here.
 */
#include "gbrt.h"
#include "expanded_view.h"
#include "object_slots.h"

/* An object's movement or draw JP (slot+$1A, slot+$29). The table can run on
 * past DFFF and into A000 (object_slots.c). */
static int object_jump(GBContext* ctx, uint16_t addr) {
    if (ctx->wram_bank != 3) return 0;
    int field = shantae_slot_field(ctx, addr);
    return field == 0x1A || field == 0x29;
}

/* A collision node's handler JP (+2, +5, +8, +B): the pool in bank 7 at
 * DD40, and its growth at E000 (object_slots.c). */
static int node_jump(GBContext* ctx, uint16_t addr) {
    int field = shantae_node_field(ctx, addr);
    return field == 2 || field == 5 || field == 8 || field == 11;
}

static int jump_slot(GBContext* ctx, uint16_t addr) {
    if (object_jump(ctx, addr) || node_jump(ctx, addr)) return 1;
    if (addr < 0xC000) return 0;
    if (addr >= 0xE000 && addr < 0xFE00) {
        if ((ctx->wram_bank == 3 && shantae_slot_memory(ctx, addr)) || shantae_node_field(ctx, addr) >= 0)
            return 0;   /* not the echo */
        addr -= 0x2000;
    }
    if (addr == 0xC374 || addr == 0xC385 || addr == 0xFFA1 || addr == 0xFFA3) return 1;
    return object_jump(ctx, addr) || node_jump(ctx, addr);
}

int shantae_player_move_return(GBContext* ctx);   /* extras.c */

int game_dispatch_override(GBContext* ctx, uint16_t addr) {
    /* Where the early player move ("Reduce input lag") returns. */
    if (addr == 0x1386) {
        shantae_player_move_return(ctx);
        return 0;
    }
    /* The object table's passes over the slots past 31. */
    if (shantae_slots_dispatch(ctx, addr)) return 1;
    /* The spawner filling an area the expanded view has just revealed. */
    if (shantae_view_dispatch(ctx, addr)) return 1;
    if (addr < 0xA000) return 0;
    if (jump_slot(ctx, addr)) {
        if (gb_read8(ctx, addr) != 0xC3) return 0;
        /* JP nn: only its destination is patched by the game. */
        uint16_t fetch = addr + (ctx->halt_bug ? 0 : 1);
        uint16_t target = gb_read8(ctx, fetch);
        target |= (uint16_t)gb_read8(ctx, (uint16_t)(fetch + 1)) << 8;
        ctx->halt_bug = 0;
        ctx->pc = target;
        gb_tick(ctx, 16);
        return 1;
    }
    /* ROM 01:7D0A -> FF80, copied by 01:7CFC. Native instruction entries
     * preserve DMA timing, frame pauses, and mutable source-page/count bytes. */
    if (addr < 0xFF80 || addr > 0xFF89) return 0;
    unsigned bug = ctx->halt_bug != 0;
    switch (addr) {
        case 0xFF80: case 0xFF84: /* LD A,n */
            if (gb_read8(ctx, addr) != 0x3E) return 0;
            ctx->a = gb_read8(ctx, (uint16_t)(addr + 1 - bug));
            ctx->pc = addr + 2 - bug;
            ctx->halt_bug = 0;
            gb_tick(ctx, 8);
            return 1;
        case 0xFF82: /* LDH ($46),A */
            if (gb_read8(ctx, addr) != 0xE0 || gb_read8(ctx, addr + 1) != 0x46) return 0;
            ctx->pc = addr + 2 - bug;
            ctx->halt_bug = 0;
            gb_tick(ctx, 12);
            gb_write8(ctx, bug ? 0xFFE0 : 0xFF46, ctx->a);
            return 1;
        case 0xFF86: /* DEC A */
            if (gb_read8(ctx, addr) != 0x3D) return 0;
            ctx->a = gb_dec8(ctx, ctx->a);
            ctx->pc = addr + 1 - bug;
            ctx->halt_bug = 0;
            gb_tick(ctx, 4);
            return 1;
        case 0xFF87: { /* JR NZ,-3 */
            if (gb_read8(ctx, addr) != 0x20 || gb_read8(ctx, addr + 1) != 0xFD) return 0;
            ctx->pc = addr + 2 - bug;
            ctx->halt_bug = 0;
            if (!ctx->f_z) ctx->pc += bug ? 0x20 : -3;
            gb_tick(ctx, ctx->f_z ? 8 : 12);
            return 1;
        }
        case 0xFF89: /* RET */
            if (gb_read8(ctx, addr) != 0xC9) return 0;
            ctx->halt_bug = 0;
            gb_ret_timed(ctx, 16);
            return 1;
        default: return 0;
    }
}
