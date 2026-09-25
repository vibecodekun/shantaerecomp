/* Every ROM entry, independent of gameplay; no battery saves are read/written. */
#include "shantae.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void shantae_exhaustive_step(GBContext*);
extern uint32_t shantae_exhaustive_id(unsigned bank, unsigned pc, unsigned bug);
extern void shantae_exhaustive_cb_boundary(GBContext*);

static void exhaustive_dispatch(GBContext* ctx, uint16_t pc) {
    ctx->pc = pc;
    shantae_exhaustive_step(ctx);
}

static void cb_boundary_dispatch(GBContext* ctx, uint16_t pc) {
    ctx->pc = pc;
    shantae_exhaustive_cb_boundary(ctx);
}

static int illegal(unsigned op) {
    switch (op) {
        case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4: case 0xEB:
        case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD: return 1;
        default: return 0;
    }
}

static void prepare(GBContext* ctx, unsigned bank, unsigned pc, unsigned bug, unsigned flags) {
    gb_context_reset(ctx, true);
    ctx->cycles = ctx->frame_cycles = ctx->last_sync_cycles = 0;
    ctx->frame_done = 0;
    ctx->rom_bank = bank;
    ctx->pc = pc;
    ctx->sp = 0xCFFE;
    ctx->bc = 0xD000;
    ctx->de = 0xD100;
    ctx->hl = 0xD200;
    ctx->a = flags ? 0xFF : 0x01;
    ctx->f_z = ctx->f_c = ctx->f_n = ctx->f_h = flags;
    gb_pack_flags(ctx);
    ctx->halt_bug = bug;
    ctx->wram[0xFFE] = 0x34;
    ctx->wram[0xFFF] = 0x12;
    ctx->wram[0x1200] = 0x81;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: whole_rom_check ROM variants|all|production\n");
        return 2;
    }
    int variants = strcmp(argv[2], "variants") == 0;
    int production = strcmp(argv[2], "production") == 0;
    if (!variants && !production && strcmp(argv[2], "all")) return 2;
    FILE* file = fopen(argv[1], "rb");
    if (!file) return 2;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    unsigned char* rom = malloc(size);
    if (!rom || fread(rom, 1, size, file) != (size_t)size) return 2;
    fclose(file);
    GBContext* a = gb_context_create(shantae_default_config());
    GBContext* b = gb_context_create(shantae_default_config());
    if (!a || !b || !gb_context_load_rom(a, rom, size) || !gb_context_load_rom(b, rom, size)) return 2;
    a->mbc_type = b->mbc_type = rom[0x147];
    if (!production) gb_set_dispatch(exhaustive_dispatch, exhaustive_dispatch);
    gbrt_interp_fallback_logging = false;
    GBDifferentialOptions options = {.max_steps = 1, .compare_memory = true,
        .log_fallbacks = true, .fail_on_fallback = true, .quiet = true};
    GBDifferentialResult result;
    unsigned char* seen = calloc(1000000, 1);
    unsigned long long checked = 0, mapped = 0;
    unsigned skipped = 0;
    for (unsigned window = 0; window <= (unsigned)size / 0x4000; ++window) {
        unsigned bank = window ? window - 1 : 0;
        unsigned base = window ? 0x4000 : 0;
        for (unsigned bug = 0; bug < 2; ++bug) {
            for (unsigned offset = 0; offset < 0x4000; ++offset) {
                unsigned pc = base + offset;
                uint32_t id = shantae_exhaustive_id(bank, pc, bug);
                if (id >= 1000000) { fprintf(stderr, "Unmapped entry %u:%u\n", bank, pc); return 1; }
                mapped++;
                if (illegal(rom[bank * 0x4000 + offset])) { skipped++; continue; }
                if ((variants || production) && seen[id] && offset < 0x3FFE) continue;
                seen[id] = 1;
                for (unsigned flags = 0; flags < ((variants || production) ? 2u : 1u); ++flags) {
                    prepare(a, bank, pc, bug, flags);
                    prepare(b, bank, pc, bug, flags);
                    if (!gb_run_differential(a, b, &options, &result) ||
                        a->total_interpreter_entries || a->total_dispatch_fallbacks) {
                        fprintf(stderr, "FAIL %03X:%04X bug=%u flags=%u native_id=%u: %s\n",
                                bank, pc, bug, flags, id, result.message);
                        return 1;
                    }
                    checked++;
                }
            }
        }
        if (window % 16 == 0) {
            printf("window %u/256: %llu mapped, %llu differential checks\n", window, mapped, checked);
            fflush(stdout);
        }
    }
    // Check operand fetches at window edges while the PPU crosses an access
    // boundary. An operand in VRAM must be read after the appropriate tick.
    for (unsigned window = 0; window <= (unsigned)size / 0x4000; ++window) {
        unsigned bank = window ? window - 1 : 0;
        unsigned base = window ? 0x4000 : 0;
        for (unsigned offset = 0x3FFE; offset <= 0x3FFF; ++offset) {
            if (illegal(rom[bank * 0x4000 + offset])) continue;
            for (unsigned phase = 0; phase < 2; ++phase) {
                prepare(a, bank, base + offset, 0, 0);
                prepare(b, bank, base + offset, 0, 0);
                gb_write8(a, 0xFF40, 0x91);
                gb_write8(b, 0xFF40, 0x91);
                gb_tick(a, phase ? 248 : 76);
                gb_tick(b, phase ? 248 : 76);
                if (!gb_run_differential(a, b, &options, &result) || a->total_interpreter_entries) {
                    fprintf(stderr, "FAIL PPU boundary %02X:%04X phase=%u: %s\n",
                            bank, base + offset, phase, result.message);
                    return 1;
                }
                checked++;
            }
        }
    }
    // Exercise every possible CB byte fetched across a bank boundary, including
    // variants absent from the stock ROM. Patches are in private test contexts.
    gb_set_dispatch(cb_boundary_dispatch, cb_boundary_dispatch);
    a->rom[0x3FFF] = b->rom[0x3FFF] = 0xCB;
    for (unsigned cb = 0; cb < 256; ++cb) {
        a->rom[0] = b->rom[0] = cb;
        for (unsigned flags = 0; flags < 2; ++flags) {
            prepare(a, 0, 0x3FFF, 0, flags);
            prepare(b, 0, 0x3FFF, 0, flags);
            if (!gb_run_differential(a, b, &options, &result) || a->total_interpreter_entries) {
                fprintf(stderr, "FAIL cross-window CB %02X: %s\n", cb, result.message);
                return 1;
            }
            checked++;
        }
    }
    printf("PASS %s: %llu mapped slots, %llu instruction comparisons (including all 256 cross-window CB opcodes); %u illegal-opcode slots explicitly trapped\n",
           argv[2], mapped, checked, skipped);
    gb_context_destroy(a);
    gb_context_destroy(b);
    free(rom);
    free(seen);
    return 0;
}
