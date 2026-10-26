// Combat skill tooltips with live values.
// HeroDisplay::App::CombatCreateSkillTooltip builds every hero skill tooltip (combat bar, character sheet,
// town party quick-select); its text (effect lines included) is cached per class and skill at load, so it
// can't follow the hero's fields. After it runs, for a configured class:
//  1. skills with formulas (skill heal/dmg, effect heal) get their info text rebuilt by the game's own builder
//     (CreateAssets' $_1, into scratch storage) with the formula values set on the skill and its effects for
//     the call, so the game's lines ("Heal 14-14", "DMG mod +40%", effect lines) show the hero's numbers;
//  2. [[...]] tokens in each line are filled (config.c);
//  3. the skill's config "tooltip" lines go in after the info text, or in its place ("tooltip_replace"), with
//     [[info]] giving that text, "@<string id>" lines from the string table, and an optional font per line
//     (config "font", or the text starting with {font|<font_ref id>}).
// Colour markup ({colour_start|<colour>} ... {colour_end}) is turned into the game's encoded form, as the
// localization loader does for strings (the renderer only understands the encoded form).
// The builder isn't given the hero: the character sheet and quick-select callers are hooked to say which
// hero it is; otherwise (the combat bar) it's the raid's selected hero, which is what the bar shows.
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GuiTooltipParameters: Text at 0x10 = Line[32] at 0x20 (0x28 bytes each), line count at 0x520.
// Line: font 0x00, colour 0x08, string 0x18, carriage return (bool) 0x20, length 0x24.
#define MAX_LINES 32
// CombatSkillTooltipParams (by value, on the stack): HeroClass* at 0x18, HeroCombatSkill* at 0x30.

// HeroCombatSkill (ActorCombatSkill): heal min/max (int) 0x364/0x368, .atk / .crit (float) 0x380 / 0x384,
// base effects vector<Effect const*> at 0x3d0, base damage modifier (float) 0x43c. Effect: .heal (float) 0xc8.
// The builder's output: HeroData's map<skill hash, vector<CombatSkillData>> at 0x18 (begin node, root = end
// node's left at 0x20, size 0x28); map node: left 0, right 8, key 0x20, vector 0x28 (begin, end, cap).
// CombatSkillData (0x618): pips, name, info (FixedLengthString<512> each, at 0, 0x200, 0x400), then a
// map<unsigned, FixedLengthString<256>> (mode texts) at 0x600 (root at 0x608).
// Windows (MSVC): the map is {head node, size} at 0x10; a node: left 0, parent 8, right 0x10, colour 0x18, is-nil 0x19,
// key 0x20, vector 0x28 (0x40 bytes); the root is the head's parent, an empty map's head points at itself.
// CombatSkillData is 0x610: the mode texts map (head, size) at 0x600.

typedef struct {
    uint8_t b[0x78];
} TipParams;

static void (*g_set_string)(void *line, const char *s);
static void (*g_add_line)(void *text, const void *line);
static void *(*g_selected_hero)(const void *party);
static void **g_raid; // &Raid::s_Instance

#ifdef _WIN32
// (the lambda's unused `this` first: clang drops it on macOS, MSVC doesn't)
typedef void (*builder_fn)(const void *self, const void *cls, void *hero_data, const void *skill, bool per_level,
                           bool check_lines);
#else
typedef void (*builder_fn)(const void *cls, void *hero_data, const void *skill, bool per_level, bool check_lines);
#endif
static builder_fn g_builder;                                            // HeroDisplay::App::CreateAssets(bool)::$_1
static const char *(*g_skill_info)(const void *cls, const void *skill); // the cached info text
static const char *(*g_skill_name)(const void *cls, const void *skill); // HeroDisplay::App::CombatSkillName
static void (*g_base_name)(char *out, const void *cls, const void *skill,
                           bool with_level);                         // ActorDisplay::App::GetCombatSkillName
static const uint8_t *(*g_combat_stat)(const void *actor, int type); // Actor::GetCombatStat -> RaidStat
static float (*g_get_dmg_mod)(const void *skill, const void *actor); // ActorCombatSkill::GetDamageModifier
static void (*g_delete)(void *);                                     // operator delete
static void (*g_colour_start)(char *fls512, const char *colour);     // TextureFont_CatColourStart
static void (*g_colour_end)(char *fls512);                           // TextureFont_CatColourEnd
typedef struct {
    uint64_t a, b;
} LocStr;                                           // Localization::LocalizedString (returned in rax:rdx)
static LocStr (*g_localize)(const char *id);        // Localization::LocalizeStringFromId
static const char *(*g_loc_string)(const LocStr *); // LocalizedString::GetString

static const void *g_ctx_hero; // set while a hooked caller builds its tooltip
static bool g_ctx_set;

// Debug: log each distinct tooltip text once (they're rebuilt every frame).
static unsigned g_seen[256];
static int g_nseen;
static void log_once(unsigned skill_hash, int i, const char *s) {
    if (!g_debug)
        return;
    unsigned h = name_hash(s) ^ (skill_hash * 31) ^ (unsigned)i;
    for (int k = 0; k < g_nseen; k++)
        if (g_seen[k] == h)
            return;
    if (g_nseen < 256)
        g_seen[g_nseen++] = h;
    logf_("tooltip skill %08x line %d: %s", skill_hash, i, s);
}

#ifdef _WIN32
// an MSVC tree's nodes below `node` (not its head: nodes flagged is-nil)
static void free_tree(uint8_t *node) {
    while (node && !node[0x19]) {
        free_tree(*(uint8_t **)(node + 0x10));
        uint8_t *left = *(uint8_t **)node;
        g_delete(node);
        node = left;
    }
}
#else
static void free_tree(void *node) {
    if (!node)
        return;
    free_tree(*(void **)node);
    free_tree(*(void **)((uint8_t *)node + 8));
    g_delete(node);
}
#endif

// Rebuilt info text per (skill, hero state), so the builder runs once per change, not every frame.
#define CACHE 128
static struct {
    const void *skill;
    unsigned state;
    char *text;
} g_cache[CACHE];
static int g_cache_next;

static unsigned hero_state(const void *hero) {
    if (!hero)
        return 0xffffffffu;
    unsigned h = 2166136261u, guid = hero_guid(hero);
    for (int f = 0; f < field_count(); f++)
        h = (h ^ (unsigned)herodata_get(guid, f)) * 16777619u;
    return h;
}

// The skill's info text rebuilt with the hero's formula values, or NULL if it has no formulas.
static const char *rebuilt_info(const ClassConfig *c, const void *hero, const void *cls, const void *skill) {
    unsigned sh = skill_id_hash(skill);
    uint8_t *sk = (uint8_t *)skill;
    double heal, dmg, eh;
    bool has_heal = config_skill_heal(c, hero, sh, &heal), has_dmg = config_skill_dmg(c, hero, sh, &dmg);
    const void **eb = *(const void ***)(sk + SKILL_BASE_EFFECTS), **ee = *(const void ***)(sk + SKILL_BASE_EFFECTS + 8);
    bool has_eff = false;
    for (const void **e = eb; e && e < ee; e++)
        if (*e && config_effect_heal(c, hero, (const char *)*e, &eh))
            has_eff = true;
    if (!has_heal && !has_dmg && !has_eff)
        return NULL;

    unsigned state = hero_state(hero);
    for (int i = 0; i < CACHE; i++)
        if (g_cache[i].skill == skill && g_cache[i].state == state)
            return g_cache[i].text;

    // set the formula values for the call
    int old_min = *(int *)(sk + SKILL_HEAL_MIN), old_max = *(int *)(sk + SKILL_HEAL_MAX);
    float old_dmg = *(float *)(sk + SKILL_BASE_DMG);
    float old_eff[32];
    int ne = 0;
    if (has_heal)
        *(int *)(sk + SKILL_HEAL_MIN) = *(int *)(sk + SKILL_HEAL_MAX) = (int)lround(heal);
    if (has_dmg)
        *(float *)(sk + SKILL_BASE_DMG) = (float)dmg;
    for (const void **e = eb; e && e < ee && ne < 32; e++, ne++) {
        float *h = (float *)((uint8_t *)*e + EFFECT_HEAL);
        old_eff[ne] = *h;
        if (*e && config_effect_heal(c, hero, (const char *)*e, &eh))
            *h = (float)eh;
    }
    uint8_t hd[0x40] = {0};
#ifdef _WIN32
    uint8_t *head = calloc(1, NODE_SIZE); // empty map: its head node (the game frees what it allocates, and
    if (!head)
        return NULL; // we free its nodes, with the same UCRT heap)
    *(void **)head = *(void **)(head + 8) = *(void **)(head + 0x10) = head;
    head[0x18] = head[0x19] = 1;
    *(void **)(hd + HD_MAP) = head;
    g_builder(NULL, cls, hd, skill, true, false);
#else
    *(void **)(hd + HD_MAP) = hd + HD_ROOT; // empty map: begin node = end node
    g_builder(cls, hd, skill, true, false);
#endif
    // restore
    *(int *)(sk + SKILL_HEAL_MIN) = old_min;
    *(int *)(sk + SKILL_HEAL_MAX) = old_max;
    *(float *)(sk + SKILL_BASE_DMG) = old_dmg;
    ne = 0;
    for (const void **e = eb; e && e < ee && ne < 32; e++, ne++)
        *(float *)((uint8_t *)*e + EFFECT_HEAL) = old_eff[ne];

#ifdef _WIN32
    uint8_t *node = *(uint8_t **)(head + 8);
    if (node == head) {
        g_delete(head);
        logf_("tooltip: builder gave nothing for skill %08x", sh);
        return NULL;
    }
    uint8_t *csd = *(uint8_t **)(node + NODE_VEC);
    char *text = csd ? strdup((const char *)(csd + CSD_INFO)) : NULL;
    if (csd) {
        uint8_t *mode_head = *(uint8_t **)(csd + CSD_MODE_MAP);
        if (mode_head) {
            free_tree(*(uint8_t **)(mode_head + 8));
            g_delete(mode_head);
        }
        g_delete(csd);
    }
    g_delete(node);
    g_delete(head);
#else
    uint8_t *node = *(uint8_t **)(hd + HD_ROOT);
    if (!node) {
        logf_("tooltip: builder gave nothing for skill %08x", sh);
        return NULL;
    }
    uint8_t *csd = *(uint8_t **)(node + NODE_VEC);
    char *text = csd ? strdup((const char *)(csd + CSD_INFO)) : NULL;
    if (csd) {
        free_tree(*(void **)(csd + CSD_MODE_ROOT));
        g_delete(csd);
    }
    g_delete(node);
#endif
    if (!text)
        return NULL;
    dbgf_("tooltip: rebuilt skill %08x for hero %u: %s", sh, hero ? hero_guid(hero) : 0, text);
    free(g_cache[g_cache_next].text);
    g_cache[g_cache_next].skill = skill;
    g_cache[g_cache_next].state = state;
    g_cache[g_cache_next].text = text;
    g_cache_next = (g_cache_next + 1) % CACHE;
    return text;
}

// Encode {colour_start|x} / {colour_end} markup in place (FixedLengthString<512> is a plain char[512]).
static void encode_colours(char *s, size_t cap) {
    if (!strstr(s, "{colour_"))
        return;
    char out[1024];
    size_t n = 0;
    for (const char *p = s; *p && n + 1 < sizeof out;) {
        char enc[512] = "";
        const char *close = strchr(p, '}');
        if (!strncmp(p, "{colour_start|", 14) && close) {
            char name[64];
            size_t l = (size_t)(close - p - 14);
            if (l >= sizeof name)
                l = sizeof name - 1;
            memcpy(name, p + 14, l);
            name[l] = 0;
            g_colour_start(enc, name);
            p = close + 1;
        } else if (!strncmp(p, "{colour_end}", 12)) {
            g_colour_end(enc);
            p += 12;
        } else {
            out[n++] = *p++;
            continue;
        }
        for (const char *e = enc; *e && n + 1 < sizeof out; e++)
            out[n++] = *e;
    }
    out[n] = 0;
    snprintf(s, cap, "%s", out);
}
void tooltip_encode_colours(char *s, size_t cap) {
    if (g_colour_start && g_colour_end)
        encode_colours(s, cap);
}

typedef void *(*create_fn)(void *out, TipParams p);
static create_fn g_orig;

// The hero's damage inputs for [[hit_min]] / [[hit_max]] (config.c). Hero::CalculateDerivedStatsInternal sets
// the damage stats (RaidStat, Actor::GetCombatStat 0 = low, 1 = high) add slot 2 to weapon damage * (1 + the
// CURRENT skill's GetDamageModifier); buffs fill the other slots. RaidStat value = (sum of adds) * (1 + sum of
// multiplies): adds at 0x30 (vector<float>), multiplies at 0x48. Weapon: HeroClass+0xba8 -> entries of 0x130
// by Hero+0x12a4 (weapon level), damage min/max (int) at 0x11c / 0x120.
#define DMG_WEAPON_SLOT 2
static int damage_vars(const void *hero, const void *skill, TipVar *out) {
    const uint8_t *h = hero;
    const uint8_t *cls = *(const uint8_t *const *)(h + HERO_CLASS);
    const uint8_t *weapons = cls ? *(const uint8_t *const *)(cls + CLASS_WEAPONS) : NULL;
    stats_rows_begin((void *)hero); // a mod's row override (a client mod gear), as in the calc
    int wl = *(const int *)(h + HERO_WEAPON_LEVEL);
    int wdmg[2] = {0, 0};
    if (weapons && wl >= 0 && wl <= 4) {
        wdmg[0] = *(const int *)(weapons + wl * WEAPON_SIZE + WEAPON_DMG_MIN);
        wdmg[1] = *(const int *)(weapons + wl * WEAPON_SIZE + WEAPON_DMG_MAX);
    }
    stats_rows_end((void *)hero);
    if (!weapons || wl < 0 || wl > 4)
        return 0;
    static const char *names[2][3] = {{"wpn_min", "dmg_extra_min", "dmg_mult_min"},
                                      {"wpn_max", "dmg_extra_max", "dmg_mult_max"}};
    int n = 0;
    for (int i = 0; i < 2; i++) {
        const uint8_t *st = g_combat_stat(hero, i);
        if (!st)
            return 0;
        const float *ab = *(const float *const *)(st + RAIDSTAT_ADD),
                    *ae = *(const float *const *)(st + RAIDSTAT_ADD + 8);
        const float *mb = *(const float *const *)(st + RAIDSTAT_MUL),
                    *me = *(const float *const *)(st + RAIDSTAT_MUL + 8);
        double add = 0, mul = 0;
        for (const float *a = ab; a && a < ae; a++)
            if (a - ab != DMG_WEAPON_SLOT)
                add += *a;
        for (const float *m = mb; m && m < me; m++)
            mul += *m;
        out[n++] = (TipVar){names[i][0], wdmg[i]};
        out[n++] = (TipVar){names[i][1], add};
        out[n++] = (TipVar){names[i][2], 1 + mul};
    }
    // the modifier's rank bonus (.rank_damage_modifiers): the game's modifier minus the skill's base
    float game = g_get_dmg_mod(skill, hero), base = *(const float *)((const uint8_t *)skill + SKILL_BASE_DMG);
    out[n++] = (TipVar){"dmg_rank", game - base};
    return n;
}

// The title template with every "[[name]]" replaced by the skill's name.
static void put_name(const char *t, const char *name, char *out, size_t outlen) {
    size_t n = 0;
    while (*t && n + 1 < outlen) {
        if (!strncmp(t, "[[name]]", 8)) {
            for (const char *s = name; *s && n + 1 < outlen; s++)
                out[n++] = *s;
            t += 8;
        } else
            out[n++] = *t++;
    }
    out[n] = 0;
}

// Replace every "[[info]]" in s with info.
static void put_info(const char *s, const char *info, char *out, size_t outlen) {
    size_t n = 0;
    while (*s && n + 1 < outlen) {
        if (!strncmp(s, "[[info]]", 8)) {
            for (const char *t = info; *t && n + 1 < outlen; t++)
                out[n++] = *t;
            s += 8;
        } else
            out[n++] = *s++;
    }
    out[n] = 0;
}

// Insert config tooltip lines (TipOut: "@string" ids, {font|id} prefixes, [[info]], tokens) at line `at`,
// styled as `tmpl`; `with` (may be NULL) applies to lines without their own. Returns the line after the last.
static unsigned insert_lines(uint8_t *tip, unsigned at, const uint8_t *tmpl, const ClassConfig *c, const void *hero,
                             unsigned sh, const TipVar *dvars, int nvars, const TipOut *extra, int n, const char *info,
                             const char *with_all) {
    unsigned *count = (unsigned *)(tip + TIP_COUNT);
    char buf[1024], tmp[1024];
    for (int k = 0; k < n && *count < MAX_LINES; k++) {
        const char *text = extra[k].text;
        if (text[0] == '@') { // a string table entry
            LocStr ls = g_localize(text + 1);
            text = g_loc_string(&ls);
            if (!text)
                text = extra[k].text;
        }
        const char *font = extra[k].font;
        char fontbuf[48];
        if (!strncmp(text, "{font|", 6) && strchr(text, '}')) { // the text picks its line's font
            const char *close = strchr(text, '}');
            size_t l = (size_t)(close - text - 6);
            if (l >= sizeof fontbuf)
                l = sizeof fontbuf - 1;
            memcpy(fontbuf, text + 6, l);
            fontbuf[l] = 0;
            font = fontbuf;
            text = close + 1;
        }
        put_info(text, info, tmp, sizeof tmp);
        if (!config_tooltip_fill(c, hero, sh, dvars, nvars, extra[k].with ? extra[k].with : with_all, tmp, buf,
                                 sizeof buf))
            snprintf(buf, sizeof buf, "%s", tmp);
        encode_colours(buf, sizeof buf);
        uint8_t *line = tip + TIP_LINES + at * LINE_SIZE;
        memmove(line + LINE_SIZE, line, (*count - at) * LINE_SIZE);
        memcpy(line, tmpl, LINE_SIZE);
        if (font) {
            void *f = (void *)ui_font(font);
            if (f)
                *(void **)line = f;
        }
        g_set_string(line, buf);
        (*count)++;
        at++;
    }
    return at;
}

static bool g_base_mode; // the town character sheet: base values, not live

// The title for skills with no "tooltip_title" (every class): "Mace Bash (1)", rank in the neutral colour.
#define DEFAULT_TITLE "[[name]] {colour_start|neutral}([[rank]]){colour_end}"

// Classes without a config: the name line (the game's "<name> <level>") as DEFAULT_TITLE, filled directly.
static void plain_title(uint8_t *tip, unsigned from, const void *cls, const void *skill, int *at) {
    unsigned count = *(unsigned *)(tip + TIP_COUNT);
    const char *name = g_skill_name(cls, skill);
    if (at)
        *at = -1;
    for (unsigned i = from; name && i < count && i < MAX_LINES; i++) {
        uint8_t *line = tip + TIP_LINES + i * LINE_SIZE;
        const char *s = *(const char **)(line + LINE_STRING);
        if (!s || strcmp(s, name))
            continue;
        char base[256] = "", buf[512];
        g_base_name(base, cls, skill, false);
        snprintf(buf, sizeof buf, "%s {colour_start|neutral}(%d){colour_end}", base[0] ? base : s,
                 *(const int *)((const uint8_t *)skill + SKILL_LEVEL) + 1);
        encode_colours(buf, sizeof buf);
        g_set_string(line, buf);
        if (at)
            *at = (int)i;
        return;
    }
}

static void *create_hook(void *out, TipParams p) {
    g_orig(out, p);
    const uint8_t *cls = *(const uint8_t **)(p.b + PARAMS_CLASS);
    const void *skill = *(const void **)(p.b + PARAMS_SKILL);
    if (!cls || !skill)
        return out;
    if (g_base_mode) {
        const void *one[1] = {skill};
        tooltip_rewrite_base(out, cls, g_ctx_hero, one, 1);
        return out;
    }
    const ClassConfig *c = config_for_class_hash(*(const unsigned *)(cls + CLASS_ID_HASH));
    if (!c) {
        plain_title(out, 0, cls, skill, NULL);
        return out;
    }
    const void *hero = g_ctx_hero;
    if (!g_ctx_set && g_raid && *g_raid)
        hero = g_selected_hero((uint8_t *)*g_raid + RAID_PARTY);
    if (hero && config_for_hero(hero) != c)
        hero = NULL; // not this class: fields read as 0
    unsigned sh = skill_id_hash(skill);

    // the skill's own numbers, for [[acc]], [[crit]], [[dmg]], [[heal_min]], [[heal_max]], [[heal]]
    // (a skill's dmg / heal formula takes precedence over dmg / heal)
    const uint8_t *sk = skill;
    TipVar vars[] = {
        {"acc", *(const float *)(sk + SKILL_ACC)},
        {"crit", *(const float *)(sk + SKILL_CRIT)},
        {"dmg", *(const float *)(sk + SKILL_BASE_DMG)},
        {"heal_min", *(const int *)(sk + SKILL_HEAL_MIN)},
        {"heal_max", *(const int *)(sk + SKILL_HEAL_MAX)},
        {"heal", *(const int *)(sk + SKILL_HEAL_MIN)},
        {"level", *(const int *)(sk + SKILL_LEVEL)},
        {"rank", *(const int *)(sk + SKILL_LEVEL) + 2 - config_guild_first_rank(c, sh)},
        {"cooldown", hero ? config_cooldown_left(c, hero, skill_id_hash(skill)) : 0},
        {"cooldown_turns", config_cooldown_turns(c, skill_id_hash(skill))},
    };
    int nvars = (int)(sizeof vars / sizeof vars[0]);
    TipVar dvars[24];
    memcpy(dvars, vars, sizeof vars);
    if (hero)
        nvars += damage_vars(hero, skill, dvars + nvars);

    uint8_t *tip = out;
    unsigned *count = (unsigned *)(tip + TIP_COUNT);
    if (*count > MAX_LINES)
        return out;
    char buf[1024], info[1024] = "", tmp[1024];
    const char *cached = g_skill_info(cls, skill), *rebuilt = rebuilt_info(c, hero, cls, skill);
    size_t cl = cached ? strlen(cached) : 0;
    int info_i = -1; // the info line: cached text + mode lines
    const char *title = config_tooltip_title(c, sh), *name = g_skill_name(cls, skill);
    if (!title)
        title = DEFAULT_TITLE;
    int name_i = -1; // the name line (the game's skill name)
    for (unsigned i = 0; name && title && i < *count; i++) {
        const char *s = *(const char **)(tip + TIP_LINES + i * LINE_SIZE + LINE_STRING);
        if (s && !strcmp(s, name)) {
            name_i = (int)i;
            break;
        }
    }
    for (unsigned i = 0; i < *count; i++) {
        uint8_t *line = tip + TIP_LINES + i * LINE_SIZE;
        const char *s = *(const char **)(line + LINE_STRING);
        if (!s)
            continue;
        log_once(sh, (int)i, s);
        if (info_i < 0 && cl && !strncmp(s, cached, cl)) {
            info_i = (int)i;
            snprintf(info, sizeof info, "%s%s", rebuilt ? rebuilt : cached, s + cl);
            s = info;
        }
        if ((int)i == name_i && title) { // the name line from the title template
            char base[256] = "";         // the name without the game's level suffix
            g_base_name(base, cls, skill, false);
            put_name(title, base[0] ? base : s, tmp, sizeof tmp);
            if (!config_tooltip_fill(c, hero, sh, dvars, nvars, NULL, tmp, buf, sizeof buf))
                snprintf(buf, sizeof buf, "%s", tmp);
            encode_colours(buf, sizeof buf);
            g_set_string(line, buf);
            continue;
        }
        if (config_tooltip_fill(c, hero, sh, dvars, nvars, NULL, s, buf, sizeof buf)) {
            encode_colours(buf, sizeof buf);
            g_set_string(line, buf);
            if ((int)i == info_i)
                snprintf(info, sizeof info, "%s", buf);
        } else if (s == info && rebuilt)
            g_set_string(line, info);
    }

    TipOut extra[MAX_LINES];
    bool replace;
    int cd_left = hero ? config_cooldown_left(c, hero, sh) : 0;
    const char *cd_line = cd_left > 0 ? config_cooldown_tooltip(c, cd_left)                             // cooling down
                                      : config_cooldown_ready_tooltip(c, config_cooldown_turns(c, sh)); // ready
    int n = 0;
    if (cd_line)
        extra[n++] = (TipOut){cd_line, NULL, NULL}; // first, under the name
    n += config_skill_tooltip(c, hero, sh, extra + n, MAX_LINES - n, &replace);
    if (!n && !replace)
        return out;
    if (!*count)
        return out;
    unsigned tmpl_i = info_i >= 0 ? (unsigned)info_i : *count - 1;
    uint8_t tmpl[LINE_SIZE];
    memcpy(tmpl, tip + TIP_LINES + tmpl_i * LINE_SIZE, LINE_SIZE); // the info line's font and colour
    tmpl[LINE_NEWLINE] = 1;
    unsigned at = info_i >= 0 ? (unsigned)info_i + 1 : *count; // insert after the info text
    if (replace && info_i >= 0) {                              // drop it
        memmove(tip + TIP_LINES + info_i * LINE_SIZE, tip + TIP_LINES + (info_i + 1) * LINE_SIZE,
                (*count - info_i - 1) * LINE_SIZE);
        (*count)--;
        at = (unsigned)info_i;
    }
    insert_lines(tip, at, tmpl, c, hero, sh, dvars, nvars, extra, n, info, NULL);
    return out;
}

// In town: the town screen exists and no raid is on (TownUI::Screen::TownDisplay::s_Instance, Raid::s_Instance).
static void **g_town_display;
static bool in_town(void) {
    bool town = g_town_display && *g_town_display && !(g_raid && *g_raid);
    static int last = -1;
    if (town != last) {
        dbgf_("tooltips: character sheet %s", town ? "in town (base values)" : "in a raid (live values)");
        last = town;
    }
    return town;
}

// UI::Panel::CharacterDisplay::ShowCombatSkillGrid's tooltip lambda: (Hero*&&, unsigned&&, vec2&&, float&&)
#ifdef _WIN32
typedef void *(*sheet_fn)(void *self, void *out, void **hero, unsigned *i, Vec2 *pos, float *f); // MSVC: this first
#else
typedef void *(*sheet_fn)(void *out, void *self, void **hero, unsigned *i, Vec2 *pos, float *f);
#endif
static sheet_fn g_sheet_orig;
#ifdef _WIN32
static void *sheet_hook(void *self, void *out, void **hero, unsigned *i, Vec2 *pos, float *f) {
#else
static void *sheet_hook(void *out, void *self, void **hero, unsigned *i, Vec2 *pos, float *f) {
#endif
    g_ctx_hero = hero ? *hero : NULL;
    g_ctx_set = true;
    unsigned index = sheet_skill_index(*i); // the page slot's class skill (sheet.c)
    g_base_mode = in_town();                // in town the sheet shows base values
#ifdef _WIN32
    void *r = g_sheet_orig(self, out, hero, &index, pos, f);
#else
    void *r = g_sheet_orig(out, self, hero, &index, pos, f);
#endif
    g_base_mode = false;
    g_ctx_hero = NULL;
    g_ctx_set = false;
    return r;
}

// TownUI::Panel::PartySetupDisplay::ShowQuickSelectSkill(vec2, Hero*, unsigned, bool)
typedef uint64_t (*quick_fn)(void *self, Vec2 pos, void *hero, unsigned i, bool b);
static quick_fn g_quick_orig;
static uint64_t quick_hook(void *self, Vec2 pos, void *hero, unsigned i, bool b) {
    g_ctx_hero = hero;
    g_ctx_set = true;
    uint64_t r = g_quick_orig(self, pos, hero, i, b);
    g_ctx_hero = NULL;
    g_ctx_set = false;
    return r;
}

// One skill's base lines under its name line: the config tooltip lines without "next" parts ("with" or
// "base": false), for a skill needing a minimum of a field (a spender's "hp_min") once per value from that
// minimum to the field's limit (e.g. Spend 1/1 ... 3/3), else once with no hero fields; in place of the game's
// info text when the skill replaces it. The hero (may be NULL) gives weapon damage for hit ranges.
static void base_block(uint8_t *tip, unsigned name_i, const ClassConfig *c, const void *hero, const void *cls,
                       const void *skill) {
    unsigned *count = (unsigned *)(tip + TIP_COUNT);
    unsigned sh = skill_id_hash(skill);
    const char *cached = g_skill_info(cls, skill);
    size_t cl = cached ? strlen(cached) : 0;
    int info_i = -1;
    for (unsigned i = name_i + 1; cl && i < *count; i++) {
        const char *s = *(const char **)(tip + TIP_LINES + i * LINE_SIZE + LINE_STRING);
        if (s && !strncmp(s, cached, cl)) {
            info_i = (int)i;
            break;
        }
    }
    TipOut extra[MAX_LINES];
    char withs[8][32];
    bool replace = false;
    int n = 0, field, lo, hi;
    if (config_skill_base_range(c, sh, &field, &lo, &hi)) {
        for (int v = lo; v <= hi && v - lo < 8; v++) {
            int m = config_skill_tooltip_base(c, sh, field, v, extra + n, MAX_LINES - n, &replace);
            snprintf(withs[v - lo], sizeof withs[0], "%s=%d", field_name(field), v);
            for (int j = n; j < n + m; j++)
                extra[j].with = withs[v - lo];
            n += m;
        }
    } else
        n = config_skill_tooltip_base(c, sh, -1, 0, extra, MAX_LINES, &replace);
    if (!n)
        return;
    const uint8_t *sk = skill;
    TipVar vars[32] = {
        {"acc", *(const float *)(sk + SKILL_ACC)},
        {"crit", *(const float *)(sk + SKILL_CRIT)},
        {"dmg", *(const float *)(sk + SKILL_BASE_DMG)},
        {"heal_min", *(const int *)(sk + SKILL_HEAL_MIN)},
        {"heal_max", *(const int *)(sk + SKILL_HEAL_MAX)},
        {"heal", *(const int *)(sk + SKILL_HEAL_MIN)},
        {"level", *(const int *)(sk + SKILL_LEVEL)},
        {"rank", *(const int *)(sk + SKILL_LEVEL) + 2 - config_guild_first_rank(c, sh)},
        {"cooldown", 0},
        {"cooldown_turns", config_cooldown_turns(c, sh)},
    };
    int nvars = 10;
    if (hero)
        nvars += damage_vars(hero, skill, vars + nvars);
    unsigned tmpl_i = info_i >= 0 ? (unsigned)info_i : name_i;
    uint8_t tmpl[LINE_SIZE];
    memcpy(tmpl, tip + TIP_LINES + tmpl_i * LINE_SIZE, LINE_SIZE);
    tmpl[LINE_NEWLINE] = 1;
    unsigned at = tmpl_i + 1;
    if (replace && info_i >= 0) {
        memmove(tip + TIP_LINES + info_i * LINE_SIZE, tip + TIP_LINES + (info_i + 1) * LINE_SIZE,
                (*count - info_i - 1) * LINE_SIZE);
        (*count)--;
        at = (unsigned)info_i;
    }
    insert_lines(tip, at, tmpl, c, hero, sh, vars, nvars, extra, n, cached ? cached : "", NULL);
}

// Base (not live) rewrite, for tooltips outside combat (the Guild, the town character sheet): for a configured
// class, each skill's name line gets the title template ([[rank]] = that skill's rank), its info text the
// skill's base lines (base_block: no live Holy Power, spenders per value), and every line its tokens filled and
// colour markup encoded. No cooldown lines.
void tooltip_rewrite_base(void *tip_params, const void *cls, const void *hero, const void *const *skills, int nskills) {
    if (!g_set_string || !g_skill_name || !g_base_name || !cls)
        return;
    const ClassConfig *c = config_for_class_hash(*(const unsigned *)((const uint8_t *)cls + CLASS_ID_HASH));
    uint8_t *tip = tip_params;
    unsigned count = *(unsigned *)(tip + TIP_COUNT);
    if (count > MAX_LINES)
        return;
    char buf[1024], tmp[1024];
    unsigned from = 0;
    int name_at[2] = {-1, -1};
    if (nskills > 2)
        nskills = 2;
    for (int k = 0; !c && k < nskills; k++) { // no config: titles only
        plain_title(tip, from, cls, skills[k], &name_at[k]);
        if (name_at[k] >= 0)
            from = (unsigned)name_at[k] + 1;
    }
    for (int k = 0; c && k < nskills; k++) {
        const void *skill = skills[k];
        const uint8_t *sk = skill;
        unsigned sh = skill_id_hash(skill);
        const char *title = config_tooltip_title(c, sh), *name = g_skill_name(cls, skill);
        if (!title)
            title = DEFAULT_TITLE;
        TipVar vars[] = {
            {"acc", *(const float *)(sk + SKILL_ACC)},
            {"crit", *(const float *)(sk + SKILL_CRIT)},
            {"dmg", *(const float *)(sk + SKILL_BASE_DMG)},
            {"heal_min", *(const int *)(sk + SKILL_HEAL_MIN)},
            {"heal_max", *(const int *)(sk + SKILL_HEAL_MAX)},
            {"heal", *(const int *)(sk + SKILL_HEAL_MIN)},
            {"level", *(const int *)(sk + SKILL_LEVEL)},
            {"rank", *(const int *)(sk + SKILL_LEVEL) + 2 - config_guild_first_rank(c, sh)},
        };
        int nvars = (int)(sizeof vars / sizeof vars[0]);
        for (unsigned i = from; name && title && i < count; i++) {
            uint8_t *line = tip + TIP_LINES + i * LINE_SIZE;
            const char *s = *(const char **)(line + LINE_STRING);
            if (!s || strcmp(s, name))
                continue;
            char base[256] = "";
            g_base_name(base, cls, skill, false);
            put_name(title, base[0] ? base : s, tmp, sizeof tmp);
            if (!config_tooltip_fill(c, NULL, sh, vars, nvars, NULL, tmp, buf, sizeof buf))
                snprintf(buf, sizeof buf, "%s", tmp);
            encode_colours(buf, sizeof buf);
            g_set_string(line, buf);
            from = i + 1; // the next skill's name comes later
            name_at[k] = (int)i;
            break;
        }
    }
    // the current level is a placeholder (below "guild_first_rank"), or both read the same (an unranked
    // title, same info): keep only the second (the next rank)
    if (nskills == 2 && name_at[0] >= 0 && name_at[1] > name_at[0]) {
        const char *a = *(const char **)(tip + TIP_LINES + name_at[0] * LINE_SIZE + LINE_STRING);
        const char *b = *(const char **)(tip + TIP_LINES + name_at[1] * LINE_SIZE + LINE_STRING);
        bool placeholder = c && *(const int *)((const uint8_t *)skills[0] + SKILL_LEVEL) + 1 <
                                    config_guild_first_rank(c, skill_id_hash(skills[0]));
        if (placeholder || (a && b && !strcmp(a, b))) {
            int drop = name_at[1] - name_at[0];
            memmove(tip + TIP_LINES + name_at[0] * LINE_SIZE, tip + TIP_LINES + name_at[1] * LINE_SIZE,
                    (count - name_at[1]) * LINE_SIZE);
            count -= (unsigned)drop;
            *(unsigned *)(tip + TIP_COUNT) = count;
            name_at[0] = name_at[1] - drop; // only the next rank's block remains
            name_at[1] = -1;
            skills = skills + 1;
            nskills = 1;
        }
    }
    if (nskills == 2 && name_at[1] > 0 && count < MAX_LINES) { // a blank line between the two ranks
        uint8_t *line = tip + TIP_LINES + name_at[1] * LINE_SIZE;
        memmove(line + LINE_SIZE, line, (count - name_at[1]) * LINE_SIZE);
        memcpy(line, line - LINE_SIZE, LINE_SIZE); // the line before's font and colour
        line[LINE_NEWLINE] = 1;
        g_set_string(line, " ");
        *(unsigned *)(tip + TIP_COUNT) = ++count;
        name_at[1]++;
    }
    for (int k = nskills - 1; c && k >= 0; k--) // last first: inserting doesn't move earlier blocks
        if (name_at[k] >= 0)
            base_block(tip, (unsigned)name_at[k], c, hero, cls, skills[k]);
    if (!c)
        return;
    count = *(unsigned *)(tip + TIP_COUNT);
    for (unsigned i = 0; i < count; i++) { // markup in the game's own lines (e.g. mod strings)
        uint8_t *line = tip + TIP_LINES + i * LINE_SIZE;
        const char *s = *(const char **)(line + LINE_STRING);
        if (!s || (!strstr(s, "[[") && !strstr(s, "{colour")))
            continue;
        if (!config_tooltip_fill(c, NULL, 0, NULL, 0, NULL, s, buf, sizeof buf))
            snprintf(buf, sizeof buf, "%s", s);
        encode_colours(buf, sizeof buf);
        g_set_string(line, buf);
    }
}

void tooltip_install(void) {
    g_set_string = (void (*)(void *, const char *))sym("_ZN20GuiTooltipParameters4Text4Line9SetStringEPKc");
    g_add_line = (void (*)(void *, const void *))sym("_ZN20GuiTooltipParameters4Text7AddLineERKNS0_4LineE");
    g_selected_hero = (void *(*)(const void *))sym("_ZNK5Party15GetSelectedHeroEv");
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    g_builder = (builder_fn)sym(
        "_ZZN11HeroDisplay3App12CreateAssetsEbENK3$_1clEP9HeroClassRNS0_12SharedAssets8HeroDataERK15HeroCombatSkillbb");
    g_skill_info = (const char *(*)(const void *, const void *))sym(
        "_ZN11HeroDisplay3App15CombatSkillInfoEPK9HeroClassPK15HeroCombatSkill");
    g_delete = (void (*)(void *))sym("_ZdlPv");
    g_skill_name = (const char *(*)(const void *, const void *))sym(
        "_ZN11HeroDisplay3App15CombatSkillNameEPK9HeroClassPK15HeroCombatSkill");
    g_base_name = (void (*)(char *, const void *, const void *, bool))sym(
        "_ZN12ActorDisplay3App18GetCombatSkillNameEPK10ActorClassPK16ActorCombatSkillb");
    g_combat_stat =
        (const uint8_t *(*)(const void *, int))sym("_ZNK5Actor13GetCombatStatEN15ActorCombatStat5Types5eTypeE");
    g_get_dmg_mod = (float (*)(const void *, const void *))sym("_ZNK16ActorCombatSkill17GetDamageModifierEPK5Actor");
    g_colour_start =
        (void (*)(char *, const char *))sym("_Z26TextureFont_CatColourStartR17FixedLengthStringILi512EEPKc");
    g_colour_end = (void (*)(char *))sym("_Z24TextureFont_CatColourEndR17FixedLengthStringILi512EE");
    g_localize = (LocStr (*)(const char *))sym("_ZN12Localization20LocalizeStringFromIdEPKc");
    g_loc_string = (const char *(*)(const LocStr *))sym("_ZNK12Localization15LocalizedString9GetStringEv");
    g_town_display = (void **)sym("_ZN6TownUI6Screen11TownDisplay10s_InstanceE");
    if (!g_set_string || !g_add_line || !g_selected_hero || !g_raid || !g_builder || !g_skill_info || !g_skill_name ||
        !g_base_name || !g_combat_stat || !g_get_dmg_mod || !g_delete || !g_colour_start || !g_colour_end ||
        !g_localize || !g_loc_string) {
        logf_("tooltips: missing symbols, off");
        return;
    }
#ifndef _WIN32 // (Windows: sym() checks the table's bytes)
    // the builder's prologue, as a check that it's the function we studied
    static const uint8_t builder_prologue[] = PROLOGUE_SUB32(0x68, 0x0B); // sub rsp,0xb68
    if (memcmp((const void *)g_builder, builder_prologue, sizeof builder_prologue)) {
        logf_("tooltips: builder differs, off");
        return;
    }
#endif

    static const uint8_t sheet_prologue[] = PROLOGUE_SUB32(0x18, 0x08); // sub rsp,0x818
    g_sheet_orig = (sheet_fn)install_hook(
        "CharacterDisplay skill tooltip",
        sym("_ZNSt3__110__function6__funcIZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClassE3$"
            "_6NS_9allocatorISB_EEF20GuiTooltipParametersS7_jS5_fEEclEOS7_OjOS5_Of"),
        (void *)sheet_hook, sheet_prologue, sizeof sheet_prologue);
    static const uint8_t quick_prologue[] = PROLOGUE_SUB32(0xE8, 0x0E); // sub rsp,0xee8
    g_quick_orig =
        (quick_fn)install_hook("PartySetupDisplay::ShowQuickSelectSkill",
                               sym("_ZN6TownUI5Panel17PartySetupDisplay20ShowQuickSelectSkillE4vec2P4Herojb"),
                               (void *)quick_hook, quick_prologue, sizeof quick_prologue);
    // Without the caller hooks the fallback (the raid's selected hero) would be wrong outside the combat bar.
    if (!g_sheet_orig || !g_quick_orig) {
        logf_("tooltips: caller hooks off, tooltips off");
        return;
    }
    static const uint8_t prologue[] = PROLOGUE_SUB8(0x38); // sub rsp,0x38
    g_orig =
        (create_fn)install_hook("HeroDisplay::App::CombatCreateSkillTooltip",
                                sym("_ZN11HeroDisplay3App24CombatCreateSkillTooltipENS0_24CombatSkillTooltipParamsE"),
                                (void *)create_hook, prologue, sizeof prologue);
}
