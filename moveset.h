#pragma once
/* "Smoother movement": changes to how Shantae's base form handles (moveset.c). */
#include <stdint.h>
struct GBContext;

/* What a whip does when a direction is held (shantae_whip_moving). */
enum {
    SHANTAE_WHIP_ORIGINAL,   /* she stops to whip */
    SHANTAE_WHIP_SLIDE,      /* she whips and keeps moving */
    SHANTAE_WHIP_CANCEL,     /* B with a direction runs at once; she whips standing still */
};

/* Settings (extras.c, shantae.ini). */
int shantae_smooth_moves(void);
void shantae_set_smooth_moves(int on);
int shantae_whip_moving(void);
void shantae_set_whip_moving(int mode);
int shantae_air_speed_b(void);
void shantae_set_air_speed_b(int on);
int shantae_fast_crawl(void);
void shantae_set_fast_crawl(int on);

/* Before a tick's movement routines, before its scripts, and before a player
 * move run early: the run flag follows B. */
void shantae_moves_tick(struct GBContext *ctx);
/* An [[imm_override]] site in bank 6. Returns 1 with the operand to use. */
int shantae_moves_imm(struct GBContext *ctx, uint16_t pc, uint8_t orig, uint8_t *value);
