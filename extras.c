/*
 * extras.c -- Shantae (USA) game hooks for the gbrecompiled runtime.
 *
 * Shantae reads its hardware type once at boot (00:3973) into HRAM $FFFE:
 * 0 = DMG, 1 = GBC, 3 = GBA (register B bit 0 at reset). Only two places test
 * for GBA: the title screen's "GBA Enhanced!" graphics (1C:423C) and the
 * palette loader (00:1C7A), which on GBA adds $40 to each palette source to
 * pick the brightened copy stored after every GBC palette set. The Bandit
 * Town Tinkerbat secret keys off the same GBA state.
 *
 * "GBA Enhanced mode" (default on) boots as a GBA. When "original colors" is
 * on, the palette loader's CP 3 at 00:1CB6 is overridden so it never matches;
 * everything else keeps seeing real GBA hardware -- the same split as the
 * "Shantae GBC palettes" IPS (00:1CB4 LDH A,($FE) -> LD A,1), but switchable
 * and on a stock ROM. Both settings live in shantae.ini and are edited from
 * the launcher's Mods page (launcher_options.c) or the Esc menu (extras_ui.cpp).
 *
 * "Remove slowdown" (default on) installs the runtime's frame hold: a frame
 * whose logic has not finished at the end of the picture waits there for the
 * CPU instead of becoming a lag frame (see shantae_frame_unfinished); with the
 * expanded view, for up to eight frames of CPU instead of two.
 *
 * "Reduce input lag" (default on) shows each tick's sprites one frame sooner.
 * The VBlank handler streams the tick's sprite graphics to VRAM with an HBlank
 * DMA (00:0A7D), complete only at the end of the next picture, so 01:667C
 * points the OAM DMA page (FF81) at the previous tick's buffer. The scroll is
 * delayed to match: each tick starts by committing the camera the previous
 * tick drew its sprites with (00:26F6 -> 03:722F). With the option on, the
 * VBlank handler copies the buffer the tick just wrote (FFD9), uploads its
 * graphics with a general DMA, and moves the copied sprites by the camera's
 * last step so they sit on the committed scroll, which the expanded view's
 * surround uses too. Game state is untouched: FF81 is restored after the copy.
 * The camera still follows a frame behind, as in the original.
 *
 * It also starts the player's moves one tick sooner. Each tick runs every
 * object's movement routine (00:0C45) and then every object's script (00:1305).
 * The player's idle routine turns a button into a script branch, and that script
 * picks the new movement routine (jump, walk, ...), so the first step waited for
 * the next tick. When the player's script has picked a new routine, the step
 * hook runs it once after the last object's script (run_player_move); from then
 * on it runs in 00:0C45 as usual, one tick ahead of the original. That first run
 * sees the joypad as the next tick would with the same buttons held: nothing
 * newly pressed. The press that picked the routine is never new to it in the
 * original, and the dance (0E:4D40) ends on a new Select press. It waits for the
 * other scripts because they can move the player first, as the original's first
 * run would find: after a death her script resumes once the entrance (09:61C0)
 * sets CB89, and the entrance puts her at the respawn point (CA1D/CA1F) a tick
 * later, after her script in the same pass. Run straight after her script, the
 * idle routine tested the water where she had drowned and she died again.
 *
 * "Smoother movement" (default on) is in moveset.c, and for the
 * transformations in forms.c; "Easier dancing" (default on) is in dance.c.
 * Their settings are kept here and their hooks are called from
 * shantae_imm_override (dance.c's also from the step hook).
 */
#include "game_extras.h"
#include "gbrt.h"
#include "gb_host_paths.h"
#include "platform_sdl.h"
#include "expanded_view.h"
#include "object_slots.h"
#include "moveset.h"
#include "dance.h"
#include "forms.h"
#include "gb_custom_view.h"

#include <stdio.h>
#include <string.h>

#define PALETTE_GBA_CHECK_PC 0x1CB6   /* CP 3 after LDH A,($FE) in the palette loader */
#define SPRITE_DMA_PC 0x0A50          /* LD A,$07 before CALL $FF80, the OAM DMA */
#define SPRITE_HDMA_PC 0x0A7D         /* LD A,$CF: HBlank DMA of the sprite graphics */
#define SCROLL_BUILT_PC 0x730E        /* AND $F8 after both camera routines' build (bank 3) */
#define MOVES_PROLOGUE_PC 0x0C4B      /* LD A,$03 in 00:0C45, the movement routines */
#define SCRIPTS_PROLOGUE_PC 0x130B    /* LD A,$03 in 00:1305, the object scripts */
#define SCRIPTS_NEXT_SLOT_PC 0x1384   /* ADD A,$7E: on to the next object slot */
#define SCRIPTS_RESUME_PC 0x1386      /* the instruction after it */
#define PLAYER_POINTER 0xCA13
#define MOVE_MARKER 0x5AA5

static int s_loaded;
static int s_gba_enhanced = 1;
static int s_original_colors = 1;
static int s_expanded_view;
static int s_view_width = 256;
static int s_view_height = 240;
static int s_view_adaptive;
static int s_native_scaling = GB_CUSTOM_NATIVE_SCALING_MODE;
static int s_native_scale = 1;
static int s_room_zoom = SHANTAE_ROOM_ZOOM_FILL;
static int s_remove_slowdown = 1;
static int s_reduce_input_lag = 1;
static int s_smooth_moves = 1;
static int s_whip_moving = SHANTAE_WHIP_SLIDE;
static int s_air_speed_b = 1;
static int s_fast_crawl = 1;
static int s_smooth_forms = 1;
static int s_easy_dance = 1;
static int s_quick_steps = 1;
static int s_transform_invincible = 1;

static int clamp_int(int value, int lo, int hi) {
    return value < lo ? lo : value > hi ? hi : value;
}

static void settings_path(char *out, size_t size) {
    gb_host_state_path("shantae.ini", out, size);
}

/* The launcher and the hardware-mode decision both run before game_on_init,
 * so every accessor loads on first use. */
static void load_settings(void) {
    if (s_loaded) return;
    s_loaded = 1;
    char path[512];
    settings_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    int value;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "gba_enhanced=%d", &value) == 1) {
            s_gba_enhanced = value != 0;
        } else if (sscanf(line, "original_gbc_colors=%d", &value) == 1) {
            s_original_colors = value != 0;
        } else if (sscanf(line, "expanded_view=%d", &value) == 1) {
            s_expanded_view = value != 0;
        } else if (sscanf(line, "view_width=%d", &value) == 1) {
            s_view_width = clamp_int(value, SHANTAE_VIEW_MIN_WIDTH, SHANTAE_VIEW_MAX_WIDTH);
        } else if (sscanf(line, "view_height=%d", &value) == 1) {
            s_view_height = clamp_int(value, SHANTAE_VIEW_MIN_HEIGHT, SHANTAE_VIEW_MAX_HEIGHT);
        } else if (sscanf(line, "view_adaptive=%d", &value) == 1) {
            s_view_adaptive = value != 0;
        } else if (sscanf(line, "native_scaling=%d", &value) == 1) {
            s_native_scaling = clamp_int(value, GB_CUSTOM_NATIVE_IN_VIEW, GB_CUSTOM_NATIVE_WHOLE_SCALE);
        } else if (sscanf(line, "native_scale=%d", &value) == 1) {
            s_native_scale = clamp_int(value, 1, SHANTAE_NATIVE_MAX_SCALE);
        } else if (sscanf(line, "room_zoom=%d", &value) == 1) {
            s_room_zoom = clamp_int(value, SHANTAE_ROOM_ZOOM_OFF, SHANTAE_ROOM_ZOOM_WHOLE);
        } else if (sscanf(line, "remove_slowdown=%d", &value) == 1) {
            s_remove_slowdown = value != 0;
        } else if (sscanf(line, "reduce_input_lag=%d", &value) == 1) {
            s_reduce_input_lag = value != 0;
        } else if (sscanf(line, "smooth_moves=%d", &value) == 1) {
            s_smooth_moves = value != 0;
        } else if (sscanf(line, "whip_moving=%d", &value) == 1) {
            s_whip_moving = clamp_int(value, SHANTAE_WHIP_ORIGINAL, SHANTAE_WHIP_CANCEL);
        } else if (sscanf(line, "air_speed_b=%d", &value) == 1) {
            s_air_speed_b = value != 0;
        } else if (sscanf(line, "fast_crawl=%d", &value) == 1) {
            s_fast_crawl = value != 0;
        } else if (sscanf(line, "smooth_forms=%d", &value) == 1) {
            s_smooth_forms = value != 0;
        } else if (sscanf(line, "easy_dance=%d", &value) == 1) {
            s_easy_dance = value != 0;
        } else if (sscanf(line, "quick_steps=%d", &value) == 1) {
            s_quick_steps = value != 0;
        } else if (sscanf(line, "transform_invincible=%d", &value) == 1) {
            s_transform_invincible = value != 0;
        }
    }
    fclose(f);
}

static void save_settings(void) {
    char path[512];
    settings_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "gba_enhanced=%d\n", s_gba_enhanced);
    fprintf(f, "original_gbc_colors=%d\n", s_original_colors);
    fprintf(f, "expanded_view=%d\nview_width=%d\nview_height=%d\nview_adaptive=%d\n",
            s_expanded_view, s_view_width, s_view_height, s_view_adaptive);
    fprintf(f, "native_scaling=%d\nnative_scale=%d\nroom_zoom=%d\n", s_native_scaling, s_native_scale,
            s_room_zoom);
    fprintf(f, "remove_slowdown=%d\n", s_remove_slowdown);
    fprintf(f, "reduce_input_lag=%d\n", s_reduce_input_lag);
    fprintf(f, "smooth_moves=%d\nwhip_moving=%d\nair_speed_b=%d\nfast_crawl=%d\nsmooth_forms=%d\n", s_smooth_moves,
            s_whip_moving, s_air_speed_b, s_fast_crawl, s_smooth_forms);
    fprintf(f, "easy_dance=%d\nquick_steps=%d\ntransform_invincible=%d\n", s_easy_dance, s_quick_steps,
            s_transform_invincible);
    fclose(f);
}

int shantae_smooth_moves(void) { load_settings(); return s_smooth_moves; }
void shantae_set_smooth_moves(int on) {
    load_settings(); s_smooth_moves = on != 0; save_settings();
}
int shantae_whip_moving(void) { load_settings(); return s_whip_moving; }
void shantae_set_whip_moving(int mode) {
    load_settings();
    s_whip_moving = clamp_int(mode, SHANTAE_WHIP_ORIGINAL, SHANTAE_WHIP_CANCEL);
    save_settings();
}
int shantae_air_speed_b(void) { load_settings(); return s_air_speed_b; }
void shantae_set_air_speed_b(int on) {
    load_settings(); s_air_speed_b = on != 0; save_settings();
}
int shantae_fast_crawl(void) { load_settings(); return s_fast_crawl; }
void shantae_set_fast_crawl(int on) {
    load_settings(); s_fast_crawl = on != 0; save_settings();
}

int shantae_smooth_forms(void) { load_settings(); return s_smooth_forms; }
void shantae_set_smooth_forms(int on) {
    load_settings(); s_smooth_forms = on != 0; save_settings();
}

int shantae_easy_dance(void) { load_settings(); return s_easy_dance; }
void shantae_set_easy_dance(int on) {
    load_settings(); s_easy_dance = on != 0; save_settings();
}
int shantae_quick_steps(void) { load_settings(); return s_quick_steps; }
void shantae_set_quick_steps(int on) {
    load_settings(); s_quick_steps = on != 0; save_settings();
}
int shantae_transform_invincible(void) { load_settings(); return s_transform_invincible; }
void shantae_set_transform_invincible(int on) {
    load_settings(); s_transform_invincible = on != 0; save_settings();
}

int shantae_remove_slowdown(void) { load_settings(); return s_remove_slowdown; }
void shantae_set_remove_slowdown(int on) {
    load_settings(); s_remove_slowdown = on != 0; save_settings();
}

int shantae_reduce_input_lag(void) { load_settings(); return s_reduce_input_lag; }
void shantae_set_reduce_input_lag(int on) {
    load_settings(); s_reduce_input_lag = on != 0; save_settings();
}

/* Every frame loop ends at 00:0851: it sets FF8F and spins until the VBlank
 * handler (00:0884) has consumed it. FF8F still clear when the visible frame
 * ends means the logic has overrun, and the handler would skip this VBlank --
 * a lag frame, seen as slowdown. Holding the frame there lets it finish. */
static int shantae_frame_unfinished(GBContext *ctx) {
    return s_remove_slowdown && ctx->hram[0x0f] == 0;
}

int shantae_expanded_view(void) { load_settings(); return s_expanded_view; }
int shantae_view_width(void) { load_settings(); return s_view_width; }
int shantae_view_height(void) { load_settings(); return s_view_height; }
int shantae_view_adaptive(void) { load_settings(); return s_view_adaptive; }
void shantae_set_expanded_view(int on) {
    load_settings(); s_expanded_view = on != 0; save_settings();
}
void shantae_set_view_size(int width, int height) {
    load_settings();
    s_view_width = clamp_int(width, SHANTAE_VIEW_MIN_WIDTH, SHANTAE_VIEW_MAX_WIDTH);
    s_view_height = clamp_int(height, SHANTAE_VIEW_MIN_HEIGHT, SHANTAE_VIEW_MAX_HEIGHT);
    save_settings();
    shantae_view_apply_size();
}
void shantae_set_view_adaptive(int on) {
    load_settings(); s_view_adaptive = on != 0; save_settings();
    shantae_view_apply_size();
}
int shantae_native_scaling(void) { load_settings(); return s_native_scaling; }
int shantae_native_scale(void) { load_settings(); return s_native_scale; }
void shantae_set_native_scaling(int scaling, int scale) {
    load_settings();
    s_native_scaling = clamp_int(scaling, GB_CUSTOM_NATIVE_IN_VIEW, GB_CUSTOM_NATIVE_WHOLE_SCALE);
    s_native_scale = clamp_int(scale, 1, SHANTAE_NATIVE_MAX_SCALE);
    save_settings();
    shantae_view_apply_size();
}
int shantae_room_zoom(void) { load_settings(); return s_room_zoom; }
void shantae_set_room_zoom(int mode) {
    load_settings();
    s_room_zoom = clamp_int(mode, SHANTAE_ROOM_ZOOM_OFF, SHANTAE_ROOM_ZOOM_WHOLE);
    save_settings();
}

int shantae_gba_enhanced(void) {
    load_settings();
    return s_gba_enhanced;
}

void shantae_set_gba_enhanced(int on) {
    load_settings();
    s_gba_enhanced = on != 0;
    save_settings();
}

int shantae_original_colors(void) {
    load_settings();
    return s_original_colors;
}

void shantae_set_original_colors(int on) {
    load_settings();
    s_original_colors = on != 0;
    save_settings();
}

/* "Reduce input lag" state for one VBlank. The camera is FFE1/FFE3, which the
 * metasprite code subtracts. */
typedef struct {
    uint8_t saved_page;           /* FF81 to restore after the copy; 0 = not swapped */
    int have_camera;
    uint64_t frame;               /* completed_frames when the camera was read */
    uint16_t camera_x, camera_y;  /* at the previous VBlank */
    int shift_x, shift_y;         /* applied to the copied sprites */
    uint16_t built_dx, built_dy;  /* C9D2/C9D4 when the scroll was last built */
} SpriteLag;

/* The player's movement routine across one script phase (see run_player_move). */
typedef struct {
    uint16_t player;       /* slot noted at the start of the script phase; 0 = none */
    uint8_t routine[3];    /* its movement routine then (bank, JP target); once pending, the new one */
    uint16_t pending;      /* slot whose new routine runs after the last slot's script */
} PlayerMove;

/* Per context: the differential run steps two. */
typedef struct {
    GBContext *ctx;
    SpriteLag sprite;
    PlayerMove move;
} LagState;
static LagState s_lag[2];
/* For shantae_hold_info. */
static unsigned long long s_fresh_copies, s_original_copies, s_early_moves;

void shantae_input_lag_counts(unsigned long long *fresh, unsigned long long *original,
                              unsigned long long *early_moves, unsigned long long *replays) {
    *fresh = s_fresh_copies;
    *original = s_original_copies;
    *early_moves = s_early_moves;
    *replays = gb_platform_preempt_replays();
}

static LagState *lag_state(GBContext *ctx) {
    for (int i = 0; i < 2; i++)
        if (s_lag[i].ctx == ctx) return &s_lag[i];
    for (int i = 0; i < 2; i++)
        if (!s_lag[i].ctx) { s_lag[i].ctx = ctx; return &s_lag[i]; }
    memset(&s_lag[1], 0, sizeof(s_lag[1]));
    s_lag[1].ctx = ctx;
    return &s_lag[1];
}

static SpriteLag *sprite_lag(GBContext *ctx) { return &lag_state(ctx)->sprite; }

static unsigned ram16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* How far the tick's sprites must move to sit on the committed scroll, on one
 * axis. The scroll (C9FB/C9FD) is the camera of its last build plus the
 * background offset (C9D2/C9D4) as it was then (see note_scroll_built): bosses
 * move the offset later in the tick, so it can differ by the VBlank. Gameplay
 * loops build at the start of a tick (00:26F6), with the previous VBlank's
 * camera; scene loops build at the end (03:72EB), with the current one.
 * Returns 0 when neither is certain. */
static int sprite_shift(int have_previous, unsigned previous, unsigned now,
                        unsigned scroll, unsigned offset, int *shift) {
    const unsigned built = (scroll - offset) & 0xFFFF;
    if (now == built || now == previous) {
        *shift = 0;   /* the camera did not move, or the build used this one */
    } else if (have_previous && built == previous) {
        *shift = (int16_t)(now - previous);
    } else {
        return 0;
    }
    /* Larger steps are room changes, not scrolling or shaking. */
    return *shift >= -64 && *shift <= 64;
}

/* At 03:730E, where both camera routines have the new scroll in C9FF-CA02
 * (03:73B4 copies it to C9FB-C9FE): the offset it holds. */
static void note_scroll_built(GBContext *ctx) {
    SpriteLag *s = sprite_lag(ctx);
    s->built_dx = (uint16_t)ram16(ctx->wram + 0x9D2);
    s->built_dy = (uint16_t)ram16(ctx->wram + 0x9D4);
}

/* At 00:0A50, before the OAM DMA of a VBlank the main loop has finished. */
static void sprite_lag_before_dma(GBContext *ctx) {
    SpriteLag *s = sprite_lag(ctx);
    const unsigned x = ram16(ctx->hram + 0x61), y = ram16(ctx->hram + 0x63);
    /* A state load or a long gap makes the previous camera meaningless. */
    const int have_previous = s->have_camera && ctx->completed_frames - s->frame <= 4;
    const unsigned previous_x = s->camera_x, previous_y = s->camera_y;
    s->camera_x = (uint16_t)x;
    s->camera_y = (uint16_t)y;
    s->frame = ctx->completed_frames;
    s->have_camera = 1;
    s->saved_page = 0;
    const uint8_t page = ctx->hram[0x01], fresh = ctx->hram[0x59];
    /* C37F set skips the graphics upload (00:0A66), and a general DMA written
     * over a running HBlank DMA would cancel it; keep the original then. */
    if (s_reduce_input_lag && ctx->wram[0x37F] == 0 &&
        !(ctx->hdma.hblank_mode && ctx->hdma.blocks_remaining) &&
        (page == 0xD7 || page == 0xD8) && (fresh == 0xD7 || fresh == 0xD8) && fresh != page &&
        sprite_shift(have_previous, previous_x, x, ram16(ctx->wram + 0x9FB), s->built_dx, &s->shift_x) &&
        sprite_shift(have_previous, previous_y, y, ram16(ctx->wram + 0x9FD), s->built_dy, &s->shift_y)) {
        s->saved_page = page;
        ctx->hram[0x01] = fresh;
    }
    if (s_reduce_input_lag) {
        if (s->saved_page) s_fresh_copies++;
        else s_original_copies++;
    }
}

/* At 00:0A7D, after the copy: restore FF81 and move the sprites onto the
 * committed scroll. Returns 1 when this VBlank copied the fresh buffer. */
static int sprite_lag_after_dma(GBContext *ctx) {
    SpriteLag *s = sprite_lag(ctx);
    if (!s->saved_page) return 0;
    ctx->hram[0x01] = s->saved_page;
    s->saved_page = 0;
    if ((!s->shift_x && !s->shift_y) || ctx->dma.active || ctx->dma.pending) return 1;
    for (int i = 0; i < 40; i++) {
        uint8_t *o = ctx->oam + i * 4;
        if (!o[0] || o[0] >= 160 || !o[1] || o[1] >= 168) continue;   /* hidden */
        const int oy = o[0] + s->shift_y, ox = o[1] + s->shift_x;
        if (oy <= 0 || oy >= 160 || ox <= 0 || ox >= 168) { o[0] = 0; continue; }
        o[0] = (uint8_t)oy;
        o[1] = (uint8_t)ox;
    }
    return 1;
}

/* Objects are slots of $7E bytes from D000 in WRAM bank 3, and from A000 when
 * the table has grown (object_slots.c).
 * Slot+$19 is the bank of the movement routine and slot+$1A a JP to it, which
 * 00:0C45 calls with BC = slot. CA13 points at the player's slot (01:77EB). */
static uint8_t *object_slot(GBContext *ctx, unsigned addr) {
    return shantae_slot_memory(ctx, addr);
}

static unsigned player_slot(GBContext *ctx) {
    const unsigned p = ram16(ctx->wram + (PLAYER_POINTER - 0xC000));
    return shantae_slot_index(ctx, p) >= 0 ? p : 0;
}

static void read_move_routine(GBContext *ctx, unsigned slot, uint8_t routine[3]) {
    const uint8_t *s = object_slot(ctx, slot);
    routine[0] = s[0x19];
    routine[1] = s[0x1B];
    routine[2] = s[0x1C];
}

/* At 00:130B, the start of the script phase. */
static void note_player_move(GBContext *ctx) {
    PlayerMove *m = &lag_state(ctx)->move;
    m->pending = 0;
    m->player = s_reduce_input_lag ? player_slot(ctx) : 0;
    if (m->player) read_move_routine(ctx, m->player, m->routine);
}

/* The slots 00:1305 has left, this one included: E of the DE pushed at 00:1314,
 * which is on top of the stack from 00:1383 to 00:138A. */
static unsigned slots_left(GBContext *ctx) {
    return gb_read16(ctx, ctx->sp) & 0xFF;
}

/* At 00:1384, after a slot's script. After the player's, note a new movement
 * routine her script picked; after the last slot's, if she still has it, stop
 * at the next instruction so the step hook can run it (run_player_move). */
static void player_script_done(GBContext *ctx) {
    PlayerMove *m = &lag_state(ctx)->move;
    if (m->player && ctx->bc == m->player) {
        const unsigned slot = m->player;
        m->player = 0;
        uint8_t routine[3];
        read_move_routine(ctx, slot, routine);
        if (memcmp(routine, m->routine, 3)) {
            m->pending = (uint16_t)slot;
            memcpy(m->routine, routine, 3);
        }
    }
    if (!m->pending || slots_left(ctx) != 1) return;
    uint8_t routine[3];
    read_move_routine(ctx, m->pending, routine);
    const uint8_t *s = object_slot(ctx, m->pending);
    if (s[0] != 0xFF && s[0x1A] == 0xC3 && !memcmp(routine, m->routine, 3))
        ctx->stopped = 1;
    else
        m->pending = 0;
}

/* 00:1386 needs A, the carry and BC. run_player_move saves them, the ROM bank,
 * SVBK and the new presses on the game's stack under MOVE_MARKER, so a save
 * state taken while the routine runs resumes correctly; the routine returns to
 * 00:1386, where this restores them. The stack otherwise holds the slot counter
 * pushed at 00:1314 (low byte $01-$9D) there. Compiled code runs on through the
 * return without going back to the scheduler, so ram_native.c's dispatch
 * override calls this too. Returns 1 when it restored. */
int shantae_player_move_return(GBContext *ctx) {
    if (ctx->pc != SCRIPTS_RESUME_PC || gb_read16(ctx, ctx->sp) != MOVE_MARKER) return 0;
    gb_pop16(ctx);
    const uint16_t presses = gb_pop16(ctx);
    ctx->hram[0x0C] = (uint8_t)presses;
    ctx->hram[0x0E] = (uint8_t)(presses >> 8);
    const uint16_t banks = gb_pop16(ctx);
    ctx->af = gb_pop16(ctx) & 0xFFF0;
    gb_unpack_flags(ctx);
    ctx->bc = gb_pop16(ctx);
    ctx->hram[0x11] = (uint8_t)(banks >> 8);
    gb_write8(ctx, 0x2000, (uint8_t)(banks >> 8));
    gb_write8(ctx, 0xFF70, (uint8_t)banks);
    return 1;
}

/* At the top of a movement routine, before it has pushed anything: whether
 * run_player_move started it (its return address and MOVE_MARKER are on top
 * of the stack), not 00:0C45. */
int shantae_player_move_early(GBContext *ctx) {
    return gb_read16(ctx, ctx->sp) == SCRIPTS_RESUME_PC &&
           gb_read16(ctx, (uint16_t)(ctx->sp + 2)) == MOVE_MARKER;
}

/* Step hook: at 00:1386 after player_script_done stopped on the last slot, call
 * the player's movement routine the way 00:0C45 does. The joypad routine
 * (01:7C31) keeps the held buttons in FF8B, the new presses in FF8C and the
 * auto-repeat presses in FF8E; the next tick has none of the latter with the
 * same buttons held. */
static void run_player_move(GBContext *ctx) {
    if (ctx->pc != SCRIPTS_RESUME_PC || shantae_player_move_return(ctx)) return;
    PlayerMove *m = &lag_state(ctx)->move;
    if (!m->pending || slots_left(ctx) != 1) return;
    const unsigned slot = m->pending;
    m->pending = 0;
    gb_pack_flags(ctx);
    gb_push16(ctx, ctx->bc);
    gb_push16(ctx, ctx->af & 0xFFF0);
    gb_push16(ctx, (uint16_t)(ctx->hram[0x11] << 8 | ctx->io[0x70]));
    gb_push16(ctx, (uint16_t)(ctx->hram[0x0E] << 8 | ctx->hram[0x0C]));
    gb_push16(ctx, MOVE_MARKER);
    gb_push16(ctx, SCRIPTS_RESUME_PC);
    ctx->hram[0x0C] = 0;
    ctx->hram[0x0E] = 0;
    shantae_moves_tick(ctx);   /* the scripts since may have set the run flag */
    /* As 00:0C45 calls it: SVBK 3, the routine's bank in A, FF91 and the MBC. */
    const uint8_t bank = object_slot(ctx, slot)[0x19];
    gb_write8(ctx, 0xFF70, 3);
    ctx->hram[0x11] = bank;
    gb_write8(ctx, 0x2000, bank);
    ctx->a = bank;
    ctx->bc = (uint16_t)slot;
    ctx->pc = (uint16_t)(slot + 0x1A);
    s_early_moves++;
}

static void step_hook(GBContext *ctx) {
    shantae_dance_step(ctx);
    run_player_move(ctx);
}

static uint8_t shantae_imm_override(GBContext *ctx, uint8_t bank, uint16_t pc, uint8_t orig) {
    /* $FFFE is only ever 0, 1 or 3, so $FF makes the GBA test fail. */
    if (bank == 0 && pc == PALETTE_GBA_CHECK_PC && s_original_colors) {
        return 0xFF;
    }
    /* "Smoother movement": the player's routines, and the run flag each pass. */
    uint8_t value;
    if (bank == 6 && shantae_moves_imm(ctx, pc, orig, &value)) return value;
    /* The transformations' routines: the monkey and harpy, the tinkerbat. */
    if ((bank == 0x0D || bank == 0x1C) && shantae_forms_imm(ctx, bank, pc, orig, &value)) return value;
    /* "Dancing": the dance routine and its script's native calls. */
    if (bank == 0x0E && shantae_dance_imm(ctx, pc, orig, &value)) return value;
    if (bank == 0 && orig == 0x03 && (pc == MOVES_PROLOGUE_PC || pc == SCRIPTS_PROLOGUE_PC)) shantae_moves_tick(ctx);
    /* Match the original byte too: the generator also emits HALT-bug copies of
     * these instructions, which read the opcode as the operand. */
    if (bank == 0 && pc == SPRITE_DMA_PC && orig == 0x07) {
        sprite_lag_before_dma(ctx);
    } else if (bank == 0 && pc == SPRITE_HDMA_PC && orig == 0xCF && sprite_lag_after_dma(ctx)) {
        return 0x4F;   /* general DMA: the graphics are in VRAM before the picture */
    } else if (bank == 0 && pc == SCRIPTS_PROLOGUE_PC && orig == 0x03) {
        note_player_move(ctx);
    } else if (bank == 0 && pc == SCRIPTS_NEXT_SLOT_PC && orig == 0x7E) {
        player_script_done(ctx);
    } else if (bank == 3 && pc == SCROLL_BUILT_PC && orig == 0xF8) {
        note_scroll_built(ctx);
    }
    return shantae_slots_imm(ctx, bank, pc, orig);
}

const char *game_get_name(void) {
    return "Shantae";
}

int game_default_hardware_mode(void) {
    return shantae_gba_enhanced() ? GB_HARDWARE_MODE_GBA : GB_HARDWARE_MODE_CGB;
}

/* The runtime's preemptive frames (Esc menu, Video). One frame is the delay
 * every Game Boy game has -- a tick's result shows in the picture after the
 * next VBlank -- and what remains of a press with "Reduce input lag" on, so
 * one covers it without skipping the first frame of any move. */
int game_default_preemptive_frames(void) {
    return 1;
}

/* Rollback state for preemptive frames: this context's hook bookkeeping and
 * the expanded view's lists. */
static void save_game_state(const GBContext *ctx, void *out) {
    LagState *s = lag_state((GBContext *)ctx);
    uint8_t *p = out;
    memcpy(p, &s->sprite, sizeof(s->sprite));
    memcpy(p + sizeof(s->sprite), &s->move, sizeof(s->move));
    shantae_view_state_save(p + sizeof(s->sprite) + sizeof(s->move));
}

static void load_game_state(GBContext *ctx, const void *in) {
    LagState *s = lag_state(ctx);
    const uint8_t *p = in;
    memcpy(&s->sprite, p, sizeof(s->sprite));
    memcpy(&s->move, p + sizeof(s->sprite), sizeof(s->move));
    shantae_view_state_load(p + sizeof(s->sprite) + sizeof(s->move));
    shantae_slots_state_loaded(ctx);
}

/* Save state files are taken at a frame boundary, just after a VBlank, so the
 * camera they hold is that VBlank's: the previous camera the next copy needs.
 * Without it the first VBlank after a load would keep the original sprite
 * timing if the camera moved. The offset the committed scroll was built with
 * is not stored; the current one stands in until the next build. */
static void state_file_loaded(GBContext *ctx) {
    LagState *s = lag_state(ctx);
    memset(&s->move, 0, sizeof(s->move));
    memset(&s->sprite, 0, sizeof(s->sprite));
    s->sprite.camera_x = (uint16_t)ram16(ctx->hram + 0x61);
    s->sprite.camera_y = (uint16_t)ram16(ctx->hram + 0x63);
    s->sprite.frame = ctx->completed_frames;
    s->sprite.have_camera = 1;
    s->sprite.built_dx = (uint16_t)ram16(ctx->wram + 0x9D2);
    s->sprite.built_dy = (uint16_t)ram16(ctx->wram + 0x9D4);
    shantae_slots_state_loaded(ctx);
    shantae_view_state_file_loaded(ctx);
}

void game_on_init(struct GBContext *ctx) {
    (void)ctx;
    load_settings();
    gbrt_imm_override_hook = shantae_imm_override;
    gb_frame_hold_hook = shantae_frame_unfinished;
    /* The expanded view keeps many more objects alive: at 1920x1080 the water
     * tower's arena takes three to eight frames of CPU a tick. The frame hold
     * waits that long before it takes a frame for a screen load. */
    if (shantae_expanded_view()) gb_frame_hold_limit = 8u * 70224u;
    gb_step_hook = step_hook;
    gb_game_state.size = sizeof(SpriteLag) + sizeof(PlayerMove) + shantae_view_state_size();
    gb_game_state.save = save_game_state;
    gb_game_state.load = load_game_state;
    gb_game_state.file_loaded = state_file_loaded;
    shantae_view_init(ctx);
    shantae_slots_init(ctx, shantae_expanded_view());
}
