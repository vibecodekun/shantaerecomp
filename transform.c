/*
 * transform.c -- "Transformation buttons": a button for each of Shantae's
 * forms switches to it at once, without the dance.
 *
 * In the original a form is danced into (dance.c): Select, the steps, and on
 * a match her script goes on at 0E:41F9, plays the dance's flash and jumps to
 * the table entry's script (0E:4342, eight bytes an entry). For a form that is
 * op 32 to 0E:4458, which spawns the silhouette object (script 0E:4E82: it
 * sets the form, CB72 = CB81 at 0E:4EC8, whitens the background and blackens
 * the sprites, fades them back and sets CB80), and op 40 to the form's start
 * script (monkey 0D:448D, elephant 0D:5AB9, harpy 0D:6CF4, spider 13:4818,
 * tinkerbat 1C:4BAA). Each start script holds her still (routine 0E:40BD,
 * which only keeps her falling) until CB80 is set, clears it, and goes on to
 * the form's own start: its movement routine, its idle animation. Turning back
 * (Select in a form) is the script 0E:4000, an effect object (0E:408D, script
 * 0E:4DF1, which sets CB72 to 0) and the same wait, and ends at her crouch
 * (06:6044) where the ceiling is low or her idle (06:49F2). 06:49F2 is the
 * idle of every form: it goes by CB72 to that form's own start (the monkey's
 * 0D:449F, just past the start script's wait), or Shantae's.
 *
 * A button sets the form and starts her script at 06:49F2 (06:6044 for
 * Shantae under a low ceiling) in that tick's script pass, with nothing on its
 * call stack, as the end of a transformation does: no dance, no silhouette,
 * the flash's sound. The form's sprite palettes come as a transformation's
 * do, from 06:4015 (extras.c, load_form_palette).
 * Her new form's routine takes over from that pass. It happens on a tick she
 * is in the player's hands: when her movement routine, run by 00:0C45 with BC
 * her slot, read the buttons (FF8B held, FF8C new presses; the joypad routine
 * 01:7C31 fills them). Every routine of every form that moves her under
 * control reads them; the ones scenes give her do not (backing away from a
 * freed genie, 06:74AC, and waiting for the scene's signal, 06:77AB), nor do
 * the dance, the transformations, a hit's knockback or a shop's (1A:7519), and
 * her idle reads none while a script holds the buttons (CB70). The hazard
 * check each form runs (06:431D, by way of 06:46E2) is no such sign: scenes
 * run it too. A press waits up to REQUEST_TICKS for such a tick, in the
 * rollback state. Only forms whose dance she has learned (their bit in
 * CB1B+n) switch; the button of the form she is turns her back into herself.
 * A town's street is walked by a routine of bank 4 that reads the buttons but
 * has no dance, so nothing switches there (the monkey fell through it); in
 * its buildings she has her idle routine (06:4AA7), which dances.
 *
 * The heal dance (Down Up A) is the script 0E:4403: the silhouette object
 * without a form (CB81 $FF), op 32 to 0E:4421, and the same wait. 0E:4421
 * spends 10 gems (CA7D-CA7E) for each heart (4 of the health in CA80) up to
 * the most she has (CA82), while the gems last: nothing when she is whole or
 * has fewer than 10. Its button calls 0E:4421 in the tick's script pass as the
 * palettes are called, with the flash's sound, in any form and whatever she is
 * doing under control; where it would heal nothing it does nothing.
 */
#include "transform.h"
#include "expanded_view.h"
#include "gbrt.h"
#include "gb_custom_view.h"
#include "object_slots.h"
#include <string.h>

#define PLAYER_POINTER 0xCA13
#define LEARNED 0xCB1B       /* + a dance's byte: its bit is set once learned */
#define EFFECT_DONE 0xCB80   /* set by the silhouette and turn-back objects */
#define FORM 0xCB72
#define NEXT_FORM 0xCB81

#define DANCE_BANK 0x0E
#define DANCES 0x4342
#define IDLE_BANK 0x06
#define IDLE_SCRIPT 0x49F2
#define CROUCH_SCRIPT 0x6044
#define HELD 0xFF8B
#define PRESSED 0xFF8C
#define SOUNDS 0xC203   /* op BC (00:13D7): the newest entry's offset, then 16 entries of 8 bytes */
#define GEMS 0xCA7D     /* 16 bits */
#define HEALTH 0xCA80
#define HEALTH_MOST 0xCA82
#define HEART_GEMS 10

/* The routines a button calls (extras.c), bank << 16 | address. */
#define FORM_PALETTE 0x064015u   /* the form's sprite palettes, from the area's table at CB73 */
#define HEAL 0x0E4421u           /* the heal dance's healing */
/* The heal dance's steps in the dance table: Down, Up, A. */
static const uint8_t s_heal_steps[] = {1, 6, 7, 0};

/* A press waits this many ticks for one she is in control: long enough to
 * switch as she lands, not so long that one made in a dialogue switches
 * after it. */
#define REQUEST_TICKS 30
static uint8_t s_request;   /* the form asked for, plus one; 0 none */
static uint8_t s_ticks;     /* the ticks it still waits */
static uint8_t s_control;   /* her movement routine read the buttons this tick */
static uint8_t s_moving;    /* in the movement pass: 00:0C4B to 00:130B */
static GBCustomReadTap s_next_tap;

static const uint8_t *global(const GBContext *ctx, unsigned addr) { return ctx->wram + (addr - 0xC000); }
static uint8_t *global_mut(GBContext *ctx, unsigned addr) { return ctx->wram + (addr - 0xC000); }

static const uint8_t *rom(const GBContext *ctx, unsigned bank, unsigned addr) {
    return ctx->rom + bank * 0x4000u + (addr - 0x4000u);
}

int shantae_form(const GBContext *ctx) {
    return ctx && ctx->wram ? *global(ctx, FORM) : 0;
}

int shantae_form_learned(const GBContext *ctx, int form) {
    if (!ctx || !ctx->wram || !ctx->rom || ctx->rom_size < (DANCE_BANK + 1) * 0x4000u) return 0;
    for (int i = 0; i < 16; i++) {
        const uint8_t *d = rom(ctx, DANCE_BANK, DANCES + i * 8);
        if (!(d[0] | d[1])) break;
        const int match = form == SHANTAE_HEAL
            ? !memcmp(rom(ctx, DANCE_BANK, d[0] | d[1] << 8), s_heal_steps, sizeof(s_heal_steps))
            : d[7] == form;
        if (match) return (*global(ctx, LEARNED + d[3]) & d[2]) != 0;
    }
    return 0;
}

int shantae_heal_possible(const GBContext *ctx) {
    if (!ctx || !ctx->wram) return 0;
    const unsigned gems = global(ctx, GEMS)[0] | global(ctx, GEMS)[1] << 8;
    return gems >= HEART_GEMS && *global(ctx, HEALTH) != *global(ctx, HEALTH_MOST);
}

void shantae_transform_request(int form) {
    if (form <= SHANTAE_FORM_NONE || form > SHANTAE_HEAL) return;
    s_request = (uint8_t)(form + 1);
    s_ticks = REQUEST_TICKS;
}

void shantae_transform_movement_pass(GBContext *ctx) {
    (void)ctx;
    s_moving = 1;
}

/* Every read the game makes (gb_custom_read_tap), before the expanded view's. */
static void read_tap(GBContext *ctx, uint16_t addr) {
    if ((addr == HELD || addr == PRESSED) && s_moving && ctx->wram &&
        ctx->bc == (global(ctx, PLAYER_POINTER)[0] | global(ctx, PLAYER_POINTER)[1] << 8))
        s_control = 1;
    if (s_next_tap) s_next_tap(ctx, addr);
}

/* A state file loaded: a press made before it is not the loaded game's. */
void shantae_transform_state_file_loaded(void) {
    s_request = s_ticks = s_control = s_moving = 0;
}

void shantae_transform_init(GBContext *ctx) {
    (void)ctx;
    s_request = s_ticks = s_control = s_moving = 0;
    if (gb_custom_read_tap == read_tap) return;
    s_next_tap = gb_custom_read_tap;
    gb_custom_read_tap = read_tap;
}

/* Room for a form. 00:29F6 places an object's box at its position (+34, +37)
 * plus its origin (+39, +3B) plus the box its animation frame gives (00:2975:
 * x and y offsets at +47 and +4B, width and height at +49 and +4D); each
 * form's idle frame has its own, and all end at her feet. 00:3297 and the
 * probes the turn back runs (00:358D, 00:3634, 00:3728) take collision $02 and
 * $62 as wall. A form switches only where its box meets no wall, so she is
 * never put inside one: the elephant does not fit the monkey's passages, and
 * Shantae crouches where she cannot stand, as after the original's turn back. */
typedef struct { int8_t x, y; uint8_t w, h; } Box;
static const Box s_boxes[SHANTAE_FORM_COUNT] = {
    {22, 16, 10, 31},   /* Shantae, standing */
    {24, 33, 7, 14},    /* monkey */
    {20, 24, 17, 23},   /* elephant */
    {22, 16, 10, 31},   /* harpy */
    {20, 27, 9, 20},    /* spider */
    {24, 33, 7, 14},    /* tinkerbat ("Smoother movement" gives her the monkey's) */
};
static const Box s_crouch = {22, 32, 10, 15};

/* The collision byte of the map's cell at x,y, as 00:3814 reads it: the map
 * directory C9F9/C9FA, the metatile bank FFDF. */
static int collision(const GBContext *ctx, int x, int y) {
    if (x < 0 || y < 0 || x >= 8192 || y >= 8192) return 0;
    const unsigned page = *global(ctx, 0xC9F9), bank = *global(ctx, 0xC9FA);
    const unsigned entry = (page << 8) + (unsigned)(y >> 8) * 64 + (unsigned)(x >> 8) * 2;
    if (entry < 0x4000 || entry + 1 >= 0x8000 || (bank + 1) * 0x4000u > ctx->rom_size) return 0;
    const unsigned cells = *rom(ctx, bank, entry) << 8, cell_bank = *rom(ctx, bank, entry + 1);
    if (cells < 0x4000 || cells >= 0x8000 || (cell_bank + 1) * 0x4000u > ctx->rom_size) return 0;
    const unsigned c = cells + ((unsigned)(y & 255) >> 4) * 32 + ((unsigned)(x & 255) >> 4) * 2;
    const unsigned lo = *rom(ctx, cell_bank, c), hi = *rom(ctx, cell_bank, c + 1);
    const unsigned tile_bank = ctx->hram[0x5F] + (lo & 15), metatile = (hi << 8) | (lo & 0xF0);
    if (metatile < 0x4000 || metatile + 16 > 0x8000 || (tile_bank + 1) * 0x4000u > ctx->rom_size) return 0;
    return *rom(ctx, tile_bank, metatile + 8 + ((y & 8) >> 2) + ((x & 8) >> 3));
}

static int fits(const GBContext *ctx, const uint8_t *s, const Box *b) {
    const int left = (int16_t)(s[0x34] | s[0x35] << 8) + (int16_t)(s[0x39] | s[0x3A] << 8) + b->x;
    const int top = (int16_t)(s[0x37] | s[0x38] << 8) + (int16_t)(s[0x3B] | s[0x3C] << 8) + b->y;
    for (int y = top & ~7; y < top + b->h; y += 8)
        for (int x = left & ~7; x < left + b->w; x += 8) {
            const int c = collision(ctx, x, y);
            if (c == 0x02 || c == 0x62) return 0;
        }
    return 1;
}

/* Her script from `script` (bank 6) in this tick's script pass, with nothing
 * on its call stack. */
static void start_script(uint8_t *s, uint16_t script) {
    s[0x16] = 0x80;
    s[0x17] = 0x00;
    s[0x02] = (uint8_t)script;
    s[0x03] = (uint8_t)(script >> 8);
    s[0x04] = IDLE_BANK;
    s[0x05] = 0xFF;
}

/* op BC 4E FF 78 00: the sound of the dance's flash (0E:4202) and of turning
 * back (0E:4DFB). */
static void flash_sound(GBContext *ctx) {
    uint8_t *newest = global_mut(ctx, SOUNDS);
    *newest = (uint8_t)((*newest + 8) & 0x7F);
    uint8_t *e = global_mut(ctx, SOUNDS + 1 + *newest);
    e[0] = 0x01;
    e[1] = 0x4E;
    e[2] = 0xFF;
    e[3] = 0x78;
    e[4] = 0x00;
}

uint32_t shantae_transform_pass(GBContext *ctx) {
    const int control = s_control;
    s_control = s_moving = 0;
    if (s_request && !control && (!s_ticks || !--s_ticks)) s_request = 0;
    if (!s_request || !control || !ctx->wram || !shantae_transform_buttons()) return 0;
    if (shantae_view_town(ctx)) {
        s_request = 0;
        return 0;
    }
    const unsigned p = global(ctx, PLAYER_POINTER)[0] | global(ctx, PLAYER_POINTER)[1] << 8;
    if (shantae_slot_index(ctx, p) < 0) return 0;
    uint8_t *s = shantae_slot_memory(ctx, p);
    int form = s_request - 1;
    s_request = 0;
    if (!shantae_form_learned(ctx, form)) return 0;
    if (form == SHANTAE_HEAL) {
        if (!shantae_heal_possible(ctx)) return 0;
        flash_sound(ctx);
        return HEAL;
    }
    if (form == shantae_form(ctx)) form = SHANTAE_FORM_NONE;
    uint16_t script = IDLE_SCRIPT;
    if (!fits(ctx, s, &s_boxes[form])) {
        if (form != SHANTAE_FORM_NONE || !fits(ctx, s, &s_crouch)) return 0;
        script = CROUCH_SCRIPT;
    }
    *global_mut(ctx, FORM) = (uint8_t)form;
    *global_mut(ctx, NEXT_FORM) = (uint8_t)form;
    *global_mut(ctx, EFFECT_DONE) = 0;
    start_script(s, script);
    flash_sound(ctx);
    return FORM_PALETTE;
}

size_t shantae_transform_state_size(void) { return 3; }
void shantae_transform_state_save(void *out) {
    uint8_t *p = out;
    p[0] = s_request;
    p[1] = s_ticks;
    p[2] = s_control;
}
void shantae_transform_state_load(const void *in) {
    const uint8_t *p = in;
    s_request = p[0];
    s_ticks = p[1];
    s_control = p[2];
}
