/*
 * launcher_options.c -- Shantae's options on the recomp-ui launcher's Mods page.
 *
 * Six built-in features: "GBA Enhanced mode" with a Colors choice,
 * "Expanded view" with an aspect ratio (or Adaptive, filling the screen), a
 * width and height in pixels and the scale of the original view it falls back
 * to, "Remove slowdown", "Reduce input lag", "Smoother movement" with what
 * a whip does on the move, the air speed, the crawl and the transformations,
 * and "Easier dancing" with the dance steps and invincibility after
 * transforming.
 * Values are stored in shantae.ini through the accessors in extras.c and take
 * effect when the game boots, so there is nothing to stage or commit.
 */
#include "game_extras.h"
#include "recomp_launcher.h"
#include "expanded_view.h"
#include "moveset.h"
#include "forms.h"
#include "dance.h"
#include "gb_custom_view.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int shantae_gba_enhanced(void);
void shantae_set_gba_enhanced(int on);
int shantae_original_colors(void);
void shantae_set_original_colors(int on);
int shantae_remove_slowdown(void);
void shantae_set_remove_slowdown(int on);
int shantae_reduce_input_lag(void);
void shantae_set_reduce_input_lag(int on);

#define PACKAGE_ID "shantae"
#define FEATURE_ID "gba_enhanced"
#define FEATURE_SLOWDOWN "remove_slowdown"
#define FEATURE_INPUT_LAG "reduce_input_lag"
#define OPTION_COLORS "colors"
#define FEATURE_VIEW "expanded_view"
#define OPTION_ASPECT "aspect"
#define OPTION_WIDTH "width"
#define OPTION_HEIGHT "height"
#define ASPECT_CUSTOM "custom"
#define ASPECT_ADAPTIVE "adaptive"
#define OPTION_NATIVE "original_view"
#define OPTION_ROOM_ZOOM "room_zoom"
static const char *const room_zooms[][2] = {
    {"off", "Off"},
    {"fill", "Fill the screen"},
    {"whole", "Whole room"},
};
#define FEATURE_MOVES "smooth_moves"
#define OPTION_WHIP "whip"
#define OPTION_AIR "air_speed"
#define OPTION_CRAWL "crawl"
#define OPTION_FORMS "transformations"
#define FEATURE_DANCE "easy_dance"
#define OPTION_STEPS "steps"
#define OPTION_INVINCIBLE "invincible"
/* In the order of SHANTAE_WHIP_*. */
static const char *const whip_modes[][2] = {
    {"original", "Original: stop to whip"},
    {"slide", "Slide: whip and keep moving"},
    {"cancel", "Cancel: B and a direction runs at once"},
};
static const char *const air_speeds[][2] = {
    {"original", "Original: set when she leaves the ground"},
    {"b", "Hold B for running speed"},
};
static const char *const crawls[][2] = {
    {"original", "Original"},
    {"b", "Hold B for walking speed"},
};
static const char *const forms[][2] = {
    {"original", "Original"},
    {"smoother", "Like Shantae"},
};
static const char *const dance_steps[][2] = {
    {"rhythm", "Original: one step a beat, in rhythm"},
    {"quick", "Quick: press the steps at any speed"},
};
static const char *const invincibles[][2] = {
    {"original", "Original"},
    {"blink", "Blink and stay safe for two seconds"},
};

static char s_error[256];

/* Original view scaling choices: the Scaling Mode, the four modes, whole
 * scales up to the largest that fits the main screen (or the chosen one), and
 * inside the view. */
static const char *const native_modes[][2] = {
    {"pixel_perfect", "Pixel Perfect"},
    {"aspect_fit", "Aspect Fit"},
    {"aspect_fill", "Aspect Fill"},
    {"stretch", "Stretch"},
};
static int native_most_scale(void) {
    SDL_DisplayMode mode;
    int most = 8;
    if (SDL_WasInit(SDL_INIT_VIDEO) && SDL_GetDesktopDisplayMode(0, &mode) == 0)
        most = mode.w / 160 < mode.h / 144 ? mode.w / 160 : mode.h / 144;
    if (shantae_native_scaling() == GB_CUSTOM_NATIVE_WHOLE_SCALE && shantae_native_scale() > most)
        most = shantae_native_scale();
    return most < 1 ? 1 : most;
}
static void native_value(char *out, size_t size) {
    const int scaling = shantae_native_scaling();
    if (scaling == GB_CUSTOM_NATIVE_SCALING_MODE) snprintf(out, size, "scaling_mode");
    else if (scaling == GB_CUSTOM_NATIVE_IN_VIEW) snprintf(out, size, "in_view");
    else if (scaling == GB_CUSTOM_NATIVE_WHOLE_SCALE) snprintf(out, size, "%dx", shantae_native_scale());
    else snprintf(out, size, "%s", native_modes[scaling][0]);
}

/* "16:9", or the size's own ratio when it matches no preset. */
static void describe_aspect(char *out, size_t size, int width, int height) {
    const ShantaeViewAspect *a = shantae_view_aspect_info(shantae_view_aspect_of(width, height));
    if (a) snprintf(out, size, "%s", a->label);
    else snprintf(out, size, "custom %.2f:1", (double)width / height);
}

/* The adaptive view fullscreen on the main screen with Pixel Perfect scaling
 * (the default). 0 when the screen size is unknown. */
static int adaptive_preview(int *width, int *height, int *screen_w, int *screen_h) {
    SDL_DisplayMode mode;
    if (!SDL_WasInit(SDL_INIT_VIDEO) || SDL_GetDesktopDisplayMode(0, &mode) != 0) return 0;
    *screen_w = mode.w;
    *screen_h = mode.h;
    gb_custom_fill_size(mode.w, mode.h, shantae_view_height(), 1, width, height);
    return 1;
}

static int is_feature(const char *package_id, const char *feature_id) {
    return package_id && feature_id &&
           strcmp(package_id, PACKAGE_ID) == 0 && strcmp(feature_id, FEATURE_ID) == 0;
}
static int is_view(const char *package_id, const char *feature_id) {
    return package_id && feature_id && strcmp(package_id, PACKAGE_ID) == 0 &&
           strcmp(feature_id, FEATURE_VIEW) == 0;
}
static int is_slowdown(const char *package_id, const char *feature_id) {
    return package_id && feature_id && strcmp(package_id, PACKAGE_ID) == 0 &&
           strcmp(feature_id, FEATURE_SLOWDOWN) == 0;
}
static int is_input_lag(const char *package_id, const char *feature_id) {
    return package_id && feature_id && strcmp(package_id, PACKAGE_ID) == 0 &&
           strcmp(feature_id, FEATURE_INPUT_LAG) == 0;
}
static int is_moves(const char *package_id, const char *feature_id) {
    return package_id && feature_id && strcmp(package_id, PACKAGE_ID) == 0 &&
           strcmp(feature_id, FEATURE_MOVES) == 0;
}
static int is_dance(const char *package_id, const char *feature_id) {
    return package_id && feature_id && strcmp(package_id, PACKAGE_ID) == 0 &&
           strcmp(feature_id, FEATURE_DANCE) == 0;
}

/* ---- package surface: a single built-in package the Features view owns ---- */

static int package_count(void *ctx) {
    (void)ctx;
    return 1;
}

static int package_get(void *ctx, int index, RecompLauncherCModPackage *out) {
    (void)ctx;
    if (index != 0 || !out) return 0;
    memset(out, 0, sizeof(*out));
    snprintf(out->id, sizeof(out->id), "%s", PACKAGE_ID);
    snprintf(out->version, sizeof(out->version), "1.0");
    snprintf(out->name, sizeof(out->name), "Shantae options");
    snprintf(out->description, sizeof(out->description),
             "Built-in options of the Shantae recompilation.");
    snprintf(out->status, sizeof(out->status), "Built in");
    out->enabled = 1;
    return 1;
}

/* ---- feature surface ---- */

static int feature_count(void *ctx) {
    (void)ctx;
    return 6;
}

static int feature_get(void *ctx, int index, RecompLauncherCModFeature *out) {
    (void)ctx;
    if (index < 0 || index > 5 || !out) return 0;
    memset(out, 0, sizeof(*out));
    if (index == 5) {
        snprintf(out->id, sizeof(out->id), "%s", FEATURE_DANCE);
        snprintf(out->package_id, sizeof(out->package_id), "%s", PACKAGE_ID);
        snprintf(out->package_version, sizeof(out->package_version), "1.0");
        snprintf(out->package_name, sizeof(out->package_name), "Shantae options");
        snprintf(out->name, sizeof(out->name), "Easier dancing");
        snprintf(out->description, sizeof(out->description),
                 "Dance like entering a code. After Select, each press is a step: press them as fast "
                 "as you like, a wrong button is skipped and the next right one carries on, and Down "
                 "starts over. The transformation, healing or warp begins on the last step. Once an "
                 "animal transformation's silhouette ends, and after turning back, she blinks and "
                 "cannot be hurt for two seconds, as after a hit. Off plays as the original.");
        snprintf(out->group, sizeof(out->group), "Gameplay");
        if (shantae_easy_dance())
            snprintf(out->status, sizeof(out->status), "On: %s steps, %s after transforming",
                     shantae_quick_steps() ? "quick" : "rhythm",
                     shantae_transform_invincible() ? "invincible" : "the original");
        else
            snprintf(out->status, sizeof(out->status), "Off: the original dance");
        out->enabled = shantae_easy_dance();
        out->option_count = 2;
        return 1;
    }
    if (index == 4) {
        static const char *const whips[] = {"stops to whip", "whip slide", "whip cancel"};
        snprintf(out->id, sizeof(out->id), "%s", FEATURE_MOVES);
        snprintf(out->package_id, sizeof(out->package_id), "%s", PACKAGE_ID);
        snprintf(out->package_version, sizeof(out->package_version), "1.0");
        snprintf(out->package_name, sizeof(out->package_name), "Shantae options");
        snprintf(out->name, sizeof(out->name), "Smoother movement");
        snprintf(out->description, sizeof(out->description),
                 "Shantae keeps her momentum. Holding B runs at once, on the ground and in the "
                 "air: B with a direction whips while she runs (or just runs, with Cancel), a "
                 "jump moves at running speed while B is held, B and Select work the moment she "
                 "lands, a landing whip no longer stops her, and B crawls at walking speed. With "
                 "Transformations the monkey and tinkerbat move the same way, the harpy keeps "
                 "her speed and flaps through her talons, and the tinkerbat squeezes through "
                 "gaps the monkey fits. Off plays as the original.");
        snprintf(out->group, sizeof(out->group), "Gameplay");
        if (shantae_smooth_moves())
            snprintf(out->status, sizeof(out->status), "On: %s, %s air speed, %s crawl, %s",
                     whips[shantae_whip_moving()], shantae_air_speed_b() ? "B for" : "original",
                     shantae_fast_crawl() ? "B for a faster" : "original",
                     shantae_smooth_forms() ? "transformations too" : "Shantae only");
        else
            snprintf(out->status, sizeof(out->status), "Off: the original moves");
        out->enabled = shantae_smooth_moves();
        out->option_count = 4;
        return 1;
    }
    if (index == 3) {
        snprintf(out->id, sizeof(out->id), "%s", FEATURE_INPUT_LAG);
        snprintf(out->package_id, sizeof(out->package_id), "%s", PACKAGE_ID);
        snprintf(out->package_version, sizeof(out->package_version), "1.0");
        snprintf(out->package_name, sizeof(out->package_name), "Shantae options");
        snprintf(out->name, sizeof(out->name), "Reduce input lag");
        snprintf(out->description, sizeof(out->description),
                 "Show sprites one frame sooner, and start Shantae's moves on the frame they are "
                 "pressed. The game shows each frame's sprites a frame late while it uploads their "
                 "graphics, and a jump or walk it starts only moves on the next frame. With this "
                 "and the Esc menu's Preemptive Frames (1 by default), a press shows on the next "
                 "picture. Off keeps the original timing.");
        snprintf(out->group, sizeof(out->group), "Gameplay");
        snprintf(out->status, sizeof(out->status), "%s",
                 shantae_reduce_input_lag() ? "On: sprites and moves one frame sooner"
                                            : "Off: original timing");
        out->enabled = shantae_reduce_input_lag();
        return 1;
    }
    if (index == 2) {
        snprintf(out->id, sizeof(out->id), "%s", FEATURE_SLOWDOWN);
        snprintf(out->package_id, sizeof(out->package_id), "%s", PACKAGE_ID);
        snprintf(out->package_version, sizeof(out->package_version), "1.0");
        snprintf(out->package_name, sizeof(out->package_name), "Shantae options");
        snprintf(out->name, sizeof(out->name), "Remove slowdown");
        snprintf(out->description, sizeof(out->description),
                 "Give the game the extra processor time a busy frame needs, so crowded scenes "
                 "(and the wider expanded views, which keep more enemies active) no longer drop "
                 "to half speed. Music and screen timing stay as on the hardware. Off plays with "
                 "the original slowdown.");
        snprintf(out->group, sizeof(out->group), "Gameplay");
        snprintf(out->status, sizeof(out->status), "%s",
                 shantae_remove_slowdown() ? "On: full speed" : "Off: original slowdown");
        out->enabled = shantae_remove_slowdown();
        return 1;
    }
    if (index == 1) {
        int width = shantae_view_width(), height = shantae_view_height();
        int adaptive = shantae_view_adaptive(), pw, ph, sw, sh;
        char aspect[64];
        describe_aspect(aspect, sizeof(aspect), width, height);
        snprintf(out->id, sizeof(out->id), "%s", FEATURE_VIEW);
        snprintf(out->package_id, sizeof(out->package_id), "%s", PACKAGE_ID);
        snprintf(out->package_version, sizeof(out->package_version), "1.0");
        snprintf(out->package_name, sizeof(out->package_name), "Shantae options");
        if (adaptive)
            snprintf(out->name, sizeof(out->name), "Expanded view (adaptive)");
        else
            snprintf(out->name, sizeof(out->name), "Expanded view (%d x %d)", width, height);
        snprintf(out->description, sizeof(out->description),
                 "Show more of the world above, below, and on both sides, with the HUD at the "
                 "bottom. Choose Adaptive to fill the whole screen or window, or an aspect ratio "
                 "and then a width and height a pixel at a time. Rooms smaller than the view zoom "
                 "in to fill the screen (Room zoom). Experimental; menus and unsupported scenes "
                 "use the original view.");
        snprintf(out->group, sizeof(out->group), "Display");
        if (!shantae_expanded_view())
            snprintf(out->status, sizeof(out->status), "Off: original 160 x 144");
        else if (adaptive && adaptive_preview(&pw, &ph, &sw, &sh))
            snprintf(out->status, sizeof(out->status),
                     "On: adaptive, at least %d tall (%d x %d fullscreen on this %d x %d screen)",
                     height, pw, ph, sw, sh);
        else if (adaptive)
            snprintf(out->status, sizeof(out->status), "On: adaptive, at least %d tall", height);
        else
            snprintf(out->status, sizeof(out->status), "On: %d x %d world view (%s)", width, height, aspect);
        out->enabled = shantae_expanded_view();
        out->option_count = 5;
        return 1;
    }
    snprintf(out->id, sizeof(out->id), "%s", FEATURE_ID);
    snprintf(out->package_id, sizeof(out->package_id), "%s", PACKAGE_ID);
    snprintf(out->package_version, sizeof(out->package_version), "1.0");
    snprintf(out->package_name, sizeof(out->package_name), "Shantae options");
    snprintf(out->name, sizeof(out->name), "GBA Enhanced mode");
    snprintf(out->description, sizeof(out->description),
             "Boot as a Game Boy Advance, as if the cartridge were played on one: the title "
             "screen shows \"GBA Enhanced!\" and the Bandit Town secret that teaches the "
             "Tinkerbat dance becomes available. Turn off to play as a plain Game Boy Color.");
    snprintf(out->group, sizeof(out->group), "Game Boy Advance");
    snprintf(out->status, sizeof(out->status), "%s",
             shantae_gba_enhanced()
                 ? (shantae_original_colors() ? "On: GBA extras with the original GBC colors"
                                              : "On: GBA extras with the brightened GBA colors")
                 : "Off: plain Game Boy Color");
    out->enabled = shantae_gba_enhanced();
    out->option_count = 1;
    return 1;
}

static int view_option_get(int index, RecompLauncherCModOption *out) {
    int width = shantae_view_width(), height = shantae_view_height();
    int adaptive = shantae_view_adaptive(), pw, ph, sw, sh;
    int preview = adaptive && adaptive_preview(&pw, &ph, &sw, &sh);
    memset(out, 0, sizeof(*out));
    snprintf(out->group, sizeof(out->group), "View size");
    out->disabled = !shantae_expanded_view();   /* size only matters when on */
    if (index == 0) {
        const ShantaeViewAspect *a = shantae_view_aspect_info(shantae_view_aspect_of(width, height));
        snprintf(out->id, sizeof(out->id), OPTION_ASPECT);
        snprintf(out->label, sizeof(out->label), "Aspect ratio");
        snprintf(out->description, sizeof(out->description),
                 "Adaptive fills the whole screen or window, whatever its shape, and follows it "
                 "when the window is resized. A ratio sets the width for that shape at the "
                 "current height; NES 16:15 at 240 tall is 256 x 240. Changing the width or "
                 "height afterwards makes the shape custom.");
        snprintf(out->value, sizeof(out->value), "%s",
                 adaptive ? ASPECT_ADAPTIVE : a ? a->id : ASPECT_CUSTOM);
        snprintf(out->default_value, sizeof(out->default_value), "16:15");
        out->type = RECOMP_MOD_OPTION_CHOICE;
        out->choice_count = shantae_view_aspect_count() + 2;
    } else if (index == 1) {
        snprintf(out->id, sizeof(out->id), OPTION_WIDTH);
        snprintf(out->label, sizeof(out->label), "Width (pixels)");
        if (preview)
            snprintf(out->description, sizeof(out->description),
                     "Adaptive: the width follows the screen or window. Fullscreen on this "
                     "%d x %d screen it is %d. Choose Custom to set it yourself.", sw, sh, pw);
        else if (adaptive)
            snprintf(out->description, sizeof(out->description),
                     "Adaptive: the width follows the screen or window. Choose Custom to set it "
                     "yourself.");
        else
            snprintf(out->description, sizeof(out->description),
                     "Game pixels across, %d to %d. The original screen is 160 wide.",
                     SHANTAE_VIEW_MIN_WIDTH, SHANTAE_VIEW_MAX_WIDTH);
        snprintf(out->value, sizeof(out->value), "%d", preview ? pw : width);
        snprintf(out->default_value, sizeof(out->default_value), "256");
        out->type = RECOMP_MOD_OPTION_INTEGER;
        out->min_value = SHANTAE_VIEW_MIN_WIDTH;
        out->max_value = SHANTAE_VIEW_MAX_WIDTH;
        out->step = 1;
        out->disabled |= adaptive;
    } else if (index == 2) {
        snprintf(out->id, sizeof(out->id), OPTION_HEIGHT);
        snprintf(out->label, sizeof(out->label), adaptive ? "Least height (pixels)" : "Height (pixels)");
        if (adaptive)
            snprintf(out->description, sizeof(out->description),
                     "Game pixels tall at the least, %d to %d, including the 16-pixel status bar. "
                     "With Pixel Perfect scaling the view takes the largest whole-pixel scale that "
                     "shows this much and fills the rest of the screen: 240 on a 1080p screen "
                     "shows 480 x 270, on 1440p 426 x 240. A screen shorter than this shows one "
                     "game pixel per screen pixel. Other scaling modes keep this height exactly "
                     "and match the screen's shape.",
                     SHANTAE_VIEW_MIN_HEIGHT, SHANTAE_VIEW_MAX_HEIGHT);
        else
            snprintf(out->description, sizeof(out->description),
                     "Game pixels tall, %d to %d, including the 16-pixel status bar. The original "
                     "screen is 144 tall. For sharp pixels with pixel-perfect scaling, pick a height "
                     "that divides your screen height evenly: 240 for 720p, 1440p and 4K; 270 for "
                     "1080p and 4K; 360 for all four.",
                     SHANTAE_VIEW_MIN_HEIGHT, SHANTAE_VIEW_MAX_HEIGHT);
        snprintf(out->value, sizeof(out->value), "%d", height);
        snprintf(out->default_value, sizeof(out->default_value), "240");
        out->type = RECOMP_MOD_OPTION_INTEGER;
        out->min_value = SHANTAE_VIEW_MIN_HEIGHT;
        out->max_value = SHANTAE_VIEW_MAX_HEIGHT;
        out->step = 1;
    } else if (index == 3) {
        snprintf(out->id, sizeof(out->id), OPTION_NATIVE);
        snprintf(out->group, sizeof(out->group), "Original view");
        snprintf(out->label, sizeof(out->label), "Original view scale");
        snprintf(out->description, sizeof(out->description),
                 "Menus, the inventory, dialogue and rooms of a single screen show the original "
                 "160 x 144 picture. By default it is presented on its own, through the shader, "
                 "and scaled to the screen like the game without the expanded view (Pixel "
                 "Perfect gives the largest whole scale: 7x on a 1080p screen). Pick another "
                 "scaling or a whole scale for it alone, or Inside the view to keep it at the "
                 "view's pixel size, centered in black.");
        native_value(out->value, sizeof(out->value));
        snprintf(out->default_value, sizeof(out->default_value), "scaling_mode");
        out->type = RECOMP_MOD_OPTION_CHOICE;
        out->choice_count = 1 + 4 + native_most_scale() + 1;
    } else if (index == 4) {
        snprintf(out->id, sizeof(out->id), OPTION_ROOM_ZOOM);
        snprintf(out->group, sizeof(out->group), "Original view");
        snprintf(out->label, sizeof(out->label), "Room zoom");
        snprintf(out->description, sizeof(out->description),
                 "Rooms smaller than the view, from a corridor a screen tall to an arena the "
                 "camera locks to, would sit in a field of black at the view's pixel size. Fill "
                 "zooms in until the room fills the screen (whole steps with Pixel Perfect), "
                 "scrolling along the side that no longer fits; Whole room zooms only as far as "
                 "keeps every side that fits in view. Never closer than the original screen. "
                 "Zooms snap while the screen is black between rooms and ease when a room's "
                 "limits change in play.");
        snprintf(out->value, sizeof(out->value), "%s", room_zooms[shantae_room_zoom()][0]);
        snprintf(out->default_value, sizeof(out->default_value), "fill");
        out->type = RECOMP_MOD_OPTION_CHOICE;
        out->choice_count = 3;
    } else {
        return 0;
    }
    return 1;
}

static int native_choice_get(int index, RecompLauncherCModChoice *out) {
    const int most = native_most_scale();
    memset(out, 0, sizeof(*out));
    if (index == 0) {
        snprintf(out->value, sizeof(out->value), "scaling_mode");
        snprintf(out->label, sizeof(out->label), "Same as Scaling Mode");
    } else if (index <= 4) {
        snprintf(out->value, sizeof(out->value), "%s", native_modes[index - 1][0]);
        snprintf(out->label, sizeof(out->label), "%s", native_modes[index - 1][1]);
    } else if (index <= 4 + most) {
        const int n = index - 4;
        snprintf(out->value, sizeof(out->value), "%dx", n);
        snprintf(out->label, sizeof(out->label), "%dx (%d x %d)", n, 160 * n, 144 * n);
    } else if (index == 5 + most) {
        snprintf(out->value, sizeof(out->value), "in_view");
        snprintf(out->label, sizeof(out->label), "Inside the view");
    } else {
        return 0;
    }
    return 1;
}

static int native_set_option(const char *value) {
    const int scale = shantae_native_scale();
    int n;
    char x;
    if (strcmp(value, "scaling_mode") == 0) {
        shantae_set_native_scaling(GB_CUSTOM_NATIVE_SCALING_MODE, scale);
        return 1;
    }
    if (strcmp(value, "in_view") == 0) {
        shantae_set_native_scaling(GB_CUSTOM_NATIVE_IN_VIEW, scale);
        return 1;
    }
    for (int i = 0; i < 4; ++i) {
        if (strcmp(value, native_modes[i][0]) == 0) {
            shantae_set_native_scaling(i, scale);
            return 1;
        }
    }
    if (sscanf(value, "%d%c", &n, &x) == 2 && x == 'x' && n >= 1 && n <= SHANTAE_NATIVE_MAX_SCALE) {
        shantae_set_native_scaling(GB_CUSTOM_NATIVE_WHOLE_SCALE, n);
        return 1;
    }
    snprintf(s_error, sizeof(s_error), "Unknown original view scale '%s'.", value);
    return 0;
}

/* Adaptive first, then the presets, then Custom. */
static int view_choice_get(int index, RecompLauncherCModChoice *out) {
    const ShantaeViewAspect *a = shantae_view_aspect_info(index - 1);
    int width = shantae_view_width(), height = shantae_view_height(), sw, sh;
    memset(out, 0, sizeof(*out));
    if (index == 0) {
        snprintf(out->value, sizeof(out->value), ASPECT_ADAPTIVE);
        snprintf(out->label, sizeof(out->label), "Adaptive (fill the screen)");
    } else if (a) {
        snprintf(out->value, sizeof(out->value), "%s", a->id);
        snprintf(out->label, sizeof(out->label), "%s", a->label);
    } else if (index == shantae_view_aspect_count() + 1) {
        /* From Adaptive, Custom starts at the size Adaptive shows here. */
        if (shantae_view_adaptive()) adaptive_preview(&width, &height, &sw, &sh);
        snprintf(out->value, sizeof(out->value), ASPECT_CUSTOM);
        snprintf(out->label, sizeof(out->label), "Custom (%.2f:1)", (double)width / height);
    } else {
        return 0;
    }
    return 1;
}

static int view_set_option(const char *option_id, const char *value) {
    int width = shantae_view_width(), height = shantae_view_height(), sw, sh;
    if (strcmp(option_id, OPTION_NATIVE) == 0) return native_set_option(value);
    if (strcmp(option_id, OPTION_ROOM_ZOOM) == 0) {
        for (int i = 0; i < 3; ++i) {
            if (strcmp(value, room_zooms[i][0]) == 0) {
                shantae_set_room_zoom(i);
                return 1;
            }
        }
        snprintf(s_error, sizeof(s_error), "Unknown room zoom '%s'.", value);
        return 0;
    }
    if (strcmp(option_id, OPTION_ASPECT) == 0) {
        if (strcmp(value, ASPECT_ADAPTIVE) == 0) {
            shantae_set_view_adaptive(1);
            return 1;
        }
        if (strcmp(value, ASPECT_CUSTOM) == 0) {
            if (shantae_view_adaptive() && adaptive_preview(&width, &height, &sw, &sh))
                shantae_set_view_size(width, height);
            shantae_set_view_adaptive(0);
            return 1;
        }
        int index = shantae_view_aspect_find(value);
        if (index < 0) {
            snprintf(s_error, sizeof(s_error), "Unknown aspect ratio '%s'.", value);
            return 0;
        }
        shantae_view_aspect_size(index, &width, &height);
        shantae_set_view_size(width, height);
        shantae_set_view_adaptive(0);
        return 1;
    }
    char *end;
    long number = strtol(value, &end, 10);
    if (end == value || *end) {
        snprintf(s_error, sizeof(s_error), "'%s' is not a whole number of pixels.", value);
        return 0;
    }
    if (strcmp(option_id, OPTION_WIDTH) == 0) {
        shantae_set_view_size((int)number, height);
        shantae_set_view_adaptive(0);
    } else if (strcmp(option_id, OPTION_HEIGHT) == 0) {   /* the least height when adaptive */
        shantae_set_view_size(width, (int)number);
    } else {
        return 0;
    }
    return 1;
}

static int moves_option_get(int index, RecompLauncherCModOption *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->group, sizeof(out->group), "Moves");
    out->type = RECOMP_MOD_OPTION_CHOICE;
    out->disabled = !shantae_smooth_moves();
    if (index == 0) {
        snprintf(out->id, sizeof(out->id), OPTION_WHIP);
        snprintf(out->label, sizeof(out->label), "Whip on the move");
        snprintf(out->description, sizeof(out->description),
                 "What B does while a direction is held, standing or crouched. Slide: the whip "
                 "comes out and hits as usual while she keeps moving, at running speed with B "
                 "held, and a whip begun in the air carries on along the ground. Cancel: she "
                 "runs (or crawls) at once without whipping, and a whip in progress ends when B "
                 "and a direction are held; stand still to whip. Original: she stops for the "
                 "whole whip.");
        snprintf(out->value, sizeof(out->value), "%s", whip_modes[shantae_whip_moving()][0]);
        snprintf(out->default_value, sizeof(out->default_value), "slide");
        out->choice_count = 3;
    } else if (index == 1) {
        snprintf(out->id, sizeof(out->id), OPTION_AIR);
        snprintf(out->label, sizeof(out->label), "Air speed");
        snprintf(out->description, sizeof(out->description),
                 "In the original a jump keeps the speed she left the ground with: running "
                 "speed out of a run, walking speed otherwise, whatever is held. With B, she "
                 "moves at running speed while B is held and at walking speed when it is "
                 "released, jumping, falling or whipping in the air.");
        snprintf(out->value, sizeof(out->value), "%s", air_speeds[shantae_air_speed_b()][0]);
        snprintf(out->default_value, sizeof(out->default_value), "b");
        out->choice_count = 2;
    } else if (index == 2) {
        snprintf(out->id, sizeof(out->id), OPTION_CRAWL);
        snprintf(out->label, sizeof(out->label), "Crawl");
        snprintf(out->description, sizeof(out->description),
                 "The crawl moves half a pixel a frame, and holding B makes her stand up and "
                 "run. With B, she stays down and crawls at walking speed, twice as fast, for "
                 "as long as B is held.");
        snprintf(out->value, sizeof(out->value), "%s", crawls[shantae_fast_crawl()][0]);
        snprintf(out->default_value, sizeof(out->default_value), "b");
        out->choice_count = 2;
    } else if (index == 3) {
        snprintf(out->id, sizeof(out->id), OPTION_FORMS);
        snprintf(out->label, sizeof(out->label), "Transformations");
        snprintf(out->description, sizeof(out->description),
                 "Like Shantae: the monkey's claw and the tinkerbat's sword follow Whip on the "
                 "move, their jumps follow Air speed, and B runs at once. The harpy keeps her "
                 "speed when her talons end instead of starting her run again, and A flaps "
                 "during them. The tinkerbat "
                 "squeezes to the monkey's height where only that fits, so she can climb or walk "
                 "into a gap the monkey can. Original: they move as in the game.");
        snprintf(out->value, sizeof(out->value), "%s", forms[shantae_smooth_forms()][0]);
        snprintf(out->default_value, sizeof(out->default_value), "smoother");
        out->choice_count = 2;
    } else {
        return 0;
    }
    return 1;
}

static int moves_choice_get(const char *option_id, int index, RecompLauncherCModChoice *out) {
    const char *const (*choices)[2] = !strcmp(option_id, OPTION_WHIP) ? whip_modes
                                    : !strcmp(option_id, OPTION_AIR) ? air_speeds
                                    : !strcmp(option_id, OPTION_CRAWL) ? crawls
                                    : !strcmp(option_id, OPTION_FORMS) ? forms : NULL;
    if (!choices || index < 0 || index >= (choices == whip_modes ? 3 : 2)) return 0;
    memset(out, 0, sizeof(*out));
    snprintf(out->value, sizeof(out->value), "%s", choices[index][0]);
    snprintf(out->label, sizeof(out->label), "%s", choices[index][1]);
    return 1;
}

static int moves_set_option(const char *option_id, const char *value) {
    if (!strcmp(option_id, OPTION_WHIP)) {
        for (int i = 0; i < 3; ++i) {
            if (!strcmp(value, whip_modes[i][0])) {
                shantae_set_whip_moving(i);
                return 1;
            }
        }
    } else if (!strcmp(option_id, OPTION_AIR) && (!strcmp(value, "b") || !strcmp(value, "original"))) {
        shantae_set_air_speed_b(!strcmp(value, "b"));
        return 1;
    } else if (!strcmp(option_id, OPTION_CRAWL) && (!strcmp(value, "b") || !strcmp(value, "original"))) {
        shantae_set_fast_crawl(!strcmp(value, "b"));
        return 1;
    } else if (!strcmp(option_id, OPTION_FORMS) && (!strcmp(value, "smoother") || !strcmp(value, "original"))) {
        shantae_set_smooth_forms(!strcmp(value, "smoother"));
        return 1;
    }
    snprintf(s_error, sizeof(s_error), "Unknown setting '%s' for '%s'.", value, option_id);
    return 0;
}

static int dance_option_get(int index, RecompLauncherCModOption *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->group, sizeof(out->group), "Dancing");
    out->type = RECOMP_MOD_OPTION_CHOICE;
    out->disabled = !shantae_easy_dance();
    out->choice_count = 2;
    if (index == 0) {
        snprintf(out->id, sizeof(out->id), OPTION_STEPS);
        snprintf(out->label, sizeof(out->label), "Dance steps");
        snprintf(out->description, sizeof(out->description),
                 "The original takes one step every eight beats, in rhythm: a beat with no press or "
                 "two presses breaks the dance, and it begins a beat after the last step. Quick: "
                 "after Select every press is a step at once. A press that goes on to no dance you "
                 "know is skipped, and Down starts over (every dance begins with it).");
        snprintf(out->value, sizeof(out->value), "%s", dance_steps[shantae_quick_steps()][0]);
        snprintf(out->default_value, sizeof(out->default_value), "quick");
    } else if (index == 1) {
        snprintf(out->id, sizeof(out->id), OPTION_INVINCIBLE);
        snprintf(out->label, sizeof(out->label), "After transforming");
        snprintf(out->description, sizeof(out->description),
                 "In the original she is safe only until the new form's silhouette ends, or until "
                 "she can move after turning back, with nothing to show it, and an enemy nearby can "
                 "hit her at once. With Blink she then flashes and cannot be hurt for two more "
                 "seconds, the same as after a hit. Healing and warps are unchanged.");
        snprintf(out->value, sizeof(out->value), "%s", invincibles[shantae_transform_invincible()][0]);
        snprintf(out->default_value, sizeof(out->default_value), "blink");
    } else {
        return 0;
    }
    return 1;
}

static int dance_choice_get(const char *option_id, int index, RecompLauncherCModChoice *out) {
    const char *const (*choices)[2] = !strcmp(option_id, OPTION_STEPS) ? dance_steps
                                    : !strcmp(option_id, OPTION_INVINCIBLE) ? invincibles : NULL;
    if (!choices || index < 0 || index >= 2) return 0;
    memset(out, 0, sizeof(*out));
    snprintf(out->value, sizeof(out->value), "%s", choices[index][0]);
    snprintf(out->label, sizeof(out->label), "%s", choices[index][1]);
    return 1;
}

static int dance_set_option(const char *option_id, const char *value) {
    if (!strcmp(option_id, OPTION_STEPS) && (!strcmp(value, "quick") || !strcmp(value, "rhythm"))) {
        shantae_set_quick_steps(!strcmp(value, "quick"));
        return 1;
    }
    if (!strcmp(option_id, OPTION_INVINCIBLE) && (!strcmp(value, "blink") || !strcmp(value, "original"))) {
        shantae_set_transform_invincible(!strcmp(value, "blink"));
        return 1;
    }
    snprintf(s_error, sizeof(s_error), "Unknown setting '%s' for '%s'.", value, option_id);
    return 0;
}

static int feature_option_get(void *ctx, const char *package_id, const char *feature_id,
                              int index, RecompLauncherCModOption *out) {
    (void)ctx;
    if (is_view(package_id, feature_id) && out) return view_option_get(index, out);
    if (is_moves(package_id, feature_id) && out) return moves_option_get(index, out);
    if (is_dance(package_id, feature_id) && out) return dance_option_get(index, out);
    if (!is_feature(package_id, feature_id) || index != 0 || !out) return 0;
    memset(out, 0, sizeof(*out));
    snprintf(out->id, sizeof(out->id), OPTION_COLORS);
    snprintf(out->label, sizeof(out->label), "Colors");
    snprintf(out->description, sizeof(out->description),
             "On a GBA the game swaps in brighter palettes made for the original unlit GBA "
             "screen. Original GBC colors keeps the Game Boy Color palettes while every "
             "other GBA extra stays on.");
    snprintf(out->value, sizeof(out->value), "%s", shantae_original_colors() ? "gbc" : "gba");
    snprintf(out->default_value, sizeof(out->default_value), "gbc");
    out->type = RECOMP_MOD_OPTION_CHOICE;
    out->choice_count = 2;
    out->disabled = !shantae_gba_enhanced();   /* colors only differ in GBA mode */
    return 1;
}

static int feature_choice_get(void *ctx, const char *package_id, const char *feature_id,
                              const char *option_id, int index, RecompLauncherCModChoice *out) {
    (void)ctx;
    if (is_moves(package_id, feature_id) && option_id && out) return moves_choice_get(option_id, index, out);
    if (is_dance(package_id, feature_id) && option_id && out) return dance_choice_get(option_id, index, out);
    if (is_view(package_id, feature_id) && option_id && strcmp(option_id, OPTION_ASPECT) == 0 && out)
        return view_choice_get(index, out);
    if (is_view(package_id, feature_id) && option_id && strcmp(option_id, OPTION_NATIVE) == 0 && out)
        return native_choice_get(index, out);
    if (is_view(package_id, feature_id) && option_id && strcmp(option_id, OPTION_ROOM_ZOOM) == 0 && out) {
        if (index < 0 || index >= 3) return 0;
        memset(out, 0, sizeof(*out));
        snprintf(out->value, sizeof(out->value), "%s", room_zooms[index][0]);
        snprintf(out->label, sizeof(out->label), "%s", room_zooms[index][1]);
        return 1;
    }
    if (!is_feature(package_id, feature_id) || !option_id ||
        strcmp(option_id, OPTION_COLORS) != 0 || !out) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (index == 0) {
        snprintf(out->value, sizeof(out->value), "gbc");
        snprintf(out->label, sizeof(out->label), "Original GBC colors");
    } else if (index == 1) {
        snprintf(out->value, sizeof(out->value), "gba");
        snprintf(out->label, sizeof(out->label), "GBA brightened colors");
    } else {
        return 0;
    }
    return 1;
}

static int feature_enable(void *ctx, const char *package_id, const char *feature_id,
                          int enabled) {
    (void)ctx;
    if (is_view(package_id, feature_id)) {
        shantae_set_expanded_view(enabled);
        return 1;
    }
    if (is_slowdown(package_id, feature_id)) {
        shantae_set_remove_slowdown(enabled);
        return 1;
    }
    if (is_input_lag(package_id, feature_id)) {
        shantae_set_reduce_input_lag(enabled);
        return 1;
    }
    if (is_moves(package_id, feature_id)) {
        shantae_set_smooth_moves(enabled);
        return 1;
    }
    if (is_dance(package_id, feature_id)) {
        shantae_set_easy_dance(enabled);
        return 1;
    }
    if (!is_feature(package_id, feature_id)) return 0;
    shantae_set_gba_enhanced(enabled);
    return 1;
}

static int feature_set_option(void *ctx, const char *package_id, const char *feature_id,
                              const char *option_id, const char *value) {
    (void)ctx;
    if (is_view(package_id, feature_id) && option_id && value) return view_set_option(option_id, value);
    if (is_moves(package_id, feature_id) && option_id && value) return moves_set_option(option_id, value);
    if (is_dance(package_id, feature_id) && option_id && value) return dance_set_option(option_id, value);
    if (!is_feature(package_id, feature_id) || !option_id || !value ||
        strcmp(option_id, OPTION_COLORS) != 0) {
        return 0;
    }
    if (strcmp(value, "gbc") == 0) {
        shantae_set_original_colors(1);
    } else if (strcmp(value, "gba") == 0) {
        shantae_set_original_colors(0);
    } else {
        snprintf(s_error, sizeof(s_error), "Unknown color setting '%s'.", value);
        return 0;
    }
    return 1;
}

static int install_archive(void *ctx, const char *archive_path) {
    (void)ctx;
    (void)archive_path;
    snprintf(s_error, sizeof(s_error),
             "Shantae's options are built in; there are no mod packages to install.");
    return 0;
}

static const char *last_error(void *ctx) {
    (void)ctx;
    return s_error;
}

static const RecompLauncherCModProvider s_provider = {
    .ctx = NULL,
    .package_count = package_count,
    .package_get = package_get,
    .install_archive = install_archive,
    .last_error = last_error,
    .feature_count = feature_count,
    .feature_get = feature_get,
    .feature_option_get = feature_option_get,
    .feature_choice_get = feature_choice_get,
    .feature_enable = feature_enable,
    .feature_set_option = feature_set_option,
    .archive_extension = ".gbmod",
    .archive_description = "Game Boy recomp mod package (.gbmod)",
};

const struct RecompLauncherCModProvider *game_get_mods(const char *exe_dir) {
    (void)exe_dir;
    return &s_provider;
}
