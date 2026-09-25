/* Exercise the actual generated dispatcher against the interpreter, without
 * loading or modifying a save. Run from the project root with a ROM and a list
 * of hex bank/address pairs produced by audit_native_coverage.py. */
#include "shantae.h"
#include <stdio.h>
#include <stdlib.h>

static GBContext* prepare(const unsigned char* rom, size_t size, unsigned bank, unsigned pc) {
    GBContext* ctx = gb_context_create(shantae_default_config());
    if (!ctx || !gb_context_load_rom(ctx, rom, size)) exit(2);
    ctx->mbc_type = rom[0x147];
    gb_context_reset(ctx, true);
    gb_write8(ctx, 0x2000, bank);
    gb_write8(ctx, 0xFF91, bank);
    gb_write8(ctx, 0xFF70, 3);
    ctx->pc = pc;
    ctx->sp = 0xCFFE;
    ctx->bc = 0xD000;
    ctx->de = 0xD100;
    ctx->hl = 0xD200;
    ctx->ime = 0;
    return ctx;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: native_dispatch_check ROM native-targets.txt\n");
        return 2;
    }
    FILE* file = fopen(argv[1], "rb");
    if (!file) return 2;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size <= 0) return 2;
    unsigned char* rom = malloc((size_t)size);
    if (!rom || fread(rom, 1, (size_t)size, file) != (size_t)size) return 2;
    fclose(file);
    gbrt_interp_fallback_logging = false; // Reference-side interpretation is intentional.
    GBDifferentialOptions options = {.max_steps = 1, .compare_memory = true,
                                    .log_fallbacks = true, .fail_on_fallback = true};
    GBDifferentialResult result;
    file = fopen(argv[2], "r");
    if (!file) return 2;
    unsigned bank, address, checked = 0, failed = 0;
    while (fscanf(file, "%x %x", &bank, &address) == 2) {
        GBContext* generated = prepare(rom, size, bank, address);
        GBContext* interpreted = prepare(rom, size, bank, address);
        if (!gb_run_differential(generated, interpreted, &options, &result)) {
            fprintf(stderr, "FAIL native entry %02X:%04X\n", bank, address);
            failed++;
        }
        checked++;
        gb_context_destroy(generated);
        gb_context_destroy(interpreted);
    }
    fclose(file);
    // Actual $32 records from the user's gameplay fallback log. Exercise
    // operand reads, MBC5 switch and JP HL into the callback, not just its entry.
    const unsigned calls[][2] = {{6, 0x57C0}, {7, 0x5A55}};
    for (unsigned i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) {
        GBContext* generated = prepare(rom, size, calls[i][0], 0x1A46);
        GBContext* interpreted = prepare(rom, size, calls[i][0], 0x1A46);
        generated->de = interpreted->de = calls[i][1];
        options.max_steps = 28;
        if (!gb_run_differential(generated, interpreted, &options, &result)) failed++;
        gb_context_destroy(generated);
        gb_context_destroy(interpreted);
    }
    // Run the two additional observed far calls all the way through the
    // banked inline-argument reader and back to the corrected return PC.
    const unsigned returns[][2] = {{0x6516, 0x651E}, {0x6528, 0x6530}};
    for (unsigned i = 0; i < sizeof(returns) / sizeof(returns[0]); i++) {
        GBContext* generated = prepare(rom, size, 5, returns[i][0]);
        GBContext* interpreted = prepare(rom, size, 5, returns[i][0]);
        options.max_steps = 1;
        unsigned steps;
        bool matched = true;
        for (steps = 0; steps < 256; steps++) {
            matched = gb_run_differential(generated, interpreted, &options, &result);
            if (!matched) break;
            if (generated->pc == returns[i][1] && generated->rom_bank == 5) break;
        }
        if (!matched || generated->pc != returns[i][1] || generated->rom_bank != 5) {
            fprintf(stderr, "FAIL far return 05:%04X (stopped at %02X:%04X after %u steps)\n",
                    returns[i][1], generated->rom_bank, generated->pc, steps);
            failed++;
        }
        gb_context_destroy(generated);
        gb_context_destroy(interpreted);
    }
    printf("Native dispatch: %u entries, 2 script calls and 2 far returns checked; %u failures\n", checked, failed);
    free(rom);
    return failed || !checked ? 1 : 0;
}
