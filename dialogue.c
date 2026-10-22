/*
 * dialogue.c -- "Text speed": B in use prints dialogue faster, up to at once.
 *
 * Text is printed by a script subroutine that the speaker's own script calls
 * (op 34) after the setup at 01:55D1: 01:55F6, or the copy at 1A:765E that
 * shops call for their items (1A:75E0). Each pass of its loop waits a frame (op 1C at
 * 01:55F6), and four more unless B is held (op A6 on FF8B), then takes the
 * next character (op AC, 01:53F4, which breaks lines by itself) and either
 * draws it into the box's map at D800 (op AE, 01:5179) or acts on a code:
 *
 *   $0D  a new line; on the box's third line the box scrolls up twice
 *        (op B0), five frames apart (op 1C at 01:5757 and 01:575B)
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
 * character either way (its waits: 1A:765E, the scroll at 1A:76F1, 1A:76F5).
 *
 * A wait (op 1C, 00:1362) adds its frames to the slot's timer, and the
 * script runs on in the same pass while the timer is still under half a
 * frame (00:136E). With B in use over printing text, these waits in either
 * loop (only these: the arrow's blinking at the waits for a new B is left as
 * it is) are shortened before they add, in whole frames, so the loop goes on
 * at once: speed n gives the speaker n frames of waiting to pass over in
 * each script pass, n + 1 characters a frame, and what a longer wait (the
 * scroll's five frames) has left after that goes by n + 1 times as fast;
 * SHANTAE_TEXT_INSTANT passes over all of them, the text running to its end
 * (or the next wait for a new B) in the pass. B is in use while held and for B_LINGER frames after, so pressing it
 * again and again, as through a shop's boxes, goes as fast as holding it
 * (the four frames a character takes without B held, at 01:5602, are
 * shortened too); once it is left alone the text prints at the original
 * pace.
 *
 * A shop (its script 1A:6D65) also waits 15 frames after "Yes" (1A:6FD7),
 * shortened the same way, and after "Thanks!" until its callback (1A:70AC)
 * has taken the price from the gems (CA7D), 10 every eighth frame (C377 AND
 * 7 at 1A:70CB); with B in use it takes them every frame (extras.c). The
 * gems still go down 10 at a time, each with its sound, and the shop still
 * waits for the last. The
 * frame hold (extras.c) keeps the picture for up to TEXT_HOLD frames of CPU
 * while a pass runs a long text out, as for a busy frame with "Remove
 * slowdown", so the text is out by the next picture.
 *
 * Scenes go on around the text, and some count on its taking that long.
 * In Risky's hold the speaker's script (0C:5288) signals Risky after each of
 * its three texts, writing 1 to $18 of her slot (op 7C at 0C:52AE, 0C:52C3,
 * 0C:52DC, on the slot at C01B). Her script (0C:5134) plays an animation
 * until $18 is set, clears it (op 30 at 0C:5140, 0C:5168, 0C:51B8) and plays
 * the next; between the second and her laughing loop, which waits for the
 * third, it clears $18 once more after a one-shot animation (0C:518E). With
 * the texts quick, the third signal came while the second was still waiting,
 * or during the one-shot: written over, or cleared, it was lost, and she
 * laughed for good with Shantae frozen. So with Text speed on the signals
 * count: each op 7C there adds one, each clear takes one, and the clear at
 * 0C:518E leaves a signal that is already waiting. The original never has a
 * signal waiting at those points (it takes each text longer than her
 * animations), so there it plays as before. The Twinkle scene (11:56C8, her
 * script 11:5534) is built the same way, its second and third signals a text
 * apart while she looks for one every ten frames, and gets the same.
 *
 * Every text in the game followed by an op 7C on another slot's field is in
 * one of those or signals once and waits for the receiver's answer (CA9C-
 * CA9E), sends its signals on fixed waits after a single text (Mimic,
 * 1C:6F5E), or sets a flag that only plays an animation while it is on (the
 * opening, [C030]+$18).
 *
 * A freed genie signals through game RAM instead, with op 94 (a byte to an
 * address). The speaker (0C:71C0) prints "Thank you for freeing me!", sets
 * CBEB to 1 (0C:71E2) and waits while it is 1; the genie (0C:6D3A) plays
 * four frames until its op 48 passes, then clears CBEB (0C:6D56) and waits
 * while it is 0 (0C:6D72) before its dance, which sets CBEB to 2 (0C:6E16).
 * The speaker then waits while it is 2 and 3, the genie flashes the screen
 * white and opens the Dance Menu (01:61F1, 01:6098), and the menu's end sets
 * 4 (05:678F). With the text quick, the speaker's 1 came about three frames
 * before the genie's clear, which wiped it: the speaker went on to the Dance
 * Menu with no flash (the expanded view's surround showing the menu's tiles
 * as they loaded), and the genie danced afterwards on the menu's 4 and left
 * the screen white, waiting for a 3 that had come and gone. So with Text
 * speed on that clear leaves a 1 that is already waiting. The original never
 * has one waiting there (the text takes longer than the four frames). Of
 * the other bytes a script clears and then waits on, none is signalled
 * after a text but a screen shake's flag (C003, 15:705E), which only shakes
 * the screen while it is on, and CBEB's other waiter (1F:7124) is signalled
 * from the Dance Menu (24:5E3A).
 *
 * Ops dispatch at 00:1345 and, after a jump op, at 00:1A12; each change is
 * made at the slot's next dispatch, once the op has written, all within one
 * run of its script.
 */
#include "dialogue.h"
#include "gbrt.h"
#include "object_slots.h"

#define PAD_B 0x02
#define HELD 0x0B          /* FF8B, from the joypad routine 01:7C31 */
#define ROM_BANK 0x11      /* FF91: the bank the script runs in */
#define OP_WAIT 0x1C
#define OP_SIGNAL 0x7C     /* write a byte to a field of the slot a pointer holds */
#define OP_SET_18 0x30     /* write a byte to the slot's own $18 */
#define OP_WRITE 0x94      /* write a byte to an address */
#define TIMER_HIGH 0x17    /* frames of the slot's timer (+$16 is the fraction) */
#define SIGNAL_FIELD 0x18

/* Frames of waiting one pass may pass over at the instant speed (a remark of
 * many boxes, debug scene 3C, takes 284 frames with B held). */
#define INSTANT_FRAMES 4096
#define TEXT_HOLD (128u * 70224u)

/* B counts as in use this many frames after it was last held, so that
 * pressing it again and again goes as fast as holding it. */
#define B_LINGER 12

/* The waits B in use shortens, by the address of their op: in each text
 * loop a character's frame and the box's scroll, the four frames a character
 * takes in 01:55F6 without B held, and in a shop the pause after "Yes"
 * (1A:6FD7, before "Thanks!"). */
static const struct { uint8_t bank; uint16_t op; } text_waits[] = {
    {0x01, 0x55F6}, {0x01, 0x5602}, {0x01, 0x5757}, {0x01, 0x575B},
    {0x1A, 0x765E}, {0x1A, 0x76F1}, {0x1A, 0x76F5},
    {0x1A, 0x6FD7},
};

/* Scenes whose speaker signals a slot after each text, by the address of the
 * ops: the speaker's signals (op 7C), the receiver's clears that take one
 * (op 30 after each of its waits for a signal) and its clear that keeps. */
static const struct {
    uint8_t bank;
    uint16_t signals[3], takes[3], keep;
} scenes[] = {
    /* Risky's hold: the speaker 0C:5288, Risky 0C:5134. */
    {0x0C, {0x52AE, 0x52C3, 0x52DC}, {0x5140, 0x5168, 0x51B8}, 0x518E},
    /* The Twinkle: the speaker 11:56C8 ("Hi again! I found the Twinkle...",
     * "Huh?", Risky's "Hah! That oughtta shut you up!"), Risky 11:5534. */
    {0x11, {0x56F0, 0x5736, 0x5755}, {0x5599, 0x55B5, 0x55F6}, 0x559F},
};

/* Scenes whose speaker signals through a byte of game RAM that the receiver
 * clears as it begins to wait: the receiver's clear (op 94), the byte and the
 * speaker's signal, which the clear leaves when it is already there. */
static const struct {
    uint8_t bank;
    uint16_t clear, addr;
    uint8_t signal;
} kept_signals[] = {
    /* A freed genie: the speaker 0C:71C0 sets CBEB to 1 after its text
     * (0C:71E2), the genie clears it (0C:6D56) and waits for it. */
    {0x0C, 0x6D56, 0xCBEB, 1},
};

static struct {
    unsigned slot;          /* the speaker given frames this pass, 0 none */
    int spare;              /* frames of waiting it may still pass over */
    int fast;               /* a wait was shortened this pass */
    int holding;            /* gb_frame_hold_limit is raised; limit was its value */
    uint32_t limit;
    int b_age;              /* script passes since B was last held */
    unsigned after_slot;    /* a slot whose op wrote a field to set at its next dispatch */
    unsigned after_addr;    /* in the object table, or game RAM with after_global */
    int after_global;
    uint8_t after_value;
} s;
static unsigned long long s_waits, s_frames, s_signals;

/* A byte of the object table, NULL outside it. */
static uint8_t *slot_field(GBContext *ctx, unsigned addr) {
    return shantae_slot_memory(ctx, addr);
}

static int listed(const uint16_t *ops, size_t count, uint16_t op) {
    for (size_t i = 0; i < count; ++i)
        if (ops[i] == op) return 1;
    return 0;
}

void shantae_text_pass(GBContext *ctx) {
    if (ctx->hram[HELD] & PAD_B) s.b_age = 0;
    else if (s.b_age < B_LINGER + 1) s.b_age++;
    if (s.holding && !s.fast) {
        gb_frame_hold_limit = s.limit;
        s.holding = 0;
    }
    s.slot = 0;
    s.fast = 0;
    s.after_slot = 0;
}

int shantae_text_running(void) {
    return s.fast;
}

int shantae_text_hurry(GBContext *ctx) {
    return shantae_text_speed() > 0 && ((ctx->hram[HELD] & PAD_B) || s.b_age <= B_LINGER);
}

/* B in use over a text loop's wait: pass over what this pass has left, and
 * what is still to wait after that at speed + 1 frames a frame. */
static void shorten_wait(GBContext *ctx, unsigned slot, uint16_t operand) {
    const int speed = shantae_text_speed();
    if (!shantae_text_hurry(ctx)) return;
    if (s.slot != slot) {
        s.slot = slot;
        s.spare = speed >= SHANTAE_TEXT_INSTANT ? INSTANT_FRAMES : speed;
    }
    const int frames = gb_read8(ctx, (uint16_t)(operand + 1));
    const int left = frames - s.spare;
    const int pass = left <= 0 ? frames : frames - (left + speed) / (speed + 1);
    uint8_t *timer = slot_field(ctx, slot + TIMER_HIGH);
    if (pass <= 0 || !timer) return;
    *timer = (uint8_t)(*timer - pass);
    s.spare = left <= 0 ? s.spare - frames : 0;
    s_waits++;
    s_frames += (unsigned)pass;
    if (!s.fast && speed >= SHANTAE_TEXT_INSTANT && gb_frame_hold_limit < TEXT_HOLD) {
        if (!s.holding) s.limit = gb_frame_hold_limit;
        s.holding = 1;
        gb_frame_hold_limit = TEXT_HOLD;
    }
    s.fast = 1;
}

/* A signal scene's op: note the field and what it is to become after it. */
static void count_signal(GBContext *ctx, unsigned slot, uint8_t bank, uint8_t code, uint16_t operand) {
    const uint16_t op = (uint16_t)(operand - 1);
    for (size_t i = 0; i < sizeof(scenes) / sizeof(scenes[0]); ++i) {
        if (scenes[i].bank != bank) continue;
        unsigned addr = slot + SIGNAL_FIELD;
        int add;
        if (code == OP_SIGNAL && listed(scenes[i].signals, 3, op)) {
            /* As op 7C finds it (00:1780): the slot at the address, plus the field. */
            const uint16_t pointer = (uint16_t)(gb_read8(ctx, operand) | gb_read8(ctx, (uint16_t)(operand + 1)) << 8);
            addr = (unsigned)(gb_read8(ctx, pointer) | gb_read8(ctx, (uint16_t)(pointer + 1)) << 8);
            addr += gb_read8(ctx, (uint16_t)(operand + 2));
            add = 1;
        } else if (code == OP_SET_18 && listed(scenes[i].takes, 3, op)) {
            add = -1;
        } else if (code == OP_SET_18 && op == scenes[i].keep) {
            add = 0;
        } else {
            continue;
        }
        const uint8_t *field = slot_field(ctx, addr);
        if (!field) return;
        int value = *field + add;
        if (value < 0) value = 0;
        if (value > 0xFF) value = 0xFF;
        s.after_slot = slot;
        s.after_addr = addr;
        s.after_global = 0;
        s.after_value = (uint8_t)value;
        return;
    }
}

/* A receiver's clear (op 94: the address, then the byte): a signal already
 * there is set again after it. */
static void keep_signal(GBContext *ctx, unsigned slot, uint8_t bank, uint16_t operand) {
    const uint16_t op = (uint16_t)(operand - 1);
    for (size_t i = 0; i < sizeof(kept_signals) / sizeof(kept_signals[0]); ++i) {
        const unsigned addr = kept_signals[i].addr;
        if (kept_signals[i].bank != bank || kept_signals[i].clear != op || !ctx->wram) continue;
        if (ctx->wram[addr - 0xC000] != kept_signals[i].signal) return;
        s.after_slot = slot;
        s.after_addr = addr;
        s.after_global = 1;
        s.after_value = kept_signals[i].signal;
        return;
    }
}

void shantae_script_op(GBContext *ctx) {
    const unsigned slot = ctx->bc;
    if (s.after_slot) {
        if (s.after_slot == slot) {
            uint8_t *field = s.after_global ? ctx->wram + (s.after_addr - 0xC000) : slot_field(ctx, s.after_addr);
            if (field && *field != s.after_value) {
                *field = s.after_value;
                s_signals++;
            }
        }
        s.after_slot = 0;
    }
    if (shantae_text_speed() <= 0) return;
    const uint8_t bank = ctx->hram[ROM_BANK], code = ctx->a;
    const uint16_t operand = ctx->de, op = (uint16_t)(operand - 1);
    if (code == OP_WAIT) {
        for (size_t i = 0; i < sizeof(text_waits) / sizeof(text_waits[0]); ++i)
            if (text_waits[i].bank == bank && text_waits[i].op == op) shorten_wait(ctx, slot, operand);
    } else if (code == OP_SIGNAL || code == OP_SET_18) {
        count_signal(ctx, slot, bank, code, operand);
    } else if (code == OP_WRITE) {
        keep_signal(ctx, slot, bank, operand);
    }
}

void shantae_text_counts(unsigned long long *waits, unsigned long long *frames, unsigned long long *signals) {
    *waits = s_waits;
    *frames = s_frames;
    *signals = s_signals;
}
