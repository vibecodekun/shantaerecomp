/* Direct checks of every statically identified writable-code family. */
#include "shantae.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int game_dispatch_override(GBContext*, uint16_t);
static const unsigned char dma_code[] = {0x3E,0xC0,0xE0,0x46,0x3E,0x28,0x3D,0x20,0xFD,0xC9};
static unsigned long long checks;

static void prepare(GBContext* ctx, unsigned bank, unsigned pc, unsigned target, unsigned bug) {
    gb_context_reset(ctx, true);
    ctx->cycles = ctx->frame_cycles = ctx->last_sync_cycles = 0;
    ctx->frame_done = 0;
    ctx->rom_bank = bank;
    ctx->wram_bank = 3;
    ctx->pc = pc;
    ctx->sp = 0xCFFE;
    ctx->wram[0xFFE] = 0x34;
    ctx->wram[0xFFF] = 0x12;
    ctx->a = 0xC0;
    ctx->halt_bug = bug;
    if (pc >= 0xC000) {
        gb_write8(ctx, pc, 0xC3);
        gb_write8(ctx, pc + 1, target & 255);
        gb_write8(ctx, pc + 2, target >> 8);
    }
}

static int compare(GBContext* a, GBContext* b, unsigned steps) {
    GBDifferentialOptions options = {.max_steps = steps, .compare_memory = true,
        .log_fallbacks = true, .fail_on_fallback = true, .quiet = true};
    GBDifferentialResult result;
    unsigned pc = a->pc;
    if (!gb_run_differential(a, b, &options, &result) || a->total_interpreter_entries) {
        fprintf(stderr, "FAIL RAM %04X: %s\n", pc, result.message);
        return 0;
    }
    checks += steps;
    return 1;
}

static int run_writer(GBContext* a, GBContext* b, unsigned bank, unsigned start, unsigned end) {
    prepare(a, bank, start, 0, 0); prepare(b, bank, start, 0, 0);
    unsigned guard = 0;
    while (a->pc != end && guard++ < 30000) if (!compare(a, b, 1)) return 0;
    if (a->pc != end) { fprintf(stderr, "Writer did not finish at %04X\n", end); return 0; }
    return 1;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    unsigned char* rom = malloc(size);
    if (!rom || fread(rom, 1, size, f) != (size_t)size) return 2;
    fclose(f);
    GBContext* a = gb_context_create(shantae_default_config());
    GBContext* b = gb_context_create(shantae_default_config());
    if (!a || !b || !gb_context_load_rom(a, rom, size) || !gb_context_load_rom(b, rom, size)) return 2;
    a->mbc_type = b->mbc_type = rom[0x147];
    gbrt_interp_fallback_logging = false;
    /* Execute the actual ROM initializers, not just hand-built fixtures. */
    if (!run_writer(a, b, 1, 0x41A3, 0x41BC) || gb_read8(a, 0xC374) != 0xC3 || gb_read16(a, 0xC375) != 0x0A43) return 1;
    if (!run_writer(a, b, 0, 0x0B6E, 0x0B73) || gb_read8(a, 0xC385) != 0xC3) return 1;
    if (!run_writer(a, b, 0, 0x2730, 0x2734) || gb_read8(a, 0xFFA1) != 0xC3) return 1;
    if (!run_writer(a, b, 1, 0x4748, 0x4754) || gb_read16(a, 0xFFA4) != 0x0B9D) return 1;
    if (!run_writer(a, b, 1, 0x47F6, 0x4802) || gb_read16(a, 0xFFA4) != 0x0BBA) return 1;
    if (!run_writer(a, b, 1, 0x4CB6, 0x4CFB)) return 1;
    for (unsigned n = 0; n < 32; ++n)
        if (gb_read8(a, 0xD000 + n * 0x7E + 0x1A) != 0xC3 || gb_read8(a, 0xD000 + n * 0x7E + 0x29) != 0xC3) return 1;
    if (!run_writer(a, b, 1, 0x7120, 0x7179)) return 1;
    for (unsigned n = 0; n < 12; ++n)
        for (unsigned off = 2; off <= 11; off += 3)
            if (gb_read8(a, 0xDD40 + n * 0x13 + off) != 0xC3 || gb_read16(a, 0xDD41 + n * 0x13 + off) != 0x0C41) return 1;
    if (!run_writer(a, b, 1, 0x7CFC, 0x7D09) || memcmp(a->hram, dma_code, sizeof(dma_code))) return 1;
    printf("PASS actual ROM writers and resulting RAM programs\n"); fflush(stdout);
    /* All live destination values, both normal and repeated-opcode fetch. */
    for (unsigned target = 0; target < 65536; ++target) {
        for (unsigned bug = 0; bug < 2; ++bug) {
            prepare(a, target & 255, 0xC374, target, bug);
            prepare(b, target & 255, 0xC374, target, bug);
            if (!compare(a, b, 1)) return 1;
        }
    }
    printf("PASS all 65536 patched JP destinations, normal and HALT-bug fetch\n"); fflush(stdout);
    unsigned slots[116], banks[116], count = 0;
    slots[count] = 0xC374; banks[count++] = 3;
    slots[count] = 0xC385; banks[count++] = 3;
    slots[count] = 0xFFA1; banks[count++] = 3;
    slots[count] = 0xFFA3; banks[count++] = 3;
    for (unsigned n = 0; n < 32; ++n) {
        slots[count] = 0xD000 + n * 0x7E + 0x1A; banks[count++] = 3;
        slots[count] = 0xD000 + n * 0x7E + 0x29; banks[count++] = 3;
    }
    for (unsigned n = 0; n < 12; ++n)
        for (unsigned off = 2; off <= 11; off += 3) {
            slots[count] = 0xDD40 + n * 0x13 + off; banks[count++] = 7;
        }
    for (unsigned i = 0; i < count; ++i) {
        for (unsigned bank = 0; bank < 256; ++bank) {
            for (unsigned echo = 0; echo < 2; ++echo) {
                unsigned pc = slots[i];
                if (echo) { if (pc >= 0xDE00) continue; pc += 0x2000; }
                prepare(a, bank, pc, 0x4000 + bank, 0);
                prepare(b, bank, pc, 0x4000 + bank, 0);
                a->wram_bank = b->wram_bank = banks[i];
                gb_write8(a, pc, 0xC3); gb_write8(b, pc, 0xC3);
                gb_write16(a, pc + 1, 0x4000 + bank); gb_write16(b, pc + 1, 0x4000 + bank);
                if (!compare(a, b, 1)) return 1;
            }
        }
    }
    printf("PASS all %u JP slots, all 256 ROM banks, mapped echo aliases\n", count); fflush(stdout);
    const unsigned entries[] = {0xFF80,0xFF82,0xFF84,0xFF86,0xFF87,0xFF89};
    for (unsigned i = 0; i < sizeof(entries)/sizeof(entries[0]); ++i)
        for (unsigned value = 0; value < 256; ++value)
            for (unsigned mode = 0; mode < 8; ++mode) {
                prepare(a, 1, entries[i], 0, mode & 1);
                prepare(b, 1, entries[i], 0, mode & 1);
                memcpy(a->hram, dma_code, sizeof(dma_code)); memcpy(b->hram, dma_code, sizeof(dma_code));
                a->hram[1] = b->hram[1] = a->hram[5] = b->hram[5] = value;
                a->a = b->a = value;
                a->f_z = b->f_z = !!(mode & 2);
                if (mode & 4) { gb_write8(a, 0xFF46, 0xC0); gb_write8(b, 0xFF46, 0xC0); }
                if (!compare(a, b, 1)) return 1;
            }
    /* Whole routine: copy/transfer/wait/return, and each possible wait count.
     * Ending at RET avoids executing the arbitrary caller after the routine. */
    for (unsigned count_value = 0; count_value < 256; ++count_value) {
        prepare(a, 1, 0xFF80, 0, 0); prepare(b, 1, 0xFF80, 0, 0);
        memcpy(a->hram, dma_code, sizeof(dma_code)); memcpy(b->hram, dma_code, sizeof(dma_code));
        a->hram[5] = b->hram[5] = count_value;
        for (unsigned j = 0; j < 160; ++j) a->wram[j] = b->wram[j] = (j * 7) ^ count_value;
        if (!compare(a, b, 4 + 2 * (count_value ? count_value : 256))) return 1;
        /* Agreement alone missed the startup crash: both backends used to
         * read FFFF while DMA still blocked the stack before RET's fetch. */
        if (count_value == 0x28 && (a->pc != 0x1234 || a->sp != 0xD000 ||
                a->cycles != 680 || a->dma.active || a->dma.pending ||
                memcmp(a->oam, a->wram, 160))) {
            fprintf(stderr, "FAIL stock DMA return: PC=%04X SP=%04X cycles=%u DMA=%u\n",
                    a->pc, a->sp, a->cycles, a->dma.active);
            return 1;
        }
    }
    /* Probe each stack-read boundary, including partial bus release. These
     * expected PCs are independent of the reference interpreter. */
    const unsigned remaining[] = {4, 8, 12};
    const unsigned expected[] = {0x1234, 0x12FF, 0xFFFF};
    for (unsigned speed = 0; speed < 2; ++speed)
        for (unsigned i = 0; i < 3; ++i) {
            prepare(a, 1, 0xFF89, 0, 0); prepare(b, 1, 0xFF89, 0, 0);
            memcpy(a->hram, dma_code, sizeof(dma_code));
            memcpy(b->hram, dma_code, sizeof(dma_code));
            a->cgb_double_speed = b->cgb_double_speed = speed;
            a->ime = b->ime = 0;
            a->ime_pending = b->ime_pending = 2;
            a->dma.active = b->dma.active = 1;
            a->dma.source_high = b->dma.source_high = 0xC0;
            a->dma.cycles_remaining = b->dma.cycles_remaining = remaining[i];
            a->dma.progress = b->dma.progress = 160 - remaining[i] / 4;
            if (!compare(a, b, 1) || a->pc != expected[i] || a->sp != 0xD000 ||
                    a->cycles != (speed ? 8u : 16u) || a->ime || a->ime_pending != 1) {
                fprintf(stderr, "FAIL DMA RET boundary: speed=%u remaining=%u PC=%04X\n",
                        speed, remaining[i], a->pc);
                return 1;
            }
        }
    printf("PASS stock DMA returns to caller; RET bus-release boundaries and EI delay\n");
    printf("PASS RAM: %llu independent instruction comparisons; no generated interpreter entries\n", checks);
    gb_context_destroy(a); gb_context_destroy(b); free(rom);
    return 0;
}
