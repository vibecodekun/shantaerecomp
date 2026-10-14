/*
 * dialogue.c -- "Instant text": A runs a dialogue's text out to its end.
 *
 * Text is printed by a script subroutine that the speaker's own script calls
 * (op 34) after the setup at 01:55D1: 01:55F6, or the copy at 1A:765E that
 * some scenes call. Each pass of its loop waits a frame (op 1C), and four more
 * unless B is held (op A6 on FF8B), then takes the next character (op AC,
 * 01:53F4, which breaks lines by itself) and either draws it into the box's
 * map at D800 (op AE, 01:5179) or acts on a code:
 *
 *   $0D  a new line; on the box's second line the box scrolls up twice
 *        (op B0), five frames apart
 *   $0E  wait for a new B (op A8 on FF8C), blinking the arrow, then a new line
 *   $0F  wait for a new B, then clear the box
 *   $FE  wait for a new B (the line break 01:53F4 makes in a full box)
 *   $FF  the end: C3AB = 1, then wait for a new B
 *   $00  return to the caller (01:5820)
 *   $5B  the next character picks a face (CB93)
 *
 * With B held the waits for $0E, $0F and $FE are passed over (op A6 again)
 * and a character takes one frame, so holding B runs the text on to $FF a
 * character a frame, where a new B carries on. The 1A copy takes a frame a
 * character either way.
 *
 * With the option a new A while a slot's script is in either loop runs it on
 * the way B held would, in the same frame: the slot's timer is set so its
 * script runs now (00:1323), B reads as held while it does, and the pass
 * steps on to the same slot again (00:1384) until the script waits at $FF, at
 * another wait for a new B (none is met with B held), or leaves the loop
 * (after $00 its caller goes on as it does in the original, up to its next
 * wait). So the box shows the text's last lines, the arrow blinks at its end,
 * and B carries on. Each run stops at the loop's next wait, so nothing past
 * the text runs sooner than it would. An A at one of the waits for a new B
 * carries on from it as that B would (FF8C reads B for that run alone), unless
 * it is the end ($FF has set C3AB). Nothing in the loop reads A, so A is
 * otherwise what it was.
 *
 * The runs cost the guest the time they take, about a frame of CPU for every
 * 40 characters (the remark of debug scene 3C, 284 frames with B held, took
 * some fifteen frames of pictures). While they go on, the frame hold
 * (extras.c) keeps the picture at its last line for up to TEXT_HOLD frames of
 * CPU instead of its own limit, as it does for a busy frame with "Remove
 * slowdown", so the text is out by the next picture.
 *
 * All of it happens within one script pass and starts again at the next
 * (00:130B), so it needs no state of its own between frames.
 */
#include "dialogue.h"
#include "gbrt.h"
#include "object_slots.h"

#define PAD_A 0x01
#define PAD_B 0x02
#define HELD 0x0B       /* FF8B, from the joypad routine 01:7C31 */
#define PRESSED 0x0C    /* FF8C: new this frame */
#define TEXT_END 0xC3AB /* set by $FF */

#define TIMER_CHECK_PC 0x1323   /* SUB $00 on the slot's timer, +$16 */
#define MAX_RUNS 4096
#define TEXT_HOLD (128u * 70224u)

/* The text loops, from their first op to their RET, and their waits for a new
 * B: from where a script waits a frame before one to the end of its blinking
 * (the op a B goes on at). */
typedef struct { uint16_t from, to; } Span;
static const struct {
    uint8_t bank;
    Span loop, waits[3];
} loops[] = {
    {0x01, {0x55F6, 0x5820}, {{0x570F, 0x5742}, {0x5775, 0x57A8}, {0x57DD, 0x5810}}},
    {0x1A, {0x765E, 0x77B3}, {{0x76A9, 0x76DC}, {0x770F, 0x7742}, {0x7777, 0x77AA}}},
};

static struct {
    unsigned slot;          /* the slot run out this pass, 0 none yet */
    int done;               /* it has stopped: nothing more this pass */
    int press_b;            /* its first run starts at a wait for a new B */
    int restore;            /* FF8B and FF8C are to be put back after its run */
    uint8_t held, pressed;
    int runs;
    int holding;            /* gb_frame_hold_limit is raised; limit was its value */
    uint32_t limit;
} s;
static unsigned long long s_texts, s_runs;

static unsigned slot_byte(GBContext *ctx, unsigned addr) {
    const uint8_t *p = shantae_slot_memory(ctx, addr);
    return p ? *p : 0xFF;
}

/* Whether the slot's script is in a text loop short of its end; *waiting:
 * at a wait for a new B. */
static int in_text(GBContext *ctx, unsigned slot, int *waiting) {
    if (shantae_slot_index(ctx, slot) < 0 || slot_byte(ctx, slot) == 0xFF) return 0;
    const unsigned bank = slot_byte(ctx, slot + 4), pc = slot_byte(ctx, slot + 2) | slot_byte(ctx, slot + 3) << 8;
    for (size_t i = 0; i < sizeof(loops) / sizeof(loops[0]); ++i) {
        if (bank != loops[i].bank || pc < loops[i].loop.from || pc >= loops[i].loop.to) continue;
        *waiting = 0;
        for (int w = 0; w < 3; ++w) *waiting |= pc >= loops[i].waits[w].from && pc < loops[i].waits[w].to;
        return !*waiting || ctx->wram[TEXT_END - 0xC000] != 1;
    }
    return 0;
}

static void stop(void) {
    s.done = 1;
    if (s.holding) gb_frame_hold_limit = s.limit;
    s.holding = 0;
}

void shantae_text_pass(GBContext *ctx) {
    (void)ctx;
    if (s.slot) stop();
    s.slot = 0;
    s.done = s.press_b = s.restore = s.runs = 0;
}

int shantae_text_running(void) {
    return s.slot && !s.done;
}

int shantae_text_imm(GBContext *ctx, uint16_t pc, uint8_t orig, uint8_t *value) {
    if (pc != TIMER_CHECK_PC || orig != 0x00 || s.done) return 0;
    if (!s.slot) {
        int waiting;
        if (!(ctx->hram[PRESSED] & PAD_A) || !shantae_instant_text() || !in_text(ctx, ctx->bc, &waiting)) return 0;
        s.slot = ctx->bc;
        s.press_b = waiting;
        s.limit = gb_frame_hold_limit;
        if (gb_frame_hold_limit < TEXT_HOLD) gb_frame_hold_limit = TEXT_HOLD;
        s.holding = 1;
        s_texts++;
    } else if (ctx->bc != s.slot) {
        return 0;
    }
    /* A minus A leaves the low byte 0 with no borrow, and the high byte goes
     * from 1 to 0: the timer counts down to 0, so the script runs now. */
    uint8_t *high = shantae_slot_memory(ctx, s.slot + 0x17);
    if (!high) return 0;
    *high = 0x01;
    s.held = ctx->hram[HELD];
    s.pressed = ctx->hram[PRESSED];
    s.restore = 1;
    ctx->hram[HELD] |= PAD_B;
    if (s.press_b) ctx->hram[PRESSED] |= PAD_B;
    s.press_b = 0;
    s.runs++;
    s_runs++;
    *value = ctx->a;
    return 1;
}

int shantae_text_again(GBContext *ctx) {
    if (!s.slot || s.done || ctx->bc != s.slot) return 0;
    if (s.restore) {
        ctx->hram[HELD] = s.held;
        ctx->hram[PRESSED] = s.pressed;
        s.restore = 0;
    }
    int waiting;
    if (s.runs >= MAX_RUNS || !in_text(ctx, s.slot, &waiting) || waiting) {
        stop();
        return 0;
    }
    /* The pass counts the slots left in the low byte of the DE it pushed at
     * 00:1314 and takes one off for this one: one more, and with ADD A,0 the
     * next slot is this one. */
    gb_write8(ctx, ctx->sp, (uint8_t)(gb_read8(ctx, ctx->sp) + 1));
    return 1;
}

void shantae_text_counts(unsigned long long *texts, unsigned long long *runs) {
    *texts = s_texts;
    *runs = s_runs;
}
