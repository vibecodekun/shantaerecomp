#pragma once
/* "Dancing": quick dance steps and invincibility after a transformation (dance.c). */
#include <stdint.h>
struct GBContext;

/* Settings (extras.c, shantae.ini). */
int shantae_easy_dance(void);
void shantae_set_easy_dance(int on);
int shantae_quick_steps(void);
void shantae_set_quick_steps(int on);
int shantae_transform_invincible(void);
void shantae_set_transform_invincible(int on);

/* An [[imm_override]] site in bank 0E. Returns 1 with the operand to use. */
int shantae_dance_imm(struct GBContext *ctx, uint16_t pc, uint8_t orig, uint8_t *value);
/* game_dispatch_override: the call to 06:72D2 that ends a transformation's
 * protection as its silhouette ends, and the turn back's last, go to the
 * blinker. Returns 1 when it set ctx->pc. */
int shantae_dance_dispatch(struct GBContext *ctx, uint16_t addr);
