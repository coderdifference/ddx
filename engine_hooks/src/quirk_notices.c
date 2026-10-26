// Quirk removal popups, on for everyone. The dungeon screen announces a quirk gained (Hero::AddQuirk's announce flag)
// and a curio's purge (Overlay_Event::InvestigateCurio's purge lambda shows curio_announcement_purge_format, "%s Quirk
// Removed!"), but nothing for any other quirk lost there: a quirk replaced at the limit, a transition, a mod's
// remove_quirk. After each of those (quirk_remove after, removed) this shows the purge's popup the same way
// (game_announce). Skipped: the purge itself (prop_applying() is 6 while its Apply runs) and anything while the dungeon
// screen isn't up (town treatment, an expedition loading; game_announce refuses).
#include "common.h"
#include <stdio.h>
#include <string.h>

static bool g_on;

bool quirk_notices_on(void) {
    return g_on;
}

// the game's format with its %s replaced by the quirk's name (a translation's other % characters stay as they are)
static void notice_text(char *out, size_t cap, const char *quirk) {
    char key[128];
    snprintf(key, sizeof key, "str_quirk_name_%.100s", quirk);
    const char *name = loc_exists(key) ? ui_loc(key) : quirk;
    const char *fmt = ui_loc("curio_announcement_purge_format");
    const char *at = strstr(fmt, "%s");
    if (!at || !strcmp(fmt, "curio_announcement_purge_format")) {
        snprintf(out, cap, "%s", name);
        return;
    }
    snprintf(out, cap, "%.*s%s%s", (int)(at - fmt), fmt, name, at + 2);
}

static void removed(HookContext *c, void *ud) {
    (void)ud;
    if (!c->result || !c->type || !*c->type || prop_applying() == 6)
        return;
    char text[128];
    notice_text(text, sizeof text, c->type);
    if (game_announce(text))
        logf_("quirk notices: %s", text);
}

void quirk_notices_install(void) {
    g_on = hook_on("quirk_remove", HOOK_AFTER, removed, NULL);
    if (!g_on)
        logf_("quirk notices: no quirk_remove point, off");
}
