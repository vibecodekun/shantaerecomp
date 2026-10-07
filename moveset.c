/*
 * moveset.c -- "Smoother movement" for Shantae's base form (bank 6).
 *
 * The player's object runs a movement routine (slot+$19 bank, +$1A JP) and a
 * script (+2-+4, timer +$16) each tick. The routines read the joypad (FF8B
 * held, FF8C new) and switch scripts; a script installs the next routine.
 *
 *   idle 06:4AA7   walk 06:502A   run 06:6A8C   jump and fall 06:5520
 *   whip 06:5E52 (script 06:5D61, resumed by its frame +$6E at 06:5DCB)
 *   air whip 06:6CCD   crouch 06:6094   crawl 06:648B   crouch whip 06:670D
 *
 * What the original does, and what this changes when the feature is on:
 *
 * - Running. A separate script (06:70BE) sets the run flag CB3C once B has
 *   been held for 15 frames; "start moving" (script 06:4FCE) and the walk
 *   routine run when it is set. A new B press on the ground always whips
 *   first, and the whip routine zeroes her speed (06:5E56) for its 24 frames,
 *   so B + direction is a standing whip and then a run. Here CB3C follows B
 *   itself (shantae_moves_tick), and the whip is one of:
 *     slide   the whip plays out while she moves: run speed with B held, walk
 *             speed without, turning with the D-pad as the air whip does. Its
 *             last tick hands on to the run or walk script directly, without
 *             the idle frame in between.
 *     cancel  a B press with a direction held is not a whip (the B tests at
 *             06:4AD6, 06:509C, 06:60EF and 06:64D3 see no press), so she runs
 *             at once; a whip in progress ends when B and a direction are held.
 *   An air whip that lands resumes on the ground (06:6D96 -> 06:5DCB), which
 *   stopped her; slide keeps her moving through it, cancel goes on running.
 *
 * - Air speed. The jump and air whip routines move at run speed when slot+$64
 *   is set, which only the run script does: a standing jump is at walk speed
 *   whatever is held. Here B held is run speed and B released walk speed,
 *   every tick (the CP 0 after reading +$64 at 06:5637, 5663, 6D37, 6D63).
 *
 * - Crawling. The crawl moves half a pixel a tick (06:6575, 06:6595), and
 *   with the run flag set it stands up into a run (06:65B1, script 06:6259).
 *   Here B held crawls at walk speed, with the crawl's animation at twice the
 *   rate, and the run flag stays clear while Down is held. The crouch whip
 *   (06:670D) slides or cancels like the standing one.
 *
 * During a slide she can leave the ground, which neither whip routine checks
 * for (they call 06:475C but not 06:47B9). On the next tick the standing whip
 * becomes the air whip at the same frame, and the crouch whip the fall.
 *
 * Nothing here is kept between ticks: every decision is made from the joypad
 * and the object, so save states and rollback need nothing from this file.
 */
#include "moveset.h"
#include "forms.h"
#include "gbrt.h"
#include "object_slots.h"

/* extras.c */
int shantae_reduce_input_lag(void);
int shantae_player_move_early(GBContext *ctx);

#define PAD_B 0x02
#define PAD_RIGHT 0x10
#define PAD_LEFT 0x20
#define PAD_DOWN 0x80

#define PLAYER_POINTER 0xCA13
#define RUN_FLAG 0xCB3C        /* 1: "start moving" runs */
#define ON_GROUND 0xCB3F       /* set by the mover (06:40CB) */
#define LOW_CEILING 0xCB41     /* set by the crouch routines (06:4263) */
#define AIRBORNE 0xCB42        /* with ON_GROUND clear: 06:47B9 starts the fall */
#define JUMP_HOLD 0xCB55       /* frames a held A still lifts her */
#define STANCE 0xCB54          /* 0 standing, 1 in the air, 2 crouched */
#define FORM 0xCB72            /* 0 herself, 1-5 a transformation */

#define SCRIPT_MOVE 0x4FCE         /* walk, or run with the run flag */
#define SCRIPT_FALL 0x5457
#define SCRIPT_WHIP_END_A 0x5DC7   /* the goto 06:49F2 after the whip's last frame */
#define SCRIPT_WHIP_END_B 0x5E4E   /* the same in the resumed whip */
#define SCRIPT_CROUCH_WHIP_END_A 0x6678   /* the test of +$65 after the crouch whip's last frame */
#define SCRIPT_CROUCH_WHIP_END_B 0x6704   /* the same in the resumed crouch whip */
#define SCRIPT_CRAWL 0x6259        /* crawl, or run with the run flag */
#define SCRIPT_CRAWL_LOW 0x6260    /* crawl under a low ceiling */
#define SCRIPT_RUN 0x6A37
#define AIR_WHIP_ROUTINE 0x6CCD

#define WALK_SPEED 0x100
#define RUN_SPEED 0x200
#define CRAWL_SPEED 0x80

/* A speed as the three bytes the ROM keeps at +$41 (256ths of a pixel a tick,
 * low byte first). Its left speeds are the complement: $000100 right,
 * $FFFEFF left. */
static uint32_t speed_bytes(unsigned right_speed, int left) {
    return (left ? ~right_speed : right_speed) & 0xFFFFFF;
}

static uint8_t *global(GBContext *ctx, unsigned addr) { return ctx->wram + (addr - 0xC000); }

/* Her base form, with the feature on. The other forms have their own routines
 * (banks 0D, 13, 1C), two of which read the run flag too (0D:47ED, 1C:4DC8). */
static int base_form(GBContext *ctx) {
    return shantae_smooth_moves() && ctx->wram && *global(ctx, FORM) == 0;
}

/* The run flag follows B. Not crouched under a low ceiling, where she cannot
 * stand, and not with Down held when B is the faster crawl. */
void shantae_moves_tick(GBContext *ctx) {
    shantae_forms_tick(ctx);   /* in a transformation */
    if (!base_form(ctx)) return;
    const uint8_t held = ctx->hram[0x0B];
    const int under = *global(ctx, STANCE) == 2 && *global(ctx, LOW_CEILING) != 0;
    *global(ctx, RUN_FLAG) = (held & PAD_B) && !under && !(shantae_fast_crawl() && (held & PAD_DOWN));
}

/* The player's slot when BC, the routines' object, is it. */
static uint8_t *player(GBContext *ctx) {
    const unsigned p = *global(ctx, PLAYER_POINTER) | *global(ctx, PLAYER_POINTER + 1) << 8;
    if (!p || ctx->bc != p || shantae_slot_index(ctx, p) < 0) return 0;
    return shantae_slot_memory(ctx, p);
}

/* As the routines switch scripts: it runs in this tick's script pass. */
static void set_script(uint8_t *s, unsigned script) {
    s[0x16] = 0x80;
    s[0x17] = 0x00;
    s[0x02] = (uint8_t)script;
    s[0x03] = (uint8_t)(script >> 8);
    s[0x04] = 0x06;
    s[0x05] = 0xFF;
}

static int script_is(const uint8_t *s, unsigned script) {
    return s[0x04] == 0x06 && (s[0x02] | s[0x03] << 8) == script;
}

/* The script's wait ends in this tick's script pass (00:131E takes $100 off
 * the timer and runs the script below $80). */
static int script_due(const uint8_t *s) {
    return (int16_t)(s[0x16] | s[0x17] << 8) < 0x180;
}

/* She slid off the ground on the tick before, by the test of 06:47B9. The
 * crouch whip falls (06:47E4). The standing whip carries on as the air whip at
 * its frame (+$6E), the way an air whip that lands carries on at 06:5DCB: the
 * air whip's routine and stance as its script 06:6C2C sets them, and the
 * script from that frame. */
static int left_ground(GBContext *ctx, uint8_t *s, int crouched) {
    static const uint16_t air_whip_frame[9] = {0x6C53, 0x6C53, 0x6C61, 0x6C6F, 0x6C7D,
                                               0x6C8B, 0x6CA4, 0x6CB4, 0x6CBD};
    if (!*global(ctx, AIRBORNE) || *global(ctx, ON_GROUND)) return 0;
    *global(ctx, JUMP_HOLD) = 0;
    if (crouched) {
        set_script(s, SCRIPT_FALL);
        return 1;
    }
    set_script(s, air_whip_frame[s[0x6E] <= 8 ? s[0x6E] : 8]);
    s[0x19] = 0x06;
    s[0x1B] = (uint8_t)AIR_WHIP_ROUTINE;
    s[0x1C] = (uint8_t)(AIR_WHIP_ROUTINE >> 8);
    s[0x6D] = 0x00;
    *global(ctx, STANCE) = 1;
    return 1;
}

/* "Reduce input lag" runs a routine her script has just picked in the same
 * tick (extras.c). When the routine before it has already moved her, a whip
 * begun that way waits for the next tick to slide: a whip that lands, or one
 * begun from a walk. +$41 still holds the speed that routine moved her at. */
static int stepped_this_tick(GBContext *ctx, const uint8_t *s) {
    return shantae_player_move_early(ctx) && (s[0x41] | s[0x42] | s[0x43]) != 0;
}

/* The whip routine's speed for this tick (06:5E56-5E5C write it to +$41),
 * with the tick's changes to her object. */
static uint32_t whip_speed(GBContext *ctx, uint8_t *s) {
    const int mode = shantae_whip_moving();
    const uint8_t held = ctx->hram[0x0B];
    const int left = !(held & PAD_RIGHT), fast = (held & PAD_B) != 0;
    const int stepped = stepped_this_tick(ctx, s);
    if (mode == SHANTAE_WHIP_ORIGINAL) return 0;
    if (mode == SHANTAE_WHIP_SLIDE) left_ground(ctx, s, 0);   /* and this tick's step is still the whip's */
    if (!(held & (PAD_LEFT | PAD_RIGHT))) return 0;
    if (mode == SHANTAE_WHIP_CANCEL) {
        if (fast) set_script(s, SCRIPT_RUN);
        return 0;
    }
    s[0x31] = (uint8_t)left;
    s[0x64] = fast ? 0x0A : 0x00;   /* a jump out of it keeps the speed, as out of a run */
    if ((script_is(s, SCRIPT_WHIP_END_A) || script_is(s, SCRIPT_WHIP_END_B)) && script_due(s)) {
        /* Its last tick: on to the run or the walk, not through the idle
         * script. With "Reduce input lag" that routine moves her this tick. */
        set_script(s, fast ? SCRIPT_RUN : SCRIPT_MOVE);
        if (shantae_reduce_input_lag()) return 0;
    }
    return stepped ? 0 : speed_bytes(fast ? RUN_SPEED : WALK_SPEED, left);
}

/* The crouch whip routine's (06:6711-6717). It turns her itself. */
static uint32_t crouch_whip_speed(GBContext *ctx, uint8_t *s) {
    const int mode = shantae_whip_moving();
    const uint8_t held = ctx->hram[0x0B];
    const int moving = (held & (PAD_LEFT | PAD_RIGHT)) != 0, fast = (held & PAD_B) != 0;
    const int stepped = stepped_this_tick(ctx, s);
    if (mode == SHANTAE_WHIP_ORIGINAL) return 0;
    if (mode == SHANTAE_WHIP_CANCEL) {
        if (moving && fast) set_script(s, *global(ctx, LOW_CEILING) ? SCRIPT_CRAWL_LOW : SCRIPT_CRAWL);
        return 0;
    }
    const int falls = left_ground(ctx, s, 1);
    /* The whip's script ends in the crawl when +$65 is set, else in the
     * crouch, which would take a tick to start the crawl again. */
    s[0x65] = (uint8_t)moving;
    if (!moving) return 0;
    /* Its last tick: with "Reduce input lag" the fall or the crawl it ends in
     * moves her. */
    const int ends = (script_is(s, SCRIPT_CROUCH_WHIP_END_A) || script_is(s, SCRIPT_CROUCH_WHIP_END_B)) &&
                     script_due(s);
    if (((falls || ends) && shantae_reduce_input_lag()) || stepped) return 0;
    return speed_bytes(fast && shantae_fast_crawl() ? WALK_SPEED : CRAWL_SPEED, !(held & PAD_RIGHT));
}

/* One run of a whip routine: its speed is decided at the first of the three
 * operands, and the other two take it from here so that they agree. The stack
 * pointer tells a run from the next. Two contexts: the differential run steps
 * two. */
typedef struct {
    GBContext *ctx;
    uint16_t sp;
    uint32_t speed;
} WhipRun;
static WhipRun s_runs[2];

static WhipRun *whip_run(GBContext *ctx) {
    for (int i = 0; i < 2; i++)
        if (s_runs[i].ctx == ctx) return &s_runs[i];
    WhipRun *r = s_runs[0].ctx ? &s_runs[1] : &s_runs[0];
    r->ctx = ctx;
    r->sp = 0;
    return r;
}

/* The crawl's speed with B held. */
static int crawl_speed(GBContext *ctx, uint8_t *s, int left, int first, uint32_t *speed) {
    if (!shantae_fast_crawl() || !(ctx->hram[0x0B] & PAD_B)) return 0;
    if (first) {
        /* The animation at twice the rate: another $100 off the script's wait. */
        const uint16_t timer = (uint16_t)((s[0x16] | s[0x17] << 8) - 0x100);
        s[0x16] = (uint8_t)timer;
        s[0x17] = (uint8_t)(timer >> 8);
    }
    *speed = speed_bytes(WALK_SPEED, left);
    return 1;
}

int shantae_moves_imm(GBContext *ctx, uint16_t pc, uint8_t orig, uint8_t *value) {
    uint8_t *s;
    uint32_t speed;
    if (!base_form(ctx) || !(s = player(ctx))) return 0;
    const uint8_t held = ctx->hram[0x0B];
    const int moving = (held & (PAD_LEFT | PAD_RIGHT)) != 0;
    /* The original operand is matched too: the generator's HALT-bug copies of
     * these instructions read the opcode as the operand. */
    switch (pc) {
    case 0x5637: case 0x5663: case 0x6D37: case 0x6D63:   /* CP 0 on +$64: Z is walk speed */
        if (orig != 0x00 || !shantae_air_speed_b()) return 0;
        *value = held & PAD_B ? (uint8_t)~ctx->a : ctx->a;
        return 1;
    case 0x5E56: case 0x5E59: case 0x5E5C:                /* LD A,0: the whip's speed */
    case 0x6711: case 0x6714: case 0x6717: {              /* LD A,0: the crouch whip's */
        if (orig != 0x00) return 0;
        const int crouched = pc >= 0x6711;
        const unsigned byte = (pc - (crouched ? 0x6711 : 0x5E56)) / 3;
        WhipRun *r = whip_run(ctx);
        if (byte == 0) {
            r->sp = ctx->sp;
            r->speed = crouched ? crouch_whip_speed(ctx, s) : whip_speed(ctx, s);
        } else if (r->sp != ctx->sp) {
            return 0;
        }
        *value = (uint8_t)(r->speed >> byte * 8);
        return 1;
    }
    case 0x6575: case 0x6578: case 0x657B:                /* LD A,$7F/$FF/$FF: crawl left */
        if (orig != (pc == 0x6575 ? 0x7F : 0xFF) || !crawl_speed(ctx, s, 1, pc == 0x6575, &speed)) return 0;
        *value = (uint8_t)(speed >> (pc - 0x6575) / 3 * 8);
        return 1;
    case 0x6595: case 0x6598: case 0x659B:                /* LD A,$80/0/0: crawl right */
        if (orig != (pc == 0x6595 ? 0x80 : 0x00) || !crawl_speed(ctx, s, 0, pc == 0x6595, &speed)) return 0;
        *value = (uint8_t)(speed >> (pc - 0x6595) / 3 * 8);
        return 1;
    case 0x4AD6: case 0x509C: case 0x60EF: case 0x64D3:   /* AND 2 on FF8C: a new B press whips */
        if (orig != 0x02 || shantae_whip_moving() != SHANTAE_WHIP_CANCEL || !moving) return 0;
        *value = 0x00;
        return 1;
    case 0x6DA4: case 0x6DA7: {                           /* LD A,$CB/$5D: an air whip lands in 06:5DCB */
        if (orig != (pc == 0x6DA4 ? 0xCB : 0x5D) || shantae_whip_moving() != SHANTAE_WHIP_CANCEL || !moving)
            return 0;
        const unsigned script = held & PAD_B ? SCRIPT_RUN : SCRIPT_MOVE;
        *value = (uint8_t)(pc == 0x6DA4 ? script : script >> 8);
        return 1;
    }
    case 0x6DC3: case 0x6DC6:                             /* LD A,$81/$66: or crouched, in 06:6681 */
        if (orig != (pc == 0x6DC3 ? 0x81 : 0x66) || shantae_whip_moving() != SHANTAE_WHIP_CANCEL || !moving)
            return 0;
        *value = (uint8_t)(pc == 0x6DC3 ? SCRIPT_CRAWL : SCRIPT_CRAWL >> 8);
        return 1;
    }
    return 0;
}
