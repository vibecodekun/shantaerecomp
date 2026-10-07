/*
 * forms.c -- "Smoother movement" for the transformations: the monkey (bank
 * 0D), the harpy (bank 0D) and the tinkerbat (bank 1C). Shantae's own moves
 * are in moveset.c; the same settings apply ("Whip on the move", "Air speed"),
 * with "Transformations" to turn this part off.
 *
 * The monkey and the tinkerbat are built like Shantae: an idle, a walk and a
 * run that reads the run flag CB3C ("start moving" scripts 0D:47ED, 1C:4DC8),
 * a jump whose speed is set by slot+$64, and an attack (the claw 0D:4FC8,
 * script 0D:4F95; the sword 1C:57AC, script 1C:5770) that zeroes her speed on
 * every tick with three LD A,0 to +$41. Unlike the whip, the attack checks for
 * falling, and once its hit is out (its script sets slot+$18) a direction
 * hands on to "start moving". So:
 *
 * - The run flag follows B, as for Shantae (shantae_forms_tick), except on
 *   the tick of a new B. Their walk routines test the run flag before the new
 *   B (0D:4877 before 0D:4894, 1C:4E46 before 1C:4E63), so with the flag set
 *   at once a press while walking would always run and never attack; the
 *   original's flag waits 15 ticks of B, so it is never set on such a tick.
 * - Slide: the attack gives a speed when a direction is held, run speed with B
 *   held and walk speed without, and turns her. Cancel: a new B with a
 *   direction held is not an attack (the B tests at 0D:4578, 0D:4894, 1C:4C43,
 *   1C:4E63), and an attack in progress goes on to "start moving" when B and a
 *   direction are held.
 * - Air speed: the CP 0 after each read of +$64 (0D:4E48, 0D:4E74 in the
 *   monkey's jump; 1C:5518, 1C:5544 in the tinkerbat's fall and jump, 1C:5724,
 *   1C:5750 in her air sword) is "not zero" while B is held.
 *
 * The harpy has no run flag: her run (0D:6F38) adds speed on every tick up to
 * two pixels, and her talon (0D:751F) keeps it, but the talon ends in the idle
 * routine (0D:6D3A), whose three LD A,0 to +$41 (0D:6D86) drop it before the
 * run starts again from nothing. Here the idle keeps a speed that a held
 * direction goes on with.
 *
 * She flies by flapping: A in her flight routine (0D:72F4) starts the flap
 * script 0D:7210, whose native call 0D:7256 gives the lift (her vertical speed
 * at +$44 less $18, and at least $FFFD7F upward), sets the stance CB54 to the
 * air and plays sound $19. The talon routine (0D:751F) reads no buttons, so
 * every A during it was lost and she fell. Here a new A during the talon does
 * what the flap script does for it, at the script pass where the flap script
 * would, and the talon plays on.
 *
 * The tinkerbat squeezes. Her box (00:2975 loads it from each frame) is 10 by
 * 20 pixels, the monkey's 7 by 14 with the same feet, and the wall and floor
 * tests (00:29F6) take the whole height, so a passage the monkey walks or
 * climbs into (16 pixels tall, as in the ice tower's warp squid alcove) is a
 * wall to her. Before each tick's movement her box is the monkey's height,
 * feet kept, when that box is clear of solid tiles and either her own is not
 * or she is pressing into a wall with an opening the smaller box fits. The
 * tiles are read as 00:2CCF and 00:3297 do (02 and 62 are solid).
 *
 * Nothing here is kept between ticks: every decision is made from the joypad,
 * her object and the map.
 */
#include "forms.h"
#include "moveset.h"
#include "gbrt.h"
#include "object_slots.h"

/* extras.c */
int shantae_reduce_input_lag(void);
int shantae_player_move_early(GBContext *ctx);

#define PAD_A 0x01
#define PAD_B 0x02
#define PAD_RIGHT 0x10
#define PAD_LEFT 0x20

#define PLAYER_POINTER 0xCA13
#define RUN_FLAG 0xCB3C
#define STANCE 0xCB54     /* 1 in the air */
#define FORM 0xCB72
#define MAP_PAGE 0xC9F9   /* the map's block directory: its page, then its bank */
#define SOUNDS 0xC203     /* op BC (00:13D7): the newest entry's offset, then 16 entries of 8 bytes */

#define WALK_SPEED 0x100
#define RUN_SPEED 0x200
#define SQUEEZED_HEIGHT 14   /* the monkey's box */

#define HARPY_TALON_SCRIPT 0x74E5   /* to 0D:751E; the talon routine follows it */
#define HARPY_TALON 0x751F
#define FLAP_LIFT (-0x281)          /* $FFFD7F, 0D:7289 */
#define FLAP_PULL 0x18              /* 0D:725B */

enum { MONKEY = 1, HARPY = 3, TINKERBAT = 5 };

/* The two forms built like Shantae. */
typedef struct {
    uint8_t form, bank;
    uint16_t attack_speed;   /* the attack routine's first LD A,$00 to +$41 */
    uint16_t start_moving;   /* script: walk, or run with the run flag */
} Form;
static const Form s_monkey = {MONKEY, 0x0D, 0x4FCC, 0x47ED};
static const Form s_tinkerbat = {TINKERBAT, 0x1C, 0x57B0, 0x4DC8};

static uint8_t *global(GBContext *ctx, unsigned addr) { return ctx->wram + (addr - 0xC000); }

static int forms_on(GBContext *ctx) {
    return shantae_smooth_moves() && shantae_smooth_forms() && ctx->wram && ctx->rom;
}

static uint8_t *player_slot(GBContext *ctx) {
    const unsigned p = *global(ctx, PLAYER_POINTER) | *global(ctx, PLAYER_POINTER + 1) << 8;
    return p && shantae_slot_index(ctx, p) >= 0 ? shantae_slot_memory(ctx, p) : 0;
}

/* The player's slot when BC, the routine's object, is it. */
static uint8_t *player(GBContext *ctx) {
    const unsigned p = *global(ctx, PLAYER_POINTER) | *global(ctx, PLAYER_POINTER + 1) << 8;
    return ctx->bc == p ? player_slot(ctx) : 0;
}

static uint32_t speed_bytes(unsigned right_speed, int left) {
    return (left ? ~right_speed : right_speed) & 0xFFFFFF;
}

static void set_script(uint8_t *s, uint8_t bank, unsigned script) {
    s[0x16] = 0x80;
    s[0x17] = 0x00;
    s[0x02] = (uint8_t)script;
    s[0x03] = (uint8_t)(script >> 8);
    s[0x04] = bank;
    s[0x05] = 0xFF;
}

static uint8_t rom_byte(GBContext *ctx, unsigned bank, unsigned addr) {
    const size_t at = addr < 0x4000 ? addr : (size_t)bank * 0x4000 + (addr & 0x3FFF);
    return at < ctx->rom_size ? ctx->rom[at] : 0;
}

/* --- The monkey and the tinkerbat ---------------------------------------- */

/* "Reduce input lag" ran this routine early, after one that moved her. */
static int stepped_this_tick(GBContext *ctx, const uint8_t *s) {
    return shantae_player_move_early(ctx) && (s[0x41] | s[0x42] | s[0x43]) != 0;
}

/* The attack routine's speed for this tick, with the tick's changes. */
static uint32_t attack_speed(GBContext *ctx, uint8_t *s, const Form *f) {
    const int mode = shantae_whip_moving();
    const uint8_t held = ctx->hram[0x0B];
    const int left = !(held & PAD_RIGHT), fast = (held & PAD_B) != 0;
    if (mode == SHANTAE_WHIP_ORIGINAL || !(held & (PAD_LEFT | PAD_RIGHT))) return 0;
    if (mode == SHANTAE_WHIP_CANCEL) {
        if (fast) set_script(s, f->bank, f->start_moving);
        return 0;
    }
    const int stepped = stepped_this_tick(ctx, s);
    s[0x31] = (uint8_t)left;
    s[0x64] = fast ? 0x0A : 0x00;   /* a jump out of it keeps the speed, as out of a run */
    /* After the hit the routine hands on to "start moving", which "Reduce
     * input lag" runs in this tick: that step is hers. */
    if ((s[0x18] && shantae_reduce_input_lag()) || stepped) return 0;
    return speed_bytes(fast ? RUN_SPEED : WALK_SPEED, left);
}

/* One run of an attack routine: its speed is decided at the first of the
 * three operands and the others take it from here. The stack pointer tells a
 * run from the next; two contexts, as the differential run steps two. */
typedef struct {
    GBContext *ctx;
    uint16_t sp;
    uint32_t speed;
} AttackRun;
static AttackRun s_runs[2];

static AttackRun *attack_run(GBContext *ctx) {
    for (int i = 0; i < 2; i++)
        if (s_runs[i].ctx == ctx) return &s_runs[i];
    AttackRun *r = s_runs[0].ctx ? &s_runs[1] : &s_runs[0];
    r->ctx = ctx;
    r->sp = 0;
    return r;
}

/* --- The tinkerbat's squeeze ---------------------------------------------- */

/* The 8 by 8 tile at (x, y) is solid, found as 00:2CCF and 00:3297 find it:
 * a row of the table at 00:0300, the block from the directory at C9F9 (in
 * bank C9FA), its metatile (two bytes), and the quarter's byte at +8 to +B of
 * the metatile's record, in bank FFDF plus the metatile's low nibble. */
static int solid(GBContext *ctx, int x, int y) {
    const unsigned xl = x & 0xFF, xh = (x >> 8) & 0xFF, yl = y & 0xFF, yh = (y >> 8) & 0xFF;
    const unsigned row = (yl >> 3) & 0xFE;
    const unsigned e = (((xl >> 3) & 0xFE) + rom_byte(ctx, 0, 0x300 + row)) & 0xFF;
    const unsigned dir_bank = *global(ctx, MAP_PAGE + 1);
    const unsigned entry = (((yh >> 2) + *global(ctx, MAP_PAGE)) & 0xFF) << 8 | ((((yh & 3) << 6) + xh * 2) & 0xFF);
    const unsigned d = (rom_byte(ctx, 0, 0x301 + row) + rom_byte(ctx, dir_bank, entry)) & 0xFF;
    const unsigned bank = rom_byte(ctx, dir_bank, entry + 1), metatile = d << 8 | e;
    const unsigned b0 = rom_byte(ctx, bank, metatile), b1 = rom_byte(ctx, bank, metatile + 1);
    const unsigned quarter = ((xl & 0x0F) >> 3) + ((yl & 8) >> 2);
    const uint8_t v = rom_byte(ctx, (ctx->hram[0x5F] + (b0 & 0x0F)) & 0xFF, b1 << 8 | ((b0 & 0xF0) + 8 + quarter));
    return v == 0x02 || v == 0x62;
}

/* A solid tile in the box from (x0, y0) to (x1, y1), edges included. */
static int hits(GBContext *ctx, int x0, int y0, int x1, int y1) {
    for (int ty = y0 >> 3; ty <= y1 >> 3; ty++)
        for (int tx = x0 >> 3; tx <= x1 >> 3; tx++)
            if (solid(ctx, tx * 8, ty * 8)) return 1;
    return 0;
}

static int word_at(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static void squeeze(GBContext *ctx, uint8_t *s) {
    /* Her frame's own box, as 00:2975 loads it: the frame (+$2E bank, +$2F)
     * for her facing, then x, y, width and height from +$10. */
    if (s[0x2E] == 0xFF || s[0x31] > 1) return;
    const unsigned frame = (s[0x2F] | s[0x30] << 8) + s[0x31] * 2;
    const unsigned data = (rom_byte(ctx, s[0x2E], frame) | rom_byte(ctx, s[0x2E], frame + 1) << 8) + 0x10;
    uint8_t box[8];
    for (int i = 0; i < 8; i++) box[i] = rom_byte(ctx, s[0x2E], data + i);
    const int xo = word_at(box), yo = word_at(box + 2), w = word_at(box + 4), h = word_at(box + 6);
    if (h <= SQUEEZED_HEIGHT || h > 64 || w <= 0 || w > 64) return;
    /* Only her frame's box or the squeezed one: anything else was set on
     * purpose. */
    const int now_yo = word_at(s + 0x4B), now_h = word_at(s + 0x4D);
    if (!(now_yo == yo && now_h == h) && !(now_yo == yo + h - SQUEEZED_HEIGHT && now_h == SQUEEZED_HEIGHT)) return;
    const int x = s[0x34] | s[0x35] << 8, y = s[0x37] | s[0x38] << 8;
    const int left = x + word_at(s + 0x39) + xo, right = left + w;
    const int top = y + word_at(s + 0x3B) + yo, bottom = top + h, low = bottom - SQUEEZED_HEIGHT;
    const uint8_t held = ctx->hram[0x0B];
    int small = 0;
    if (!hits(ctx, left, low, right, bottom)) {
        if (hits(ctx, left, top, right, bottom))
            small = 1;   /* only the smaller box fits where she is */
        else if (held & PAD_LEFT)
            small = hits(ctx, left - 1, top, left - 1, bottom) && !hits(ctx, left - 1, low, left - 1, bottom);
        else if (held & PAD_RIGHT)
            small = hits(ctx, right + 1, top, right + 1, bottom) && !hits(ctx, right + 1, low, right + 1, bottom);
    }
    const int box_top = small ? yo + h - SQUEEZED_HEIGHT : yo, box_h = small ? SQUEEZED_HEIGHT : h;
    s[0x4B] = (uint8_t)box_top;
    s[0x4C] = (uint8_t)(box_top >> 8);
    s[0x4D] = (uint8_t)box_h;
    s[0x4E] = (uint8_t)(box_h >> 8);
}

void shantae_forms_tick(GBContext *ctx) {
    if (!forms_on(ctx)) return;
    const uint8_t form = *global(ctx, FORM);
    if (form != MONKEY && form != TINKERBAT) return;
    /* A new B is the attack, unless cancel turns it into the run (a direction
     * held). An early move sees no new presses (run_player_move). */
    const uint8_t held = ctx->hram[0x0B], pressed = ctx->hram[0x0C];
    const int attack = (pressed & PAD_B) &&
                       !(shantae_whip_moving() == SHANTAE_WHIP_CANCEL && (held & (PAD_LEFT | PAD_RIGHT)));
    *global(ctx, RUN_FLAG) = (held & PAD_B) && !attack;
    uint8_t *s;
    if (form == TINKERBAT && (s = player_slot(ctx)) && s[0] != 0xFF) squeeze(ctx, s);
}

void shantae_forms_script_pass(GBContext *ctx) {
    if (!forms_on(ctx) || *global(ctx, FORM) != HARPY || !(ctx->hram[0x0C] & PAD_A)) return;
    uint8_t *s = player_slot(ctx);
    if (!s || s[0] == 0xFF || s[0x19] != 0x0D || (s[0x1B] | s[0x1C] << 8) != HARPY_TALON) return;
    /* Still the talon's script: on a landing this tick 0D:694C has set the
     * landing's. */
    const unsigned script = s[0x02] | s[0x03] << 8;
    if (s[0x04] != 0x0D || script < HARPY_TALON_SCRIPT || script >= HARPY_TALON) return;
    /* 0D:7256: the speed less $18, and at least the flap's lift. */
    int32_t speed = (int32_t)((uint32_t)(s[0x44] | s[0x45] << 8 | s[0x46] << 16) << 8) >> 8;
    speed -= FLAP_PULL;
    if (speed > FLAP_LIFT) speed = FLAP_LIFT;
    s[0x44] = (uint8_t)speed;
    s[0x45] = (uint8_t)(speed >> 8);
    s[0x46] = (uint8_t)(speed >> 16);
    *global(ctx, STANCE) = 1;   /* op 94 54 CB 01: the talon ends as in the air */
    /* op BC 19 40 1E 00: the flap's sound. */
    uint8_t *newest = global(ctx, SOUNDS);
    *newest = (uint8_t)((*newest + 8) & 0x7F);
    uint8_t *e = global(ctx, SOUNDS + 1 + *newest);
    e[0] = 0x01;
    e[1] = 0x19;
    e[2] = 0x40;
    e[3] = 0x1E;
    e[4] = 0x00;
}

int shantae_forms_imm(GBContext *ctx, uint8_t bank, uint16_t pc, uint8_t orig, uint8_t *value) {
    uint8_t *s;
    if (!forms_on(ctx) || !(s = player(ctx))) return 0;
    const uint8_t form = *global(ctx, FORM), held = ctx->hram[0x0B];
    const Form *f = bank == 0x0D && form == MONKEY ? &s_monkey : bank == 0x1C && form == TINKERBAT ? &s_tinkerbat : 0;
    /* The original operand is matched too: the generator's HALT-bug copies of
     * these instructions read the opcode as the operand. */
    switch ((uint32_t)bank << 16 | pc) {
    case 0x0D4FCC: case 0x0D4FCF: case 0x0D4FD2:     /* LD A,0: the claw's speed */
    case 0x1C57B0: case 0x1C57B3: case 0x1C57B6: {   /* LD A,0: the sword's */
        if (!f || orig != 0x00) return 0;
        const unsigned byte = (unsigned)(pc - f->attack_speed) / 3;
        AttackRun *r = attack_run(ctx);
        if (byte == 0) {
            r->sp = ctx->sp;
            r->speed = attack_speed(ctx, s, f);
        } else if (r->sp != ctx->sp) {
            return 0;
        }
        *value = (uint8_t)(r->speed >> byte * 8);
        return 1;
    }
    case 0x0D4E48: case 0x0D4E74:                     /* CP 0 on +$64: Z is walk speed */
    case 0x1C5518: case 0x1C5544: case 0x1C5724: case 0x1C5750:
        if (!f || orig != 0x00 || !shantae_air_speed_b()) return 0;
        *value = held & PAD_B ? (uint8_t)~ctx->a : ctx->a;
        return 1;
    case 0x0D4578: case 0x0D4894: case 0x1C4C43: case 0x1C4E63:   /* AND 2 on FF8C: a new B attacks */
        if (!f || orig != 0x02 || shantae_whip_moving() != SHANTAE_WHIP_CANCEL || !(held & (PAD_LEFT | PAD_RIGHT)))
            return 0;
        *value = 0x00;
        return 1;
    case 0x0D6D86: case 0x0D6D89: case 0x0D6D8C: {   /* LD A,0: the harpy's idle stops her */
        if (form != HARPY || orig != 0x00) return 0;
        const int moving = s[0x41] | s[0x42] | s[0x43], left = (s[0x43] & 0x80) != 0;
        if (!moving || !(held & (left ? PAD_LEFT : PAD_RIGHT))) return 0;
        *value = s[0x41 + (pc - 0x6D86) / 3];   /* she goes on at the speed she has */
        return 1;
    }
    }
    return 0;
}
