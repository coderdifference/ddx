// Drawing on the expedition screen: wrap a catalogued game function (docs/15-game-hooks.md), call on to the
// game, then draw with the UI library (docs/11-ui-library.md). Positions come from a layout file the player
// can edit while the game runs (DDXExample/engine/ui/ddx_example.layout.json).
#include "example.h"
#include <stdio.h>

static void **g_raid_display;              // RaidDisplay::s_Instance: the expedition screen
static void (*g_pre_panel)(void *display); // the next wrapper inside ours, or the game

static void h_pre_panel(void *display) {
    g_pre_panel(display); // always call on: other mods wrap this function too
    if (!g_raid_display || display != *g_raid_display)
        return;
    UiLayout *l = ui_layout_mod("ddx_example.layout.json");
    if (ui_layout_num(l, "overlay.show", 1) == 0)
        return;

    char kills[16], line[128];
    snprintf(kills, sizeof kills, "%d", example_campaign_kills());
    const char *keys[] = {"kills"}, *vals[] = {kills};
    ui_format(line, sizeof line, "str_ddx_example_overlay", keys, vals, 1); // "Kills this campaign: {kills}"

    Vec2 centre = ui_screen_centre();
    Vec2 offset = ui_layout_vec2(l, "overlay.pos", (Vec2){0, -500}); // from the screen's centre
    ui_text_anchored((Vec2){centre.x + offset.x, centre.y + offset.y}, line,
                     ui_layout_str(l, "overlay.font", "tooltip"), ui_layout_str(l, "overlay.colour", "notable"),
                     1 /* top centre */, 0 /* no wrap */);
}

void example_overlay_install(void) {
    g_raid_display = (void **)sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    if (!g_raid_display ||
        !game_hook_wrap("RaidDisplay::PrePanelShowInternal", (void *)h_pre_panel, (void **)&g_pre_panel))
        logf_("DDX Example: expedition screen hook unavailable, no overlay");
}
