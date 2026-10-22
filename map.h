#pragma once
/* The map screen (map.c): the world as sector cells and the area Shantae is
 * in as its terrain, the places that matter marked, explored as she goes,
 * and the time to nightfall. */
#include <stddef.h>
#include <stdint.h>
struct GBContext;

/* Settings (extras.c, shantae.ini): the map's button; the whole map from the
 * start rather than as she explores; enemies marked. */
int shantae_map_on(void);
void shantae_set_map_on(int on);
int shantae_map_whole(void);
void shantae_set_map_whole(int on);
int shantae_map_enemies(void);
void shantae_set_map_enemies(int on);
/* The size it is drawn at, as the HUD's (hud.h): the picture's own pixels by
 * default, so a shader preset that works per pixel draws it as the game. */
int shantae_map_size(void);
void shantae_set_map_size(int size);
/* "The Zombie Caravan stays": at its first stop, between the Ghost and Archer
 * Forests, every night, rather than moving on each night once the story has
 * it do so. */
int shantae_caravan_stays(void);
void shantae_set_caravan_stays(int on);
/* The CP $06 on CA9D at 12:4260, before the caravan's roll (extras.c):
 * returns the byte to compare with. */
uint8_t shantae_map_caravan_roll(struct GBContext *ctx, uint8_t orig);

/* game_on_init. */
void shantae_map_init(struct GBContext *ctx);
/* Once a frame while the game runs (game_on_frame): marks what the camera
 * shows as explored. */
void shantae_map_frame(struct GBContext *ctx);
/* The map's button was pressed: opens the map (the game is held) or closes
 * it. Returns whether it is open. */
int shantae_map_toggle(struct GBContext *ctx);
int shantae_map_is_open(void);
/* While open, each presented frame: the Game Boy buttons held (as
 * gb_platform_joypad_held) pan (the pad), zoom between the area and the
 * world (A), show the world page from a labyrinth (Select), and close (B,
 * Start). */
void shantae_map_input(struct GBContext *ctx, int buttons);
/* The map at width x height of its own pixels into out (0xAARRGGBB).
 * Returns 0 when there is nothing to draw. */
int shantae_map_draw(struct GBContext *ctx, uint32_t *out, int width, int height);

/* The save routines' ends (extras.c's [[imm_override]] sites in bank 4):
 * a file saved, loaded (or found empty: a new game), or erased. */
void shantae_map_file_saved(struct GBContext *ctx);
void shantae_map_file_loaded(struct GBContext *ctx, int ok);
void shantae_map_file_erased(struct GBContext *ctx);
/* After a state file loads (its map bytes may predate the map). */
void shantae_map_state_file_loaded(struct GBContext *ctx);

/* The day and night clock (C3E9): whether it is night, the seconds to the
 * next change, and whether the clock stands still (C3D5). */
void shantae_map_clock(const struct GBContext *ctx, int *night, int *seconds, int *stopped);

/* Debug (shantae_map): the areas found, cells explored, markers placed. */
void shantae_map_counts(struct GBContext *ctx, int *areas, int *cells, int *explored, int *markers);
/* Debug: everything found, as JSON written to `path`: each area (its room's
 * call, page, cell and size, and which of its cells are open, rock or none,
 * and explored), and each marker (its kind, area, world position, collected
 * flag and whether it is set). Returns 0 when it cannot write it. */
int shantae_map_describe(struct GBContext *ctx, const char *path);
/* Debug: the view as opening the map sets it, at `zoom` (0 cells, 1
 * terrain); or `page` centered at x,y (page pixels, 256 a sector) when page
 * >= 0. `whole` 0 or 1 shows only what is explored or everything, -1 as the
 * setting has it. */
void shantae_map_debug_view(struct GBContext *ctx, int zoom, int page, int x, int y, int whole);
