#pragma once
/* "Text speed": B in use prints dialogue faster, up to at once (dialogue.c). */
#include <stdint.h>
struct GBContext;

/* 0 the original; n gives the speaker n more frames of text a frame; instant
 * runs the text out in the pass. */
#define SHANTAE_TEXT_INSTANT 10

/* Settings (extras.c, shantae.ini). */
int shantae_text_speed(void);
void shantae_set_text_speed(int speed);

/* Hooks in the object script pass, from extras.c: its start (00:130B), and
 * each op's dispatch (00:1345, 00:1A12) with A the op, DE the script after it
 * and BC the slot. */
void shantae_text_pass(struct GBContext *ctx);
void shantae_script_op(struct GBContext *ctx);
/* A wait was shortened this pass: the frame hold keeps the picture. */
int shantae_text_running(void);
/* Text speed is on and B is in use: held now or within the last few frames,
 * so pressing it again and again counts. A shop then takes its gems every
 * frame (extras.c, 1A:70CB), not every eighth. */
int shantae_text_hurry(struct GBContext *ctx);
/* Debug counts since launch (shantae_hold_info): waits shortened, the frames
 * of waiting passed over, and signal fields set (Risky's hold). */
void shantae_text_counts(unsigned long long *waits, unsigned long long *frames, unsigned long long *signals);
