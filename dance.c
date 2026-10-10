/*
 * dance.c -- "Dancing": the dance's steps entered at any speed, and
 * invincibility after a transformation.
 *
 * Select starts a dance (script 0E:4179): Shantae's movement routine becomes
 * 0E:4D40 and her script plays a pose of eight beats (four ticks each) at a
 * time. While a beat's window is open (slot+$18 = 0) the routine ORs the held
 * buttons into CB59; after each pose 0E:4D8F turns them into one step in CB5A
 * (Down 1, B 2, Right 4, Up 6, A 7, Left 9; anything else 0), 0E:42A3 records
 * it in a ring of 16 (CB5B, its end in CB6C; a Down marks a start, CB6B), and
 * 0E:42D2 compares the steps since the last Down with every dance learned (the
 * table at 0E:4342, eight bytes each: its steps, the bit in CB1B+n that says
 * it was learned, the script to run, the form). So one step is taken every 32
 * ticks, a beat with no press or two presses breaks the sequence, and a dance
 * matches only one pose after its last step, when another has been recorded.
 * A match sets slot+$18 to $FF, CB6D-CB6F to the script and CB81 to the form,
 * and the dance script goes on at 0E:41F9.
 *
 * With quick steps the window never opens (the CP 0 on slot+$18 at 0E:4D54),
 * so the original records nothing. Every new press is a step instead, taken in
 * the dance routine at 0E:4D48: a step that goes on to a learned dance is kept,
 * a Down starts over, and any other press is skipped. When the steps make a
 * whole dance it is matched as 0E:42D2 does, and her script goes on at 0E:41F9
 * in the same tick. The steps so far are kept in CB6D, which the original only
 * uses for a match: the dance (table index << 4) whose first steps they are,
 * and how many. The dance's start (the ring's fill at 0E:4260) clears it.
 *
 * Each step still shows its own pose. The dance script's loop calls a pose
 * from a table by CB5A (op 38 at 0E:41C5: 0 the plain dance, 1 Down, 2 B,
 * 4 Right, 6 Up, 7 A, 9 Left) and, the pose done, turns CB59 into the next
 * step. A press sets CB5A to its step and starts her script at 0E:41C5 with
 * nothing on its call stack, so the pose plays from its first beat in the tick
 * of the press, and a later press starts its own; with the window shut the
 * next step is 0, the plain dance. The press that completes a dance goes
 * straight to the match.
 *
 * Invincibility. CB56 counts the reasons she cannot be hurt: the damage
 * handler (04:4E5F) returns while it is set, 06:72CD adds one and 06:72D2 takes
 * one off. After a hit her form's script spawns a blinker (06:7265, script
 * 06:72DA) that flashes her sprite for 120 ticks and then takes its one off. A
 * transformation adds one at the match (0E:4207) and spawns an object (0E:4458,
 * script 0E:4E82) that sets the new form (0E:4EC8), shows her as a silhouette
 * (the background white, the sprites black), fades the colours back and, on the
 * tick they are back, takes the one off with the call to 06:72D2 at 0E:4F7D:
 * about 70 ticks after she appears and 50 after she can move, with nothing to
 * show it. The heal dance runs the same object without a form (CB81 $FF).
 *
 * Turning back into Shantae (Select in a form) runs the script 0E:4000: two
 * added at its start (06:72CD twice), one taken off by its effect, and the last
 * taken off by the call to 06:72D2 at 0E:407B, on the tick she can move again
 * (her idle script 06:49F2 follows), so nothing protects her from then on.
 *
 * With the option both calls go to 06:7265 instead: the blinker takes over the
 * one she still has, flashes her and takes it off 120 ticks later. So she
 * blinks from the end of a transformation's silhouette, and from the tick she
 * can move after turning back. Op 32 (00:1A46) reaches the routine with JP HL,
 * so the dispatcher offers 06:72D2 to shantae_dance_dispatch, which tells these
 * two calls by op 32's stack.
 */
#include "dance.h"
#include "gbrt.h"
#include "object_slots.h"

#define PAD_SELECT 0x04

#define PLAYER_POINTER 0xCA13
#define LEARNED 0xCB1B      /* + a dance's byte: its bit is set once it is learned */
#define INVINCIBLE 0xCB56   /* nonzero: no damage (04:4E5F) */
#define STEP 0xCB5A         /* the step whose pose the dance script plays next */
#define PROGRESS 0xCB6D     /* the steps so far; the match's script in the original */
#define FORM 0xCB72
#define NEXT_FORM 0xCB81

#define BLINKER 0x7265         /* 06: spawns the blinker */
#define TAKE_ONE_OFF 0x72D2    /* 06: CB56 less one, if set */
#define NATIVE_RETURN 0x1A5B   /* 00: after op 32's CALL 00:1A66 */
#define TURN_BACK_LAST 0x407F  /* 0E: after the turn back's call to 06:72D2 (op 32 at 0E:407B) */
#define SILHOUETTE_END 0x4F81  /* 0E: after the transformation's call to 06:72D2 (op 32 at 0E:4F7D) */

#define DANCE_BANK 0x0E
#define DANCES 0x4342       /* 0E: the table 0E:42D2 matches with */
#define SCRIPT_MATCHED 0x41F9
#define SCRIPT_POSE 0x41C5   /* op 38: the pose for CB5A, then the next step */

/* Buttons as the steps 0E:4D8F makes of them, Down first: it starts a dance. */
static const struct { uint8_t button, step; } s_steps[] = {
    {0x80, 1}, {0x02, 2}, {0x10, 4}, {0x40, 6}, {0x01, 7}, {0x20, 9},
};

static uint8_t *global(GBContext *ctx, unsigned addr) { return ctx->wram + (addr - 0xC000); }

static const uint8_t *rom(GBContext *ctx, unsigned addr) {
    return ctx->rom + DANCE_BANK * 0x4000 + (addr - 0x4000);
}

/* The table's dance at index i, or NULL past its end. */
static const uint8_t *dance(GBContext *ctx, int i) {
    const uint8_t *d = rom(ctx, DANCES + i * 8);
    return d[0] | d[1] ? d : 0;
}

static const uint8_t *dance_steps(GBContext *ctx, const uint8_t *d) { return rom(ctx, d[0] | d[1] << 8); }

static int learned(GBContext *ctx, const uint8_t *d) { return (*global(ctx, LEARNED + d[3]) & d[2]) != 0; }

/* A learned dance whose first steps are dance `e`'s first `n` and then `step`,
 * or -1. */
static int goes_on(GBContext *ctx, int e, int n, uint8_t step) {
    const uint8_t *so_far = n ? dance_steps(ctx, dance(ctx, e)) : 0, *d;
    for (int i = 0; (d = dance(ctx, i)); i++) {
        const uint8_t *s = dance_steps(ctx, d);
        int k = 0;
        while (k < n && s[k] == so_far[k]) k++;
        if (k == n && s[n] == step && learned(ctx, d)) return i;
    }
    return -1;
}

/* Her script as 0E:42D2's match leaves it: on at 0E:41F9 in this tick's
 * script pass, with nothing on its call stack (the pose's call and loop). */
static void match(GBContext *ctx, uint8_t *s, const uint8_t *d) {
    *global(ctx, PROGRESS) = d[4];
    *global(ctx, PROGRESS + 1) = d[5];
    *global(ctx, PROGRESS + 2) = d[6];
    *global(ctx, NEXT_FORM) = d[7];
    s[0x18] = 0xFF;
    s[0x16] = 0x80;
    s[0x17] = 0x00;
    s[0x02] = (uint8_t)SCRIPT_MATCHED;
    s[0x03] = (uint8_t)(SCRIPT_MATCHED >> 8);
    s[0x04] = DANCE_BANK;
    s[0x05] = 0xFF;
}

/* The steps so far, from CB6D: 0 when it names no dance's first steps (it
 * holds something else when the dance began with quick steps off). */
static int steps_so_far(GBContext *ctx, int *e) {
    const int n = *global(ctx, PROGRESS) & 0x0F;
    *e = *global(ctx, PROGRESS) >> 4;
    for (int i = 0; i <= *e; i++)
        if (!dance(ctx, i)) return 0;
    const uint8_t *s = dance_steps(ctx, dance(ctx, *e));
    for (int k = 0; k < n; k++)
        if (!s[k]) return 0;
    return n;
}

/* Her script as the loop at 0E:41C5 starts a pose: the step's, from its
 * first beat, in this tick's script pass. */
static void pose(uint8_t *s, GBContext *ctx, uint8_t step) {
    *global(ctx, STEP) = step;
    s[0x16] = 0x80;
    s[0x17] = 0x00;
    s[0x02] = (uint8_t)SCRIPT_POSE;
    s[0x03] = (uint8_t)(SCRIPT_POSE >> 8);
    s[0x04] = DANCE_BANK;
    s[0x05] = 0xFF;
}

/* This tick's new presses as steps. Down first; the others in any order that
 * goes on, so that two pressed together count whichever comes first. The pose
 * shown is the last step taken's, or the first press's if none was taken. */
static void take_steps(GBContext *ctx, uint8_t *s, uint8_t pressed) {
    int e, n = steps_so_far(ctx, &e), taken;
    uint8_t shown = 0;
    for (unsigned i = 0; i < sizeof(s_steps) / sizeof(s_steps[0]) && !shown; i++)
        if (pressed & s_steps[i].button) shown = s_steps[i].step;
    do {
        taken = 0;
        for (unsigned i = 0; i < sizeof(s_steps) / sizeof(s_steps[0]); i++) {
            if (!(pressed & s_steps[i].button)) continue;
            int next = goes_on(ctx, e, n, s_steps[i].step);
            if (next >= 0) {
                e = next;
                n++;
            } else if ((next = goes_on(ctx, 0, 0, s_steps[i].step)) >= 0) {
                e = next;   /* a dance's first step (Down) starts over */
                n = 1;
            } else {
                continue;   /* not a step of any dance from here: skipped */
            }
            pressed &= (uint8_t)~s_steps[i].button;
            taken = 1;
            shown = s_steps[i].step;
            if (!dance_steps(ctx, dance(ctx, e))[n]) {
                match(ctx, s, dance(ctx, e));
                return;
            }
        }
    } while (taken && pressed);
    *global(ctx, PROGRESS) = (uint8_t)(n ? e << 4 | n : 0);
    if (shown) pose(s, ctx, shown);
}

/* The player's slot when BC, the routine's object, is it. */
static uint8_t *player(GBContext *ctx) {
    const unsigned p = *global(ctx, PLAYER_POINTER) | *global(ctx, PLAYER_POINTER + 1) << 8;
    if (!p || ctx->bc != p || shantae_slot_index(ctx, p) < 0) return 0;
    return shantae_slot_memory(ctx, p);
}

static int quick_steps(GBContext *ctx) {
    return shantae_easy_dance() && shantae_quick_steps() && ctx->wram && ctx->rom &&
           ctx->rom_size >= (DANCE_BANK + 1) * 0x4000u;
}

int shantae_dance_dispatch(GBContext *ctx, uint16_t addr) {
    if (addr != TAKE_ONE_OFF || ctx->rom_bank != 0x06 || !ctx->wram || !shantae_easy_dance() ||
        !shantae_transform_invincible())
        return 0;
    /* Op 32's stack: its return, DE (the script after the call), BC (the
     * object) and AF with the script's bank in A (00:1A48). */
    const uint16_t sp = ctx->sp;
    const uint16_t script = gb_read16(ctx, (uint16_t)(sp + 2)), object = gb_read16(ctx, (uint16_t)(sp + 4));
    const unsigned p = *global(ctx, PLAYER_POINTER) | *global(ctx, PLAYER_POINTER + 1) << 8;
    if (gb_read16(ctx, sp) != NATIVE_RETURN || gb_read16(ctx, (uint16_t)(sp + 6)) >> 8 != DANCE_BANK) return 0;
    if (script == TURN_BACK_LAST) {
        if (object != p) return 0;
    } else if (script != SILHOUETTE_END || shantae_slot_index(ctx, object) < 0 || !*global(ctx, FORM)) {
        return 0;   /* not the transformation's object, or the heal dance's (no form) */
    }
    if (!*global(ctx, INVINCIBLE)) *global(ctx, INVINCIBLE) = 1;   /* one for the blinker to take off */
    ctx->pc = BLINKER;
    return 1;
}

int shantae_dance_imm(GBContext *ctx, uint16_t pc, uint8_t orig, uint8_t *value) {
    uint8_t *s;
    /* The original operand is matched too: the generator's HALT-bug copies of
     * these instructions read the opcode as the operand. */
    switch (pc) {
    case 0x4260:   /* LD A,$01: the ring's fill as a dance starts */
        if (orig == 0x01 && quick_steps(ctx)) *global(ctx, PROGRESS) = 0;
        return 0;
    case 0x4D48:   /* AND $04 on FF8C in the dance routine: Select ends it */
        if (orig == 0x04 && quick_steps(ctx) && (s = player(ctx)) && !(ctx->hram[0x0C] & PAD_SELECT))
            take_steps(ctx, s, ctx->hram[0x0C]);
        return 0;
    case 0x4D54:   /* CP $00 on slot+$18: the beat's window is open */
        if (orig != 0x00 || !quick_steps(ctx) || !player(ctx)) return 0;
        *value = (uint8_t)~ctx->a;
        return 1;
    }
    return 0;
}
