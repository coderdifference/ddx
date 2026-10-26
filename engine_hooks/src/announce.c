// The library's announcement: a dialog saying Darkest Dungeon Extender is running and what it is
// (see common.h, "announce.c"). Its look is dialog.layout.json "announce" (a styled ui_confirm: blocks of text and
// pictures over the dialog frame), its text the package's strings (engine_announce_*).
//
// What: the registered mods applied to this campaign, in its mod list's order (mods.c). Nothing is shown unless the
// library package itself is applied (else its art and strings aren't mounted).
// When: the first screen of the campaign the player can act on, checked at each frame's end, if the campaign hasn't
// seen the library (campaign field "announced", campaigndata.c) or an applied registered mod (its field
// mod_<name hash>): so it comes back whenever a mod built on the library is added to the campaign, and marks that mod
// new in the list. Shown sets them all, saved with the campaign's next save.
//   - An expedition, the Old Road included: after the library's raid_ready point (exploring: the first frame the
//     dungeon takes input; a save loaded mid-battle: the first hero's turn), for the same Raid::s_Instance.
//   - The Hamlet: no raid, and the town's estate bar drew this frame (EstateSummary::PreChildrenShowInternal, wrapped
//     through the catalogue), the town isn't fading (TownDisplay::GetIsFadingInternal) and no building, news, town
//     event or other Hamlet display is open (TownDisplay::IsAnyBuildingActive, GetIsNewsDisplayVisible,
//     GetIsTownEventDisplayVisible, IsAnyHamletDisplayActive), for delay_frames frames in a row.
//   - Never while a question dialog is up or queued (ui_confirm_busy).
// Layout "announce.test" 1 shows it once per game start even when the campaign has seen it (for tuning the look).
// Windows: the TownDisplay checks are stand-ins (syms_win.c).
#include "common.h"
#include <stdio.h>
#include <string.h>

#define FIELD "announced"

// ---------------------------------------------------------------- the mods built on the library (the list)
#define MAX_MODS 24
#define ID_MAX   64
static struct {
    char name[ID_MAX], font[ID_MAX], colour[ID_MAX], field[16];
    int mod;
    bool fresh;
} g_mods[MAX_MODS];
static int g_nmods;
static char g_lines[MAX_MODS][512]; // the lines as drawn (name + new mark)

// 1: applied, 0: not, -1: unknown (no campaign)
static int applied(int mod) {
    return !mods_campaign() ? -1 : mods_pos(mod) >= 0 ? 1 : 0;
}

void announce_mod(const char *name_id, const char *font, const char *colour) {
    if (!name_id || !name_id[0])
        return;
    for (int i = 0; i < g_nmods; i++)
        if (!strcmp(g_mods[i].name, name_id))
            return;
    if (g_nmods >= MAX_MODS) {
        logf_("announce: more than %d mods, %s left out", MAX_MODS, name_id);
        return;
    }
    snprintf(g_mods[g_nmods].name, ID_MAX, "%s", name_id);
    snprintf(g_mods[g_nmods].font, ID_MAX, "%s", font ? font : "");
    snprintf(g_mods[g_nmods].colour, ID_MAX, "%s", colour ? colour : "");
    snprintf(g_mods[g_nmods].field, sizeof g_mods[0].field, "mod_%08x", name_hash(name_id));
    campaigndata_field(g_mods[g_nmods].field); // known before a campaign loads, so it's read back
    g_mods[g_nmods].mod = mods_current();
    logf_("announce: mod %s registered by %s (campaign field %s)", name_id,
          g_mods[g_nmods].mod >= 0 ? mods_folder(g_mods[g_nmods].mod) : "no mod", g_mods[g_nmods].field);
    g_nmods++;
}

// The applied mods in the campaign's mod list order (registration order while it isn't known)
static int mod_lines(UiConfirmLine *out, int max) {
    int order[MAX_MODS], pos[MAX_MODS], n = 0, k = 0;
    for (int i = 0; i < g_nmods; i++) {
        pos[i] = applied(g_mods[i].mod) < 0 ? -2 : mods_pos(g_mods[i].mod);
        if (pos[i] != -1)
            order[k++] = i;
    }
    for (int a = 1; a < k; a++) // stable: equal places keep registration order
        for (int b = a; b > 0 && pos[order[b - 1]] > pos[order[b]]; b--) {
            int t = order[b];
            order[b] = order[b - 1];
            order[b - 1] = t;
        }
    for (int j = 0; j < k && n < max; j++) {
        int i = order[j];
        snprintf(g_lines[i], sizeof g_lines[0], "%s%s", ui_loc(g_mods[i].name),
                 g_mods[i].fresh ? ui_loc("engine_announce_new") : "");
        out[n++] = (UiConfirmLine){g_lines[i], g_mods[i].font, g_mods[i].colour};
    }
    return n;
}

// Something this campaign hasn't announced: the library itself, or an applied mod added since. Nothing while the
// campaign's mods aren't known, or without the library package applied.
static bool unseen(void) {
    if (!mods_package_applied())
        return false;
    if (!campaigndata_get(FIELD))
        return true;
    for (int i = 0; i < g_nmods; i++)
        if (applied(g_mods[i].mod) == 1 && !campaigndata_get(g_mods[i].field))
            return true;
    return false;
}

static void **g_raid, **g_town;
static bool (*g_fading)(const void *town), (*g_news)(const void *town), (*g_event)(const void *town),
    (*g_hamlet)(const void *town), (*g_building)(void *town);
static void (*o_summary)(void *summary);
static unsigned g_town_frame;
static bool g_town_seen;
static const void *g_ready_raid; // the raid raid_ready last fired for
static int g_quiet;              // frames in a row the screen has been ready
static bool g_test_shown;

static void h_summary(void *summary) {
    o_summary(summary);
    g_town_frame = ui_frame();
    g_town_seen = true;
}

static void on_raid_ready(HookContext *c, void *ud) {
    (void)ud;
    g_ready_raid = c->raid;
}

static bool town_ready(void) {
    void *town = g_town ? *g_town : NULL;
    if (!town || !g_town_seen || ui_frame() - g_town_frame > 1)
        return false;
    if (g_fading && g_fading(town))
        return false;
    if ((g_news && g_news(town)) || (g_event && g_event(town)) || (g_hamlet && g_hamlet(town)) ||
        (g_building && g_building(town)))
        return false;
    return true;
}

static void show(void) {
    static const UiConfirmOption ok[1] = {{"ok", "engine_announce_ok", NULL}};
    bool first = !campaigndata_get(FIELD);
    int fresh = 0;
    for (int i = 0; i < g_nmods; i++) {
        g_mods[i].fresh = false;
        if (applied(g_mods[i].mod) != 1)
            continue; // not in this campaign: neither listed nor marked seen
        g_mods[i].fresh = !first && !campaigndata_get(g_mods[i].field); // the first time, every mod is new
        fresh += g_mods[i].fresh;
        campaigndata_set(g_mods[i].field, 1);
    }
    campaigndata_set(FIELD, 1);
    g_test_shown = true;
    logf_("announce: shown (%s, %d new mod(s); saved with the next save)", first ? "first time" : "mods added", fresh);
    ui_confirm_styled("announce", "", ok, 1, NULL, 0);
}

static void tick(void) {
    UiLayout *l = ui_layout_mod("dialog.layout.json");
    if (!l || !ui_layout_num(l, "announce.enabled", 1))
        return;
    bool test = ui_layout_num(l, "announce.test", 0) && !g_test_shown;
    void *raid = g_raid ? *g_raid : NULL;
    if (!raid)
        g_ready_raid = NULL; // a later raid at the same address isn't ready until it says so
    if ((!unseen() && !test) || ui_confirm_busy()) {
        g_quiet = 0;
        return;
    }
    bool ready;
    int delay;
    if (raid)
        ready = g_ready_raid == raid, delay = (int)ui_layout_num(l, "announce.raid_delay_frames", 1);
    else
        ready = town_ready(), delay = (int)ui_layout_num(l, "announce.delay_frames", 30);
    if (!ready) {
        g_quiet = 0;
        return;
    }
    if (++g_quiet >= delay) {
        g_quiet = 0;
        show();
    }
}

void announce_install(void) {
    campaigndata_field(FIELD);
    ui_confirm_lines("ddx_mods", mod_lines);
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    g_town = (void **)sym("_ZN6TownUI6Screen11TownDisplay10s_InstanceE");
    g_fading = (bool (*)(const void *))sym("_ZNK6TownUI6Screen11TownDisplay19GetIsFadingInternalEv");
    g_news = (bool (*)(const void *))sym("_ZNK6TownUI6Screen11TownDisplay23GetIsNewsDisplayVisibleEv");
    g_event = (bool (*)(const void *))sym("_ZNK6TownUI6Screen11TownDisplay28GetIsTownEventDisplayVisibleEv");
    g_hamlet = (bool (*)(const void *))sym("_ZNK6TownUI6Screen11TownDisplay24IsAnyHamletDisplayActiveEv");
    g_building = (bool (*)(void *))sym("_ZN6TownUI6Screen11TownDisplay19IsAnyBuildingActiveEv");
    if (!g_raid || !g_town) {
        logf_("announce: symbols missing, no announcement");
        return;
    }
    if (!game_hook_wrap("EstateSummary::PreChildrenShowInternal", (void *)h_summary, (void **)&o_summary))
        logf_("announce: the estate bar hook is off, no announcement in the Hamlet");
    hook_on("raid_ready", HOOK_AFTER, on_raid_ready, NULL);
    ui_on_frame_end(tick);
}
