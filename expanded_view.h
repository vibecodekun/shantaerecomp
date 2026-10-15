#pragma once
#include <stddef.h>
#include <stdint.h>
struct GBContext;
void shantae_view_init(struct GBContext *ctx);
int shantae_expanded_view(void);
void shantae_set_expanded_view(int on);
/* "Reduce input lag" (extras.c): VBlanks that copied the fresh sprite buffer,
 * ones that kept the original, player movement routines run early, and frames
 * the runtime's preemptive frames ran again. */
void shantae_input_lag_counts(unsigned long long *fresh, unsigned long long *original,
                              unsigned long long *early_moves, unsigned long long *replays);
/* Addresses the dispatcher offers (ram_native.c): 00:1143, where the object
 * spawner fills an area the view has just revealed. 1 when it redirected. */
int shantae_view_dispatch(struct GBContext *ctx, uint16_t addr);
/* An [[imm_override]] site the view answers (extras.c): the spider's shot
 * lives while it is in the picture. Returns 1 with the operand to use. */
int shantae_view_imm(struct GBContext *ctx, uint8_t bank, uint16_t pc, uint8_t orig, uint8_t *value);
/* Whether the room in guest RAM is a town, which keeps the original picture,
 * activation and object table (expanded_background.inc). */
int shantae_view_town(const struct GBContext *ctx);
/* The view's state for rollback (preemptive frames); see gb_game_state. */
size_t shantae_view_state_size(void);
void shantae_view_state_save(void *out);
void shantae_view_state_load(const void *in);
/* A save state file was loaded: mend what the view lost in states saved
 * before a fix (Sky's crow at the desert labyrinth, an eye's jar in the third
 * labyrinth). */
void shantae_view_state_file_loaded(struct GBContext *ctx);

/* View size in game pixels, including the 16-pixel status bar. The original
 * screen is 160 x 144; the default is the NES-size 256 x 240. The maximum is
 * the whole map (00:26FD: 32 x 32 sectors of 256 pixels); rooms are smaller,
 * and a view larger than its room shows the room centered. */
#define SHANTAE_VIEW_MIN_WIDTH 160
#define SHANTAE_VIEW_MAX_WIDTH 8192
#define SHANTAE_VIEW_MIN_HEIGHT 144
#define SHANTAE_VIEW_MAX_HEIGHT 8192
int shantae_view_width(void);
int shantae_view_height(void);
void shantae_set_view_size(int width, int height);
/* Adaptive: the view fills the window or screen and is at least the view
 * height tall (gb_custom_fill_size); the stored width is kept for when a fixed
 * size is chosen again. */
int shantae_view_adaptive(void);
void shantae_set_view_adaptive(int on);
/* The original 160 x 144 picture the view shows for menus, dialogue and
 * one-screen rooms: GB_CUSTOM_NATIVE_IN_VIEW (centered at the view's pixel
 * size), GB_CUSTOM_NATIVE_SCALING_MODE (the default: on its own, scaled like
 * the game without the view), a scaling mode 0-3, or
 * GB_CUSTOM_NATIVE_WHOLE_SCALE at `scale` screen pixels per game pixel
 * (gb_custom_view.h). */
#define SHANTAE_NATIVE_MAX_SCALE 64
int shantae_native_scaling(void);
int shantae_native_scale(void);
void shantae_set_native_scaling(int scaling, int scale);
/* Rooms smaller than the view: shown at the view's scale (OFF), zoomed until
 * they fill the window (FILL, the default) or as far as keeps every side that
 * fits whole (WHOLE). */
enum { SHANTAE_ROOM_ZOOM_OFF, SHANTAE_ROOM_ZOOM_FILL, SHANTAE_ROOM_ZOOM_WHOLE };
int shantae_room_zoom(void);
void shantae_set_room_zoom(int mode);
/* The bottom UI (the status bar, a dialogue box, a town's name panel): N view
 * pixels for each of its pixels (1 the default, as the world's), or AUTO, as
 * large on screen as in the original picture. Other than the world's, it is a
 * layer the window draws through the shader chain at its own scale, and the
 * world keeps above it. */
#define SHANTAE_UI_AUTO 0
#define SHANTAE_UI_MAX_SCALE 16
int shantae_ui_scale(void);
void shantae_set_ui_scale(int scale);
/* While a dialogue box larger than the status bar would cover the room's
 * floor, raise the world until it clears it (1, the default), or cover it as
 * the original does (0). */
int shantae_dialogue_lift(void);
void shantae_set_dialogue_lift(int on);
/* Pass a changed size or mode to a running expanded view (no-op when off). */
void shantae_view_apply_size(void);

/* Aspect presets. The aspect is not stored: a size shows the preset whose
 * width it matches at its height, or none (custom). */
typedef struct { const char *id, *label; int num, den; } ShantaeViewAspect;
int shantae_view_aspect_count(void);
const ShantaeViewAspect *shantae_view_aspect_info(int index);
int shantae_view_aspect_find(const char *id);
int shantae_view_aspect_of(int width, int height);
/* Size of preset `index` at *height (kept when the width limits allow). */
void shantae_view_aspect_size(int index, int *width, int *height);
/* The size a new height gives a width x height view: a preset keeps its shape
 * (16:9 at 144 is 256 x 144), at the height asked for (a width past the limits
 * is clamped, so 16:15 at 144 is the original 160 x 144); a custom size keeps
 * its width. */
void shantae_view_size_at_height(int width, int height, int new_height, int *out_width, int *out_height);
void shantae_set_view_height(int height);
/* Original height: the original screen's 144 rows, so the view keeps the
 * original's zoom and only adds to the sides. A preset keeps its shape; a
 * custom size takes the screen's (screen_w x screen_h; its own shape when that
 * is 0), even when it is already 144 tall: there its width is what keeps the
 * zoom from the original's (426 x 144 is shown no larger than 426 x 240 on a
 * 16:9 screen). The result gives itself again. */
void shantae_view_original_size(int width, int height, int screen_w, int screen_h, int *out_width,
                                int *out_height);
