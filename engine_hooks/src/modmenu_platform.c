// Native pause-menu adapter. macOS symbols/prologues verified against the symbolled game binary.
#include "modmenu_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *app;
static void (*profile_save)(void *, int, bool, bool, unsigned, bool, bool);
static bool (*profile_in_raid)(const void *);
static void (*flush)(void);
static void *(*open_restore)(int, const char *, unsigned, bool, unsigned short);
static void (*close_restore)(void *);
static bool (*paused)(void *);
static void (*o_pre)(void *), (*o_base)(void *), (*o_load)(void *, const void *);
#ifndef _WIN32
static void (*o_cleanup)(void *);
#endif
static void (*o_update)(void *, float);
static bool (*o_deactivate)(void *, bool);
static bool resolved, supported;
static const uint8_t pre_bytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                    0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xc8, 0x05, 0,    0};
static const uint8_t base_bytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                     0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xb8, 0x04, 0,    0};
static const uint8_t update_bytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                       0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
static const uint8_t load_bytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                     0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x58};
#ifndef _WIN32
static const uint8_t cleanup_bytes[] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89,
                                        0xfb, 0x80, 0xbf, 0x35, 0x17, 0,    0,    1};
#endif
static const uint8_t deactivate_bytes[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53,
                                           0x48, 0x83, 0xec, 0x40, 0x41, 0x89, 0xf6};
#define PRE        "_ZN2UI5Panel4Menu23PreChildrenShowInternalEv"
#define BASE       "_ZN2UI5Panel4Menu8ShowBaseEv"
#define UPDATE     "_ZN2UI5Panel4Menu12ActiveUpdateEf"
#define LOAD       "_ZN7Darkest12LoadCampaignERKNS_20ProcessProfileResultE"
#define CLEANUP    "_ZN7Darkest15CleanupCampaignEv"
#define DEACTIVATE "_ZN2UI5Panel4Menu17AttemptDeactivateEb"
static bool bytes(const char *symbol, const uint8_t *expected, size_t n) {
    const void *p = sym(symbol);
#ifdef _WIN32
    (void)expected;
    (void)n;
    return p != NULL; // syms_win validates this build's bytes.
#else
    return p && !memcmp(p, expected, n);
#endif
}
#ifdef _WIN32
static bool windows_in_raid(const void *profile) {
    return ((const uint8_t *)profile)[0xac5] != 0;
}
static bool windows_paused(void *unused) {
    (void)unused;
    return mods_campaign();
} // Only called within active Menu hooks.
static void windows_flush(void) {
    void (*fn)(void *) = sym("win:modmenu FlushSaveData");
    _Alignas(16) uint8_t empty[64] = {0};
    if (fn)
        fn(empty);
}
static void windows_close(void *restore) {
    if (restore) {
        void (*destroy)(void *, unsigned) = (void *)((void ***)restore)[0][0];
        destroy(restore, 1);
    }
    int *count = sym("win:OpenRestoreDataCount");
    if (count)
        --*count;
}
#endif
static void campaign_changed(void) {
    if (!mods_campaign())
        modmenu_reset();
}

bool mm_platform_ready(void) {
    if (resolved)
        return supported;
    resolved = true;
    app = sym("App");
    profile_save = sym("_ZN15ProfileGameFile4SaveE12StorageMountbbjbb");
#ifdef _WIN32
    profile_in_raid = windows_in_raid;
    flush = windows_flush;
    close_restore = windows_close;
    paused = windows_paused;
    if (!sym("win:modmenu FlushSaveData") || !sym("win:OpenRestoreDataCount"))
        return false;
#else
    profile_in_raid = sym("_ZNK15ProfileGameFile8IsInRaidEv");
    flush = sym("_Z13FlushSaveDatav");
    open_restore = sym("_Z16open_RestoreData12StorageMount8c_stringjbt");
    close_restore = sym("_Z17close_RestoreDataP11RestoreData");
    paused = sym("_ZN7Darkest11GetIsPausedEv");
#endif
    open_restore = sym("_Z16open_RestoreData12StorageMount8c_stringjbt");
    supported = mm_text_ready() && app && profile_save && profile_in_raid && flush && open_restore && close_restore &&
                paused && bytes(PRE, pre_bytes, sizeof pre_bytes) && bytes(BASE, base_bytes, sizeof base_bytes) &&
                bytes(UPDATE, update_bytes, sizeof update_bytes) && bytes(LOAD, load_bytes, sizeof load_bytes) &&
                bytes(DEACTIVATE, deactivate_bytes, sizeof deactivate_bytes);
#ifndef _WIN32
    supported = supported && bytes(CLEANUP, cleanup_bytes, sizeof cleanup_bytes);
#endif
    return supported;
}
// Read-after-flush verifies the namespace through the game's storage reader, without restoring global options.
bool mm_platform_save(void) {
    if (!supported || !app || !paused(app) || !mm_is_open())
        return false;
    uint8_t *profile = (uint8_t *)app + 0x748; // Darkest::Save's ProfileGameFile, verified at 0x10028b670.
    profile_save(profile, 5, false, false, 0, true, profile_in_raid(profile));
    flush();
    void *restore = open_restore(5, "persist.game.json", 2, false, 0);
    if (!restore)
        return false;
    bool (*push)(void *, const char *) = (void *)((void ***)restore)[0][GL(0x10, 0x18) / 8];
    const char *text = NULL;
    bool (*get)(void *, const char *, const char **) = (void *)((void ***)restore)[0][GL(0x70, 0x80) / 8];
    bool ok = push(restore, "profile_options") && get(restore, "ddx_mod_options", &text) && text;
    // Compare the actual payload, not merely the existence of a prior saved namespace.
    const char *expected = modmenu_saved_namespace();
    ok = ok && expected && strnlen(text, MM_LIMIT + 1) <= MM_LIMIT && !strcmp(text, expected);
    close_restore(restore);
    return ok;
}
static void h_load(void *self, const void *result) {
    o_load(self, result);
    if (mods_campaign())
        modmenu_loaded();
}
#ifndef _WIN32
static void h_cleanup(void *self) {
    modmenu_reset();
    o_cleanup(self);
}
#endif
static void h_update(void *self, float dt) {
    if (!mm_is_open())
        o_update(self, dt);
}
static bool h_deactivate(void *self, bool force) {
    if (modmenu_closed_this_frame())
        return false;
    if (mm_is_open()) {
        mm_set_open(false);
        return false;
    }
    return o_deactivate(self, force);
}
static void h_pre(void *self) {
    if (mm_is_open())
        mm_draw();
    else
        o_pre(self);
}
static void h_base(void *self) {
    o_base(self);
    if (!mm_available() || !paused(app) || *(int *)((uint8_t *)self + GL(0x78, 0x80)) != 0)
        return;
    Vec2 c = ui_screen_centre(), pos = {c.x - 150, c.y + 280};
    bool hover = ui_mouse_in(pos, (Vec2){300, 38});
    ui_text((Vec2){c.x, pos.y}, "Mod Options", "confirm_dialog_answer", hover ? "white" : "confirm_dialog_answer");
    if (hover && ui_click(UI_MOUSE_LEFT, 8))
        mm_set_open(true);
}
void mm_platform_install(void) {
    if (!mm_platform_ready()) {
        logf_("modmenu: pause adapter unavailable for this build");
        return;
    }
#define HOOK(slot, name, fn, code)                                                                                     \
    *(void **)&slot = install_hook("modmenu " name, sym(name), (void *)fn, code, sizeof code)
    HOOK(o_load, LOAD, h_load, load_bytes);
#ifndef _WIN32
    HOOK(o_cleanup, CLEANUP, h_cleanup, cleanup_bytes);
#endif
    mods_on_campaign(campaign_changed);
    HOOK(o_update, UPDATE, h_update, update_bytes);
    HOOK(o_deactivate, DEACTIVATE, h_deactivate, deactivate_bytes);
    HOOK(o_pre, PRE, h_pre, pre_bytes);
    HOOK(o_base, BASE, h_base, base_bytes);
#undef HOOK
    supported = o_load && o_update && o_deactivate && o_pre && o_base;
#ifndef _WIN32
    supported = supported && o_cleanup;
#endif
    logf_("modmenu: native pause adapter %s", supported ? "installed" : "incomplete, disabled");
}
