// Make story-quest loading screens skippable as soon as the game says it is safe to continue. The game
// already draws its localized "Press SPACE or CLICK to Continue" text and handles the screen transition;
// enabling that path early avoids duplicating UI or changing how ordinary loading screens work.
#include "common.h"

typedef bool (*show_continue_fn)(void *screen, float dt, bool can_continue);

static show_continue_fn g_show_continue;
static void **g_raid_instance;
static bool (*g_key_pressed)(unsigned key, bool repeat, bool flag);

static bool story_raid(void) {
    void *r = g_raid_instance ? *g_raid_instance : NULL;
    void *quest = r ? *(void **)((uint8_t *)r + RAID_QUEST) : NULL;
    return quest && *(const uint8_t *)((const uint8_t *)quest + QUEST_IS_PLOT);
}

static bool skip_input(void) {
    bool space = g_key_pressed && g_key_pressed(32, false, false);
    const UiMouseButton *left = ui_mouse(UI_MOUSE_LEFT);
    return space || (left && left->pressed);
}

static bool show_continue_hook(void *screen, float dt, bool can_continue) {
    if (!story_raid())
        return g_show_continue(screen, dt, can_continue);

#ifdef _WIN32
    bool ready = ((uint8_t *)screen)[0x2cc] && ((uint8_t *)screen)[0x2cd];
#else
    bool ready = ((uint8_t *)screen)[0x2b4] && ((uint8_t *)screen)[0x2b5];
#endif
    // Do not consume a key during resource loading. The original method has these same gates, but
    // checking them here also prevents our early return from bypassing them.
    if (!ready)
        return g_show_continue(screen, dt, can_continue);

    // LoadingScreen::ShowContinue's native readiness checks still decide when it is safe. Once those
    // pass, ask it to draw the native prompt. The method also sees other keys, so only Space and click
    // are allowed to finish the story intro while it would otherwise still be narrating.
    bool pressed = skip_input();
    if (pressed) {
#ifdef _WIN32
        ((uint8_t *)screen)[0x2d7] = 1; // LoadingScreen::skipNarration (MSVC layout)
#else
        ((uint8_t *)screen)[0x2bf] = 1; // LoadingScreen::skipNarration
#endif
        logf_("story intro skip requested");
    }
    bool native_result = g_show_continue(screen, dt, true);
    return pressed || (can_continue && native_result);
}

void story_intro_skip_install(void) {
    g_raid_instance = (void **)sym("_ZN4Raid10s_InstanceE");
    g_key_pressed = (bool (*)(unsigned, bool, bool))sym("_Z18Input_IsKeyPressedjbb");
#ifdef _WIN32
    g_show_continue =
        (show_continue_fn)install_hook("LoadingScreen::ShowContinue (story intro skip)",
                                       sym("_ZN13LoadingScreen12ShowContinueEfb"), (void *)show_continue_hook, NULL, 0);
#else
    static const uint8_t prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                       0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x58, 0x03, 0x00, 0x00};
    g_show_continue = (show_continue_fn)install_hook("LoadingScreen::ShowContinue (story intro skip)",
                                                     sym("_ZN13LoadingScreen12ShowContinueEfb"),
                                                     (void *)show_continue_hook, prologue, sizeof prologue);
#endif
    if (!g_show_continue)
        logf_("story intro skip unavailable");
}
