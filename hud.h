#pragma once
/* The transformation buttons' HUD (hud.c): an icon for each form and for the
 * heal, in the Dance Menu's look, drawn in a top corner of the picture; and
 * the day and night clock, in the status bar's. */
#include <stdint.h>
struct GBContext;

/* Where it is drawn (extras.c, shantae.ini): inside the picture, whatever its
 * shape, in the corner chosen; at the bottom, above the bottom UI as it is
 * (the status bar, a dialogue box, a town's panel). */
enum {
    SHANTAE_HUD_OFF,
    SHANTAE_HUD_TOP_LEFT,
    SHANTAE_HUD_TOP_RIGHT,
    SHANTAE_HUD_BOTTOM_LEFT,
    SHANTAE_HUD_BOTTOM_RIGHT,
};
int shantae_hud_mode(void);
void shantae_set_hud_mode(int mode);
/* Its size: SHANTAE_HUD_AUTO, half the whole scale the original picture has
 * in the window (3 screen pixels a pixel on a 1080p screen), 1-8 screen
 * pixels for each of its pixels, or SHANTAE_HUD_PICTURE, the picture's own
 * pixels on the picture's pixel grid, so a shader preset that works per pixel
 * (an LCD's dots, a CRT's scanlines) draws them as it draws the game's. */
#define SHANTAE_HUD_AUTO 0
#define SHANTAE_HUD_SIZE_MAX 8
#define SHANTAE_HUD_PICTURE (SHANTAE_HUD_SIZE_MAX + 1)
int shantae_hud_size(void);
void shantae_set_hud_size(int size);
/* How its icons are laid out: 1-3 columns (3 by default, two rows of three),
 * or SHANTAE_HUD_ROW, all in one row. */
#define SHANTAE_HUD_ROW 6
int shantae_hud_columns(void);
void shantae_set_hud_columns(int columns);

/* The HUD as it is now, in its own pixels (0xAARRGGBB, transparent where it
 * draws nothing), at most HUD_MAX_W x HUD_MAX_H. Returns 0 when there is
 * nothing to show: the buttons are off, no dance is learned yet, or there is
 * no player. */
#define SHANTAE_HUD_MAX_W 192
#define SHANTAE_HUD_MAX_H 192
int shantae_hud_draw(struct GBContext *ctx, uint32_t *out, int *width, int *height);

/* The day and night clock: where it is drawn (a SHANTAE_HUD_* corner, or
 * off), and its size as the HUD's (SHANTAE_HUD_PICTURE by default, as the
 * status bar's numerals are drawn). In a corner the
 * HUD has too, it sits beside the HUD, below it at the top and above it at
 * the bottom. */
int shantae_clock_mode(void);
void shantae_set_clock_mode(int mode);
int shantae_clock_size(void);
void shantae_set_clock_size(int size);
/* The clock as it is now: the sun or the moon and the time to the next
 * change, SHANTAE_CLOCK_W x SHANTAE_CLOCK_H, transparent around its outline
 * (0xAARRGGBB). Drawn whatever the
 * setting (the map shows it too); 0 when there is no player. */
#define SHANTAE_CLOCK_W 39
#define SHANTAE_CLOCK_H 11
int shantae_clock_draw(struct GBContext *ctx, uint32_t *out, int *width, int *height);
