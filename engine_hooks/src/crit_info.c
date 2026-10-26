// Class crit bonuses: every hero class's .info "crit: .effects" (e.g. the Occultist's and Vestal's on_crit_healdone,
// +25% healing skills) apply to the hero when it crits; vanilla never says so. Two places show them:
// - The character sheet's CRIT stat tooltip (HeroDisplay::App::ShowBaseStats's $_3 lambda; closure: +8 the
//   GuiTooltipParameters, its Text at +0x10; +0x18 the Actor) gets a title and a line per crit effect: its buffs'
//   descriptions as the buff tray writes them (Buff::Display::GetDescription) and its duration (engine_rounds_*).
//   The class's crit effects: ActorClass +0x568 / +0x570, std::vector<Effect const*> (Actor::GetSkillEffects appends
//   them on a crit); an effect's buffs: Effect +0x348 / +0x350, std::vector<unsigned> of buff id hashes
//   (Buff::Library::GetBuff), its duration +0x154. A buff keeps its own duration (Buff::Instance +0x4c) when that is
//   above 0; only a buff without one takes the effect's (EffectGetTargetActors).
// - The Buff! popup of a crit bonus (a buff whose source is bsrc_crit) shows engine_crit_popup_<buff id> (e.g.
//   "Crit: Healing!"), through popup_on_buff; a buff without that string keeps "Buff!" (a mod's own crit buffs: add
//   their strings).
// - In words (popup_on_line, for an expedition log such as an event-log client): "Dismas gains the Highwayman class
//   crit bonus: +15 dodge (2 rds)" (engine_crit_log).
// - Origin labels (buff_origin_on: the stat and combat tray tooltips) name a crit bonus engine_crit_label ("Class:
//   Crit") instead of the game's "Crit".
// The library's own: always on, its strings in the library package.
#include "common.h"
#include <stdio.h>
#include <string.h>

#define MAX_LINES 32

typedef struct {
    char s[256];
} Str256; // FixedLengthString<256>, returned by hidden pointer
static void (*o_crit_tip)(void *closure);
#ifndef _WIN32
static Str256 (*g_description)(const void *buff, const void *actor);
#else
static char *(*g_description)(char *out, const void *buff, const void *actor); // MSVC: returns the text, not always out
#endif
static const void *(*g_get_buff)(void *library, unsigned hash);
static void **g_buff_library;
static const char *(*g_source_id)(unsigned source);
static void (*g_add_line)(void *text, const void *line);
static void (*g_set_string)(void *line, const char *s);
static void (*g_colour_entry)(void *entry, unsigned id);
static uint64_t (*g_entry_hvec4)(const void *entry);

static Str256 description(const void *buff, const void *actor) {
#ifndef _WIN32
    return g_description(buff, actor);
#else
    Str256 d;
    d.s[0] = 0;
    const char *text = g_description(d.s, buff, actor);
    if (text && text != d.s)
        snprintf(d.s, sizeof d.s, "%s", text);
    return d;
#endif
}

// "until end of round", "for 1 round", "for {n} rounds": a raw round count as the game ends it (n - 1 rounds)
static void rounds_text(int n, char *out, size_t cap) {
    if (n <= 1) {
        snprintf(out, cap, "%s", ui_loc("engine_rounds_end"));
        return;
    }
    if (n == 2) {
        snprintf(out, cap, "%s", ui_loc("engine_rounds_one"));
        return;
    }
    char num[16];
    snprintf(num, sizeof num, "%d", n - 1);
    const char *keys[1] = {"n"}, *values[1] = {num};
    ui_format(out, cap, "engine_rounds_n", keys, values, 1);
}

static void add_line(void *text, const char *s, const char *font, const char *colour) {
    if (*(unsigned *)((uint8_t *)text + TEXT_COUNT) >= MAX_LINES)
        return;
    uint8_t line[0x28], entry[256];
    memset(line, 0, sizeof line);
    g_colour_entry(entry, name_hash(colour));
    *(void **)line = (void *)ui_font(font);
    *(uint64_t *)(line + 0x08) = g_entry_hvec4(entry);
    *(uint16_t *)(line + 0x10) = 1;
    line[0x20] = 1; // on its own line
    g_set_string(line, s);
    g_add_line(text, line);
}

// One crit effect's buffs' descriptions (repeats dropped, e.g. the low and high damage halves) and its duration (raw
// rounds). False: nothing to say.
static bool effect_text(const uint8_t *effect, const void *actor, char *out, size_t cap, int *rounds) {
    const unsigned *h = *(const unsigned *const *)(effect + EFFECT_BUFF_HASHES);
    const unsigned *end = *(const unsigned *const *)(effect + EFFECT_BUFF_HASHES + 8);
    char seen[4][256];
    int nseen = 0, duration = 0;
    int effect_duration = *(const int *)(effect + EFFECT_DURATION);
    size_t n = 0;
    out[0] = 0;
    for (; h && h < end && nseen < 4; h++) {
        const void *buff = g_get_buff(*g_buff_library, *h);
        if (!buff)
            continue;
        int own = *(const int *)((const uint8_t *)buff + BUFF_DURATION);
        int d_rounds = own > 0 ? own : effect_duration;
        if (d_rounds > duration)
            duration = d_rounds;
        Str256 d = description(buff, actor);
        if (!d.s[0])
            continue;
        bool dup = false;
        for (int i = 0; i < nseen; i++)
            dup |= !strcmp(seen[i], d.s);
        if (dup)
            continue;
        snprintf(seen[nseen++], sizeof seen[0], "%s", d.s);
        n += (size_t)snprintf(out + n, n < cap ? cap - n : 0, "%s%s", n ? ", " : "", d.s);
    }
    *rounds = duration;
    return out[0] != 0;
}

// The same as a line of the CRIT tooltip: the descriptions, then the duration ("for 2 rounds").
static bool effect_line(const uint8_t *effect, const void *actor, char *out, size_t cap) {
    int duration;
    if (!effect_text(effect, actor, out, cap, &duration))
        return false;
    size_t n = strlen(out);
    if (duration > 0 && n < cap) {
        char rounds[64];
        rounds_text(duration, rounds, sizeof rounds);
        snprintf(out + n, cap - n, " %s", rounds);
    }
    return true;
}

static void crit_tip_hook(void *closure) {
    o_crit_tip(closure);
    uint8_t *tip = *(uint8_t **)((uint8_t *)closure + CLOSURE_TOOLTIP);
    const void *actor = *(void **)((uint8_t *)closure + CLOSURE_ACTOR);
    if (!tip || !actor || !is_hero(actor))
        return;
    const uint8_t *cls = (*(const void *(*const *const *)(const void *))actor)[ACTOR_GET_CLASS / 8](actor);
    if (!cls)
        return;
    const uint8_t *const *e = *(const uint8_t *const *const *)(cls + CLASS_CRIT_EFFECTS);
    const uint8_t *const *end = *(const uint8_t *const *const *)(cls + CLASS_CRIT_EFFECTS + 8);
    bool titled = false;
    for (; e && e < end; e++) {
        char line[512];
        if (!*e || !effect_line(*e, actor, line, sizeof line))
            continue;
        if (!titled)
            add_line(tip + TIP_TEXT, ui_loc("engine_crit_bonus_title"), "tooltip", "notable");
        titled = true;
        add_line(tip + TIP_TEXT, line, "tooltip", "neutral");
    }
}

static const char *crit_popup(const void *actor, const void *buff) {
    (void)actor;
    const char *src = g_source_id ? g_source_id(*(const unsigned *)((const uint8_t *)buff + BUFF_SOURCE)) : NULL;
    if (!src || strcmp(src, "bsrc_crit"))
        return NULL;
    char key[128];
    snprintf(key, sizeof key, "engine_crit_popup_%.80s", (const char *)buff + BUFF_ID);
    return loc_exists(key) ? ui_loc(key) : NULL;
}

// A crit bonus popup in words (popup_on_line): "Dismas gains the Highwayman class crit bonus: +15 dodge (2 rds)"
// (engine_crit_log; the class's name hero_class_name_<id>; the crit effect holding the popup's buff).
static bool crit_log_line(const PopupEvent *p, PopupLine *out) {
    if (!p->buff || !p->actor || !is_hero(p->actor) || !g_source_id)
        return false;
    const char *src = g_source_id(*(const unsigned *)((const uint8_t *)p->buff + BUFF_SOURCE));
    if (!src || strcmp(src, "bsrc_crit"))
        return false;
    const uint8_t *cls = (*(const void *(*const *const *)(const void *))p->actor)[ACTOR_GET_CLASS / 8](p->actor);
    if (!cls)
        return false;
    unsigned id = name_hash((const char *)p->buff + BUFF_ID);
    const uint8_t *const *e = *(const uint8_t *const *const *)(cls + CLASS_CRIT_EFFECTS);
    const uint8_t *const *end = *(const uint8_t *const *const *)(cls + CLASS_CRIT_EFFECTS + 8);
    for (; e && e < end; e++) {
        const unsigned *h = *e ? *(const unsigned *const *)(*e + EFFECT_BUFF_HASHES) : NULL;
        const unsigned *hend = *e ? *(const unsigned *const *)(*e + EFFECT_BUFF_HASHES + 8) : NULL;
        bool holds = false;
        for (; h && h < hend && !holds; h++)
            holds = *h == id;
        int rounds;
        if (!holds || !effect_text(*e, p->actor, out->detail, sizeof out->detail, &rounds))
            continue;
        size_t n = strlen(out->detail);
        duration_text(rounds, *(const int *)(*e + EFFECT_DURATION_TYPE), out->detail + n, sizeof out->detail - n);
        char key[96];
        snprintf(key, sizeof key, "hero_class_name_%.60s", (const char *)cls + CLASS_ID);
        snprintf(out->source, sizeof out->source, "%s", ui_loc(key));
        snprintf(out->wording, sizeof out->wording, "%s", ui_loc("engine_crit_log"));
        return true;
    }
    return false;
}

static const char *crit_label(const void *actor, const void *buff) {
    (void)actor;
    const char *src = g_source_id ? g_source_id(*(const unsigned *)((const uint8_t *)buff + BUFF_SOURCE)) : NULL;
    return src && !strcmp(src, "bsrc_crit") ? ui_loc("engine_crit_label") : NULL;
}

void crit_info_install(void) {
    void *tip =
        sym("_ZNSt3__110__function6__funcIZN11HeroDisplay3App13ShowBaseStatsERK4Hero4vec2jbbPK5Actor12LayoutAnchorbE3$_"
            "3NS_9allocatorISC_EEFvvEEclEv");
    *(void **)&g_description = sym("_ZN4Buff7Display14GetDescriptionEPKNS_8InstanceEPK5Actor");
    *(void **)&g_get_buff = sym("_ZN4Buff7Library7GetBuffEj");
    g_buff_library = (void **)sym("_ZN4Buff7Library10s_InstanceE");
    *(void **)&g_source_id = sym("_ZN4Buff11GetSourceIdENS_6SourceE");
    *(void **)&g_add_line = sym("_ZN20GuiTooltipParameters4Text7AddLineERKNS0_4LineE");
    *(void **)&g_set_string = sym("_ZN20GuiTooltipParameters4Text4Line9SetStringEPKc");
    *(void **)&g_colour_entry = sym("_ZN13ColourLibrary8GetEntryEj");
    *(void **)&g_entry_hvec4 = sym("_ZNK13ColourLibrary5Entrycv5hvec4Ev");
    if (g_source_id) {
        popup_on_buff(crit_popup);
        buff_origin_on(crit_label);
    }
    if (g_source_id && g_get_buff && g_buff_library && g_description)
        popup_on_line(crit_log_line);
    else
        logf_("crit info: Buff::GetSourceId not found, crit bonus popups keep Buff!");
    if (!tip || !g_description || !g_get_buff || !g_buff_library || !g_add_line || !g_set_string || !g_colour_entry ||
        !g_entry_hvec4) {
        logf_("crit info: game functions not found, the CRIT tooltip has no crit bonus line");
        return;
    }
    game_hook_wrap("HeroDisplay::App::ShowBaseStats CRIT tooltip", (void *)crit_tip_hook, (void **)&o_crit_tip);
}
