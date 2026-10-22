#pragma once
/* "Transformation buttons": a button for each of Shantae's forms switches to
 * it at once, without the dance (transform.c). */
#include <stddef.h>
#include <stdint.h>
struct GBContext;

/* Setting (extras.c, shantae.ini). */
int shantae_transform_buttons(void);
void shantae_set_transform_buttons(int on);

/* The forms, as CB72 holds them. */
enum {
    SHANTAE_FORM_NONE,
    SHANTAE_FORM_MONKEY,
    SHANTAE_FORM_ELEPHANT,
    SHANTAE_FORM_HARPY,
    SHANTAE_FORM_SPIDER,
    SHANTAE_FORM_TINKERBAT,
    SHANTAE_FORM_COUNT,
};
/* The heal dance's button, after the forms (CB72 never holds it). */
#define SHANTAE_HEAL SHANTAE_FORM_COUNT

/* A button for `form` was pressed: she switches to it on the next tick she
 * is in control, or back into herself if she is that form already. For
 * SHANTAE_HEAL she heals as the heal dance heals, in any form. */
void shantae_transform_request(int form);
/* Whether the heal dance would heal her now: a heart to fill and the gems
 * for it. */
int shantae_heal_possible(const struct GBContext *ctx);
/* The form of the game's action `action` (extras.c, game_get_actions), or
 * SHANTAE_FORM_NONE. */
int shantae_action_form(int action);
/* Whether she has learned the form's dance; and the form she is now. */
int shantae_form_learned(const struct GBContext *ctx, int form);
int shantae_form(const struct GBContext *ctx);

/* game_on_init, after the expanded view's: installs a read tap ahead of its. */
void shantae_transform_init(struct GBContext *ctx);
/* At the start of a tick's movement pass (00:0C4B). */
void shantae_transform_movement_pass(struct GBContext *ctx);
/* At the start of a tick's script pass (00:130B). Returns the ROM routine
 * the button calls in that tick (extras.c), as bank << 16 | address, or 0:
 * her new form's sprite palettes (06:4015) when she switched, the heal dance's
 * healing (0E:4421) when she heals. */
uint32_t shantae_transform_pass(struct GBContext *ctx);

/* Rollback state (preemptive frames, rewind): a press waiting for its tick. */
size_t shantae_transform_state_size(void);
void shantae_transform_state_save(void *out);
void shantae_transform_state_load(const void *in);
/* A save state file was loaded: drops a press still waiting. */
void shantae_transform_state_file_loaded(void);
