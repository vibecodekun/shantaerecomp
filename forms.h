#pragma once
/* "Smoother movement" for the transformations (forms.c). */
#include <stdint.h>
struct GBContext;

/* Setting (extras.c, shantae.ini): the transformations move like Shantae. */
int shantae_smooth_forms(void);
void shantae_set_smooth_forms(int on);

/* Before a tick's movement routines, its scripts and an early player move,
 * in a transformation: the run flag follows B, and the tinkerbat squeezes. */
void shantae_forms_tick(struct GBContext *ctx);
/* An [[imm_override]] site in bank 0D or 1C. Returns 1 with the operand to use. */
int shantae_forms_imm(struct GBContext *ctx, uint8_t bank, uint16_t pc, uint8_t orig, uint8_t *value);
