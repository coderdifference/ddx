// Buff origins: what put a buff on an actor, by name, for the popups and the character sheet's stat tooltips.
//
// Recording: effects.c calls buff_origin_enter(target, performer, effect) / buff_origin_leave() around every
// EffectApplyInternal, and buff_origin_added(actor, guid, instance) from Buff::Group::AddBuff. A buff added inside an
// application gets an origin (actor, guid -> popup text, label):
//   - a named source (buff_source_effect: a client names the next application of an effect on a target, e.g.
//     a client mod affix triggers: popup "Ravenous!", label "Ravenous"), consumed by that application;
//   - else the skill being performed by the effect's performer (skill_resolution_*, cooldown.c), or, for an effect
//     applied after the skill returned (queued effects: .queue true, e.g. Shadow Fade's "GR Dodge 4"), the performer's
//     last used skill (skill_last_used) if the effect is one of that skill's (ActorCombatSkill::GetEffectAtIndex for
//     the performer's effect set, actor +0x11b0, as Actor::GetSkillEffects reads it), and the performer's name:
//     "Battle Ballad (Junia)", the skill's name coloured by who used it (hero "buff", monster "harmful");
//     ActorDisplay::App::GetCombatSkillName gives hero and monster skill names;
//   - else, for an effect from another actor without a skill, the game's source name and the performer's name
//     ("Trinket (Reynauld)").
// Origins live in a ring (the newest MAX_ORIGINS); named sources still pending clear at battle end.
//
// Naming (buff_origin_label): client callbacks first (buff_origin_on; a client mod names its gear, affix and set
// buffs), then the recorded origin, then the game's own sources: a trinket buff (source bsrc_trinket, source id = an
// equipped item's id hash: str_inventory_title_trinket<id>) and a quirk buff (bsrc_quirk; its source id is 0, so: the
// hero's quirk whose class lists a buff with this id: Hero +0x12f8 std::vector<Hero::QuirkInstance> (0x38 each, the
// Quirk::Class* at +0, as Hero::HasQuirkWithClassId reads it), the class's buffs at +0xb0 std::vector<Buff::Instance>
// (0x1d0 each, as Hero::AddQuirkAndApplyBuffs adds them), its id at +0: str_quirk_name_<id>). NULL: the game's source label stays.
//
// The character sheet's stat tooltips (HeroDisplay::App::ShowBaseStats' lambdas) end with
// StatAddAdditionalTooltipBuffLines(tooltip, buffs, b, u, mult, suffix, decimals): for each of the 34 Buff::Source
// values it totals the buffs of that source matching the stat (Buff::GetTotalFromFiltredInstanceList, filter by b, u
// and the source) and, if any, writes StatAddAdditionalTooltipStatAmount(tooltip, "buff_<source id>", total * mult,
// suffix, decimals): "%+.<decimals>f%s %s" (decimals 0: rounded half away from zero) with the localized label, font
// kStat ("stat"), colour kNeutral ("neutral"), skipped if |amount| <= 1e-6. This module owns both: the buffs (Buff::Instance pointers into
// the hero's group, actor +0x460, entries 0x1d8 with the instance at +8) are split by label; the unnamed ones go to
// the game's function as one list (its lines as before), each label's to it as its own list while the line function
// writes that label instead of the source's. The hero: ShowBaseStats' argument (hooked; its stat lambdas capture its
// stack, so they run inside it), kept only if the listed buffs lie in its group.
// Stat line labels (stat_label_on): a client may relabel any of the hero's stat tooltip lines by the game's label id
// (e.g. a client mod: "str_stat_base_weapon" -> the equipped weapon's name). The base lines are
// StatSetBaseTooltipStatAmount(tooltip, id, value, suffix, decimals): " %.<decimals>f%s %s", never skipped; the
// additional lines StatAddAdditionalTooltipStatAmount, as above.
// PROT: the sheet's PROT tooltip (ShowBaseStats' $_5) has no base line (vanilla armour gives no PROT) and lists buffs
// only when there are some; when the hero's armour part of PROT (GetCombatStat(3) -> RaidStat::GetBuffAddValue(3),
// the part the other stats call "str_stat_base_armour") isn't 0, a base line for it goes after the title, labelled
// as str_stat_base_armour (so stat_label_on clients rename it too).
// Resistances: ResistanceDisplay::SingleResistance(actor, type, pos) builds its tooltip inline: base, resolve level,
// then per Buff::Source the matching buffs (stat type 0xb, resistance type at +0x44, source at +0x54) totalled by
// Buff::GetTotalFromFiltredInstanceList (the call at +0x14be) and written "%+.0f%s %s" (total * 100, "%",
// buff_<source>), the trap disarm line, then GUI_Tooltip (+0x174c). While it runs for a hero, that total call sees only
// the buffs without a label; each labelled buff is totalled alone with the same filter and summed per label, and the
// labelled lines are added before GUI_Tooltip shows it (ui_tooltip_on_show). For the resistances with a skill chance
// (the buff stat <resistance id>_chance: stun, poison (blight), bleed, move, debuff) a blank line and the hero's own
// chance follow: a title (the mod's str_resist_chance_title_<resistance id>, "Bleed Skill Chance:"; without one, the
// total as the game describes the stat, buff_stat_tooltip_<stat>) and a line per origin, as the resistance's own
// lines list origins without a total.
// Combat tray icons (Overlay_Basic::ShowBuffIconTooltip(actor, type, ...), hooked for its actor and type): the
// buff lists (AddTrayBuffDescriptions, buffs and debuffs and the other icons' buff lines) go in groups by label (the
// origin, else the game's source name), each group's lines under its label, as the game's own header option writes
// one ("<label>\n<first line>"; a call asking for that header gets these labels). Bleed (type 7, buff stat 5),
// Blight (6, stat 4), Marked (12, stat 1), Stealth (23, stat 0x2b) and Riposte (18, source 14) list no buffs; a line per origin follows the
// game's ("<label>: <the game's DoT line for its buffs>", or the label for marks and stealth), added through the
// tooltip's GUI_HotSpot (+0x3b21; HotSpotParameters +0x50 is the GuiTooltipParameters) in the last line's font and
// colour.
// Windows (MSVC inlines what the macOS hooks catch): Buff::GetTotalFromFiltredInstanceList is inlined in
// SingleResistance, so its filter (lambda_4, the std::function it passes) is hooked instead: a labelled buff the
// filter takes adds to its label's total and is reported as not taken. AddTrayBuffDescriptions is inlined in most of
// ShowBuffIconTooltip's cases (Buffs and Debuffs among them), so every copy is caught through what it calls:
// Buff::Display::ConsolidateFilteredInstanceList consolidates each label's buffs apart (so no line merges two labels)
// and joins them, and the first described instance of each label gets "<label>\n" in front of its description
// (GetDescriptionWithDuration / GetDescription). Death's Door recovery (16) writes its source as a header itself.
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_PENDING          32
#define MAX_ORIGINS          256
#define MAX_SCOPES           8
#define POPUP_MAX            160
#define LABEL_MAX            160       // with colour markup
#define SKILL_COLOUR_HERO    "buff"    // colours.darkest ids: #5ec9d6, the Buff! popup's
#define SKILL_COLOUR_MONSTER "harmful" // 177 25 0, monster names
#define MAX_LIST             96
#define MAX_LABELS           16

static struct {
    const void *target;
    unsigned effect;
    char popup[POPUP_MAX], label[LABEL_MAX];
} g_pending[MAX_PENDING];
static int g_npending;
static struct {
    const void *actor;
    unsigned guid;
    char popup[POPUP_MAX], label[LABEL_MAX];
} g_origins[MAX_ORIGINS];
static int g_next_origin;
static struct {
    const void *target, *performer;
    char popup[POPUP_MAX], label[LABEL_MAX];
    bool named;
} g_scopes[MAX_SCOPES];
static int g_nscopes;

#define MAX_CLIENTS 4
static BuffOriginName g_clients[MAX_CLIENTS];
static int g_nclients;
static StatLabel g_stat_clients[MAX_CLIENTS];
static int g_nstat_clients;
static ModsList g_clients_mods, g_stat_mods; // their mods: the top mod's asked first

void stat_label_on(StatLabel fn) {
    if (fn && g_nstat_clients < MAX_CLIENTS && mods_list_add(&g_stat_mods) >= 0)
        g_stat_clients[g_nstat_clients++] = fn;
}

static void (*g_skill_name)(char *out64, const void *cls, const void *skill, bool with_level);
static const void *(*g_monster_class)(const void *monster);
static const char *(*g_source_id)(unsigned source);
static unsigned (*g_items)(const void *inventory);
static const uint8_t *(*g_item)(const void *inventory, unsigned i);
static unsigned (*g_item_hash)(const void *item);
static unsigned (*g_skill_effects)(const void *skill, unsigned set);
static const void *(*g_skill_effect)(const void *skill, unsigned set, unsigned i);

void buff_source_effect(const void *target, const char *effect, const char *popup, const char *label) {
    if (!target || !effect || ((!popup || !*popup) && (!label || !*label)))
        return;
    if (g_npending == MAX_PENDING)
        memmove(g_pending, g_pending + 1, sizeof g_pending[0] * --g_npending);
    g_pending[g_npending].target = target;
    g_pending[g_npending].effect = name_hash(effect);
    snprintf(g_pending[g_npending].popup, POPUP_MAX, "%s", popup ? popup : "");
    snprintf(g_pending[g_npending++].label, LABEL_MAX, "%s", label ? label : "");
}

void buff_origin_on(BuffOriginName fn) {
    if (fn && g_nclients < MAX_CLIENTS && mods_list_add(&g_clients_mods) >= 0)
        g_clients[g_nclients++] = fn;
}

static void actor_name(const void *actor, char *out, size_t n) {
    size_t i = 0;
    const char *s = actor ? (const char *)actor + ACTOR_NAME : "";
    while (i + 1 < n && i < 63 && s[i] >= 0x20 && s[i] != 0x7f) {
        out[i] = s[i];
        i++;
    }
    out[i] = 0;
}

// The skill's display name (hero or monster), or "" (unknown, or not a plain name).
static void skill_name(const void *actor, const void *skill, char *out, size_t n) {
    char buf[64] = "";
    const void *cls = is_hero(actor) ? *(const void *const *)((const uint8_t *)actor + HERO_CLASS)
                      : is_monster(actor) && g_monster_class ? g_monster_class(actor)
                                                             : NULL;
    if (cls && skill && g_skill_name)
        g_skill_name(buf, cls, skill, false);
    snprintf(out, n, "%s", buf[0] && buf[0] != '<' ? buf : "");
}

// Is effect (by name) one of skill's effects for the performer's effect set?
static bool skill_has_effect(const void *performer, const void *skill, const void *effect) {
    if (!skill || !effect || !g_skill_effects || !g_skill_effect)
        return false;
    unsigned set = *(const unsigned *)((const uint8_t *)performer + ACTOR_EFFECT_SET), n = g_skill_effects(skill, set);
    for (unsigned i = 0; i < n && i < 64; i++) {
        const void *e = g_skill_effect(skill, set, i);
        if (e && (e == effect || !strcmp((const char *)e, (const char *)effect)))
            return true;
    }
    return false;
}

void buff_origin_enter(const void *target, const void *performer, const void *effect) {
    int d = g_nscopes++;
    if (d >= MAX_SCOPES)
        return;
    g_scopes[d].target = target;
    g_scopes[d].performer = performer;
    g_scopes[d].popup[0] = g_scopes[d].label[0] = 0;
    g_scopes[d].named = false;
    unsigned h = effect ? name_hash((const char *)effect) : 0;
    for (int i = 0; effect && i < g_npending; i++) {
        if (g_pending[i].target != target || g_pending[i].effect != h)
            continue;
        snprintf(g_scopes[d].popup, POPUP_MAX, "%s", g_pending[i].popup);
        snprintf(g_scopes[d].label, LABEL_MAX, "%s", g_pending[i].label);
        g_scopes[d].named = true;
        memmove(g_pending + i, g_pending + i + 1, sizeof g_pending[0] * (size_t)(g_npending - i - 1));
        g_npending--;
        return;
    }
    char who[64], what[64] = "";
    actor_name(performer, who, sizeof who);
    if (!who[0])
        return;
    const void *skill = performer && skill_resolution_performer() == performer ? skill_resolution_skill() : NULL;
    if (!skill && performer) { // a queued effect of the skill the performer just used
        const void *last = skill_last_used(performer, NULL);
        if (skill_has_effect(performer, last, effect))
            skill = last;
    }
    if (skill)
        skill_name(performer, skill, what, sizeof what);
    if (what[0]) { // retain a plain popup name as well as the coloured tooltip label
        snprintf(g_scopes[d].popup, POPUP_MAX, "%s (%s)", what, who);
        snprintf(g_scopes[d].label, LABEL_MAX, "{colour_start|%s}%.60s{colour_end} (%.60s)",
                 is_hero(performer) ? SKILL_COLOUR_HERO : SKILL_COLOUR_MONSTER, what, who);
    }
}

void buff_origin_leave(void) {
    if (g_nscopes > 0)
        g_nscopes--;
}

const char *buff_origin_scope_popup(const void *actor) {
    if (g_nscopes <= 0 || g_nscopes > MAX_SCOPES)
        return NULL;
    const typeof(g_scopes[0]) *s = &g_scopes[g_nscopes - 1];
    return s->target == actor && s->popup[0] ? s->popup : NULL;
}

void buff_origin_added(const void *actor, unsigned guid, const void *instance) {
    if (g_nscopes <= 0 || g_nscopes > MAX_SCOPES || !actor)
        return;
    const typeof(g_scopes[0]) *s = &g_scopes[g_nscopes - 1];
    char label[LABEL_MAX];
    snprintf(label, sizeof label, "%s", s->label);
    if (!label[0] && !s->named && s->performer && s->performer != actor && instance && g_source_id) {
        // no skill: the game's source name and who applied it
        char key[80], who[64];
        const char *src = g_source_id(*(const unsigned *)((const uint8_t *)instance + BUFF_SOURCE));
        snprintf(key, sizeof key, "buff_%s", src ? src : "");
        actor_name(s->performer, who, sizeof who);
        if (src && loc_exists(key) && who[0])
            snprintf(label, sizeof label, "%s (%s)", ui_loc(key), who);
    }
    if (!label[0] && !s->popup[0])
        return;
    g_origins[g_next_origin].actor = actor;
    g_origins[g_next_origin].guid = guid;
    snprintf(g_origins[g_next_origin].popup, POPUP_MAX, "%s", s->popup);
    snprintf(g_origins[g_next_origin].label, LABEL_MAX, "%s", label);
    g_next_origin = (g_next_origin + 1) % MAX_ORIGINS;
}

static int find_origin(const void *actor, unsigned guid) {
    for (int k = 1; k <= MAX_ORIGINS; k++) { // newest first: a guid reused later wins
        int i = (g_next_origin - k + MAX_ORIGINS) % MAX_ORIGINS;
        if (g_origins[i].actor == actor && g_origins[i].guid == guid)
            return i;
    }
    return -1;
}

const char *buff_origin_popup(const void *actor, unsigned guid) {
    int i = find_origin(actor, guid);
    return i >= 0 && g_origins[i].popup[0] ? g_origins[i].popup : NULL;
}

// The game's own sources we can name: an equipped trinket, a quirk.
static bool vanilla_label(const void *actor, const uint8_t *buff, char *out, size_t cap) {
    const char *src = g_source_id ? g_source_id(*(const unsigned *)(buff + BUFF_SOURCE)) : NULL;
    unsigned id = *(const unsigned *)(buff + BUFF_SOURCE_ID);
    char key[128];
    if (src && !strcmp(src, "bsrc_trinket") && g_items && g_item && g_item_hash) {
        const void *inv = (const uint8_t *)actor + ACTOR_TRINKETS;
        unsigned n = g_items(inv);
        for (unsigned i = 0; i < n && i < 16; i++) {
            const uint8_t *item = g_item(inv, i);
            if (!item || g_item_hash(item) != id)
                continue;
            snprintf(key, sizeof key, "str_inventory_title_trinket%.80s", (const char *)item + ITEM_ID);
            if (!loc_exists(key))
                return false;
            snprintf(out, cap, "%s", ui_loc(key));
            return true;
        }
    }
    if (src && !strcmp(src, "bsrc_quirk") && is_hero(actor)) {
        const uint8_t *const *qv = (const uint8_t *const *)((const uint8_t *)actor + HERO_QUIRKS);
        for (const uint8_t *q = qv[0]; q && q + QUIRK_SIZE <= qv[1]; q += QUIRK_SIZE) {
            const uint8_t *cls = *(const uint8_t *const *)q;
            const uint8_t *const *bv = cls ? (const uint8_t *const *)(cls + QUIRKCLASS_BUFFS) : NULL;
            for (const uint8_t *b = bv ? bv[0] : NULL; b && b + BUFF_INSTANCE_SIZE <= bv[1]; b += BUFF_INSTANCE_SIZE) {
                if (strcmp((const char *)b + BUFF_ID, (const char *)buff + BUFF_ID))
                    continue;
                snprintf(key, sizeof key, "str_quirk_name_%.80s", (const char *)cls);
                if (!loc_exists(key))
                    return false;
                snprintf(out, cap, "%s", ui_loc(key));
                return true;
            }
        }
    }
    return false;
}

bool buff_origin_label(const void *actor, const void *buff, char *out, size_t cap) {
    if (!actor || !buff || !cap)
        return false;
    out[0] = 0;
    MODS_EACH(g_clients_mods, true, i) {
        const char *t = g_clients[i](actor, buff);
        if (t && *t) {
            snprintf(out, cap, "%s", t);
            return true;
        }
    }
    int k = find_origin(actor, *(const unsigned *)((const uint8_t *)buff - 8));
    if (k >= 0 && g_origins[k].label[0]) {
        snprintf(out, cap, "%s", g_origins[k].label);
        return true;
    }
    return vanilla_label(actor, buff, out, cap);
}

static void clear_pending(HookContext *ctx, void *ud) {
    (void)ctx;
    (void)ud;
    g_npending = 0;
}

// ---------------------------------------------------------------- stat tooltips
typedef struct {
    const void **begin, **end, **cap;
} PtrList;
static void (*o_lines)(void *tip, const PtrList *buffs, bool b, unsigned u, float mult, const char *suffix,
                       unsigned dec);
static void (*o_amount)(void *tip, const char *id, float value, const char *suffix, unsigned dec);
static void (*o_base)(void *tip, const char *id, float value, const char *suffix, unsigned dec);
static void (*o_show_stats)(const void *hero, Vec2 pos, unsigned a, bool b, bool c, const void *actor, int anchor,
                            bool d);
static void (*g_add_line)(void *text, const void *line);
static void (*g_set_string)(void *line, const char *s);
static void (*g_colour_entry)(void *entry, unsigned id);
static uint64_t (*g_entry_hvec4)(const void *entry);
static const void *g_sheet_hero; // the hero ShowBaseStats is drawing (NULL outside it)
static const char *g_line_label; // set: the line function writes this label

static void show_stats_hook(const void *hero, Vec2 pos, unsigned a, bool b, bool c, const void *actor, int anchor,
                            bool d) {
    const void *outer = g_sheet_hero;
    g_sheet_hero = hero;
    o_show_stats(hero, pos, a, b, c, actor, anchor, d);
    g_sheet_hero = outer;
}

// A client's label for the stat line whose game label id is id, or NULL.
static const char *stat_label(const char *id) {
    if (g_sheet_hero && id)
        MODS_EACH(g_stat_mods, true, i) {
            const char *t = g_stat_clients[i](g_sheet_hero, id);
            if (t && *t)
                return t;
        }
    return NULL;
}

// The line as StatSetBaseTooltipStatAmount (sign false: " %.Nf") / StatAddAdditionalTooltipStatAmount (sign true:
// "%+.Nf") write it, with label as the text.
static void write_line_text(void *tip, const char *text);
static void write_line_text_colour(void *tip, const char *text, const char *colour);

static void write_line(void *tip, bool sign, float value, const char *suffix, unsigned dec, const char *label) {
    double v = dec ? value : truncf(value + copysignf(0.49999997f, value));
    char text[512];
    if (!label[0])
        text[0] = ' ', text[1] = 0; // a blank line
    else
        snprintf(text, sizeof text, sign ? "%+.*f%s %s" : " %.*f%s %s", (int)dec, v, suffix ? suffix : "", label);
    write_line_text(tip, text);
}

// One line of text, as the stat tooltip lines are drawn (font kStat, colour kNeutral).
static void write_line_text(void *tip, const char *text) {
    write_line_text_colour(tip, text, "neutral");
}

static void write_line_text_colour(void *tip, const char *text, const char *colour) {
    if (*(unsigned *)((uint8_t *)tip + 0x520) >= 32)
        return;
    uint8_t line[0x28], entry[256];
    memset(line, 0, sizeof line);
    g_colour_entry(entry, name_hash(colour)); // the game's ColourIds::kNeutral, FontIds::kStat
    *(void **)line = (void *)ui_font("stat");
    *(uint64_t *)(line + 0x08) = g_entry_hvec4(entry);
    *(uint16_t *)(line + 0x10) = 1;
    line[0x20] = 1;
    char encoded[512];
    snprintf(encoded, sizeof encoded, "%s", text);
    tooltip_encode_colours(encoded, sizeof encoded); // labels may carry {colour_start|x} markup
    g_set_string(line, encoded);
    g_add_line((uint8_t *)tip + 0x10, line);
}

static void amount_hook(void *tip, const char *id, float value, const char *suffix, unsigned dec) {
    const char *label = g_line_label ? g_line_label : stat_label(id);
    if (!label) {
        o_amount(tip, id, value, suffix, dec);
        return;
    }
    if (fabsf(value) > 1e-6f)
        write_line(tip, true, value, suffix, dec, label);
}

static void base_hook(void *tip, const char *id, float value, const char *suffix, unsigned dec) {
    const char *label = stat_label(id);
    if (!label) {
        o_base(tip, id, value, suffix, dec);
        return;
    }
    write_line(tip, false, value, suffix, dec, label);
}

static bool in_group(const void *actor, const void *buff) {
    const uint8_t *const *g = (const uint8_t *const *)((const uint8_t *)actor + ACTOR_BUFF_GROUP);
    const uint8_t *p = buff;
    return g[0] && p >= g[0] + 8 && p < g[1] && (size_t)(p - 8 - g[0]) % BUFF_ENTRY_SIZE == 0;
}

static void lines_hook(void *tip, const PtrList *buffs, bool b, unsigned u, float mult, const char *suffix,
                       unsigned dec) {
    const void *hero = g_sheet_hero;
    size_t n = buffs && buffs->begin && buffs->end > buffs->begin ? (size_t)(buffs->end - buffs->begin) : 0;
    if (!hero || !n || n > MAX_LIST) {
        o_lines(tip, buffs, b, u, mult, suffix, dec);
        return;
    }
    for (size_t i = 0; i < n; i++)
        if (!in_group(hero, buffs->begin[i])) {
            o_lines(tip, buffs, b, u, mult, suffix, dec);
            return;
        }
    static char labels[MAX_LABELS][LABEL_MAX];
    int nlabels = 0, of[MAX_LIST];
    const void *plain[MAX_LIST];
    size_t nplain = 0;
    for (size_t i = 0; i < n; i++) {
        char l[LABEL_MAX];
        of[i] = -1;
        if (!buff_origin_label(hero, buffs->begin[i], l, sizeof l)) {
            plain[nplain++] = buffs->begin[i];
            continue;
        }
        int k = 0;
        while (k < nlabels && strcmp(labels[k], l))
            k++;
        if (k == nlabels) {
            if (nlabels == MAX_LABELS) {
                plain[nplain++] = buffs->begin[i];
                continue;
            }
            snprintf(labels[nlabels++], LABEL_MAX, "%s", l);
        }
        of[i] = k;
    }
    if (!nlabels) {
        o_lines(tip, buffs, b, u, mult, suffix, dec);
        return;
    }
    PtrList sub = {plain, plain + nplain, plain + nplain};
    if (nplain)
        o_lines(tip, &sub, b, u, mult, suffix, dec);
    for (int k = 0; k < nlabels; k++) {
        const void *mine[MAX_LIST];
        size_t m = 0;
        for (size_t i = 0; i < n; i++)
            if (of[i] == k)
                mine[m++] = buffs->begin[i];
        PtrList one = {mine, mine + m, mine + m};
        g_line_label = labels[k];
        o_lines(tip, &one, b, u, mult, suffix, dec);
        g_line_label = NULL;
    }
}
// ---------------------------------------------------------------- PROT and resistance tooltips
#define TIP_MAX_LINES    32
#define STAT_PROTECTION  3 // ActorCombatStat::Types
#define STAT_PART_ARMOUR 3 // RaidStat::GetBuffAddValue: the armour part ("str_stat_base_armour")
#ifdef _WIN32
#define RESIST_TIP_CALL 0xb3d // SingleResistance 0x80d610 + this: GUI_Tooltip
#else
#define RESIST_TOTAL_CALL 0x14be // SingleResistance + this: its per-source GetTotalFromFiltredInstanceList
#define RESIST_TIP_CALL   0x174c // SingleResistance + this: GUI_Tooltip
#endif
static void (*o_prot)(void *closure);
static const void *(*g_combat_stat)(const void *actor, int type);
static float (*g_part_value)(const void *raid_stat, unsigned part);
static void (*o_resist)(const void *actor, int type, Vec2 pos);
static const char *(*g_resist_id)(int type);
static int (*g_stat_type)(const char *id);
static PtrList (*g_active_buffs)(const void *actor);
#ifdef _WIN32
static bool (*o_resist_filter)(void *closure, const void *const *buff);
static void (*g_deallocate)(void *p, size_t bytes); // MSVC's std::_Deallocate (syms_win.c)
#else
static float (*o_total)(const PtrList *buffs, unsigned *count, void *filter);
static void (*g_delete)(void *);
static const uint8_t *g_total_ret;
#endif
static const uint8_t *g_resist_tip_ret;
static const void *g_resist_hero;
static int g_resist_type;
static struct {
    char label[LABEL_MAX];
    float total;
} g_racc[MAX_LABELS];
static int g_nracc;

// Move the tooltip's last line to index at (after the title).
static void move_last_line(void *tip, unsigned at) {
    uint8_t *lines = (uint8_t *)tip + TIP_LINES;
    unsigned n = *(unsigned *)((uint8_t *)tip + TIP_COUNT);
    if (n < 2 || at >= n - 1 || n > TIP_MAX_LINES)
        return;
    uint8_t last[LINE_SIZE];
    memcpy(last, lines + (n - 1) * LINE_SIZE, LINE_SIZE);
    memmove(lines + (at + 1) * LINE_SIZE, lines + at * LINE_SIZE, (n - 1 - at) * LINE_SIZE);
    memcpy(lines + at * LINE_SIZE, last, LINE_SIZE);
}

static void prot_hook(void *closure) {
    o_prot(closure);
    void *tip = *(void **)((uint8_t *)closure + 0x08);
    const void *hero = g_sheet_hero;
    if (!tip || !hero || *(unsigned *)((uint8_t *)tip + TIP_COUNT) >= TIP_MAX_LINES)
        return;
    const void *stat = g_combat_stat(hero, STAT_PROTECTION);
    float v = stat ? g_part_value(stat, STAT_PART_ARMOUR) * 100.0f : 0.0f;
    if (!(fabsf(v) > 1e-6f))
        return;
    const char *label = stat_label("str_stat_base_armour");
    write_line(tip, false, v, "%", 0, label ? label : ui_loc("str_stat_base_armour"));
    move_last_line(tip, 1);
}

static void resist_hook(const void *actor, int type, Vec2 pos) {
    const void *outer = g_resist_hero;
    int outer_type = g_resist_type;
    g_resist_hero = is_hero(actor) ? actor : NULL;
    g_resist_type = type;
    g_nracc = 0;
    o_resist(actor, type, pos);
    g_resist_hero = outer;
    g_resist_type = outer_type;
}

// Adds v to label's resistance line; false: no room for another label (the buff stays in the game's lines).
static bool racc_add(const char *label, float v) {
    int k = 0;
    while (k < g_nracc && strcmp(g_racc[k].label, label))
        k++;
    if (k == g_nracc) {
        if (g_nracc == MAX_LABELS)
            return false;
        snprintf(g_racc[g_nracc].label, LABEL_MAX, "%s", label);
        g_racc[g_nracc++].total = 0;
    }
    g_racc[k].total += v;
    return true;
}

#ifdef _WIN32
// SingleResistance's filter (per source, for each of the hero's active buffs): a labelled buff it takes goes to its
// label's line instead of the source's.
static bool resist_filter_hook(void *closure, const void *const *buff) {
    bool taken = o_resist_filter(closure, buff);
    const uint8_t *b = taken && g_resist_hero && buff ? *buff : NULL;
    char l[LABEL_MAX];
    if (!b || !in_group(g_resist_hero, b) || !buff_origin_label(g_resist_hero, b, l, sizeof l))
        return taken;
    return !racc_add(l, *(const float *)(b + BUFF_AMOUNT));
}
#else
static float total_hook(const PtrList *buffs, unsigned *count, void *filter) {
    if (!g_resist_hero || __builtin_return_address(0) != g_total_ret)
        return o_total(buffs, count, filter);
    size_t n = buffs && buffs->begin && buffs->end > buffs->begin ? (size_t)(buffs->end - buffs->begin) : 0;
    if (n > MAX_LIST)
        return o_total(buffs, count, filter);
    const void *plain[MAX_LIST];
    size_t nplain = 0;
    for (size_t i = 0; i < n; i++) {
        char l[LABEL_MAX];
        const void *b = buffs->begin[i];
        if (!in_group(g_resist_hero, b) || !buff_origin_label(g_resist_hero, b, l, sizeof l)) {
            plain[nplain++] = b;
            continue;
        }
        PtrList one = {&buffs->begin[i], &buffs->begin[i] + 1, &buffs->begin[i] + 1};
        unsigned c = 0;
        float v = o_total(&one, &c, filter);
        if (c && !racc_add(l, v))
            plain[nplain++] = b;
    }
    PtrList sub = {plain, plain + nplain, plain + nplain};
    return o_total(&sub, count, filter);
}
#endif

// The hero's skill chance for a resistance (its <id>_chance buffs), after a blank line: the total as the game
// describes the stat, then a line per origin (the game's source name for unnamed buffs).
static void add_penetration(void *tip, const void *hero, int type) {
    const char *rid = g_resist_id ? g_resist_id(type) : NULL;
    if (!rid || !g_stat_type || !g_active_buffs)
        return;
    char stat[48], key[80];
    snprintf(stat, sizeof stat, "%s_chance", rid);
    snprintf(key, sizeof key, "buff_stat_tooltip_%s", stat);
    if (!loc_exists(key))
        return; // no skill chance for this resistance
    int st = g_stat_type(stat);
    PtrList all = g_active_buffs(hero);
    struct {
        char label[LABEL_MAX];
        float total;
    } acc[MAX_LABELS];
    int nacc = 0;
    float sum = 0;
    for (const void **p = all.begin; p && p < all.end; p++) {
        const uint8_t *b = *p;
        if (!b || *(const int *)(b + BUFF_STAT) != st)
            continue;
        char l[LABEL_MAX] = "";
        if (!(in_group(hero, b) && buff_origin_label(hero, b, l, sizeof l)) && g_source_id) {
            char sk[80];
            const char *src = g_source_id(*(const unsigned *)(b + BUFF_SOURCE));
            snprintf(sk, sizeof sk, "buff_%s", src ? src : "");
            snprintf(l, sizeof l, "%s", loc_exists(sk) ? ui_loc(sk) : "");
        }
        float v = *(const float *)(b + BUFF_AMOUNT);
        sum += v;
        int k = 0;
        while (k < nacc && strcmp(acc[k].label, l))
            k++;
        if (k == nacc && nacc < MAX_LABELS) {
            snprintf(acc[nacc].label, LABEL_MAX, "%s", l);
            acc[nacc++].total = 0;
        }
        if (k < nacc)
            acc[k].total += v;
    }
#ifdef _WIN32
    if (all.begin && g_deallocate)
        g_deallocate((void *)all.begin, (size_t)((const uint8_t *)all.cap - (const uint8_t *)all.begin));
#else
    if (all.begin && g_delete)
        g_delete((void *)all.begin);
#endif
    if (!nacc)
        return;
    char head[160], title[80];
    snprintf(title, sizeof title, "str_resist_chance_title_%s", rid);
    if (loc_exists(title))
        snprintf(head, sizeof head, "%s", ui_loc(title));
    else
        snprintf(head, sizeof head, ui_loc(key), (int)lroundf(sum * 100.0f));
    write_line(tip, false, 0, "", 0, ""); // the blank line
    write_line_text_colour(tip, head, "notable");
    for (int k = 0; k < nacc; k++)
        write_line(tip, true, acc[k].total * 100.0f, "%", 0, acc[k].label);
}

static void resist_tip(void *params, const void *ret, const uint8_t *caller) {
    (void)caller;
    if (!g_resist_hero || ret != g_resist_tip_ret || !params)
        return;
    for (int k = 0; k < g_nracc; k++)
        if (fabsf(g_racc[k].total) > 1e-6f)
            write_line(params, true, g_racc[k].total * 100.0f, "%", 0, g_racc[k].label);
    add_penetration(params, g_resist_hero, g_resist_type);
}

// ---------------------------------------------------------------- combat tray icon tooltips
#define TRAY_BLIGHT         6 // TrayIconTypes (the textures GetBuffIconTexture picks per type)
#define TRAY_BLEED          7
#define TRAY_MARK           12
#define TRAY_RIPOSTE        18
#define TRAY_STEALTH        23
#define BUFF_SOURCE_RIPOSTE 14 // GetTrayRiposteBuffInstanceList: source 14, not a status stat
#define BUFF_STAT_TAG       1  // Buff::Stat::Types the tooltip's predicates match ($_6, $_0, $_1)
#define BUFF_STAT_POISON    4
#define BUFF_STAT_BLEED     5
#define BUFF_STAT_STEALTH   0x2b // the stealth tooltip's predicate ($_9)
#define TRAY_DD_RECOVERY    16   // Death's Door recovery: its lines open with their source (the header option)
#define LINE_TEXT           256  // FixedLengthString<256>
#ifdef _WIN32
#define TRAY_HOTSPOT_CALL 0x25f4 // ShowBuffIconTooltip 0x6f08b0 + this: its GUI_HotSpot
#else
#define TRAY_HOTSPOT_CALL 0x3b21 // ShowBuffIconTooltip + this: its GUI_HotSpot
typedef struct {
    char (*begin)[LINE_TEXT], (*end)[LINE_TEXT], (*cap)[LINE_TEXT];
} LineVec;
static void (*o_tray_lines)(void *self, LineVec *out, const PtrList *buffs, const void *actor, bool header, bool b,
                            bool dur);
#endif
static void (*o_tray_tip)(void *self, void *actor, int type, uint64_t id, Vec2 pos, Vec2 size, int anchor, void *raid,
                          unsigned sub);
static bool (*g_is_status)(int stat); // Buff::Stat::GetIsStatusFromType
static const uint8_t *g_tray_hotspot_ret;
static const void *g_tray_actor;
static int g_tray_type = -1;

// A buff's label: its origin, else the game's name for its source.
static bool label_or_source(const void *actor, const uint8_t *b, char *out, size_t cap) {
    if (in_group(actor, b) && buff_origin_label(actor, b, out, cap))
        return true;
    const char *src = g_source_id ? g_source_id(*(const unsigned *)(b + BUFF_SOURCE)) : NULL;
    char key[80];
    snprintf(key, sizeof key, "buff_%s", src ? src : "");
    snprintf(out, cap, "%s", src && loc_exists(key) ? ui_loc(key) : "");
    return out[0] != 0;
}

// The tray tooltip's buffs in groups by label: labels[k], of[i] buff i's group (-1: no label). -1: not the tooltip's
// actor's buffs.
static int tray_groups(const void *actor, const PtrList *buffs, char labels[][LABEL_MAX], int *of) {
    size_t n = buffs && buffs->begin && buffs->end > buffs->begin ? (size_t)(buffs->end - buffs->begin) : 0;
    if (!g_tray_actor || actor != g_tray_actor || !n || n > MAX_LIST)
        return -1;
    for (size_t i = 0; i < n; i++)
        if (!in_group(actor, buffs->begin[i]))
            return -1;
    int nlabels = 0;
    for (size_t i = 0; i < n; i++) {
        char l[LABEL_MAX];
        of[i] = -1;
        if (!label_or_source(actor, buffs->begin[i], l, sizeof l))
            continue;
        int k = 0;
        while (k < nlabels && strcmp(labels[k], l))
            k++;
        if (k == nlabels && nlabels < MAX_LABELS)
            snprintf(labels[nlabels++], LABEL_MAX, "%s", l);
        if (k < nlabels)
            of[i] = k;
    }
    return nlabels;
}

#ifdef _WIN32
// The lists' copies of AddTrayBuffDescriptions: ConsolidateFilteredInstanceList(list), then for each instance with a
// description GetDescriptionWithDuration (or GetDescription) as a line. Each label's buffs are consolidated apart and
// joined (plain first, as the game lists them), and the first described instance of each label's group gets the
// label in front, as macOS's tray_lines_hook writes them.
typedef struct {
    uint8_t *begin, *end, *cap;
} InstanceVec; // std::vector<Buff::Instance>
static InstanceVec *(*o_consolidate)(InstanceVec *out, const PtrList *buffs);
static char *(*o_desc_duration)(char *out, const void *instance, const void *actor, bool b);
static char *(*o_desc)(char *out, const void *instance, const void *actor);
static void *(*g_allocate)(size_t bytes); // MSVC's std::_Allocate ("_Znwm" on Windows)
static char g_cons_labels[MAX_LABELS][LABEL_MAX];
static struct {
    const uint8_t *begin;
    size_t n;
    int nlabels;
    bool described;
    struct {
        size_t start, end;
        bool done;
    } group[MAX_LABELS];
} g_cons;
static int g_desc_depth; // GetDescriptionWithDuration calls GetDescription

static InstanceVec *consolidate_hook(InstanceVec *out, const PtrList *buffs) {
    int of[MAX_LIST];
    int nlabels = tray_groups(g_tray_actor, buffs, g_cons_labels, of);
    g_cons.n = 0;
    size_t n = nlabels > 0 ? (size_t)(buffs->end - buffs->begin) : 0;
    uint8_t *joined = n && g_allocate && g_deallocate ? g_allocate(n * BUFF_INSTANCE_SIZE) : NULL;
    if (!joined)
        return o_consolidate(out, buffs);
    size_t total = 0;
    for (int k = -1; k < nlabels; k++) {
        const void *mine[MAX_LIST];
        size_t m = 0;
        for (size_t i = 0; i < n; i++)
            if (of[i] == k)
                mine[m++] = buffs->begin[i];
        if (k >= 0)
            g_cons.group[k].start = total, g_cons.group[k].done = false;
        if (m) {
            PtrList one = {mine, mine + m, mine + m};
            InstanceVec part = {0};
            o_consolidate(&part, &one);
            if (part.begin) { // the instances move: their members hold no pointers to themselves
                size_t bytes = (size_t)(part.end - part.begin);
                memcpy(joined + total * BUFF_INSTANCE_SIZE, part.begin, bytes);
                total += bytes / BUFF_INSTANCE_SIZE;
                g_deallocate(part.begin, (size_t)(part.cap - part.begin));
            }
        }
        if (k >= 0)
            g_cons.group[k].end = total;
    }
    out->begin = joined;
    out->end = joined + total * BUFF_INSTANCE_SIZE;
    out->cap = joined + n * BUFF_INSTANCE_SIZE; // the size allocated: the vector frees it by its capacity
    g_cons.begin = joined;
    g_cons.n = total;
    g_cons.nlabels = nlabels;
    g_cons.described = false;
    return out;
}

// The label to write before this instance's description, or NULL.
static const char *cons_label(const void *instance) {
    const uint8_t *p = instance;
    if (!g_tray_actor || !g_cons.n || !p || p < g_cons.begin || p >= g_cons.begin + g_cons.n * BUFF_INSTANCE_SIZE)
        return NULL;
    size_t i = (size_t)(p - g_cons.begin) / BUFF_INSTANCE_SIZE;
    bool first = !g_cons.described;
    g_cons.described = true;
    for (int k = 0; k < g_cons.nlabels; k++) {
        if (i < g_cons.group[k].start || i >= g_cons.group[k].end)
            continue;
        if (g_cons.group[k].done)
            return NULL;
        g_cons.group[k].done = true;
        if (first && g_tray_type == TRAY_DD_RECOVERY && g_source_id) { // the game writes its source before it
            char key[80];
            const char *src = g_source_id(*(const unsigned *)(p + BUFF_SOURCE));
            snprintf(key, sizeof key, "buff_%s", src ? src : "");
            if (src && loc_exists(key) && !strcmp(ui_loc(key), g_cons_labels[k]))
                return NULL;
        }
        return g_cons_labels[k];
    }
    return NULL;
}

static char *labelled(const char *label, const char *text) {
    static char line[2 * LINE_TEXT];
    snprintf(line, sizeof line, "%s\n%s", label, text ? text : "");
    tooltip_encode_colours(line, sizeof line);
    return line;
}

static char *desc_duration_hook(char *out, const void *instance, const void *actor, bool b) {
    g_desc_depth++;
    char *text = o_desc_duration(out, instance, actor, b);
    g_desc_depth--;
    const char *label = cons_label(instance);
    return label ? labelled(label, text) : text;
}

static char *desc_hook(char *out, const void *instance, const void *actor) {
    char *text = o_desc(out, instance, actor);
    const char *label = g_desc_depth ? NULL : cons_label(instance);
    return label ? labelled(label, text) : text;
}
#endif

static void tray_tip_hook(void *self, void *actor, int type, uint64_t id, Vec2 pos, Vec2 size, int anchor, void *raid,
                          unsigned sub) {
    const void *outer = g_tray_actor;
    int outer_type = g_tray_type;
    g_tray_actor = actor;
    g_tray_type = type;
#ifdef _WIN32
    g_cons.n = 0;
#endif
    o_tray_tip(self, actor, type, id, pos, size, anchor, raid, sub);
    g_tray_actor = outer;
    g_tray_type = outer_type;
#ifdef _WIN32
    g_cons.n = 0;
#endif
}

#ifndef _WIN32
// The buff and debuff lists (and the other icons' buff lines): the buffs go in groups by label, each group's lines
// (the game's, consolidated within the group) under its label, as the game's own header option writes a source
// ("<label>\n<first line>"); the one call that asks for the game's header gets the labels instead.
static void tray_lines_hook(void *self, LineVec *out, const PtrList *buffs, const void *actor, bool header, bool b,
                            bool dur) {
    static char labels[MAX_LABELS][LABEL_MAX];
    int of[MAX_LIST];
    int nlabels = out ? tray_groups(actor, buffs, labels, of) : -1;
    if (nlabels < 0) {
        o_tray_lines(self, out, buffs, actor, header, b, dur);
        return;
    }
    size_t n = (size_t)(buffs->end - buffs->begin);
    for (int k = -1; k < nlabels; k++) { // -1: the buffs without a label, as the game lists them
        const void *mine[MAX_LIST];
        size_t m = 0;
        for (size_t i = 0; i < n; i++)
            if (of[i] == k)
                mine[m++] = buffs->begin[i];
        if (!m)
            continue;
        PtrList one = {mine, mine + m, mine + m};
        size_t before = out->begin ? (size_t)(out->end - out->begin) : 0;
        o_tray_lines(self, out, &one, actor, false, b, dur);
        size_t after = out->begin ? (size_t)(out->end - out->begin) : 0;
        if (k < 0 || after <= before)
            continue;
        char *first = out->begin[before], text[2 * LINE_TEXT];
        snprintf(text, sizeof text, "%s\n%s", labels[k], first);
        tooltip_encode_colours(text, sizeof text);
        snprintf(first, LINE_TEXT, "%s", text);
    }
}
#endif

// One more line in the tooltip, in the font and colour of its last line.
static void tray_add_line(void *tip, const char *text) {
    unsigned count = *(unsigned *)((uint8_t *)tip + TIP_COUNT);
    if (!count || count >= TIP_MAX_LINES)
        return;
    uint8_t line[LINE_SIZE];
    memset(line, 0, sizeof line);
    memcpy(line, (uint8_t *)tip + TIP_LINES + (count - 1) * LINE_SIZE, 0x10); // font, colour
    *(uint16_t *)(line + 0x10) = 1;
    line[0x20] = 1;
    char encoded[512];
    snprintf(encoded, sizeof encoded, "%s", text);
    tooltip_encode_colours(encoded, sizeof encoded);
    g_set_string(line, encoded);
    g_add_line((uint8_t *)tip + 0x10, line);
}

// Bleed, Blight, Marked, Stealth and Riposte show one total or title for all their buffs; a line per origin follows:
// "<label>: <the game's line for that origin's buffs>" for the DoTs, the label for the others. Riposte's buffs are
// GetTrayRiposteBuffInstanceList's: source 14 (riposte) and not a status stat.
static bool tray_hotspot(const uint8_t *params) {
    if (!g_tray_actor || ui_hotspot_caller() != g_tray_hotspot_ret || !params)
        return true;
    bool riposte = g_tray_type == TRAY_RIPOSTE && g_is_status;
    int stat = g_tray_type == TRAY_BLEED     ? BUFF_STAT_BLEED
               : g_tray_type == TRAY_BLIGHT  ? BUFF_STAT_POISON
               : g_tray_type == TRAY_MARK    ? BUFF_STAT_TAG
               : g_tray_type == TRAY_STEALTH ? BUFF_STAT_STEALTH
                                             : -1;
    void *tip = *(void *const *)(params + HOTSPOT_TOOLTIP);
    if ((stat < 0 && !riposte) || !tip)
        return true;
    const char *fmt = stat == BUFF_STAT_BLEED    ? "tray_icon_tooltip_bleed_format"
                      : stat == BUFF_STAT_POISON ? "tray_icon_tooltip_poison_format"
                                                 : NULL;
    struct {
        char label[LABEL_MAX];
        float amount;
        int rounds;
    } acc[MAX_LABELS];
    int nacc = 0;
    const uint8_t *const *g = (const uint8_t *const *)((const uint8_t *)g_tray_actor + ACTOR_BUFF_GROUP);
    for (const uint8_t *e = g[0]; e && e + BUFF_ENTRY_SIZE <= g[1]; e += BUFF_ENTRY_SIZE) {
        const uint8_t *b = e + 8;
        if (riposte ? *(const unsigned *)(b + BUFF_SOURCE) != BUFF_SOURCE_RIPOSTE ||
                          g_is_status(*(const int *)(b + BUFF_STAT))
                    : *(const int *)(b + BUFF_STAT) != stat)
            continue;
        char l[LABEL_MAX];
        if (!label_or_source(g_tray_actor, b, l, sizeof l))
            continue;
        int k = 0;
        while (k < nacc && strcmp(acc[k].label, l))
            k++;
        if (k == nacc) {
            if (nacc == MAX_LABELS)
                continue;
            snprintf(acc[nacc].label, LABEL_MAX, "%s", l);
            acc[nacc].amount = 0;
            acc[nacc++].rounds = 0;
        }
        acc[k].amount += *(const float *)(b + BUFF_AMOUNT);
        int r = *(const int *)(b + BUFF_DURATION);
        if (r > acc[k].rounds)
            acc[k].rounds = r;
    }
    for (int k = 0; k < nacc; k++) {
        char amount[256], text[512];
        if (!fmt) {
            tray_add_line(tip, acc[k].label);
            continue;
        }
        snprintf(amount, sizeof amount, ui_loc(fmt), (int)acc[k].amount, acc[k].rounds); // "%d damage per rd (%d rds)"
        snprintf(text, sizeof text, "%s: %s", acc[k].label, amount);
        tray_add_line(tip, text);
    }
    return true;
}
void buff_origin_install(void) {
    *(void **)&g_skill_name = sym("_ZN12ActorDisplay3App18GetCombatSkillNameEPK10ActorClassPK16ActorCombatSkillb");
    *(void **)&g_monster_class = sym("_ZNK7Monster15GetMonsterClassEv");
    *(void **)&g_source_id = sym("_ZN4Buff11GetSourceIdENS_6SourceE");
    *(void **)&g_items = sym("_ZNK9Inventory6System16GetAmountOfItemsEv");
    *(void **)&g_item = sym("_ZNK9Inventory6System7GetItemEj");
    *(void **)&g_item_hash = sym("_ZNK9Inventory4Item9GetIdHashEv");
    *(void **)&g_skill_effects = sym("_ZNK16ActorCombatSkill18GetNumberOfEffectsEj");
    *(void **)&g_skill_effect = sym("_ZNK16ActorCombatSkill16GetEffectAtIndexEjj");
    if (!hook_on("battle_end", HOOK_AFTER, clear_pending, NULL))
        logf_("buff origins: named sources not cleared at battle end");

    void *lines = sym("_ZN11HeroDisplay3App33StatAddAdditionalTooltipBuffLinesER20GuiTooltipParametersRKNSt3__"
                      "16vectorIPKN4Buff8InstanceENS3_9allocatorIS8_EEEEbjfPKcj");
    void *amount = sym("_ZN11HeroDisplay3App34StatAddAdditionalTooltipStatAmountER20GuiTooltipParametersPKcfS4_j");
    void *base = sym("_ZN11HeroDisplay3App28StatSetBaseTooltipStatAmountER20GuiTooltipParametersPKcfS4_j");
    void *show = sym("_ZN11HeroDisplay3App13ShowBaseStatsERK4Hero4vec2jbbPK5Actor12LayoutAnchorb");
    *(void **)&g_add_line = sym("_ZN20GuiTooltipParameters4Text7AddLineERKNS0_4LineE");
    *(void **)&g_set_string = sym("_ZN20GuiTooltipParameters4Text4Line9SetStringEPKc");
    *(void **)&g_colour_entry = sym("_ZN13ColourLibrary8GetEntryEj");
    *(void **)&g_entry_hvec4 = sym("_ZNK13ColourLibrary5Entrycv5hvec4Ev");
    if (!lines || !amount || !show || !g_add_line || !g_set_string || !g_colour_entry || !g_entry_hvec4) {
        logf_("buff origins: stat tooltip functions not found, its buff lines keep the game's source names");
        return;
    }
    static const uint8_t lines_pro[] = PROLOGUE_SUB32(0x98, 0x00);  // sub rsp,0x98
    static const uint8_t amount_pro[] = PROLOGUE_SUB32(0x58, 0x04); // sub rsp,0x458
    static const uint8_t show_pro[] = PROLOGUE_SUB32(0xd8, 0x0d);   // sub rsp,0xdd8
    *(void **)&o_amount = install_hook("StatAddAdditionalTooltipStatAmount (buff origins)", amount, (void *)amount_hook,
                                       amount_pro, sizeof amount_pro);
    if (base)
        *(void **)&o_base = install_hook("StatSetBaseTooltipStatAmount (stat labels)", base, (void *)base_hook,
                                         amount_pro, sizeof amount_pro); // same prologue: sub rsp,0x458
    if (!o_base)
        logf_("buff origins: StatSetBaseTooltipStatAmount not hooked, base stat lines keep the game's labels");
    *(void **)&o_show_stats = install_hook("HeroDisplay::App::ShowBaseStats (buff origins)", show,
                                           (void *)show_stats_hook, show_pro, sizeof show_pro);
    if (o_amount && o_show_stats)
        *(void **)&o_lines = install_hook("StatAddAdditionalTooltipBuffLines (buff origins)", lines, (void *)lines_hook,
                                          lines_pro, sizeof lines_pro);
    if (!o_lines)
        logf_("buff origins: stat tooltip not hooked, its buff lines keep the game's source names");

    void *prot =
        sym("_ZNSt3__110__function6__funcIZN11HeroDisplay3App13ShowBaseStatsERK4Hero4vec2jbbPK5Actor12LayoutAnchorbE3$_"
            "5NS_9allocatorISC_EEFvvEEclEv");
    *(void **)&g_combat_stat = sym("_ZNK5Actor13GetCombatStatEN15ActorCombatStat5Types5eTypeE");
    *(void **)&g_part_value = sym("_ZNK8RaidStat15GetBuffAddValueEj");
    if (prot && g_combat_stat && g_part_value && o_show_stats) {
        // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; sub rsp,0x240
        static const uint8_t prot_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                           0x54, 0x53, 0x48, 0x81, 0xEC, 0x40, 0x02, 0x00, 0x00};
        *(void **)&o_prot = install_hook("ShowBaseStats PROT tooltip (armour line)", prot, (void *)prot_hook, prot_pro,
                                         sizeof prot_pro);
    }
    if (!o_prot)
        logf_("buff origins: PROT tooltip not hooked, no armour line");

    void *resist = sym("_ZN17ResistanceDisplay16SingleResistanceEPK5ActorN15ResistanceTypes15eResistanceTypeE4vec2");
    *(void **)&g_resist_id = sym("_ZN14ResistanceType13GetIdFromTypeEN15ResistanceTypes15eResistanceTypeE");
    *(void **)&g_stat_type = sym("_ZN4Buff4Stat13GetTypeFromIdEPKc");
    *(void **)&g_active_buffs = sym("_ZNK5Actor14GetActiveBuffsEv");
    g_resist_tip_ret =
        resist ? call_site_return(resist, RESIST_TIP_CALL, sym("_Z11GUI_TooltipRK20GuiTooltipParametersb")) : NULL;
#ifdef _WIN32
    *(void **)&g_deallocate = sym("win:std::_Deallocate");
    void *filter = sym("win:ResistanceDisplay::SingleResistance lambda_4");
    if (filter && g_resist_tip_ret) {
        *(void **)&o_resist_filter = install_hook("SingleResistance's buff filter (resistance origins)", filter,
                                                  (void *)resist_filter_hook, NULL, 0);
        if (o_resist_filter)
            *(void **)&o_resist = install_hook("ResistanceDisplay::SingleResistance (resistance origins)", resist,
                                               (void *)resist_hook, NULL, 0);
        if (o_resist)
            ui_tooltip_on_show(resist_tip);
    }
#else
    void *total = sym("_ZN4Buff31GetTotalFromFiltredInstanceListERKNSt3__16vectorIPKNS_8InstanceENS0_9allocatorIS4_"
                      "EEEEPjNS0_8functionIFbS4_EEE");
    *(void **)&g_delete = sym("_ZdlPv");
    g_total_ret = resist && total ? call_site_return(resist, RESIST_TOTAL_CALL, total) : NULL;
    if (g_total_ret && g_resist_tip_ret) {
        static const uint8_t resist_pro[] = PROLOGUE_SUB32(0x08, 0x0a); // sub rsp,0xa08
        static const uint8_t total_pro[] = PROLOGUE_SUB8(0x18);         // sub rsp,0x18
        *(void **)&o_total = install_hook("Buff::GetTotalFromFiltredInstanceList (resistance origins)", total,
                                          (void *)total_hook, total_pro, sizeof total_pro);
        if (o_total)
            *(void **)&o_resist = install_hook("ResistanceDisplay::SingleResistance (resistance origins)", resist,
                                               (void *)resist_hook, resist_pro, sizeof resist_pro);
        if (o_resist)
            ui_tooltip_on_show(resist_tip);
    }
#endif
    if (!o_resist)
        logf_("buff origins: resistance tooltips not hooked, they keep the game's source names");

    void *tray = sym("_ZN13Overlay_Basic19ShowBuffIconTooltipEP5ActorN13TrayIconTypes13eTrayIconTypeEy4vec2S4_"
                     "12LayoutAnchorRN6RaidUI6Screen11RaidDisplayEj");
    *(void **)&g_is_status = sym("_ZN4Buff4Stat19GetIsStatusFromTypeENS0_5Types5eTypeE");
    bool grouped = false; // the buff lists are grouped by label
#ifdef _WIN32
    void *consolidate =
        sym("_ZN4Buff7Display31ConsolidateFilteredInstanceListERKNSt3__16vectorIPKNS_8InstanceENS1_9allocatorIS5_EEEE");
    void *desc_duration = sym("_ZN4Buff7Display26GetDescriptionWithDurationEPKNS_8InstanceEPK5Actorb");
    void *desc = sym("_ZN4Buff7Display14GetDescriptionEPKNS_8InstanceEPK5Actor");
    *(void **)&g_allocate = sym("_Znwm");
    if (tray && consolidate && desc_duration && desc && g_allocate && g_deallocate) {
        *(void **)&o_tray_tip =
            install_hook("Overlay_Basic::ShowBuffIconTooltip (buff origins)", tray, (void *)tray_tip_hook, NULL, 0);
        if (o_tray_tip) {
            *(void **)&o_desc_duration = install_hook("Buff::Display::GetDescriptionWithDuration (buff origins)",
                                                      desc_duration, (void *)desc_duration_hook, NULL, 0);
            *(void **)&o_desc =
                install_hook("Buff::Display::GetDescription (buff origins)", desc, (void *)desc_hook, NULL, 0);
        }
        if (o_desc_duration && o_desc)
            *(void **)&o_consolidate = install_hook("Buff::Display::ConsolidateFilteredInstanceList (buff origins)",
                                                    consolidate, (void *)consolidate_hook, NULL, 0);
        grouped = o_consolidate != NULL;
    }
#else
    void *tray_lines = sym("_ZN13Overlay_Basic23AddTrayBuffDescriptionsERNSt3__16vectorI17FixedLengthStringILi256EENS0_"
                           "9allocatorIS3_EEEERKNS1_IPKN4Buff8InstanceENS4_ISB_EEEEPK5Actorbbb");
    if (tray && tray_lines) {
        static const uint8_t tray_pro[] = PROLOGUE_SUB32(0x98, 0x0f);       // sub rsp,0xf98
        static const uint8_t tray_lines_pro[] = PROLOGUE_SUB32(0x68, 0x02); // sub rsp,0x268
        *(void **)&o_tray_tip = install_hook("Overlay_Basic::ShowBuffIconTooltip (buff origins)", tray,
                                             (void *)tray_tip_hook, tray_pro, sizeof tray_pro);
        if (o_tray_tip)
            *(void **)&o_tray_lines = install_hook("Overlay_Basic::AddTrayBuffDescriptions (buff origins)", tray_lines,
                                                   (void *)tray_lines_hook, tray_lines_pro, sizeof tray_lines_pro);
        grouped = o_tray_lines != NULL;
    }
#endif
    if (tray && o_tray_tip) {
        g_tray_hotspot_ret = call_site_return(
            tray, TRAY_HOTSPOT_CALL, sym("_Z11GUI_HotSpotRK17HotSpotParametersNSt3__18functionIFvvEEES5_S5_S5_"));
        if (g_tray_hotspot_ret)
            ui_on_hotspot(tray_hotspot, NULL);
    }
    if (!grouped)
        logf_("buff origins: combat tray tooltips not hooked, they keep the game's lines");
    else if (!g_tray_hotspot_ret)
        logf_("buff origins: tray tooltip hot spot not found, Bleed / Blight / Marked list no origins");
}
