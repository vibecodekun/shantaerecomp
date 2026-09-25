// Shantae options in the runtime's Esc menu (ImGui needs a C++ TU). The same
// settings are on the launcher's Mods page (launcher_options.c).
#include "imgui.h"

#include <cstdio>

#include "game_extras.h"
#include "gb_custom_view.h"
extern "C" {
#include "expanded_view.h"
}

extern "C" int shantae_gba_enhanced(void);
extern "C" void shantae_set_gba_enhanced(int on);
extern "C" int shantae_original_colors(void);
extern "C" void shantae_set_original_colors(int on);
extern "C" int shantae_remove_slowdown(void);
extern "C" void shantae_set_remove_slowdown(int on);
extern "C" int shantae_reduce_input_lag(void);
extern "C" void shantae_set_reduce_input_lag(int on);

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
        if (ImGui::InputInt("Least height", &height, 1, 8)) shantae_set_view_size(width, height);
        ImGui::TextDisabled("Fills the window. Pixel Perfect scaling uses the largest");
        ImGui::TextDisabled("whole-pixel scale that shows at least this height.");
    } else {
        if (ImGui::InputInt("Width", &width, 1, 8)) shantae_set_view_size(width, height);
        if (ImGui::InputInt("Height", &height, 1, 8)) shantae_set_view_size(width, height);
    }
    ImGui::TextDisabled("%d to %d wide, %d to %d tall. NES is 256 x 240.", SHANTAE_VIEW_MIN_WIDTH,
                        SHANTAE_VIEW_MAX_WIDTH, SHANTAE_VIEW_MIN_HEIGHT, SHANTAE_VIEW_MAX_HEIGHT);
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
    ImGui::TextDisabled("Menus, dialogue and one-screen rooms show the original");
    ImGui::TextDisabled("160 x 144 picture, scaled and shaded on its own.");
    ImGui::TextDisabled("Inside the view keeps it at the view's pixel size.");

    static const char *const zooms[] = {"Off", "Fill the screen", "Whole room"};
    int room_zoom = shantae_room_zoom();
    if (ImGui::Combo("Room zoom", &room_zoom, zooms, 3)) shantae_set_room_zoom(room_zoom);
    ImGui::TextDisabled("Rooms smaller than the view zoom in instead of sitting");
    ImGui::TextDisabled("in black. Fill: until the room fills the screen, the");
    ImGui::TextDisabled("rest scrolling. Whole room: no side that fits is cut.");
    if (!expanded) ImGui::EndDisabled();
    ImGui::TextDisabled(running ? "Experimental. Size changes apply now; turning the view"
                                : "Experimental. Takes effect on the next launch.");
    if (running) ImGui::TextDisabled("on or off takes effect on the next launch.");
}
