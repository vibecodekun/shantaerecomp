/* Which instructions write a memory range, from a saved state: the game's own
 * code, stepped one instruction at a time with the game's hooks installed.
 *
 *   trace_writes ROM STATE INPUT FRAMES WATCH [MAX]
 *
 * INPUT is "buttons:frames,..." as the debug server takes buttons (R L U D A
 * B S T, - for none), held for that many frames, then nothing. WATCH is
 * vram:BANK:ADDR:LEN, wram:BANK:ADDR:LEN (ADDR in C000-DFFF; bank 0 for
 * C000-CFFF) or hram:ADDR:LEN, hex. Each instruction that changes the range is
 * printed with the frame, bank:pc it ran at, its registers and what changed,
 * up to MAX lines (default 64). Research only: nothing in the game uses it. */
#include "shantae.h"
#include "platform_sdl.h"
#include "game_extras.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *watched(GBContext *ctx, const char *kind, unsigned bank, unsigned addr) {
    if (!strcmp(kind, "vram")) return ctx->vram + bank * 0x2000 + (addr - 0x8000);
    if (!strcmp(kind, "hram")) return ctx->hram + (addr - 0xFF80);
    if (addr < 0xD000) return ctx->wram + (addr - 0xC000);
    return ctx->wram + bank * 0x1000 + (addr - 0xD000);
}

static void buttons_of(const char *s, uint8_t *dpad, uint8_t *buttons) {
    *dpad = 0xFF;
    *buttons = 0xFF;
    for (; *s && *s != ':'; ++s) switch (*s) {
        case 'R': *dpad &= ~0x01; break;
        case 'L': *dpad &= ~0x02; break;
        case 'U': *dpad &= ~0x04; break;
        case 'D': *dpad &= ~0x08; break;
        case 'A': *buttons &= ~0x01; break;
        case 'B': *buttons &= ~0x02; break;
        case 'T': *buttons &= ~0x04; break;
        case 'S': *buttons &= ~0x08; break;
    }
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: trace_writes ROM STATE INPUT FRAMES WATCH [MAX]\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    unsigned char *rom = malloc(size);
    if (!rom || fread(rom, 1, size, f) != (size_t)size) return 2;
    fclose(f);
    GBContext *ctx = gb_context_create(shantae_default_config());
    if (!ctx || !gb_context_load_rom(ctx, rom, size)) return 2;
    gbrt_interp_fallback_logging = false;
    game_on_init(ctx);
    if (!gb_context_load_state_file(ctx, argv[2])) {
        fprintf(stderr, "could not load %s\n", argv[2]);
        return 2;
    }
    const long frames = strtol(argv[4], NULL, 0);
    const int max = argc > 6 ? atoi(argv[6]) : 64;
    char kind[8] = {0};
    unsigned bank = 0, addr = 0, len = 0;
    if (!strncmp(argv[5], "hram", 4)) {
        strcpy(kind, "hram");
        if (sscanf(argv[5] + 5, "%x:%x", &addr, &len) != 2) return 2;
    } else if (sscanf(argv[5], "%4[a-z]:%x:%x:%x", kind, &bank, &addr, &len) != 4) {
        return 2;
    }
    uint8_t *before = malloc(len);
    const char *input = argv[3];
    long input_left = 0;
    uint64_t start = ctx->completed_frames;
    int printed = 0;
    uint64_t frame = start;
    while (ctx->completed_frames - start < (uint64_t)frames && printed < max) {
        if (ctx->completed_frames != frame || input_left == 0) {
            if (ctx->completed_frames != frame) {
                frame = ctx->completed_frames;
                if (input_left > 0) input_left--;
            }
            if (input_left == 0) {
                if (*input) {
                    const char *colon = strchr(input, ':');
                    buttons_of(input, &g_joypad_dpad, &g_joypad_buttons);
                    input_left = colon ? strtol(colon + 1, NULL, 10) : 1;
                    const char *comma = strchr(input, ',');
                    input = comma ? comma + 1 : input + strlen(input);
                } else {
                    g_joypad_dpad = g_joypad_buttons = 0xFF;
                    input_left = -1;
                }
            }
        }
        memcpy(before, watched(ctx, kind, bank, addr), len);
        const uint16_t pc = ctx->pc, sp = ctx->sp;
        const uint8_t rom_bank = (uint8_t)ctx->rom_bank, svbk = ctx->io[0x70];
        gb_pack_flags(ctx);
        const uint16_t af = ctx->af, bc = ctx->bc, de = ctx->de, hl = ctx->hl;
        gb_debug_step(ctx, GB_EXECUTION_GENERATED);
        /* As the frame runner does once a picture is done. */
        if (ctx->frame_done) gb_reset_frame(ctx);
        const uint8_t *now = watched(ctx, kind, bank, addr);
        if (memcmp(before, now, len)) {
            unsigned first = 0;
            while (before[first] == now[first]) first++;
            unsigned last = len - 1;
            while (before[last] == now[last]) last--;
            printf("frame %llu %02X:%04X af=%04X bc=%04X de=%04X hl=%04X sp=%04X svbk=%u hdma=%04X->%04X  +%04X..%04X:",
                   (unsigned long long)(ctx->completed_frames - start), rom_bank, pc, af, bc, de, hl, sp, svbk,
                   ctx->hdma.source, ctx->hdma.dest, first, last);
            for (unsigned i = first; i <= last && i < first + 16; ++i) printf(" %02X", now[i]);
            printf("\n");
            printed++;
        }
    }
    return 0;
}
