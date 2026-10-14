#pragma once
/* "Instant text": A runs a dialogue's text out to its end at once (dialogue.c). */
#include <stdint.h>
struct GBContext;

/* Settings (extras.c, shantae.ini). */
int shantae_instant_text(void);
void shantae_set_instant_text(int on);

/* Hooks in the object script pass (00:1305), from extras.c: its start
 * (00:130B), a slot's timer about to count down (the SUB $00 at 00:1323;
 * returns 1 with the operand to use), and the step on to the next slot
 * (00:1384; returns 1 when the same slot's script is to run again). */
void shantae_text_pass(struct GBContext *ctx);
int shantae_text_imm(struct GBContext *ctx, uint16_t pc, uint8_t orig, uint8_t *value);
int shantae_text_again(struct GBContext *ctx);
/* A text is being run out: the frame hold keeps the picture until it is. */
int shantae_text_running(void);
/* Debug counts since launch (shantae_view_info): texts run out and the
 * script runs that took. */
void shantae_text_counts(unsigned long long *texts, unsigned long long *runs);
