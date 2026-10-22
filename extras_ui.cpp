// Shantae options in the runtime's Esc menu (ImGui needs a C++ TU). The same
// settings are on the launcher's Mods page (launcher_options.c).
#include "imgui.h"

#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <vector>

#include "game_extras.h"
#include "gb_custom_view.h"
extern "C" {
#include "expanded_view.h"
#include "moveset.h"
#include "forms.h"
#include "dance.h"
#include "dialogue.h"
#include "transform.h"
#include "hud.h"
#include "map.h"
int shantae_map_action(void);
}

extern "C" int shantae_gba_enhanced(void);
extern "C" void shantae_set_gba_enhanced(int on);
extern "C" int shantae_original_colors(void);
extern "C" void shantae_set_original_colors(int on);
extern "C" int shantae_remove_slowdown(void);
extern "C" void shantae_set_remove_slowdown(int on);
extern "C" int shantae_reduce_input_lag(void);
extern "C" void shantae_set_reduce_input_lag(int on);

// The desktop of the display the game's window is on; 0 x 0 when unknown.
static void screen_size(int *width, int *height) {
    SDL_Window *window = SDL_GetKeyboardFocus();
    const int display = window ? SDL_GetWindowDisplayIndex(window) : 0;
    SDL_DisplayMode mode;
    *width = *height = 0;
    if (SDL_GetDesktopDisplayMode(display < 0 ? 0 : display, &mode) == 0) {
        *width = mode.w;
        *height = mode.h;
    }
}

// A size for the HUD or the clock: Auto, 1-8x, or the picture's pixels.
static void size_combo(const char *name, int size, void (*set)(int)) {
    char label[48];
    if (size == SHANTAE_HUD_AUTO) snprintf(label, sizeof(label), "Auto: half the original's pixels");
    else if (size == SHANTAE_HUD_PICTURE) snprintf(label, sizeof(label), "The picture's pixels");
    else snprintf(label, sizeof(label), "%dx", size);
    if (!ImGui::BeginCombo(name, label)) return;
    if (ImGui::Selectable("The picture's pixels", size == SHANTAE_HUD_PICTURE)) set(SHANTAE_HUD_PICTURE);
    if (ImGui::Selectable("Auto: half the original's pixels", size == SHANTAE_HUD_AUTO)) set(SHANTAE_HUD_AUTO);
    for (int n = 1; n <= SHANTAE_HUD_SIZE_MAX; ++n) {
        snprintf(label, sizeof(label), "%dx", n);
        if (ImGui::Selectable(label, size == n)) set(n);
    }
    ImGui::EndCombo();
}

void game_draw_overlay(struct GBContext *ctx) {
    (void)ctx;
    if (!ImGui::CollapsingHeader("Shantae")) {
        return;
    }
    bool gba = shantae_gba_enhanced() != 0;
    if (ImGui::Checkbox("GBA Enhanced mode", &gba)) {
        shantae_set_gba_enhanced(gba ? 1 : 0);
    }
    ImGui::TextDisabled("Boot as a GBA: \"GBA Enhanced!\" title and the Tinkerbat secret.");
    ImGui::TextDisabled("Takes effect on the next launch.");

    if (!gba) ImGui::BeginDisabled();
    bool original = shantae_original_colors() != 0;
    if (ImGui::Checkbox("Original GBC colors", &original)) {
        shantae_set_original_colors(original ? 1 : 0);
    }
    ImGui::TextDisabled("Instead of the brightened GBA palettes. Applies on the next");
    ImGui::TextDisabled("palette load (e.g. a room change).");
    if (!gba) ImGui::EndDisabled();

    ImGui::Separator();
    bool fast = shantae_remove_slowdown() != 0;
    if (ImGui::Checkbox("Remove slowdown", &fast)) {
        shantae_set_remove_slowdown(fast ? 1 : 0);
    }
    ImGui::TextDisabled("Busy frames get the time they need instead of lagging.");

    bool quick = shantae_reduce_input_lag() != 0;
    if (ImGui::Checkbox("Reduce input lag", &quick)) {
        shantae_set_reduce_input_lag(quick ? 1 : 0);
    }
    ImGui::TextDisabled("Sprites and Shantae's moves one frame sooner each.");

    ImGui::Separator();
    bool smooth = shantae_smooth_moves() != 0;
    if (ImGui::Checkbox("Smoother movement", &smooth)) {
        shantae_set_smooth_moves(smooth ? 1 : 0);
    }
    ImGui::TextDisabled("Holding B runs at once, on the ground and in the air,");
    ImGui::TextDisabled("and B whips and Select dances the moment she lands.");
    if (!smooth) ImGui::BeginDisabled();
    // In the order of SHANTAE_WHIP_*.
    static const char *const whips[] = {"Original: stop to whip", "Slide: whip and keep moving",
                                        "Cancel: B and a direction runs at once"};
    int whip = shantae_whip_moving();
    if (ImGui::Combo("Whip on the move", &whip, whips, 3)) shantae_set_whip_moving(whip);
    static const char *const airs[] = {"Original: set when she leaves the ground", "Hold B for running speed"};
    int air = shantae_air_speed_b();
    if (ImGui::Combo("Air speed", &air, airs, 2)) shantae_set_air_speed_b(air);
    static const char *const crawls[] = {"Original", "Hold B for walking speed"};
    int crawl = shantae_fast_crawl();
    if (ImGui::Combo("Crawl", &crawl, crawls, 2)) shantae_set_fast_crawl(crawl);
    static const char *const transformations[] = {"Original", "Like Shantae"};
    int forms = shantae_smooth_forms();
    if (ImGui::Combo("Transformations", &forms, transformations, 2)) shantae_set_smooth_forms(forms);
    ImGui::TextDisabled("Monkey and tinkerbat: claw, sword and jumps as above;");
    ImGui::TextDisabled("B runs (the spider too) and climbs faster; the harpy");
    ImGui::TextDisabled("flaps through talons; charges redo, turn and jump;");
    ImGui::TextDisabled("A grabs and leaves webs. Changes apply at once.");
    if (!smooth) ImGui::EndDisabled();

    ImGui::Separator();
    bool dance = shantae_easy_dance() != 0;
    if (ImGui::Checkbox("Easier dancing", &dance)) {
        shantae_set_easy_dance(dance ? 1 : 0);
    }
    ImGui::TextDisabled("After Select, press the steps like a code.");
    if (!dance) ImGui::BeginDisabled();
    static const char *const steps[] = {"Original: one step a beat, in rhythm", "Quick: any speed, wrong buttons skipped"};
    int step_mode = shantae_quick_steps();
    if (ImGui::Combo("Dance steps", &step_mode, steps, 2)) shantae_set_quick_steps(step_mode);
    static const char *const afters[] = {"Original", "Blink and stay safe for two seconds"};
    int safe = shantae_transform_invincible();
    if (ImGui::Combo("After transforming", &safe, afters, 2)) shantae_set_transform_invincible(safe);
    ImGui::TextDisabled("Down starts a dance over. Changes apply at once.");
    if (!dance) ImGui::EndDisabled();

    ImGui::Separator();
    bool buttons = shantae_transform_buttons() != 0;
    if (ImGui::Checkbox("Transformation buttons", &buttons)) {
        shantae_set_transform_buttons(buttons ? 1 : 0);
    }
    ImGui::TextDisabled("A button for each form she has danced: she becomes it");
    ImGui::TextDisabled("at once, and its button again turns her back. One more");
    ImGui::TextDisabled("heals as the heal dance does. Bind them in Controls");
    ImGui::TextDisabled("(keys 1-6; X, Y, LB and RB on a pad).");
    if (!buttons) ImGui::BeginDisabled();
    static const char *const huds[] = {"Off", "Top left", "Top right", "Bottom left", "Bottom right"};
    int hud = shantae_hud_mode();
    if (ImGui::Combo("Buttons HUD", &hud, huds, 5)) shantae_set_hud_mode(hud);
    ImGui::TextDisabled("The Dance Menu's icons in a corner of the picture,");
    ImGui::TextDisabled("whatever its shape; at the bottom, above the status");
    ImGui::TextDisabled("bar or a dialogue box.");
    if (hud == SHANTAE_HUD_OFF) ImGui::BeginDisabled();
    static const char *const layouts[] = {"1 column", "2 columns", "3 columns", "One row"};
    const int columns = shantae_hud_columns();
    int layout = columns >= SHANTAE_HUD_ROW ? 3 : columns - 1;
    if (ImGui::Combo("HUD layout", &layout, layouts, 4)) shantae_set_hud_columns(layout == 3 ? SHANTAE_HUD_ROW : layout + 1);
    size_combo("HUD size", shantae_hud_size(), shantae_set_hud_size);
    ImGui::TextDisabled("Screen pixels for each of its pixels. Auto is half the");
    ImGui::TextDisabled("original picture's at this window's size (3x on a 1080p");
    ImGui::TextDisabled("screen), the same whatever the view or zoom. The");
    ImGui::TextDisabled("picture's pixels: the game's own size, on their grid, so");
    ImGui::TextDisabled("a shader preset's dots or scanlines are the game's.");
    if (hud == SHANTAE_HUD_OFF) ImGui::EndDisabled();
    if (!buttons) ImGui::EndDisabled();

    ImGui::Separator();
    bool map = shantae_map_on() != 0;
    if (ImGui::Checkbox("Map", &map)) shantae_set_map_on(map ? 1 : 0);
    ImGui::TextDisabled("Its button (M; the right stick's click on a pad) opens");
    ImGui::TextDisabled("the world as cells and the area she is in as its terrain.");
    ImGui::TextDisabled("The pad moves it, A zooms, Select shows the world from a");
    ImGui::TextDisabled("labyrinth, B closes it. The sun or moon is the time to");
    ImGui::TextDisabled("the next nightfall or daybreak.");
    if (!map) ImGui::BeginDisabled();
    bool whole = shantae_map_whole() != 0;
    if (ImGui::Checkbox("Whole map from the start", &whole)) shantae_set_map_whole(whole ? 1 : 0);
    ImGui::TextDisabled("Off: it fills in as she explores, kept with each save file.");
    bool enemies = shantae_map_enemies() != 0;
    if (ImGui::Checkbox("Show enemies", &enemies)) shantae_set_map_enemies(enemies ? 1 : 0);
    size_combo("Map size", shantae_map_size(), shantae_set_map_size);
    ImGui::TextDisabled("The picture's pixels: the game's own, so a shader preset");
    ImGui::TextDisabled("draws it as the game. Auto shows more at once.");
    if (!map) ImGui::EndDisabled();

    ImGui::Separator();
    int clock = shantae_clock_mode();
    if (ImGui::Combo("Day and night clock", &clock, huds, 5)) shantae_set_clock_mode(clock);
    ImGui::TextDisabled("The sun or the moon and the time to the next change,");
    ImGui::TextDisabled("in the status bar's numerals, in a corner of the picture");
    ImGui::TextDisabled("(beside the buttons' HUD in its corner). Dimmed while");
    ImGui::TextDisabled("time stands still, as in scenes.");
    if (clock == SHANTAE_HUD_OFF) ImGui::BeginDisabled();
    size_combo("Clock size", shantae_clock_size(), shantae_set_clock_size);
    if (clock == SHANTAE_HUD_OFF) ImGui::EndDisabled();

    ImGui::Separator();
    bool caravan = shantae_caravan_stays() != 0;
    if (ImGui::Checkbox("Zombie Caravan stays", &caravan)) shantae_set_caravan_stays(caravan ? 1 : 0);
    ImGui::TextDisabled("Every night between the Ghost and Archer Forests, where");
    ImGui::TextDisabled("it first stops. Off: later in the story it moves on each");
    ImGui::TextDisabled("night, as in the original; the map shows where once she");
    ImGui::TextDisabled("crosses a border it can stop at.");

    ImGui::Separator();
    int text_speed = shantae_text_speed();
    const char *text_format = text_speed <= 0 ? "0: Original"
                            : text_speed >= SHANTAE_TEXT_INSTANT ? "10: Instant" : "%d";
    if (ImGui::SliderInt("Text speed", &text_speed, 0, SHANTAE_TEXT_INSTANT, text_format,
                         ImGuiSliderFlags_AlwaysClamp)) {
        shantae_set_text_speed(text_speed);
    }
    ImGui::TextDisabled("B, held or pressed again and again, prints speed + 1");
    ImGui::TextDisabled("letters a frame, 10 at once; shops take gems at once.");

    ImGui::Separator();
    bool expanded = shantae_expanded_view() != 0;
    if (ImGui::Checkbox("Expanded view", &expanded)) {
        shantae_set_expanded_view(expanded ? 1 : 0);
    }
    ImGui::TextDisabled("Show more of the world above, below, and to either side.");
    if (!expanded) ImGui::BeginDisabled();
    // While the view runs, the size on screen (Adaptive follows the window).
    const bool running = gb_custom_render != nullptr;
    const bool adaptive = shantae_view_adaptive() != 0;
    int width = shantae_view_width(), height = shantae_view_height();
    // The view before room zoom shrinks it to a small room.
    const int shown_width = running ? gb_custom_view_width : width;
    const int shown_height = running ? gb_custom_view_height : height;
    const int preset = adaptive ? -1 : shantae_view_aspect_of(width, height);
    const char *adaptive_label = "Adaptive (fill the screen)";
    char custom[32];
    snprintf(custom, sizeof(custom), "Custom (%.2f:1)", (double)shown_width / shown_height);
    const ShantaeViewAspect *current = shantae_view_aspect_info(preset);
    if (ImGui::BeginCombo("Aspect ratio", adaptive ? adaptive_label : current ? current->label : custom)) {
        if (ImGui::Selectable(adaptive_label, adaptive)) shantae_set_view_adaptive(1);
        for (int i = 0; i < shantae_view_aspect_count(); ++i) {
            if (ImGui::Selectable(shantae_view_aspect_info(i)->label, i == preset)) {
                shantae_view_aspect_size(i, &width, &height);
                shantae_set_view_size(width, height);
                shantae_set_view_adaptive(0);
            }
        }
        // From Adaptive, Custom keeps the size on screen now.
        if (ImGui::Selectable(custom, !adaptive && preset < 0) && adaptive) {
            shantae_set_view_size(shown_width, shown_height);
            shantae_set_view_adaptive(0);
        }
        ImGui::EndCombo();
    }
    // Step 1 pixel; Ctrl + click steps 8.
    if (adaptive) {
        int follows = shown_width;
        ImGui::BeginDisabled();
        ImGui::InputInt("Width", &follows, 1, 8);
        ImGui::EndDisabled();
        if (ImGui::InputInt("Least height", &height, 1, 8)) shantae_set_view_height(height);
        ImGui::TextDisabled("Fills the window. Pixel Perfect scaling uses the largest");
        ImGui::TextDisabled("whole-pixel scale that shows at least this height.");
    } else {
        if (ImGui::InputInt("Width", &width, 1, 8)) shantae_set_view_size(width, height);
        // A preset keeps its shape: 16:9 stays 16:9.
        if (ImGui::InputInt("Height", &height, 1, 8)) shantae_set_view_height(height);
    }
    ImGui::TextDisabled("%d to %d wide, %d to %d tall. NES is 256 x 240.", SHANTAE_VIEW_MIN_WIDTH,
                        SHANTAE_VIEW_MAX_WIDTH, SHANTAE_VIEW_MIN_HEIGHT, SHANTAE_VIEW_MAX_HEIGHT);
    // The original's height keeps its zoom: a preset keeps its shape, a custom
    // size takes the screen's (426 x 144 becomes 256 x 144 on a 16:9 screen).
    if (ImGui::Button("Original height (144)")) {
        int w = width, h = SHANTAE_VIEW_MIN_HEIGHT, sw = 0, sh = 0;
        if (!adaptive) {
            screen_size(&sw, &sh);
            shantae_view_original_size(width, height, sw, sh, &w, &h);
        }
        if (w != width || h != height) shantae_set_view_size(w, h);
    }
    ImGui::TextDisabled("More to either side at the original zoom, dialogue");
    ImGui::TextDisabled("and towns too (16:9 is then 256 x 144).");
    if (running && (gb_custom_width != shown_width || gb_custom_height != shown_height))
        ImGui::TextDisabled("Showing %d x %d; this room zooms to %d x %d.", shown_width, shown_height,
                            gb_custom_width, gb_custom_height);
    else if (running)
        ImGui::TextDisabled("Showing %d x %d.", shown_width, shown_height);

    // The original 160 x 144 picture: its own scaling, or centered in the view.
    // Whole scales up to the largest that fits the window (or the one chosen).
    static const char *const modes[] = {"Pixel Perfect", "Aspect Fit", "Aspect Fill", "Stretch"};
    const int scaling = shantae_native_scaling(), scale = shantae_native_scale();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    int most = (int)display.x / 160 < (int)display.y / 144 ? (int)display.x / 160 : (int)display.y / 144;
    if (scaling == GB_CUSTOM_NATIVE_WHOLE_SCALE && scale > most) most = scale;
    if (most < 1) most = 1;
    char label[48];
    if (scaling == GB_CUSTOM_NATIVE_SCALING_MODE) snprintf(label, sizeof(label), "Same as Scaling Mode");
    else if (scaling == GB_CUSTOM_NATIVE_IN_VIEW) snprintf(label, sizeof(label), "Inside the view");
    else if (scaling == GB_CUSTOM_NATIVE_WHOLE_SCALE) snprintf(label, sizeof(label), "%dx", scale);
    else snprintf(label, sizeof(label), "%s", modes[scaling]);
    if (ImGui::BeginCombo("Original view", label)) {
        if (ImGui::Selectable("Same as Scaling Mode", scaling == GB_CUSTOM_NATIVE_SCALING_MODE))
            shantae_set_native_scaling(GB_CUSTOM_NATIVE_SCALING_MODE, scale);
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(modes[i], scaling == i)) shantae_set_native_scaling(i, scale);
        for (int n = 1; n <= most; ++n) {
            snprintf(label, sizeof(label), "%dx (%d x %d)", n, 160 * n, 144 * n);
            if (ImGui::Selectable(label, scaling == GB_CUSTOM_NATIVE_WHOLE_SCALE && scale == n))
                shantae_set_native_scaling(GB_CUSTOM_NATIVE_WHOLE_SCALE, n);
        }
        if (ImGui::Selectable("Inside the view", scaling == GB_CUSTOM_NATIVE_IN_VIEW))
            shantae_set_native_scaling(GB_CUSTOM_NATIVE_IN_VIEW, scale);
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("Menus and one-screen rooms show the original 160 x 144");
    ImGui::TextDisabled("picture, scaled and shaded on its own.");
    ImGui::TextDisabled("Inside the view keeps it at the view's pixel size.");

    // The status bar, dialogue boxes and towns' name panels.
    const int ui = shantae_ui_scale();
    char ui_label[48];
    if (ui == SHANTAE_UI_AUTO) snprintf(ui_label, sizeof(ui_label), "Auto: as in the original");
    else snprintf(ui_label, sizeof(ui_label), ui == 1 ? "1x: the view's pixels" : "%dx", ui);
    if (ImGui::BeginCombo("Bottom UI size", ui_label)) {
        if (ImGui::Selectable("Auto: as in the original", ui == SHANTAE_UI_AUTO)) shantae_set_ui_scale(SHANTAE_UI_AUTO);
        for (int n = 1; n <= 8; ++n) {
            snprintf(ui_label, sizeof(ui_label), n == 1 ? "1x: the view's pixels" : "%dx", n);
            if (ImGui::Selectable(ui_label, ui == n)) shantae_set_ui_scale(n);
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("The status bar, dialogue and towns' name panels: the");
    ImGui::TextDisabled("view's own pixels, a multiple of them, or as large as in");
    ImGui::TextDisabled("the original picture, the same when a room zooms in.");
    ImGui::TextDisabled("The world stays above them.");
    static const char *const fits[] = {"Under it, as original", "Fits above it"};
    int ui_fit = shantae_ui_fit() != 0;
    if (ImGui::Combo("World", &ui_fit, fits, 2)) shantae_set_ui_fit(ui_fit);
    ImGui::TextDisabled("Fits keeps the whole room above the bottom UI, sky");
    ImGui::TextDisabled("to ground, zooming out while a dialogue box is up.");
    ImGui::TextDisabled("Under it lays the bottom UI over the world's bottom.");

    static const char *const zooms[] = {"Off", "Fill the screen", "Whole room"};
    int room_zoom = shantae_room_zoom();
    if (ImGui::Combo("Room zoom", &room_zoom, zooms, 3)) shantae_set_room_zoom(room_zoom);
    ImGui::TextDisabled("Rooms smaller than the view zoom in instead of sitting");
    ImGui::TextDisabled("in black. Fill: until the room fills the screen, the");
    ImGui::TextDisabled("rest scrolling, a view of another shape too (NES 16:15");
    ImGui::TextDisabled("at its width). Whole room: no side that fits is cut.");
    if (!expanded) ImGui::EndDisabled();
    ImGui::TextDisabled(running ? "Experimental. Size changes apply now; turning the view"
                                : "Experimental. Takes effect on the next launch.");
    if (running) ImGui::TextDisabled("on or off takes effect on the next launch.");
}

// The transformation buttons' HUD (hud.c) is drawn inside the picture,
// whatever its shape (the original's 10:9, the expanded view's), in the corner
// chosen: never in the black beside it, which is the expanded view's to fill.
// A bottom corner sits above the bottom UI as it is (shantae_view_bottom_rows:
// the status bar, a dialogue box while one is up, a town's panel, at the size
// the view gives them). In play the window is always on screen as one of them
// (WY 128 the bar, 104 a box or panel), and a menu has none (the inventory
// moves it to 144) or covers the picture with it: the HUD is drawn only while
// a bottom UI shows, under half the picture. It is a little translucent. The
// day and night clock (hud.c) is drawn the same way, in a corner of its own,
// or beside the HUD in the HUD's: below it at the top, above it at the bottom.
static const int HUD_ALPHA = 220;

static bool corner_right(int corner) {
    return corner == SHANTAE_HUD_TOP_RIGHT || corner == SHANTAE_HUD_BOTTOM_RIGHT;
}
static bool corner_low(int corner) {
    return corner == SHANTAE_HUD_BOTTOM_LEFT || corner == SHANTAE_HUD_BOTTOM_RIGHT;
}

// The top left of a piece w x h in `corner` of a picture at px,py of pw x ph,
// `margin` in from the corner and from the bottom UI's top, `bottom` above
// the picture's foot.
static void hud_place(int corner, float px, float py, float pw, float ph, float w, float h, float margin,
                      float bottom, float *x, float *y) {
    *x = corner_right(corner) ? px + pw - margin - w : px + margin;
    *y = corner_low(corner) ? py + ph - bottom - margin - h : py + margin;
}

// Where the HUD (w x h) and the clock (cw x ch) go, either one 0 x 0 when it
// is not drawn: each two of its pixels (`hud_scale`, `clock_scale`) in from
// its corner, the clock as far again from the HUD when they share one.
static void place_both(float px, float py, float pw, float ph, float bottom, float w, float h, float hud_scale,
                       float cw, float ch, float clock_scale, float *x, float *y, float *cx, float *cy) {
    const int corner = shantae_hud_mode(), clock_corner = shantae_clock_mode();
    hud_place(corner, px, py, pw, ph, w, h, 2.0f * hud_scale, bottom, x, y);
    hud_place(clock_corner, px, py, pw, ph, cw, ch, 2.0f * clock_scale, bottom, cx, cy);
    if (clock_corner != corner || w <= 0 || h <= 0) return;
    *cy = corner_low(corner) ? *y - 2.0f * clock_scale - ch : *y + h + 2.0f * clock_scale;
}

// Half the whole scale the original picture has in this window (3 screen
// pixels a pixel on a 1080p screen): it follows the window, not the picture,
// so the room zoom and the view's shape leave it as it is.
static int auto_scale(const GBHudFrame *frame) {
    const float original = frame->window_w / 160.0f < frame->window_h / 144.0f ? frame->window_w / 160.0f
                                                                                : frame->window_h / 144.0f;
    return original >= 4.0f ? (int)original / 2 : 1;
}

// Window pixels for each of a piece's pixels at the size chosen: Auto, 1-8,
// or the picture's own (frame->scale).
static float piece_scale(int size, const GBHudFrame *frame) {
    if (size == SHANTAE_HUD_PICTURE) return frame->scale > 0.0f ? frame->scale : (float)auto_scale(frame);
    return (float)(size != SHANTAE_HUD_AUTO ? size : auto_scale(frame));
}

// A piece at the picture's pixels starts on one of them, so its pixels are
// the picture's, cell for cell: rounded away from the corner's edge it is
// placed from (it is already on one, or beside the HUD, which it then does
// not overlap).
static void snap(const GBHudFrame *frame, int corner, float *x, float *y) {
    const float s = frame->scale;
    if (s <= 0.0f) return;
    const float px = (*x - frame->x) / s, py = (*y - frame->y) / s;
    *x = frame->x + s * (corner_right(corner) ? floorf(px + 1e-3f) : ceilf(px - 1e-3f));
    *y = frame->y + s * (corner_low(corner) ? floorf(py + 1e-3f) : ceilf(py - 1e-3f));
}

// The map screen (map.c) while it is open: the whole picture, at the size
// chosen (the picture's own pixels by default, on its grid), shaded on its own
// as the HUD is (texture slot 1; the HUD is not drawn meanwhile). The game is
// held, so its buttons are read here: the pad
// pans, A zooms, Select shows the world from a labyrinth, and B, Start or the
// map's own button close it.
static void draw_map(struct GBContext *ctx, const GBHudFrame *frame) {
    const float scale = piece_scale(shantae_map_size(), frame);
    const int w = (int)(frame->w / scale + 0.01f), h = (int)(frame->h / scale + 0.01f);
    if (w <= 0 || h <= 0) return;
    static std::vector<uint32_t> pixels;
    pixels.resize((size_t)w * h);
    if (!shantae_map_draw(ctx, pixels.data(), w, h)) return;
    const ImTextureID plain = (ImTextureID)gb_platform_hud_texture(1, pixels.data(), w, h);
    if (!plain) return;
    const float mw = (float)w * scale, mh = (float)h * scale;
    const float x = frame->x + (frame->w - mw) / 2.0f, y = frame->y + (frame->h - mh) / 2.0f;
    const ImTextureID shaded = (ImTextureID)gb_platform_hud_shaded(1, mw, mh);
    ImGui::GetBackgroundDrawList()->AddImage(shaded ? shaded : plain, ImVec2(x, y), ImVec2(x + mw, y + mh));
}

static void map_buttons(struct GBContext *ctx, const GBHudFrame *frame) {
    static int was_open, button_held = 1;
    const int button = gb_platform_game_action_held(shantae_map_action());
    if (shantae_map_is_open() && was_open && button && !button_held) shantae_map_toggle(ctx);
    else if (shantae_map_is_open() && !frame->menu_open) shantae_map_input(ctx, gb_platform_joypad_held());
    button_held = button;
    was_open = shantae_map_is_open();
}

// Over the picture, apart from it: at the size chosen, or Auto (auto_scale).
// A shader preset runs on it on its own (gb_platform_hud_shaded), as on the
// bottom UI.
void game_draw_hud(struct GBContext *ctx, const GBHudFrame *frame) {
    map_buttons(ctx, frame);
    if (shantae_map_is_open()) {
        if (frame->layers) draw_map(ctx, frame);
        return;
    }
    if (!frame->layers || frame->picture_h <= 0) return;   // drawn into the picture instead
    const int rows = shantae_view_bottom_rows(ctx, frame->native);
    if (rows <= 0 || rows * 2 >= frame->picture_h) return;
    static uint32_t pixels[SHANTAE_HUD_MAX_W * SHANTAE_HUD_MAX_H], clock[SHANTAE_CLOCK_W * SHANTAE_CLOCK_H];
    int w = 0, h = 0, cw = 0, ch = 0;
    const bool hud = shantae_hud_draw(ctx, pixels, &w, &h) != 0;
    const bool timer = shantae_clock_mode() != SHANTAE_HUD_OFF && shantae_clock_draw(ctx, clock, &cw, &ch);
    if (!hud && !timer) return;
    const float scale = piece_scale(shantae_hud_size(), frame), clock_scale = piece_scale(shantae_clock_size(), frame);
    const float hw = hud ? (float)w * scale : 0.0f, hh = hud ? (float)h * scale : 0.0f;
    const float tw = (float)cw * clock_scale, th = (float)ch * clock_scale;
    float x, y, tx, ty;
    place_both(frame->x, frame->y, frame->w, frame->h, rows * frame->h / frame->picture_h, hw, hh, scale, tw, th,
               clock_scale, &x, &y, &tx, &ty);
    if (shantae_hud_size() == SHANTAE_HUD_PICTURE) snap(frame, shantae_hud_mode(), &x, &y);
    if (shantae_clock_size() == SHANTAE_HUD_PICTURE) snap(frame, shantae_clock_mode(), &tx, &ty);
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(frame->x, frame->y), ImVec2(frame->x + frame->w, frame->y + frame->h), true);
    // Slot 0 the HUD, 2 the clock: each through a chain of its own.
    const struct { bool on; int slot; const uint32_t *pixels; int w, h; float x, y, sw, sh; } pieces[] = {
        {hud, 0, pixels, w, h, x, y, hw, hh},
        {timer, 2, clock, cw, ch, tx, ty, tw, th},
    };
    for (const auto &p : pieces) {
        if (!p.on) continue;
        const ImTextureID plain = (ImTextureID)gb_platform_hud_texture(p.slot, p.pixels, p.w, p.h);
        if (!plain) continue;
        const ImTextureID shaded = (ImTextureID)gb_platform_hud_shaded(p.slot, p.sw, p.sh);
        draw->AddImage(shaded ? shaded : plain, ImVec2(p.x, p.y), ImVec2(p.x + p.sw, p.y + p.sh), ImVec2(0, 0),
                       ImVec2(1, 1), IM_COL32(255, 255, 255, HUD_ALPHA));
    }
    draw->PopClipRect();
}

// A piece (w x h) into the picture at x0,y0, a little translucent.
static void blend_into(uint32_t *picture, int width, int height, const uint32_t *pixels, int w, int h, int x0,
                       int y0) {
    for (int y = 0; y < h; ++y) {
        if (y0 + y < 0 || y0 + y >= height) continue;
        for (int x = 0; x < w; ++x) {
            const uint32_t c = pixels[y * w + x];
            const unsigned a = (c >> 24) * HUD_ALPHA / 255;
            if (!a || x0 + x < 0 || x0 + x >= width) continue;
            uint32_t *p = picture + (size_t)(y0 + y) * width + x0 + x;
            uint32_t mixed = 0xFF000000u;
            for (int shift = 0; shift < 24; shift += 8) {
                const unsigned over = (c >> shift) & 0xFF, under = (*p >> shift) & 0xFF;
                mixed |= ((over * a + under * (255 - a)) / 255) << shift;
            }
            *p = mixed;
        }
    }
}

// With a preset that draws over the whole window (a bezel), the picture is
// somewhere inside what the preset draws: the HUD and the clock go into the
// picture, one of their pixels a picture pixel, and the preset shades them
// with the game.
void game_draw_hud_into_picture(struct GBContext *ctx, uint32_t *picture, int width, int height, int native) {
    if (shantae_map_is_open()) {   // the map, a map pixel a picture pixel, over the whole picture
        static std::vector<uint32_t> map;
        map.resize((size_t)width * height);
        if (!shantae_map_draw(ctx, map.data(), width, height)) return;
        for (size_t i = 0; i < map.size(); ++i) {
            const uint32_t c = map[i];
            const unsigned a = c >> 24;
            uint32_t mixed = 0xFF000000u;
            for (int shift = 0; shift < 24; shift += 8) {
                const unsigned over = (c >> shift) & 0xFF, under = (picture[i] >> shift) & 0xFF;
                mixed |= ((over * a + under * (255 - a)) / 255) << shift;
            }
            picture[i] = mixed;
        }
        return;
    }
    const int rows = shantae_view_bottom_rows(ctx, native);
    if (rows <= 0 || rows * 2 >= height) return;
    static uint32_t pixels[SHANTAE_HUD_MAX_W * SHANTAE_HUD_MAX_H], clock[SHANTAE_CLOCK_W * SHANTAE_CLOCK_H];
    int w = 0, h = 0, cw = 0, ch = 0;
    const bool hud = shantae_hud_draw(ctx, pixels, &w, &h) != 0;
    const bool timer = shantae_clock_mode() != SHANTAE_HUD_OFF && shantae_clock_draw(ctx, clock, &cw, &ch);
    if (!hud) w = h = 0;
    float x, y, tx, ty;
    place_both(0.0f, 0.0f, (float)width, (float)height, (float)rows, (float)w, (float)h, 1.0f, (float)cw, (float)ch,
               1.0f, &x, &y, &tx, &ty);
    if (hud) blend_into(picture, width, height, pixels, w, h, (int)x, (int)y);
    if (timer) blend_into(picture, width, height, clock, cw, ch, (int)tx, (int)ty);
}
