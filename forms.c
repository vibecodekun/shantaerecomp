/*
 * forms.c -- "Smoother movement" for the transformations: the monkey, the
 * elephant and the harpy (bank 0D), the spider (bank 13) and the tinkerbat
 * (bank 1C). Shantae's own moves are in moveset.c; the same settings apply
 * ("Whip on the move", "Air speed"), with "Transformations" to turn this part
 * off.
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
 *   1C:5750 in her air sword) is "not zero" while B is held. The monkey's air
 *   claw (0D:4ED7) only turns her (0D:4F85, 0D:4F8D) and keeps the speed of
 *   the jump's last tick, which is walk speed after B is let go to press it;
 *   here a direction gives the jump's speed, as the tinkerbat's air sword does.
 * - Climbing (0D:4ABE, 1C:5080) is a pixel a tick (0D:4B6F, 0D:4B8B; 1C:5131,
 *   1C:514D); with B held it is two, and the climb's animation counter (+$66)
 *   moves twice as far.
 *
 * The harpy has no run flag: her run (0D:6F38) adds speed on every tick up to
 * two pixels, and her talon (0D:751F) keeps it, but the talon ends in the idle
 * routine (0D:6D3A), whose three LD A,0 to +$41 (0D:6D86) drop it before the
 * run starts again from nothing. Here the idle keeps a speed that a held
 * direction goes on with. The talon is the mover alone, so her speed stood
 * still through it: a talon early in the run, at a fraction of a pixel, held
 * her there for 20 ticks. Here a held direction changes it as the run does on
 * the ground and the flight in the air.
 *
 * She flies by flapping: A in her flight routine (0D:72F4) starts the flap
 * script 0D:7210, whose native call 0D:7256 gives the lift (her vertical speed
 * at +$44 less $18, and at least $FFFD7F upward), sets the stance CB54 to the
 * air and plays sound $19. The talon routine (0D:751F) reads no buttons, so
 * every A during it was lost and she fell. Here a new A during the talon does
 * what the flap script does for it, at the script pass where the flap script
 * would, and the talon plays on. On the tick she reaches the ground (or the
 * ground drops away under her run) the routine that took the A sets the
 * landing's or the glide's script over the flap's; here the flap's is kept.
 *
 * The elephant charges with B (script 0D:64A5, routine 0D:650E) and the
 * tinkerbat with Down and B (1C:646D, 1C:64CA). The charge routines read no
 * buttons, so a charge ran until its speed was gone. Here a new B (Down and B
 * for the tinkerbat) during it charges again and a new A jumps (0D:60B6,
 * 1C:51F9), as from standing. The tinkerbat's charge started only from
 * standing (1C:4C82): her walk, run and landing take B as the sword. Here
 * Down and B charge from them too, so she can charge while pressing into what
 * she breaks. A charge hits what it touches when it starts (00:39A3 needs the
 * object's +$1F, set when she pushes into it), not what it runs into.
 *
 * A charge goes the way she faces (its script's native call, 0D:64E9 and
 * 1C:64A5, reads +$31), and the idle and walk routines take B after a
 * direction, so B with the other way held charged the way she was facing.
 * Here a charge starting on a tick, from a charge too, faces the way held.
 * The elephant's turn (script 0D:5EF7, routine 0D:5F1E: ten ticks, then
 * 0D:5F15 faces her about), her landing (0D:61D8) and her stomp (0D:62A6, a
 * landing with Down held: the hit, then four frames of ten ticks in 0D:62D8)
 * take no B, and the turn and the stomp no buttons at all, so a B during the
 * turn was lost and the stomp held her for 40 ticks. Here B charges and A
 * jumps from the turn, facing the way it turns her; B charges from the
 * landing; and from the tick after the stomp's hit B charges, A jumps and a
 * direction hands on to the idle script, whose routine walks or turns her.
 * The turn also stood her for its ten ticks, and a tick in the idle routine
 * after it; from a walk, the walk routine set the turn's script and then its
 * idle script over it for standing still (0D:5E35), a tick more. Here she
 * walks the way held from the tick she turns, through the turn into the walk.
 *
 * The spider walks a pixel a tick (13:4C2E, 13:4C43) and has no run. Here B
 * held is two, with the walk's animation at twice the rate, and with "Air
 * speed" the same in her jump (13:5280, 13:5295). A new B starts her shot
 * (script 13:4C77), whose routine 13:4CA0 is a RET: she stands for its six
 * ticks. With "Whip on the move" not "Original", a direction held during it
 * moves her with the walk's mover (13:4BE5), at run speed with B held, so B
 * runs and shoots at once. On a web (tiles $64, routine 13:6068) she moves a
 * pixel a tick each way, undone when the tile there is solid (13:476B); with
 * B held each step and its undo are two, and her animation (+$75) twice. The
 * tinkerbat climbs webs with a copy of it (1C:6113, with no shot), the same
 * with B held.
 *
 * Webs. The spider's jump and fall (13:5105) and the tinkerbat's (1C:5375)
 * take hold of a web whenever the tile by her feet is one (13:476B, 1C:5D41),
 * and the tinkerbat's jump script (1C:5282, through 1C:529B) before she has
 * left the ground, so a jump in front of a web stuck to it; on a web A did
 * nothing. Here the test finds a web in the air only on a new A and never as
 * a jump starts, and a new A on a web jumps off it with the script for
 * climbing off its top (13:5063, 1C:51B7): A jumps, A again takes hold, and
 * A on the web jumps off. A step onto a tile that is neither web nor solid
 * (the CP $02 after each step's test) left the web by itself, a jump off its
 * top or its sides or a drop off its bottom (13:61FB, 1C:628A); here any tile
 * that is not web is solid there, so she stays on it until A.
 *
 * The tinkerbat has the monkey's box. Hers (00:2975 loads it from each frame,
 * at +$47-+$4E) is 10 by 20 pixels, the monkey's 7 by 14 with the same feet
 * and middle, and the wall and floor tests (00:29F6, the climb's 1C:4F51)
 * take the whole box, so the 16-pixel pipes and passages the monkey climbs
 * through were walls to her. Before each tick's movement her frame's box is
 * the monkey's, feet and middle kept.
 *
 * Nothing here is kept between ticks: every decision is made from the joypad
 * and her object.
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
#define PAD_SELECT 0x04
#define PAD_RIGHT 0x10
#define PAD_LEFT 0x20
#define PAD_DOWN 0x80

#define PLAYER_POINTER 0xCA13
#define RUN_FLAG 0xCB3C
#define STANCE 0xCB54     /* 1 in the air */
#define INPUT_HELD 0xCB70 /* set by scripts: the idle routines read no buttons */
#define WALKED 0xCB79     /* set while a scene walks her: the routines only move her */
#define FORM 0xCB72
#define SOUNDS 0xC203     /* op BC (00:13D7): the newest entry's offset, then 16 entries of 8 bytes */

#define WALK_SPEED 0x100
#define RUN_SPEED 0x200
#define MONKEY_WIDTH 7    /* the monkey's box */
#define MONKEY_HEIGHT 14

#define HARPY_TALON_SCRIPT 0x74E5   /* to 0D:751E; the talon routine follows it */
#define HARPY_TALON 0x751F
#define HARPY_FLIGHT 0x72A6
#define HARPY_RUN 0x6F38
#define HARPY_IDLE 0x6D3A
#define HARPY_RET 0x7526            /* the talon routine's last RET */
#define HARPY_GLIDE 0x6C35          /* script 0D:6BF0 */
#define HARPY_FLAP_SCRIPT 0x7210    /* as 0D:7355 and 0D:6CD5 start it for A */
#define HARPY_GLIDE_SCRIPT 0x6BF0
#define HARPY_RUN_SCRIPT 0x6ED1
#define HARPY_LANDING_SCRIPT 0x73EA
#define HARPY_TOP_SPEED 0x200       /* her run's and flight's (0D:70BE, 0D:73AF) */
#define HARPY_PUSH 0x10
#define HARPY_DRAG 0x08             /* 0D:69D4, on the ground */
#define FLAP_LIFT (-0x281)          /* $FFFD7F, 0D:7289 */
#define FLAP_PULL 0x18              /* 0D:725B */

#define SPIDER_SHOT 0x4CA0          /* the shot's routine: RET */
#define SPIDER_WALK_MOVER 0x4BE5    /* the walk routine's tail: 13:4630, 13:4790, 13:47ED */
#define TINKERBAT_WALK 0x4E0F
#define TINKERBAT_RUN 0x5C00
#define TINKERBAT_LANDING 0x5596
#define ELEPHANT_IDLE_SCRIPT 0x5BE6
#define ELEPHANT_TURN 0x5F1E        /* script 0D:5EF7 */
#define ELEPHANT_TURN_SCRIPT 0x5EF7
#define ELEPHANT_LANDING 0x61D8
#define ELEPHANT_STOMP 0x62D8       /* script 0D:62A6, which ends where the routine starts */
#define ELEPHANT_STOMP_SCRIPT 0x62A6

enum { MONKEY = 1, ELEPHANT = 2, HARPY = 3, SPIDER = 4, TINKERBAT = 5 };

/* The two forms built like Shantae. */
typedef struct {
    uint8_t form, bank;
    uint16_t attack_speed;   /* the attack routine's first LD A,$00 to +$41 */
    uint16_t start_moving;   /* script: walk, or run with the run flag */
} Form;
static const Form s_monkey = {MONKEY, 0x0D, 0x4FCC, 0x47ED};
static const Form s_tinkerbat = {TINKERBAT, 0x1C, 0x57B0, 0x4DC8};

/* The two charges: the routine, and the scripts the idle routine starts for B and A. */
typedef struct {
    uint8_t form, bank;
    uint16_t routine, charge, jump;
} Charge;
static const Charge s_charges[2] = {
    {ELEPHANT, 0x0D, 0x650E, 0x64A5, 0x60B6},
    {TINKERBAT, 0x1C, 0x64CA, 0x646D, 0x51F9},
};

/* The webs: the routine on one, the script that climbs off its top with a
 * jump, the CP $64 that ends the web test (13:476B, 1C:5D41), where the test
 * returns to in the jump and fall routine and in the tinkerbat's jump script
 * (1C:529B), and the CP $02 after the test in each step (left, up, right,
 * down) that undoes it where the tile is solid. */
typedef struct {
    uint8_t form, bank;
    uint16_t routine, jump_off, test, in_air, at_start;
    uint16_t edges[4];
} Web;
static const Web s_webs[2] = {
    {SPIDER, 0x13, 0x6068, 0x5063, 0x478D, 0x5192, 0, {0x60F0, 0x612C, 0x6168, 0x61A4}},
    {TINKERBAT, 0x1C, 0x6113, 0x51B7, 0x5D69, 0x5456, 0x52A5, {0x617F, 0x61BB, 0x61F7, 0x6233}},
};

/* The climbs: the first LD A of the speed up (and down), and the animation's ADD (SUB) $40. */
typedef struct {
    uint8_t bank;
    uint16_t up, up_step, down, down_step;
} Climb;
static const Climb s_climbs[2] = {
    {0x0D, 0x4B73, 0x4B81, 0x4B8F, 0x4B9D},   /* the monkey, 0D:4ABE */
    {0x1C, 0x5135, 0x5143, 0x5151, 0x515F},   /* the tinkerbat, 1C:5080 */
};

static uint8_t *global(GBContext *ctx, unsigned addr) { return ctx->wram + (addr - 0xC000); }

static int forms_on(GBContext *ctx) {
    return shantae_smooth_moves() && shantae_smooth_forms() && ctx->wram && ctx->rom;
}

static unsigned player_pointer(GBContext *ctx) {
    return *global(ctx, PLAYER_POINTER) | *global(ctx, PLAYER_POINTER + 1) << 8;
}

static uint8_t *player_slot(GBContext *ctx) {
    const unsigned p = player_pointer(ctx);
    return p && shantae_slot_index(ctx, p) >= 0 ? shantae_slot_memory(ctx, p) : 0;
}

/* The player's slot when BC, the routine's object, is it. */
static uint8_t *player(GBContext *ctx) {
    return ctx->bc == player_pointer(ctx) ? player_slot(ctx) : 0;
}

static unsigned word_of(const uint8_t *p) { return p[0] | p[1] << 8; }

static uint32_t speed_bytes(unsigned right_speed, int left) {
    return (left ? ~right_speed : right_speed) & 0xFFFFFF;
}

static void set_speed(uint8_t *s, uint32_t speed) {
    s[0x41] = (uint8_t)speed;
    s[0x42] = (uint8_t)(speed >> 8);
    s[0x43] = (uint8_t)(speed >> 16);
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

/* One of the three operands that write a speed, low byte first: its byte. */
static int speed_operand(uint32_t speed, unsigned byte, uint8_t *value) {
    *value = (uint8_t)(speed >> byte * 8);
    return 1;
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

/* The tinkerbat's frame box, the monkey's size: as 00:2975 loads it, the
 * frame (+$2E bank, +$2F) for her facing, then x, y, width and height from
 * +$10, to +$47 (x), +$4B (y), +$49 (width) and +$4D (height). */
static void monkey_box(GBContext *ctx, uint8_t *s) {
    if (s[0x2E] == 0xFF || s[0x31] > 1) return;
    const unsigned frame = (s[0x2F] | s[0x30] << 8) + s[0x31] * 2;
    const unsigned data = (rom_byte(ctx, s[0x2E], frame) | rom_byte(ctx, s[0x2E], frame + 1) << 8) + 0x10;
    int16_t box[4];
    for (int i = 0; i < 4; i++)
        box[i] = (int16_t)(rom_byte(ctx, s[0x2E], data + i * 2) | rom_byte(ctx, s[0x2E], data + i * 2 + 1) << 8);
    const int xo = box[0], yo = box[1], w = box[2], h = box[3];
    if (w <= MONKEY_WIDTH || h <= MONKEY_HEIGHT || w > 64 || h > 64) return;
    const int small_xo = xo + (w - MONKEY_WIDTH + 1) / 2, small_yo = yo + h - MONKEY_HEIGHT;
    /* Only her frame's box or the monkey's: anything else was set on purpose. */
    const int16_t now[4] = {(int16_t)word_of(s + 0x47), (int16_t)word_of(s + 0x4B), (int16_t)word_of(s + 0x49),
                            (int16_t)word_of(s + 0x4D)};
    const int own = now[0] == xo && now[1] == yo && now[2] == w && now[3] == h;
    const int small = now[0] == small_xo && now[1] == small_yo && now[2] == MONKEY_WIDTH && now[3] == MONKEY_HEIGHT;
    if (!own && !small) return;
    const int16_t out[4][2] = {{0x47, (int16_t)small_xo}, {0x49, MONKEY_WIDTH}, {0x4B, (int16_t)small_yo},
                               {0x4D, MONKEY_HEIGHT}};
    for (int i = 0; i < 4; i++) {
        s[out[i][0]] = (uint8_t)out[i][1];
        s[out[i][0] + 1] = (uint8_t)(out[i][1] >> 8);
    }
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
    if (form == TINKERBAT && (s = player_slot(ctx)) && s[0] != 0xFF) monkey_box(ctx, s);
}

/* --- The script pass: the harpy's flaps, the charges ----------------------- */

static void harpy_flap(GBContext *ctx, uint8_t *s) {
    if (!(ctx->hram[0x0C] & PAD_A) || s[0x19] != 0x0D || word_of(s + 0x1B) != HARPY_TALON) return;
    /* Still the talon's script: on a landing this tick 0D:694C has set the
     * landing's. */
    const unsigned script = word_of(s + 0x02);
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

/* A new A on the tick she reaches the ground or leaves it: her flight
 * (0D:72A6), glide (0D:6C35) and run (0D:6F38) take it first and start the
 * flap script (0D:7355, 0D:6CD5, 0D:7044), and then, the mover having put her
 * down or the ground having dropped away under her run, set the glide
 * (0D:72C2, the run's 0D:69A9) or the landing (0D:6C55, 0D:6CA4) over it.
 * Such a script, set this tick and not yet run, is the flap's again. */
static void harpy_landing_flap(GBContext *ctx, uint8_t *s) {
    if (!(ctx->hram[0x0C] & PAD_A) || *global(ctx, WALKED) || s[0x19] != 0x0D) return;
    const unsigned routine = word_of(s + 0x1B), script = word_of(s + 0x02);
    if (routine != HARPY_FLIGHT && routine != HARPY_GLIDE && routine != HARPY_RUN) return;
    if (s[0x04] != 0x0D || word_of(s + 0x16) != 0x0080) return;
    if (script == HARPY_GLIDE_SCRIPT || script == HARPY_RUN_SCRIPT || script == HARPY_LANDING_SCRIPT)
        set_script(s, 0x0D, HARPY_FLAP_SCRIPT);
}

/* After this tick's routines and before the scripts: the script a routine
 * that takes no press (a charge, the elephant's turn, landing and stomp, the
 * tinkerbat's walk, run and landing for Down and B) has left is the one this
 * tick's presses start instead, in the idle routines' order (B over A). On
 * the tick a charge runs out its routine has set the idle script, which the
 * press replaces too. */
static void charge_presses(GBContext *ctx, uint8_t *s, uint8_t form) {
    const uint8_t pressed = ctx->hram[0x0C], held = ctx->hram[0x0B];
    if (*global(ctx, INPUT_HELD) || *global(ctx, WALKED)) return;
    const Charge *c = &s_charges[form == TINKERBAT];
    if (s[0x19] != c->bank) return;
    const unsigned routine = word_of(s + 0x1B), script = word_of(s + 0x02);
    const int charge = (pressed & PAD_B) && (form == ELEPHANT || (held & PAD_DOWN));
    const int jump = (pressed & PAD_A) != 0;
    int charges = 0, jumps = 0;   /* what a press does in this routine */
    if (routine == c->routine) {
        charges = jumps = 1;
    } else if (form == ELEPHANT && routine == ELEPHANT_TURN) {
        charges = jumps = 1;
        if (charge || jump) s[0x31] ^= 1;   /* the way the turn faces her (0D:5F15) */
    } else if (form == ELEPHANT && routine == ELEPHANT_LANDING) {
        charges = 1;   /* it takes A and the directions itself */
    } else if (form == ELEPHANT && routine == ELEPHANT_STOMP) {
        /* Its hit is out: the script's native call 04:5641 set it as she landed. */
        if (s[0x04] != 0x0D || script < ELEPHANT_STOMP_SCRIPT || script >= ELEPHANT_STOMP) return;
        charges = jumps = 1;
        if (!charge && !jump && (held & (PAD_LEFT | PAD_RIGHT))) set_script(s, 0x0D, ELEPHANT_IDLE_SCRIPT);
    } else if (form == TINKERBAT) {
        charges = routine == TINKERBAT_WALK || routine == TINKERBAT_RUN || routine == TINKERBAT_LANDING;
    }
    if (charge && charges) set_script(s, c->bank, c->charge);
    else if (jump && jumps) set_script(s, c->bank, c->jump);
    /* A charge starting now, from the idle or walk routine too, faces the way held. */
    if (charge && s[0x04] == c->bank && word_of(s + 0x02) == c->charge) {
        if (held & PAD_LEFT) s[0x31] = 1;
        else if (held & PAD_RIGHT) s[0x31] = 0;
    }
}

/* A new A on a web jumps off it; her jump steers from the next tick. */
static void web_presses(GBContext *ctx, uint8_t *s, const Web *w) {
    if (!(ctx->hram[0x0C] & PAD_A) || *global(ctx, INPUT_HELD) || *global(ctx, WALKED)) return;
    if (s[0x19] != w->bank || word_of(s + 0x1B) != w->routine) return;
    set_speed(s, 0);
    set_script(s, w->bank, w->jump_off);
}

void shantae_forms_script_pass(GBContext *ctx) {
    uint8_t *s;
    if (!forms_on(ctx) || !(s = player_slot(ctx)) || s[0] == 0xFF) return;
    const uint8_t form = *global(ctx, FORM);
    if (form == HARPY) {
        harpy_flap(ctx, s);
        harpy_landing_flap(ctx, s);
    }
    if (form == ELEPHANT || form == TINKERBAT) charge_presses(ctx, s, form);
    if (form == SPIDER || form == TINKERBAT) web_presses(ctx, s, &s_webs[form == TINKERBAT]);
}

/* --- At her movement JP: the talon, the turn, the spider's shot ----------- */

static int32_t signed_speed(const uint8_t *s) {
    return (int32_t)((uint32_t)(s[0x41] | s[0x42] << 8 | s[0x43] << 16) << 8) >> 8;
}

/* The harpy's talon (0D:751F) is the mover alone: her speed stays as the
 * talon found it, so a talon early in her run held her near standing for its
 * 20 ticks. With a direction held it changes as her run's does on the ground
 * ($10 that way while under two pixels, 0D:7073, 0D:70BE, and the drag of
 * 0D:69D4) and as her flight's in the air (0D:7374, 0D:73AF), and turns her. */
static void harpy_talon_steer(GBContext *ctx, uint8_t *s) {
    const uint8_t held = ctx->hram[0x0B];
    if (!(held & (PAD_LEFT | PAD_RIGHT))) return;
    const int left = (held & PAD_LEFT) != 0;
    int32_t v = signed_speed(s);
    if (left && v >= -(HARPY_TOP_SPEED + 1)) v -= HARPY_PUSH;
    else if (!left && v < HARPY_TOP_SPEED) v += HARPY_PUSH;
    s[0x31] = (uint8_t)left;
    if (*global(ctx, STANCE) != 1) {   /* the drag, toward 0 and no further */
        const int32_t was = v;
        v += v < 0 ? HARPY_DRAG : -HARPY_DRAG;
        if ((was < 0) != (v < 0)) v = 0;
    }
    set_speed(s, (uint32_t)v & 0xFFFFFF);
}

/* The JP at +$1A to a RET of the bank, in place of her routine. */
static int skip_routine(GBContext *ctx) {
    ctx->halt_bug = 0;
    ctx->pc = HARPY_RET;
    gb_tick(ctx, 16);
    return 1;
}

/* Her idle (0D:6D3A) keeps her speed with a direction held the way she moves
 * (0D:6D86) and hands on to the run (0D:6E21, 0D:6E47: its script and her
 * facing). "Reduce input lag" runs the run in the same tick, so there the
 * idle takes no step of its own: it sets the run's script and her facing and
 * returns. Otherwise the end of a talon was two steps on each of two ticks,
 * the talon's and the idle's, then the idle's and the run's. */
static int harpy_idle_hands_on(GBContext *ctx, uint8_t *s) {
    const uint8_t held = ctx->hram[0x0B];
    const int moving = s[0x41] | s[0x42] | s[0x43], left = (s[0x43] & 0x80) != 0;
    if (!shantae_reduce_input_lag() || !moving || !(held & (left ? PAD_LEFT : PAD_RIGHT))) return 0;
    if (ctx->hram[0x0C] & (PAD_A | PAD_B | PAD_SELECT)) return 0;   /* the idle's own presses */
    set_script(s, 0x0D, HARPY_RUN_SCRIPT);
    s[0x31] = (uint8_t)left;
    return skip_routine(ctx);
}

/* The elephant's turn (0D:5F1E, script 0D:5EF7: ten ticks, then 0D:5F15
 * faces her about) stands her, the script's op 68 having zeroed her speed:
 * with the way it turns her held she walks that way through it, at the walk's
 * speed (0D:5E9C, 0D:5EBA). */
static void elephant_turn_step(GBContext *ctx, uint8_t *s) {
    const int left = s[0x31] == 0;   /* facing right, the turn faces her left */
    set_speed(s, ctx->hram[0x0B] & (left ? PAD_LEFT : PAD_RIGHT) ? speed_bytes(WALK_SPEED, left) : 0);
}

/* The spider's shot (13:4CA0, a RET) with a direction held: the walk's mover. */
static int spider_shot(GBContext *ctx, uint8_t *s) {
    if (shantae_whip_moving() == SHANTAE_WHIP_ORIGINAL) return 0;
    const uint8_t held = ctx->hram[0x0B];
    if (!(held & (PAD_LEFT | PAD_RIGHT))) return 0;
    /* The walk before it has moved her in this tick ("Reduce input lag"). */
    if (stepped_this_tick(ctx, s)) return 0;
    const int left = !(held & PAD_RIGHT);
    s[0x31] = (uint8_t)left;
    set_speed(s, speed_bytes(held & PAD_B ? RUN_SPEED : WALK_SPEED, left));
    /* The JP at +$1A, to the walk's mover in place of the RET. */
    ctx->halt_bug = 0;
    ctx->pc = SPIDER_WALK_MOVER;
    gb_tick(ctx, 16);
    return 1;
}

int shantae_forms_dispatch(GBContext *ctx, uint16_t addr) {
    if (!forms_on(ctx)) return 0;
    const unsigned p = player_pointer(ctx);
    uint8_t *s;
    if (!p || addr != (uint16_t)(p + 0x1A) || !(s = player_slot(ctx))) return 0;
    if (s[0x1A] != 0xC3 || ctx->rom_bank != s[0x19]) return 0;
    const uint8_t form = *global(ctx, FORM);
    const unsigned routine = word_of(s + 0x1B);
    if (form == SPIDER && s[0x19] == 0x13 && routine == SPIDER_SHOT) return spider_shot(ctx, s);
    if (s[0x19] != 0x0D || *global(ctx, INPUT_HELD) || *global(ctx, WALKED)) return 0;
    if (form == HARPY && routine == HARPY_TALON) {
        /* "Reduce input lag" runs it in the tick of the B, after her run has
         * moved her: its first step waits for the next tick. */
        if (stepped_this_tick(ctx, s)) return skip_routine(ctx);
        harpy_talon_steer(ctx, s);
    } else if (form == HARPY && routine == HARPY_IDLE) {
        return harpy_idle_hands_on(ctx, s);
    } else if (form == ELEPHANT && routine == ELEPHANT_TURN) {
        elephant_turn_step(ctx, s);
    }
    return 0;   /* the JP goes on to the routine */
}

/* --- The operands ---------------------------------------------------------- */

/* A climb's speed and animation step with B held: twice. */
static int climb_operand(const Climb *c, uint16_t pc, uint8_t orig, uint8_t *value) {
    if (pc == c->up_step || pc == c->down_step) {   /* ADD A,$40 / SUB $40 on +$66 */
        if (orig != 0x40) return 0;
        *value = 0x80;
        return 1;
    }
    const int up = pc - c->up < 9;
    const unsigned byte = (unsigned)(pc - (up ? c->up : c->down)) / 3;
    const uint32_t slow = speed_bytes(WALK_SPEED, up);
    if (orig != (uint8_t)(slow >> byte * 8)) return 0;
    return speed_operand(speed_bytes(RUN_SPEED, up), byte, value);
}

static int spider_operand(GBContext *ctx, uint8_t *s, uint16_t pc, uint8_t orig, uint8_t *value) {
    const uint8_t held = ctx->hram[0x0B];
    if (!(held & PAD_B)) return 0;
    switch (pc) {
    case 0x4C32: case 0x4C35: case 0x4C38:   /* LD A,$FF/$FE/$FF: the walk, left */
    case 0x4C47: case 0x4C4A: case 0x4C4D:   /* LD A,0/1/0: right */
    case 0x528B: case 0x528E: case 0x5291:   /* the jump, left */
    case 0x52A0: case 0x52A3: case 0x52A6: { /* and right */
        const int left = pc < 0x4C47 || (pc >= 0x528B && pc < 0x52A0);
        const unsigned first = pc < 0x5000 ? (left ? 0x4C32 : 0x4C47) : (left ? 0x528B : 0x52A0);
        const unsigned byte = (unsigned)(pc - first) / 3;
        if (orig != (uint8_t)(speed_bytes(WALK_SPEED, left) >> byte * 8)) return 0;
        if (pc >= 0x5000 && !shantae_air_speed_b()) return 0;
        if (pc == first && pc < 0x5000) {
            /* The walk's animation at twice the rate: another $100 off the script's wait. */
            const uint16_t timer = (uint16_t)(word_of(s + 0x16) - 0x100);
            s[0x16] = (uint8_t)timer;
            s[0x17] = (uint8_t)(timer >> 8);
        }
        return speed_operand(speed_bytes(RUN_SPEED, left), byte, value);
    }
    case 0x60DE: case 0x60F9: case 0x611A: case 0x6135:   /* SUB/ADD $01: a step on the web, and its undo */
    case 0x6156: case 0x6171: case 0x6192: case 0x61AD:
        if (orig != 0x01) return 0;
        *value = 0x02;
        return 1;
    case 0x61CE:                                         /* ADD A,$80 on +$75: her animation */
        if (orig != 0x80) return 0;
        /* $100 in place of $80: the frame (+$76, 0 to 9) on by one. */
        s[0x76] = (uint8_t)((s[0x76] + 1) % 10);
        *value = 0x00;
        return 1;
    }
    return 0;
}

/* The CP $64 that ends a web test: not a web where the test is her jump's or
 * fall's, except on a new A, nor where it is her jump script's. */
static int web_operand(GBContext *ctx, const Web *w, uint8_t orig, uint8_t *value) {
    if (orig != 0x64) return 0;
    const uint16_t back = gb_read16(ctx, ctx->sp);   /* POP DE / POP BC are done */
    if (!(back == w->in_air && !(ctx->hram[0x0C] & PAD_A)) && !(w->at_start && back == w->at_start)) return 0;
    *value = (uint8_t)~ctx->a;
    return 1;
}

/* The CP $02 after a step's web test, where the tile is not a web: solid
 * whatever it is, so the step is undone and she stays on the web. */
static int web_edge(const Web *w, uint16_t pc) {
    for (int i = 0; i < 4; i++)
        if (pc == w->edges[i]) return 1;
    return 0;
}

int shantae_forms_imm(GBContext *ctx, uint8_t bank, uint16_t pc, uint8_t orig, uint8_t *value) {
    uint8_t *s;
    if (!forms_on(ctx) || !(s = player(ctx))) return 0;
    const uint8_t form = *global(ctx, FORM), held = ctx->hram[0x0B];
    const Form *f = bank == 0x0D && form == MONKEY ? &s_monkey : bank == 0x1C && form == TINKERBAT ? &s_tinkerbat : 0;
    for (int i = 0; i < 2; i++) {
        if (bank != s_webs[i].bank) continue;
        if (pc == s_webs[i].test) return form == s_webs[i].form && web_operand(ctx, &s_webs[i], orig, value);
        if (web_edge(&s_webs[i], pc)) {
            if (form != s_webs[i].form || orig != 0x02) return 0;
            *value = ctx->a;
            return 1;
        }
    }
    if (bank == 0x13) return form == SPIDER && spider_operand(ctx, s, pc, orig, value);
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
    case 0x0D4F89: case 0x0D4F91: {                   /* LD A,1 / LD A,0 to +$31: the air claw turns her */
        const int left = pc == 0x4F89;
        if (!f || orig != (uint8_t)left) return 0;
        /* And moves her at the jump's speed (0D:4E3C, 0D:4E68). */
        const int fast = shantae_air_speed_b() ? (held & PAD_B) != 0 : s[0x64] != 0;
        set_speed(s, speed_bytes(fast ? RUN_SPEED : WALK_SPEED, left));
        return 0;
    }
    case 0x0D4B73: case 0x0D4B76: case 0x0D4B79: case 0x0D4B81:   /* the monkey climbing up */
    case 0x0D4B8F: case 0x0D4B92: case 0x0D4B95: case 0x0D4B9D:   /* and down */
    case 0x1C5135: case 0x1C5138: case 0x1C513B: case 0x1C5143:   /* the tinkerbat */
    case 0x1C5151: case 0x1C5154: case 0x1C5157: case 0x1C515F:
        return f && (held & PAD_B) && climb_operand(&s_climbs[bank == 0x1C], pc, orig, value);
    case 0x1C616D: case 0x1C6188: case 0x1C61A9: case 0x1C61C4:   /* SUB/ADD $01: a step on the web, and its undo */
    case 0x1C61E5: case 0x1C6200: case 0x1C6221: case 0x1C623C:
        if (!f || orig != 0x01 || !(held & PAD_B)) return 0;
        *value = 0x02;
        return 1;
    case 0x1C625D:                                                 /* ADD A,$40 on +$75: her animation on it */
        if (!f || orig != 0x40 || !(held & PAD_B)) return 0;
        *value = 0x80;
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
    case 0x0D5C57: case 0x0D5C5A: case 0x0D5C5D: {   /* LD A,0: the elephant's idle stops her */
        if (form != ELEPHANT || orig != 0x00) return 0;
        /* After the turn she walks on the way it faced her, with no tick at
         * rest; not where "Reduce input lag" runs it after the turn has moved
         * her this tick (the walk it hands on to sets its own speed). */
        const int moving = s[0x41] | s[0x42] | s[0x43], left = (s[0x43] & 0x80) != 0;
        if (!moving || s[0x31] != left || !(held & (left ? PAD_LEFT : PAD_RIGHT)) || stepped_this_tick(ctx, s))
            return 0;
        *value = s[0x41 + (pc - 0x5C57) / 3];
        return 1;
    }
    case 0x0D5E43: case 0x0D5E46: {                  /* LD A,$E6/$5B: the walk's idle script, at no speed */
        /* Its turn (0D:5ED8) set this tick, with the other way held: kept. */
        static const uint8_t turn[3] = {(uint8_t)ELEPHANT_TURN_SCRIPT, ELEPHANT_TURN_SCRIPT >> 8, 0x0D};
        const unsigned byte = pc == 0x5E43 ? 0 : 1;
        if (form != ELEPHANT || orig != (byte ? 0x5B : 0xE6)) return 0;
        if (s[0x02] != turn[0] || s[0x03] != turn[1] || s[0x04] != turn[2]) return 0;
        *value = turn[byte];
        return 1;
    }
    }
    return 0;
}
