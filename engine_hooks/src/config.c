// Engine config: per-class fields, skill conditions and actions, read from <mod>/engine/*.engine.json
// in the game's mods folder and in Steam Workshop mods.
//
// {
//   "class":  "example_knight",
//   "fields": ["hp", "spec", "tier"],                        // per-hero integers, saved as ddx_<field>
//   "enums":  {"spec": ["none", "holy", "prot", "ret"]},     // names usable as values (index)
//   "limits": {"hp": [0, 3]},                                // clamp after actions
//   "skills": {"<skill id>": {"spec": "ret" | ["prot","ret"], "hp_min": 1, "hp_max": 2,
//                             "dmg": "-100% + 70% * hp",           // formulas over fields (see expr.c)
//                             "heal": "4 + 5 * hp"}},
//   "actions": {"<effect name>": {"remove_buffs": ["EX_*_SPEC_*"], "set": {"hp": 0, "spec": "holy"}, "add": {"hp": 1}}},
//   "animation_variants": ["{spec}_{hp}", "{spec}"],         // <anim>_<variant>, first one the sprite has
//   "effect_variants": {"ex_x": "ex_x_{hp}"},               // apply the named effect instead (if it exists)
//   "effects": {"ex_mend_heal": {"heal": "2 + 3 * hp"}},     // effect numbers from the performer's fields
//   "debug": true                                            // verbose log (animations, formulas, actions)
//   "announce": {"name": "str_my_mod", "font": "my_font", "colour": "my_colour"}  // global: this mod in the
//                                                            library announcement's list (announce.c)
//   "riposte": {"skills": ["melee"], "no_skill_bark": "str_x"}  // global: heroes riposte with one of their own
//                                                            // usable skills of these types (riposte.c)
//   "act_outs": {"allow_back_to_back": true}                 // global (any file; one without "class" may hold
//                                                            // only global sections): the same turn-start act-out
//                                                            // may follow itself (act_outs.c); true wins
//   "items": {"<item id>": {"spec": ["holy", "prot"]}}      // only heroes of this class may use the item,
//                                                            // and only while the conditions hold;
//                                                            // "combat": false = not during a battle
//   "cooldowns": {"builder": {"turns": 1, "unit": "own"}}    // skills with "cooldown": "builder" (or a list)
//                                                            // share it: after one is used, all are unusable
//                                                            // for the next `turns` turns: the hero's own
//                                                            // ("own") or any actor's ("actor"); counter
//                                                            // field cd_<name>
//   "skill_pages": {"size": 4, "icons": ["<prev.png>", "<next.png>", "<prev off>", "<next off>"]}
//                                                            // combat bar paging: more skills than `size` ->
//                                                            // pages; prev/next tabs (disabled icons: 1 page)
//   (skills also take "guild_row": 1: the Guild shows the skill in that row (1 = top), rows in the order of this
//    section; skills without one follow, a few per row; "guild_first_rank": 2: ranks below it are a placeholder
//    (a level 0 that can't be used): the Guild shows the skill disabled, without its rank, until that rank is
//    bought, while it can't be bought yet; "sheet_page": 1: the character sheet page (1 = first) the skill is on,
//    for several Guild rows on one page; without it the skill's page is its guild_row)
//   "guild_centred_rows": [1, 3]                             // Guild rows centred on the widest row (and drawn
//                                                            // without a divider: headers for the row below)
//   "cooldown_tooltip": ["@one turn text", "@n turns text"]  // added under a cooling skill's name
//                                                            // ([[cooldown]] = turns left)
//   "cooldown_ready_tooltip": ["@1 turn", "@n turns"]        // the same place, for a skill with a cooldown
//                                                            // that's ready ([[cooldown_turns]] = its length)
//   (skills also take "tooltip": ["line", {"text": "line", "hp_max": 2, "font": "tooltip_header"}, ...]: lines
//    added after the skill's info text, an object's line only when its conditions (as for skills) hold; "font"
//    is a font_ref id from fonts/fonts.darkest; text "@<string id>" is taken from the string table.
//    "tooltip_replace": true drops the game's info text (stats and effect lines) and puts the lines in its
//    place; [[info]] in a line gives that text, with live values; a line's "with": "hp+1" applies that field
//    change to all its tokens that don't have their own @, so one string serves for "next" lines.
//    "tooltip_title": "[[name]] ([[rank]])" rewrites the name line, [[name]] being the game's name; set at the
//    top level for all the class's skills, or per skill, which wins)
//   (base tooltips, outside combat: the Guild and the town character sheet: the skill's lines without "with" and
//    without "base": false, filled with no live values; a skill with a "<field>_min" on a limited field lists
//    them once per value from that minimum to the limit, e.g. a spender's Spend 1/1 ... 3/3)
//   (hook points run actions through the global "hooks" section: hook_rules.c; a rule's "class" filter and its
//    {"action": "<name>"} run this class's actions)
// }
// A skill with no entry is always allowed; "hidden": true never shows it (keeps it defined, e.g. for saves). Conditions: "<field>": value or list (any of), "<field>_min",
// "<field>_max"; all must hold. Actions run when an effect with that name applies to a hero of the class:
// first remove_buffs (glob patterns on buff ids; absent buffs are fine), then set, then add (clamped).
// Tooltip text (the class's skill tooltips, including the mod's own strings, and "tooltip" lines) can hold
// [[expr]] or [[expr|fmt]]: a formula over the hero's fields, plus "heal" / "dmg" (the skill's formulas) and
// "heal_<effect>" (an effect's heal formula); a bare enum field gives its name. fmt: "+" signed, "%" percent
// (0.4 -> 40%), "+%" both; default a rounded number. "expr@hp+1" (or @hp-1, @hp=3) evaluates it with that
// field changed (clamped by limits), e.g. "[[heal@hp+1]]" = the heal at one more Holy Power. The caller can
// add values (the tooltip gives the skill's own acc, crit, dmg, heal_min, heal_max, heal); formulas win.
// "hit_min" / "hit_max": the hero's damage range with this skill, as the game computes it (needs the caller's
// wpn_*, dmg_rank, dmg_extra_*, dmg_mult_* values): ceil((extra + wpn * (1 + dmg + rank)) * mult), dmg being
// the skill's formula (with any @ change) or its .dmg.
#include "common.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- fields (global registry)
static char g_fields[MAX_FIELDS][32];
static int g_nfields;

int field_count(void) {
    return g_nfields;
}
const char *field_name(int i) {
    return g_fields[i];
}
int field_find(const char *name) {
    for (int i = 0; i < g_nfields; i++)
        if (!strcmp(g_fields[i], name))
            return i;
    return -1;
}
int field_register(const char *name) {
    int i = field_find(name);
    if (i >= 0)
        return i;
    if (g_nfields >= MAX_FIELDS || strlen(name) >= sizeof g_fields[0]) {
        logf_("too many fields / name too long: %s", name);
        return -1;
    }
    strcpy(g_fields[g_nfields], name);
    return g_nfields++;
}

// ---------------------------------------------------------------- class config
#define MAX_VALS  8
#define MAX_CONDS 8
#define MAX_OPS   8
#define MAX_ENUM  16
enum { OP_EQ, OP_MIN, OP_MAX };
typedef struct {
    int field, op, nvals, vals[MAX_VALS];
} Cond;
typedef struct {
    unsigned skill_hash;
    char id[64];
    int ncond;
    Cond cond[MAX_CONDS];
    char dmg[96];
    char heal[96];
    int ntip;
    struct TipLine *tip;
    bool tip_replace;
    char *tip_title;
    bool hidden;
    int ncd;
    int cd[4];
    int guild_row, guild_first_rank, sheet_page;
} SkillRule;
typedef struct TipLine {
    char *text;
    char font[48];
    char with[32];
    int ncond;
    Cond cond[MAX_CONDS];
    bool no_base;
} TipLine;
typedef struct {
    int field;
    bool add;
    int value;
} Op;
#define MAX_REMOVE 16
typedef struct {
    char name[96];
    int nops;
    Op ops[MAX_OPS];
    int nremove;
    char remove[MAX_REMOVE][64];
} Action;
typedef struct {
    int field, n;
    char names[MAX_ENUM][32];
} Enum;

struct ClassConfig {
    char name[64];
    unsigned class_hash;
    int nenums;
    Enum enums[MAX_FIELDS];
    bool has_limit[MAX_FIELDS];
    int lo[MAX_FIELDS], hi[MAX_FIELDS];
    int nrules;
    SkillRule *rules;
    int nactions;
    Action *actions;
    int nvariants;
    char variants[8][64];
    int neffvar;
    struct {
        char name[96];
        char tmpl[96];
    } effvar[32];
    int neffheal;
    struct {
        char name[96];
        char heal[96];
    } effheal[32];
    char *tip_title;
    int ncds;
    struct {
        char name[32];
        int field, turns;
        bool actor;
    } cds[16];
    char *cd_tip[2];       // one turn, several turns
    char *cd_ready_tip[2]; // ready, cooldown of one turn / several
    int page_size;
    char page_icon[4][128]; // skill bar paging: prev, next, prev off, next off
    int ncentred;
    int centred[16]; // Guild rows drawn centred (guild_centred_rows)
    int nitems;
    struct {
        unsigned id_hash;
        char id[64];
        bool no_combat;
        int ncond;
        Cond cond[MAX_CONDS];
    } items[16];
    int mod; // the mod whose file it is (mods.c): used only while it's active
};

#define MAX_CLASSES 16
static ClassConfig g_classes[MAX_CLASSES];
static int g_nclasses;

static bool value_of(const ClassConfig *c, int field, const JVal *v, int *out) {
    if (v->t == J_NUM) {
        *out = (int)v->num;
        return true;
    }
    if (v->t == J_BOOL) {
        *out = v->b;
        return true;
    }
    if (v->t == J_STR)
        for (int e = 0; e < c->nenums; e++)
            if (c->enums[e].field == field)
                for (int i = 0; i < c->enums[e].n; i++)
                    if (!strcmp(c->enums[e].names[i], v->str)) {
                        *out = i;
                        return true;
                    }
    logf_("config %s: bad value for field %s", c->name, field_name(field));
    return false;
}

// "<field>": value or list (any of), "<field>_min", "<field>_max"
static bool parse_cond(const ClassConfig *c, const char *k, const JVal *v, Cond *cd) {
    char key[64];
    snprintf(key, sizeof key, "%s", k);
    int op = OP_EQ;
    size_t kl = strlen(key);
    if (kl > 4 && !strcmp(key + kl - 4, "_min")) {
        op = OP_MIN;
        key[kl - 4] = 0;
    } else if (kl > 4 && !strcmp(key + kl - 4, "_max")) {
        op = OP_MAX;
        key[kl - 4] = 0;
    }
    memset(cd, 0, sizeof *cd);
    cd->field = field_register(key);
    cd->op = op;
    if (cd->field < 0)
        return false;
    if (v->t == J_ARR) {
        for (int k2 = 0; k2 < v->n && cd->nvals < MAX_VALS; k2++)
            if (value_of(c, cd->field, &v->items[k2], &cd->vals[cd->nvals]))
                cd->nvals++;
    } else if (value_of(c, cd->field, v, &cd->vals[0]))
        cd->nvals = 1;
    return cd->nvals > 0;
}

// Settings (riposte, act_outs), from any file: built again at each campaign load from the active mods' files, lowest
// priority first, so a source or type named again by a mod higher in the campaign's list replaces it, and a switch
// turned on by one active mod is on for all (at start-up, before any campaign: every file, for install decisions).
static bool g_reapply; // building them for a campaign: per-file lines only in debug
#define slogf_(...)                                                                                                    \
    do {                                                                                                               \
        if (g_reapply)                                                                                                 \
            dbgf_(__VA_ARGS__);                                                                                        \
        else                                                                                                           \
            logf_(__VA_ARGS__);                                                                                        \
    } while (0)
static ActOutConfig g_act_outs;
const ActOutConfig *config_act_outs(void) {
    return &g_act_outs;
}

static RiposteConfig g_riposte;
const RiposteConfig *config_riposte(void) {
    return &g_riposte;
}

static void parse_riposte(const JVal *r, const char *path) {
    if (!r)
        return;
    if (r->t != J_OBJ) {
        slogf_("config %s: \"riposte\" isn't an object", path);
        return;
    }
    const JVal *types = json_get(r, "skills"), *bark = json_get(r, "no_skill_bark");
    if (types && types->t == J_ARR) {
        g_riposte.ntypes = 0;
        for (int i = 0; i < types->n && g_riposte.ntypes < RIPOSTE_MAX_TYPES; i++)
            if (types->items[i].t == J_STR)
                snprintf(g_riposte.types[g_riposte.ntypes++], sizeof g_riposte.types[0], "%s", types->items[i].str);
        slogf_("config %s: riposte from the hero's own skills (%d type(s))", path, g_riposte.ntypes);
    } else if (types)
        slogf_("config %s: riposte.skills is a list of skill types", path);
    if (bark && bark->t == J_STR)
        snprintf(g_riposte.no_skill_bark, sizeof g_riposte.no_skill_bark, "%s", bark->str);
}

// "announce": {"name": "<string id>", "font": "<font_ref>", "colour": "<colour id>"}: the mod in the announcement's list
static void parse_announce(const JVal *a, const char *path) {
    if (!a)
        return;
    const JVal *name = a->t == J_OBJ ? json_get(a, "name") : NULL;
    if (!name || name->t != J_STR) {
        slogf_("config %s: \"announce\" needs a \"name\" string id", path);
        return;
    }
    const JVal *font = json_get(a, "font"), *colour = json_get(a, "colour");
    announce_mod(name->str, font && font->t == J_STR ? font->str : NULL,
                 colour && colour->t == J_STR ? colour->str : NULL);
}

static void parse_act_outs(const JVal *ao, const char *path) {
    if (!ao || ao->t != J_OBJ)
        return;
    const JVal *ps = json_get(ao, "per_source"), *mpt = json_get(ao, "max_per_turn");
    if (ps && ps->t == J_BOOL && ps->b) {
        g_act_outs.per_source = true;
        slogf_("config %s: act_outs.per_source on", path);
    }
    if (mpt && mpt->t == J_NUM && mpt->num >= 0)
        g_act_outs.max_per_turn = (int)mpt->num;
    const JVal *um = json_get(ao, "use_max");
    if (um && um->t == J_BOOL && um->b) {
        g_act_outs.use_max = true;
        slogf_("config %s: act_outs.use_max on", path);
    }
    const JVal *b2b = json_get(ao, "allow_back_to_back");
    if (b2b && b2b->t == J_BOOL && b2b->b) {
        g_act_outs.allow_back_to_back = true;
        slogf_("config %s: act_outs.allow_back_to_back on", path);
    }
    // "chances": {"<trait or quirk id>": {"<act-out id>": percentage, ...}}: a source named again (another mod) is replaced
    const JVal *ch = json_get(ao, "chances");
    for (int i = 0; ch && ch->t == J_OBJ && i < ch->n; i++) {
        const JVal *tw = &ch->items[i];
        if (tw->t != J_OBJ) {
            slogf_("config %s: act_outs.chances.%s isn't an object", path, ch->keys[i]);
            continue;
        }
        ActOutChances *t = NULL;
        unsigned h = name_hash(ch->keys[i]);
        for (int k = 0; k < g_act_outs.ntraits; k++)
            if (g_act_outs.traits[k].trait_hash == h)
                t = &g_act_outs.traits[k];
        if (!t && g_act_outs.ntraits < ACT_OUT_MAX_TRAITS)
            t = &g_act_outs.traits[g_act_outs.ntraits++];
        if (!t) {
            slogf_("config %s: too many act_outs.chances traits", path);
            break;
        }
        memset(t, 0, sizeof *t);
        snprintf(t->trait, sizeof t->trait, "%s", ch->keys[i]);
        t->trait_hash = h;
        for (int j = 0; j < tw->n && t->n < ACT_OUT_MAX_TYPES; j++) {
            if (tw->items[j].t != J_NUM || tw->items[j].num < 0) {
                slogf_("config %s: act_outs.chances.%s.%s: a weight is a number >= 0", path, t->trait, tw->keys[j]);
                continue;
            }
            snprintf(t->w[t->n].type, sizeof t->w[0].type, "%s", tw->keys[j]);
            t->w[t->n++].weight = (float)tw->items[j].num;
        }
        slogf_("config %s: act_outs.chances.%s: %d act-out weights", path, t->trait, t->n);
    }
    // "rules": [{"source": "fearful", "act_out": "ignore_command", "when": "torch < 25", "weight": 3}, ...]
    const JVal *rules = json_get(ao, "rules");
    for (int i = 0; rules && rules->t == J_ARR && i < rules->n; i++) {
        const JVal *r = &rules->items[i];
        if (r->t != J_OBJ)
            continue;
        if (g_act_outs.nrules >= ACT_OUT_MAX_JSON_RULES) {
            slogf_("config %s: too many act_outs.rules", path);
            break;
        }
        ActOutJsonRule *o = &g_act_outs.rules[g_act_outs.nrules];
        memset(o, 0, sizeof *o);
        const JVal *src = json_get(r, "source"), *ao_id = json_get(r, "act_out"), *when = json_get(r, "when"),
                   *w = json_get(r, "weight");
        snprintf(o->source, sizeof o->source, "%s", src && src->t == J_STR ? src->str : "*");
        snprintf(o->act_out, sizeof o->act_out, "%s", ao_id && ao_id->t == J_STR ? ao_id->str : "*");
        if (when && when->t == J_STR)
            snprintf(o->when, sizeof o->when, "%s", when->str);
        if (w && w->t == J_NUM)
            snprintf(o->weight, sizeof o->weight, "%g", w->num);
        else if (w && w->t == J_STR)
            snprintf(o->weight, sizeof o->weight, "%s", w->str);
        else {
            slogf_("config %s: act_outs.rules[%d] has no \"weight\" (a number or formula), skipped", path, i);
            continue;
        }
        snprintf(o->file, sizeof o->file, "%s", path);
        g_act_outs.nrules++;
    }
    if (g_act_outs.nrules)
        slogf_("config %s: act_outs.rules: %d in all so far", path, g_act_outs.nrules);
    // "prerequisites": {"<source>": {"match": "all", "requires": [{"source": "abusive", "within": "turn",
    // "nothing_counts": false}, "fearful", ...]}}: a source named again (another mod) is replaced
    const JVal *pre = json_get(ao, "prerequisites");
    for (int i = 0; pre && pre->t == J_OBJ && i < pre->n; i++) {
        const JVal *def = &pre->items[i];
        const JVal *req = def->t == J_OBJ ? json_get(def, "requires") : NULL;
        if (!req || req->t != J_ARR) {
            slogf_("config %s: act_outs.prerequisites.%s needs \"requires\": [...]", path, pre->keys[i]);
            continue;
        }
        ActOutPrereqs *p = NULL;
        for (int k = 0; k < g_act_outs.nprereqs; k++)
            if (!strcmp(g_act_outs.prereqs[k].source, pre->keys[i]))
                p = &g_act_outs.prereqs[k];
        if (!p && g_act_outs.nprereqs < ACT_OUT_MAX_PREREQ_SOURCES)
            p = &g_act_outs.prereqs[g_act_outs.nprereqs++];
        if (!p) {
            slogf_("config %s: too many act_outs.prerequisites", path);
            break;
        }
        memset(p, 0, sizeof *p);
        snprintf(p->source, sizeof p->source, "%s", pre->keys[i]);
        const JVal *match = json_get(def, "match");
        if (match && match->t == J_STR) {
            if (!strcmp(match->str, "any"))
                p->any = true;
            else if (strcmp(match->str, "all"))
                slogf_("config %s: act_outs.prerequisites.%s.match \"%s\": all or any (all used)", path, p->source,
                       match->str);
        }
        for (int j = 0; j < req->n && p->n < ACT_OUT_MAX_PREREQS; j++) {
            const JVal *r = &req->items[j];
            ActOutPrereq *q = &p->req[p->n];
            memset(q, 0, sizeof *q);
            const JVal *src = r->t == J_STR ? r : r->t == J_OBJ ? json_get(r, "source") : NULL;
            if (!src || src->t != J_STR) {
                slogf_("config %s: act_outs.prerequisites.%s: a requirement without \"source\"", path, p->source);
                continue;
            }
            snprintf(q->source, sizeof q->source, "%s", src->str);
            const JVal *within = r->t == J_OBJ ? json_get(r, "within") : NULL,
                       *nc = r->t == J_OBJ ? json_get(r, "nothing_counts") : NULL;
            if (within && within->t == J_STR) {
                if (!strcmp(within->str, "battle"))
                    q->within = ACT_OUT_WITHIN_BATTLE;
                else if (!strcmp(within->str, "expedition"))
                    q->within = ACT_OUT_WITHIN_EXPEDITION;
                else if (strcmp(within->str, "turn"))
                    slogf_(
                        "config %s: act_outs.prerequisites.%s: within \"%s\": turn, battle or expedition (turn used)",
                        path, p->source, within->str);
            }
            q->nothing_counts = nc && nc->t == J_BOOL && nc->b;
            p->n++;
        }
        slogf_("config %s: act_outs.prerequisites.%s: %d (%s)", path, p->source, p->n, p->any ? "any" : "all");
    }
    // "types": {"heal_lowest_ally": {"like": "heal_self", "outcome": "heal", "target": "party_lowest_hp"}}: new
    // act-out types; an id named again (another mod) is replaced
    const JVal *types = json_get(ao, "types");
    for (int i = 0; types && types->t == J_OBJ && i < types->n; i++) {
        const JVal *d = &types->items[i];
        if (d->t != J_OBJ) {
            slogf_("config %s: act_outs.types.%s isn't an object", path, types->keys[i]);
            continue;
        }
        ActOutJsonType *t = NULL;
        for (int k = 0; k < g_act_outs.ntypes; k++)
            if (!strcmp(g_act_outs.types[k].id, types->keys[i]))
                t = &g_act_outs.types[k];
        if (!t && g_act_outs.ntypes < ACT_OUT_MAX_JSON_TYPES)
            t = &g_act_outs.types[g_act_outs.ntypes++];
        if (!t) {
            slogf_("config %s: too many act_outs.types", path);
            break;
        }
        memset(t, 0, sizeof *t);
        snprintf(t->id, sizeof t->id, "%s", types->keys[i]);
        snprintf(t->file, sizeof t->file, "%s", path);
        const JVal *like = json_get(d, "like"), *oc = json_get(d, "outcome"), *tg = json_get(d, "target"),
                   *res = json_get(d, "result"), *bc = json_get(d, "bark_chance"), *bd = json_get(d, "burndown");
        if (like && like->t == J_STR)
            snprintf(t->like, sizeof t->like, "%s", like->str);
        if (oc && oc->t == J_STR)
            snprintf(t->outcome, sizeof t->outcome, "%s", oc->str);
        snprintf(t->target, sizeof t->target, "%s", tg && tg->t == J_STR ? tg->str : "self");
        t->result = res && res->t == J_NUM ? (int)res->num : -1;
        t->bark_chance = bc && bc->t == J_NUM ? (float)bc->num : -1;
        t->burndown = bd && bd->t == J_NUM ? (float)bd->num : -1;
        slogf_("config %s: act_outs.types.%s (%s on %s)", path, t->id, t->outcome[0] ? t->outcome : "no outcome",
               t->target);
    }
}

static void parse_class(const JVal *root, const char *path) {
    const JVal *cls = json_get(root, "class");
    if (!cls || cls->t != J_STR) {
        if (!json_get(root, "act_outs") && !json_get(root, "hooks"))
            logf_("%s: missing \"class\"", path); // else a file of global sections
        return;
    }
    if (g_nclasses >= MAX_CLASSES) {
        logf_("too many classes");
        return;
    }
    ClassConfig *c = &g_classes[g_nclasses++];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", cls->str);
    c->class_hash = name_hash(c->name);
    c->mod = mods_current();

    const JVal *fields = json_get(root, "fields");
    for (int i = 0; fields && fields->t == J_ARR && i < fields->n; i++)
        if (fields->items[i].t == J_STR)
            field_register(fields->items[i].str);

    const JVal *enums = json_get(root, "enums");
    for (int i = 0; enums && enums->t == J_OBJ && i < enums->n && c->nenums < MAX_FIELDS; i++) {
        Enum *e = &c->enums[c->nenums++];
        e->field = field_register(enums->keys[i]);
        const JVal *arr = &enums->items[i];
        for (int j = 0; arr->t == J_ARR && j < arr->n && j < MAX_ENUM; j++)
            if (arr->items[j].t == J_STR)
                snprintf(e->names[e->n++], sizeof e->names[0], "%s", arr->items[j].str);
    }

    const JVal *limits = json_get(root, "limits");
    for (int i = 0; limits && limits->t == J_OBJ && i < limits->n; i++) {
        int f = field_register(limits->keys[i]);
        const JVal *a = &limits->items[i];
        if (f < 0 || a->t != J_ARR || a->n != 2)
            continue;
        c->has_limit[f] = true;
        c->lo[f] = (int)a->items[0].num;
        c->hi[f] = (int)a->items[1].num;
    }

    const JVal *cds = json_get(root, "cooldowns");
    for (int i = 0; cds && cds->t == J_OBJ && i < cds->n && c->ncds < 16; i++) {
        typeof(c->cds[0]) *cd = &c->cds[c->ncds];
        snprintf(cd->name, sizeof cd->name, "%s", cds->keys[i]);
        char fname[40];
        snprintf(fname, sizeof fname, "cd_%s", cd->name);
        cd->field = field_register(fname);
        const JVal *t = json_get(&cds->items[i], "turns"), *u = json_get(&cds->items[i], "unit");
        cd->turns = t && t->t == J_NUM ? (int)t->num : 1;
        cd->actor = u && u->t == J_STR && !strcmp(u->str, "actor");
        if (cd->field >= 0)
            c->ncds++;
    }
    const JVal *pages = json_get(root, "skill_pages");
    if (pages && pages->t == J_OBJ) {
        const JVal *sz = json_get(pages, "size"), *icons = json_get(pages, "icons");
        c->page_size = sz && sz->t == J_NUM ? (int)sz->num : 4;
        for (int i = 0; icons && icons->t == J_ARR && i < icons->n && i < 4; i++)
            if (icons->items[i].t == J_STR)
                snprintf(c->page_icon[i], sizeof c->page_icon[0], "%s", icons->items[i].str);
    }
    const JVal *gc = json_get(root, "guild_centred_rows");
    for (int i = 0; gc && gc->t == J_ARR && i < gc->n && c->ncentred < 16; i++)
        if (gc->items[i].t == J_NUM)
            c->centred[c->ncentred++] = (int)gc->items[i].num;
    const JVal *cdt = json_get(root, "cooldown_tooltip");
    if (cdt && cdt->t == J_STR)
        c->cd_tip[0] = c->cd_tip[1] = strdup(cdt->str);
    for (int i = 0; cdt && cdt->t == J_ARR && i < cdt->n && i < 2; i++)
        if (cdt->items[i].t == J_STR)
            c->cd_tip[i] = strdup(cdt->items[i].str);
    if (c->cd_tip[0] && !c->cd_tip[1])
        c->cd_tip[1] = c->cd_tip[0];
    const JVal *cdr = json_get(root, "cooldown_ready_tooltip");
    if (cdr && cdr->t == J_STR)
        c->cd_ready_tip[0] = c->cd_ready_tip[1] = strdup(cdr->str);
    for (int i = 0; cdr && cdr->t == J_ARR && i < cdr->n && i < 2; i++)
        if (cdr->items[i].t == J_STR)
            c->cd_ready_tip[i] = strdup(cdr->items[i].str);
    if (c->cd_ready_tip[0] && !c->cd_ready_tip[1])
        c->cd_ready_tip[1] = c->cd_ready_tip[0];

    const JVal *skills = json_get(root, "skills");
    if (skills && skills->t == J_OBJ) {
        c->rules = calloc(skills->n, sizeof(SkillRule));
        for (int i = 0; i < skills->n; i++) {
            SkillRule *r = &c->rules[c->nrules++];
            snprintf(r->id, sizeof r->id, "%s", skills->keys[i]);
            r->skill_hash = name_hash(r->id);
            const JVal *conds = &skills->items[i];
            for (int j = 0; conds->t == J_OBJ && j < conds->n && r->ncond < MAX_CONDS; j++) {
                if (!strcmp(conds->keys[j], "dmg")) {
                    if (conds->items[j].t == J_STR)
                        snprintf(r->dmg, sizeof r->dmg, "%s", conds->items[j].str);
                    continue;
                }
                if (!strcmp(conds->keys[j], "cooldown")) {
                    const JVal *v = &conds->items[j];
                    int nv = v->t == J_ARR ? v->n : 1;
                    for (int k = 0; k < nv && r->ncd < 4; k++) {
                        const JVal *it = v->t == J_ARR ? &v->items[k] : v;
                        int g = -1;
                        for (int m = 0; it->t == J_STR && m < c->ncds; m++)
                            if (!strcmp(c->cds[m].name, it->str))
                                g = m;
                        if (g < 0)
                            logf_("config %s: skill %s: unknown cooldown", c->name, r->id);
                        else
                            r->cd[r->ncd++] = g;
                    }
                    continue;
                }
                if (!strcmp(conds->keys[j], "sheet_page")) {
                    if (conds->items[j].t == J_NUM)
                        r->sheet_page = (int)conds->items[j].num;
                    continue;
                }
                if (!strcmp(conds->keys[j], "guild_first_rank")) {
                    if (conds->items[j].t == J_NUM)
                        r->guild_first_rank = (int)conds->items[j].num;
                    continue;
                }
                if (!strcmp(conds->keys[j], "guild_row")) {
                    if (conds->items[j].t == J_NUM)
                        r->guild_row = (int)conds->items[j].num;
                    continue;
                }
                if (!strcmp(conds->keys[j], "hidden")) {
                    r->hidden = conds->items[j].t == J_BOOL && conds->items[j].b;
                    continue;
                }
                if (!strcmp(conds->keys[j], "tooltip_title")) {
                    if (conds->items[j].t == J_STR)
                        r->tip_title = strdup(conds->items[j].str);
                    continue;
                }
                if (!strcmp(conds->keys[j], "tooltip_replace")) {
                    r->tip_replace = conds->items[j].t == J_BOOL && conds->items[j].b;
                    continue;
                }
                if (!strcmp(conds->keys[j], "tooltip")) {
                    const JVal *t = &conds->items[j];
                    if (t->t == J_ARR)
                        r->tip = calloc(t->n, sizeof(TipLine));
                    for (int k = 0; t->t == J_ARR && k < t->n; k++) {
                        const JVal *it = &t->items[k];
                        TipLine *tl = &r->tip[r->ntip];
                        if (it->t == J_STR) {
                            tl->text = strdup(it->str);
                            r->ntip++;
                            continue;
                        }
                        const JVal *text = json_get(it, "text");
                        if (it->t != J_OBJ || !text || text->t != J_STR) {
                            logf_("config %s: bad tooltip line for %s", c->name, r->id);
                            continue;
                        }
                        tl->text = strdup(text->str);
                        const JVal *font = json_get(it, "font");
                        if (font && font->t == J_STR)
                            snprintf(tl->font, sizeof tl->font, "%s", font->str);
                        const JVal *with = json_get(it, "with");
                        if (with && with->t == J_STR)
                            snprintf(tl->with, sizeof tl->with, "%s", with->str);
                        const JVal *base = json_get(it, "base");
                        tl->no_base = base && base->t == J_BOOL && !base->b;
                        for (int m = 0; m < it->n && tl->ncond < MAX_CONDS; m++)
                            if (strcmp(it->keys[m], "text") && strcmp(it->keys[m], "font") &&
                                strcmp(it->keys[m], "with") && strcmp(it->keys[m], "base") &&
                                parse_cond(c, it->keys[m], &it->items[m], &tl->cond[tl->ncond]))
                                tl->ncond++;
                        r->ntip++;
                    }
                    continue;
                }
                if (!strcmp(conds->keys[j], "heal")) {
                    if (conds->items[j].t == J_STR)
                        snprintf(r->heal, sizeof r->heal, "%s", conds->items[j].str);
                    continue;
                }
                if (parse_cond(c, conds->keys[j], &conds->items[j], &r->cond[r->ncond]))
                    r->ncond++;
            }
        }
    }

    const JVal *actions = json_get(root, "actions");
    if (actions && actions->t == J_OBJ) {
        c->actions = calloc(actions->n, sizeof(Action));
        for (int i = 0; i < actions->n; i++) {
            Action *a = &c->actions[c->nactions++];
            snprintf(a->name, sizeof a->name, "%s", actions->keys[i]);
            const JVal *rb = json_get(&actions->items[i], "remove_buffs");
            for (int j = 0; rb && rb->t == J_ARR && j < rb->n && a->nremove < MAX_REMOVE; j++)
                if (rb->items[j].t == J_STR)
                    snprintf(a->remove[a->nremove++], sizeof a->remove[0], "%s", rb->items[j].str);
            static const char *kinds[] = {"set", "add"};
            for (int k = 0; k < 2; k++) { // sets first, then adds
                const JVal *ops = json_get(&actions->items[i], kinds[k]);
                for (int j = 0; ops && ops->t == J_OBJ && j < ops->n && a->nops < MAX_OPS; j++) {
                    Op *o = &a->ops[a->nops];
                    o->field = field_register(ops->keys[j]);
                    o->add = k == 1;
                    if (o->field >= 0 && value_of(c, o->field, &ops->items[j], &o->value))
                        a->nops++;
                }
            }
        }
    }
    const JVal *variants = json_get(root, "animation_variants");
    for (int i = 0; variants && variants->t == J_ARR && i < variants->n && c->nvariants < 8; i++)
        if (variants->items[i].t == J_STR)
            snprintf(c->variants[c->nvariants++], sizeof c->variants[0], "%s", variants->items[i].str);

    const JVal *ev = json_get(root, "effect_variants");
    for (int i = 0; ev && ev->t == J_OBJ && i < ev->n && c->neffvar < 32; i++) {
        if (ev->items[i].t != J_STR)
            continue;
        snprintf(c->effvar[c->neffvar].name, sizeof c->effvar[0].name, "%s", ev->keys[i]);
        snprintf(c->effvar[c->neffvar].tmpl, sizeof c->effvar[0].tmpl, "%s", ev->items[i].str);
        c->neffvar++;
    }

    const JVal *effs = json_get(root, "effects");
    for (int i = 0; effs && effs->t == J_OBJ && i < effs->n && c->neffheal < 32; i++) {
        const JVal *h = json_get(&effs->items[i], "heal");
        if (!h || h->t != J_STR)
            continue;
        snprintf(c->effheal[c->neffheal].name, sizeof c->effheal[0].name, "%s", effs->keys[i]);
        snprintf(c->effheal[c->neffheal].heal, sizeof c->effheal[0].heal, "%s", h->str);
        c->neffheal++;
    }
    const JVal *items = json_get(root, "items");
    for (int i = 0; items && items->t == J_OBJ && i < items->n && c->nitems < 16; i++) {
        typeof(c->items[0]) *it = &c->items[c->nitems++];
        snprintf(it->id, sizeof it->id, "%s", items->keys[i]);
        it->id_hash = name_hash(it->id);
        const JVal *cs = &items->items[i];
        for (int j = 0; cs->t == J_OBJ && j < cs->n && it->ncond < MAX_CONDS; j++) {
            if (!strcmp(cs->keys[j], "combat")) {
                it->no_combat = cs->items[j].t == J_BOOL && !cs->items[j].b;
                continue;
            }
            if (parse_cond(c, cs->keys[j], &cs->items[j], &it->cond[it->ncond]))
                it->ncond++;
        }
    }
    const JVal *title = json_get(root, "tooltip_title");
    if (title && title->t == J_STR)
        c->tip_title = strdup(title->str);
    const JVal *dbg = json_get(root, "debug");
    if (dbg && dbg->t == J_BOOL && dbg->b)
        g_debug = true;

    if (json_get(root, "events"))
        logf_("config %s: \"events\" is gone: use the global \"hooks\" section", path);

    logf_("config %s: %d skill rules, %d actions, %d animation variants, %d effect variants (%s)", c->name, c->nrules,
          c->nactions, c->nvariants, c->neffvar, path);
}

static void apply_settings(const JVal *root, const char *path) {
    parse_riposte(json_get(root, "riposte"), path);
    parse_act_outs(json_get(root, "act_outs"), path);
}

static void parse_globals(const JVal *root, const char *path) {
    hook_rules_parse(json_get(root, "hooks"), path);
    parse_announce(json_get(root, "announce"), path);
    apply_settings(root, path);
}

// Every config file, kept parsed: settings are built again from them per campaign.
#define MAX_FILES 64
static struct {
    char *path;
    JVal *root;
    int mod;
} g_files[MAX_FILES];
static int g_nfiles;

static void load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc(len + 1);
    size_t got = fread(text, 1, len, f);
    text[got] = 0;
    fclose(f);
    char err[128];
    JVal *root = json_parse(text, err, sizeof err);
    free(text);
    if (!root) {
        logf_("%s: JSON error: %s", path, err);
        return;
    }
    int mod = mods_find(path);
    mods_set_current(mod); // what this file registers is its mod's
    parse_globals(root, path);
    parse_class(root, path);
    mods_set_current(-1);
    if (g_nfiles < MAX_FILES) {
        g_files[g_nfiles].path = strdup(path);
        g_files[g_nfiles].root = root;
        g_files[g_nfiles++].mod = mod;
    } else {
        logf_("config: more than %d files, %s's settings apply only at start-up", MAX_FILES, path);
        json_free(root);
    }
}

static void apply_mod_settings(int mod) {
    for (int i = 0; i < g_nfiles; i++)
        if (g_files[i].mod == mod)
            apply_settings(g_files[i].root, g_files[i].path);
}

static void on_campaign(void) {
    memset(&g_act_outs, 0, sizeof g_act_outs);
    memset(&g_riposte, 0, sizeof g_riposte);
    int order[64], n = mods_by_priority(order, 64);
    g_reapply = true;
    apply_mod_settings(-1); // files outside any mod: lowest priority
    for (int k = n - 1; k >= 0; k--)
        apply_mod_settings(order[k]);
    g_reapply = false;
    int classes = 0;
    for (int i = 0; i < g_nclasses; i++)
        classes += config_for_class_hash(g_classes[i].class_hash) == &g_classes[i];
    logf_("config: campaign settings from %d mod(s): %d class(es) in use, riposte %s, act_outs %d chance source(s), "
          "%d rule(s), %d type(s)",
          n, classes, g_riposte.ntypes ? "from the hero's skills" : "vanilla", g_act_outs.ntraits, g_act_outs.nrules,
          g_act_outs.ntypes);
}

// <dir>/<mod>/engine/*.engine.json
static void scan_mods(const char *dir) {
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char eng[4096];
        snprintf(eng, sizeof eng, "%s/%s/engine", dir, e->d_name);
        DIR *d2 = opendir(eng);
        if (!d2)
            continue;
        struct dirent *f;
        while ((f = readdir(d2))) {
            size_t l = strlen(f->d_name);
            if (l > 12 && !strcmp(f->d_name + l - 12, ".engine.json")) {
                char p[4200];
                snprintf(p, sizeof p, "%s/%s", eng, f->d_name);
                load_file(p);
            }
        }
        closedir(d2);
    }
    closedir(d);
}

void config_load_all(void) {
    field_register("test"); // always available for experiments
    char dir[4096];
    if (game_data_dir(dir, sizeof dir)) {
        char mods[4200];
        snprintf(mods, sizeof mods, "%s/mods", dir);
        scan_mods(mods);
    }
    if (workshop_dir(dir, sizeof dir))
        scan_mods(dir);
    logf_("config: %d class(es), %d field(s)", g_nclasses, g_nfields);
    mods_on_campaign(on_campaign);
}

// A class's config from the active mod highest in the campaign's list (a file outside any mod: always, lowest).
static int priority(int mod) {
    return mod < 0 ? 1 << 30 : mods_pos(mod);
}
const ClassConfig *config_for_class_hash(unsigned h) {
    const ClassConfig *best = NULL;
    for (int i = 0; i < g_nclasses; i++)
        if (g_classes[i].class_hash == h && mods_active(g_classes[i].mod) &&
            (!best || priority(g_classes[i].mod) < priority(best->mod)))
            best = &g_classes[i];
    return best;
}
const ClassConfig *config_for_hero(const void *hero) {
    return is_hero(hero) ? config_for_class_hash(hero_class_hash(hero)) : NULL;
}

// All conditions hold for the hero (fields read as 0 without one; field ofield read as oval).
static bool conds_hold_with(const void *hero, const Cond *conds, int n, int ofield, int oval) {
    for (int j = 0; j < n; j++) {
        const Cond *cd = &conds[j];
        int v = cd->field == ofield ? oval : hero ? herodata_get(hero_guid(hero), cd->field) : 0;
        if (cd->op == OP_MIN && v < cd->vals[0])
            return false;
        if (cd->op == OP_MAX && v > cd->vals[0])
            return false;
        if (cd->op == OP_EQ) {
            bool any = false;
            for (int k = 0; k < cd->nvals; k++)
                any |= v == cd->vals[k];
            if (!any)
                return false;
        }
    }
    return true;
}
static bool conds_hold(const void *hero, const Cond *conds, int n) {
    return conds_hold_with(hero, conds, n, -1, 0);
}

bool config_skill_allowed(const ClassConfig *c, const void *hero, unsigned skill_hash) {
    for (int i = 0; i < c->nrules; i++)
        if (c->rules[i].skill_hash == skill_hash)
            return !c->rules[i].hidden && conds_hold(hero, c->rules[i].cond, c->rules[i].ncond);
    return true;
}

// -1: no class configures the item (the game decides); 0: not usable by this hero; 1: usable (as far as we care).
int config_item_allowed(const void *hero, unsigned item_hash, bool in_battle) {
    for (int i = 0; i < g_nclasses; i++) {
        const ClassConfig *c = &g_classes[i];
        if (config_for_class_hash(c->class_hash) != c)
            continue; // not in use in this campaign
        for (int k = 0; k < c->nitems; k++) {
            if (c->items[k].id_hash != item_hash)
                continue;
            if (config_for_hero(hero) != c)
                return 0;
            if (in_battle && c->items[k].no_combat)
                return 0;
            return conds_hold(hero, c->items[k].cond, c->items[k].ncond) ? 1 : 0;
        }
    }
    return -1;
}

bool config_apply_action(const ClassConfig *c, const void *hero, const char *effect_name) {
    for (int i = 0; i < c->nactions; i++) {
        const Action *a = &c->actions[i];
        if (strcmp(a->name, effect_name))
            continue;
        unsigned guid = hero_guid(hero);
        if (a->nremove) {
            const char *pats[MAX_REMOVE];
            for (int j = 0; j < a->nremove; j++)
                pats[j] = a->remove[j];
            int removed = actor_remove_buffs((void *)hero, pats, a->nremove);
            dbgf_("%s: hero %u removed %d buff(s)", effect_name, guid, removed);
        }
        for (int j = 0; j < a->nops; j++) {
            const Op *o = &a->ops[j];
            int old = herodata_get(guid, o->field);
            int v = o->add ? old + o->value : o->value;
            if (c->has_limit[o->field]) {
                if (v < c->lo[o->field])
                    v = c->lo[o->field];
                if (v > c->hi[o->field])
                    v = c->hi[o->field];
            }
            herodata_set(guid, o->field, v);
            dbgf_("%s: hero %u %s %d -> %d", effect_name, guid, field_name(o->field), old, v);
        }
        return true;
    }
    return false;
}

// Text of a field's value: the enum name if the field has one for this value, else the number.
static void field_text(const ClassConfig *c, int field, int v, char *out, size_t outlen) {
    for (int e = 0; e < c->nenums; e++)
        if (c->enums[e].field == field && v >= 0 && v < c->enums[e].n) {
            snprintf(out, outlen, "%s", c->enums[e].names[v]);
            return;
        }
    snprintf(out, outlen, "%d", v);
}

// Fill a template: "{field}" -> the hero's value (enum name if the field has one).
static void fill_template(const ClassConfig *c, const void *hero, const char *t, char *out, size_t outlen) {
    unsigned guid = hero_guid(hero);
    size_t n = 0;
    while (*t && n + 1 < outlen) {
        if (*t == '{') {
            const char *close = strchr(t, '}');
            if (!close)
                break;
            char name[32];
            size_t l = (size_t)(close - t - 1);
            if (l >= sizeof name)
                l = sizeof name - 1;
            memcpy(name, t + 1, l);
            name[l] = 0;
            char val[32];
            int f = field_find(name);
            field_text(c, f, herodata_get(guid, f), val, sizeof val);
            for (const char *v = val; *v && n + 1 < outlen; v++)
                out[n++] = *v;
            t = close + 1;
        } else
            out[n++] = *t++;
    }
    out[n] = 0;
}

bool config_animation_variant(const ClassConfig *c, const void *hero, int i, char *out, size_t outlen) {
    if (i >= c->nvariants)
        return false;
    fill_template(c, hero, c->variants[i], out, outlen);
    return true;
}

bool config_effect_variant(const ClassConfig *c, const void *hero, const char *name, char *out, size_t outlen) {
    for (int i = 0; i < c->neffvar; i++)
        if (!strcmp(c->effvar[i].name, name)) {
            fill_template(c, hero, c->effvar[i].tmpl, out, outlen);
            return true;
        }
    return false;
}

// Formula names: the hero's fields (0 without a hero). In tooltips (skill_hash set) also "heal" / "dmg"
// (the skill's formulas) and "heal_<effect>" (an effect's heal formula).
// ofield/oval: one field read as another value (tooltip "@field+n"), -1 for none.
typedef struct {
    const ClassConfig *c;
    const void *hero;
    unsigned skill_hash;
    int ofield, oval;
    const TipVar *vars;
    int nvars;
} FieldCtx;
static bool rule_formula(const FieldCtx *fc, unsigned skill_hash, bool heal, double *out);
static bool effect_formula(const FieldCtx *fc, const char *effect_name, double *out);
static int field_value(const FieldCtx *fc, int f) {
    if (f == fc->ofield)
        return fc->oval;
    return fc->hero ? herodata_get(hero_guid(fc->hero), f) : 0;
}
static double field_lookup(const char *name, bool *ok, void *ctx) {
    const FieldCtx *fc = ctx;
    int f = field_find(name);
    if (f >= 0)
        return field_value(fc, f);
    size_t nl = strlen(name); // "<field>_max" / "<field>_min": the field's limits
    if (nl > 4 && (!strcmp(name + nl - 4, "_max") || !strcmp(name + nl - 4, "_min"))) {
        char base[32];
        snprintf(base, sizeof base, "%.*s", (int)(nl - 4), name);
        int lf = field_find(base);
        if (lf >= 0 && fc->c->has_limit[lf])
            return name[nl - 2] == 'a' ? fc->c->hi[lf] : fc->c->lo[lf];
    }
    double v = 0;
    if (fc->skill_hash && (!strcmp(name, "hit_min") || !strcmp(name, "hit_max"))) {
        const char *sfx = name + 3; // "_min" / "_max"
        char k[32];
        double wpn, extra, mult, dmg, rank = 0;
        bool ok2 = true;
        snprintf(k, sizeof k, "wpn%s", sfx);
        wpn = field_lookup(k, &ok2, ctx);
        snprintf(k, sizeof k, "dmg_extra%s", sfx);
        extra = field_lookup(k, &ok2, ctx);
        snprintf(k, sizeof k, "dmg_mult%s", sfx);
        mult = field_lookup(k, &ok2, ctx);
        dmg = field_lookup("dmg", &ok2, ctx);
        bool ok3 = true;
        rank = field_lookup("dmg_rank", &ok3, ctx);
        if (!ok3)
            rank = 0;
        if (ok2)
            return ceil((extra + wpn * (1 + dmg + rank)) * mult);
    }
    if (fc->skill_hash) {
        if (!strcmp(name, "heal") && rule_formula(fc, fc->skill_hash, true, &v))
            return v;
        if (!strcmp(name, "dmg") && rule_formula(fc, fc->skill_hash, false, &v))
            return v;
        if (!strncmp(name, "heal_", 5) && effect_formula(fc, name + 5, &v))
            return v;
        for (int i = 0; i < fc->nvars; i++)
            if (!strcmp(fc->vars[i].name, name))
                return fc->vars[i].value;
    }
    *ok = false;
    return 0;
}

// Formulas are evaluated with plain fields only (no heal/dmg names), keeping any override.
static bool rule_formula(const FieldCtx *fc, unsigned skill_hash, bool heal, double *out) {
    for (int i = 0; i < fc->c->nrules; i++) {
        const SkillRule *r = &fc->c->rules[i];
        const char *f = heal ? r->heal : r->dmg;
        if (r->skill_hash != skill_hash || !f[0])
            continue;
        FieldCtx plain = *fc;
        plain.skill_hash = 0;
        return expr_eval(f, field_lookup, &plain, out);
    }
    return false;
}
static bool effect_formula(const FieldCtx *fc, const char *effect_name, double *out) {
    for (int i = 0; i < fc->c->neffheal; i++) {
        if (strcmp(fc->c->effheal[i].name, effect_name))
            continue;
        FieldCtx plain = *fc;
        plain.skill_hash = 0;
        return expr_eval(fc->c->effheal[i].heal, field_lookup, &plain, out);
    }
    return false;
}
bool config_skill_dmg(const ClassConfig *c, const void *hero, unsigned skill_hash, double *out) {
    FieldCtx fc = {c, hero, 0, -1, 0, NULL, 0};
    return rule_formula(&fc, skill_hash, false, out);
}
bool config_skill_heal(const ClassConfig *c, const void *hero, unsigned skill_hash, double *out) {
    FieldCtx fc = {c, hero, 0, -1, 0, NULL, 0};
    return rule_formula(&fc, skill_hash, true, out);
}
bool config_effect_heal(const ClassConfig *c, const void *hero, const char *effect_name, double *out) {
    FieldCtx fc = {c, hero, 0, -1, 0, NULL, 0};
    return effect_formula(&fc, effect_name, out);
}

bool config_any_formulas(void) {
    for (int i = 0; i < g_nclasses; i++)
        for (int j = 0; j < g_classes[i].nrules; j++)
            if (g_classes[i].rules[j].dmg[0] || g_classes[i].rules[j].heal[0])
                return true;
    return false;
}

// ---------------------------------------------------------------- tooltips
int config_skill_tooltip(const ClassConfig *c, const void *hero, unsigned skill_hash, TipOut *out, int max,
                         bool *replace) {
    int n = 0;
    *replace = false;
    for (int i = 0; i < c->nrules; i++) {
        const SkillRule *r = &c->rules[i];
        if (r->skill_hash != skill_hash)
            continue;
        *replace = r->tip_replace;
        for (int k = 0; k < r->ntip && n < max; k++)
            if (conds_hold(hero, r->tip[k].cond, r->tip[k].ncond)) {
                out[n].text = r->tip[k].text;
                out[n].font = r->tip[k].font[0] ? r->tip[k].font : NULL;
                out[n].with = r->tip[k].with[0] ? r->tip[k].with : NULL;
                n++;
            }
        break;
    }
    return n;
}

int config_skill_tooltip_base(const ClassConfig *c, unsigned skill_hash, int field, int value, TipOut *out, int max,
                              bool *replace) {
    int n = 0;
    *replace = false;
    for (int i = 0; i < c->nrules; i++) {
        const SkillRule *r = &c->rules[i];
        if (r->skill_hash != skill_hash)
            continue;
        *replace = r->tip_replace;
        for (int k = 0; k < r->ntip && n < max; k++) {
            const TipLine *t = &r->tip[k];
            if (t->with[0] || t->no_base)
                continue; // "next" lines are about the live value
            if (!conds_hold_with(NULL, t->cond, t->ncond, field, value))
                continue;
            out[n].text = t->text;
            out[n].font = t->font[0] ? t->font : NULL;
            out[n].with = NULL;
            n++;
        }
        break;
    }
    return n;
}

static const SkillRule *rule_for(const ClassConfig *c, unsigned skill_hash);
bool config_skill_base_range(const ClassConfig *c, unsigned skill_hash, int *field, int *lo, int *hi) {
    const SkillRule *r = rule_for(c, skill_hash);
    for (int k = 0; r && k < r->ncond; k++) {
        const Cond *cd = &r->cond[k];
        if (cd->op == OP_MIN && cd->field >= 0 && c->has_limit[cd->field]) {
            *field = cd->field;
            *lo = cd->vals[0] > c->lo[cd->field] ? cd->vals[0] : c->lo[cd->field];
            *hi = c->hi[cd->field];
            return *lo <= *hi;
        }
    }
    return false;
}

static void put(char *out, size_t outlen, size_t *n, const char *s) {
    while (*s && *n + 1 < outlen)
        out[(*n)++] = *s++;
}

// One token body, "expr" or "expr|fmt", into text.
static void token_text(const ClassConfig *c, const void *hero, unsigned skill_hash, const TipVar *vars, int nvars,
                       const char *with, const char *body, char *out, size_t outlen) {
    char ex[160], fmt[8] = "";
    if (with && !strchr(body, '@')) { // the line's default field change
        const char *bar = strchr(body, '|');
        if (bar)
            snprintf(ex, sizeof ex, "%.*s@%s%s", (int)(bar - body), body, with, bar);
        else
            snprintf(ex, sizeof ex, "%s@%s", body, with);
    } else
        snprintf(ex, sizeof ex, "%s", body);
    char *bar = strchr(ex, '|');
    if (bar) {
        *bar = 0;
        snprintf(fmt, sizeof fmt, "%s", bar + 1);
    }
    FieldCtx fc = {c, hero, skill_hash ? skill_hash : 1, -1, 0, vars, nvars};
    char *at = strchr(ex, '@');
    if (at) { // "@field+n", "@field-n", "@field=n"
        *at++ = 0;
        char *op = at + strcspn(at, "+-=");
        char opc = *op;
        if (opc)
            *op = 0;
        int f = field_find(at);
        if (f < 0 || !opc) {
            snprintf(out, outlen, "[[?@%s]]", at);
            return;
        }
        int n = atoi(op + 1), v = field_value(&fc, f);
        v = opc == '+' ? v + n : opc == '-' ? v - n : n;
        if (c->has_limit[f]) {
            if (v < c->lo[f])
                v = c->lo[f];
            if (v > c->hi[f])
                v = c->hi[f];
        }
        fc.ofield = f;
        fc.oval = v;
    }
    for (char *e = ex + strlen(ex); e > ex && e[-1] == ' ';)
        *--e = 0;
    int f = field_find(ex);
    if (f >= 0 && !fmt[0]) { // bare field: enum name or number
        field_text(c, f, field_value(&fc, f), out, outlen);
        return;
    }
    double v;
    if (!expr_eval(ex, field_lookup, &fc, &v)) {
        snprintf(out, outlen, "[[?%s]]", ex);
        return;
    }
    bool sign = strchr(fmt, '+') != NULL, pct = strchr(fmt, '%') != NULL;
    long r = lround(pct ? v * 100 : v);
    snprintf(out, outlen, sign ? "%+ld%s" : "%ld%s", r, pct ? "%" : "");
}

bool config_tooltip_fill(const ClassConfig *c, const void *hero, unsigned skill_hash, const TipVar *vars, int nvars,
                         const char *with, const char *in, char *out, size_t outlen) {
    if (!strstr(in, "[["))
        return false;
    size_t n = 0;
    while (*in && n + 1 < outlen) {
        const char *open = strstr(in, "[["), *close = open ? strstr(open + 2, "]]") : NULL;
        if (!open || !close) {
            put(out, outlen, &n, in);
            break;
        }
        while (in < open && n + 1 < outlen)
            out[n++] = *in++;
        char body[128], val[64];
        size_t l = (size_t)(close - open - 2);
        if (l >= sizeof body)
            l = sizeof body - 1;
        memcpy(body, open + 2, l);
        body[l] = 0;
        token_text(c, hero, skill_hash, vars, nvars, with, body, val, sizeof val);
        put(out, outlen, &n, val);
        in = close + 2;
    }
    out[n] = 0;
    return true;
}

const char *config_tooltip_title(const ClassConfig *c, unsigned skill_hash) {
    for (int i = 0; i < c->nrules; i++)
        if (c->rules[i].skill_hash == skill_hash && c->rules[i].tip_title)
            return c->rules[i].tip_title;
    return c->tip_title;
}

// ---------------------------------------------------------------- cooldowns
static const SkillRule *rule_for(const ClassConfig *c, unsigned skill_hash) {
    for (int i = 0; i < c->nrules; i++)
        if (c->rules[i].skill_hash == skill_hash)
            return &c->rules[i];
    return NULL;
}

int config_cooldown_left(const ClassConfig *c, const void *hero, unsigned skill_hash) {
    const SkillRule *r = rule_for(c, skill_hash);
    int left = 0;
    unsigned guid = hero_guid(hero);
    for (int k = 0; r && k < r->ncd; k++) {
        int v = herodata_get(guid, c->cds[r->cd[k]].field);
        if (v > left)
            left = v;
    }
    return left;
}

void config_cooldown_start(const ClassConfig *c, const void *hero, unsigned skill_hash, bool own_turn) {
    const SkillRule *r = rule_for(c, skill_hash);
    unsigned guid = hero_guid(hero);
    for (int k = 0; r && k < r->ncd; k++) {
        const typeof(c->cds[0]) *cd = &c->cds[r->cd[k]];
        // +1: the turn the skill is used on ends (own) / the next turn starts (actor) before the count means
        // anything, so `turns` whole turns stay blocked after it. Used on another actor's turn (a riposte), no own
        // turn ends first, so an own cooldown starts at `turns`.
        int left = cd->turns + (own_turn || cd->actor ? 1 : 0);
        herodata_set(guid, cd->field, left);
        dbgf_("cooldown %s: hero %u skill %08x -> %d", cd->name, guid, skill_hash, left);
    }
}

// One tick: own = the hero's own turn ended; actor = an actor's turn started (for every hero).
void config_cooldown_tick(const ClassConfig *c, unsigned guid, bool actor) {
    for (int k = 0; k < c->ncds; k++) {
        if (c->cds[k].actor != actor)
            continue;
        int v = herodata_get(guid, c->cds[k].field);
        if (v > 0)
            herodata_set(guid, c->cds[k].field, v - 1);
    }
}

void config_cooldown_reset(const ClassConfig *c, unsigned guid) {
    for (int k = 0; k < c->ncds; k++)
        if (herodata_get(guid, c->cds[k].field))
            herodata_set(guid, c->cds[k].field, 0);
}

bool config_any_cooldowns(void) {
    for (int i = 0; i < g_nclasses; i++)
        if (g_classes[i].ncds)
            return true;
    return false;
}

const ClassConfig *config_class_at(int i) {
    return i < g_nclasses ? &g_classes[i] : NULL;
}

const char *config_cooldown_tooltip(const ClassConfig *c, int left) {
    return left <= 0 ? NULL : c->cd_tip[left == 1 ? 0 : 1];
}

int config_cooldown_turns(const ClassConfig *c, unsigned skill_hash) {
    const SkillRule *r = rule_for(c, skill_hash);
    int t = 0;
    for (int k = 0; r && k < r->ncd; k++)
        if (c->cds[r->cd[k]].turns > t)
            t = c->cds[r->cd[k]].turns;
    return t;
}

const char *config_cooldown_ready_tooltip(const ClassConfig *c, int turns) {
    return turns <= 0 ? NULL : c->cd_ready_tip[turns == 1 ? 0 : 1];
}

int config_page_size(const ClassConfig *c) {
    return c->page_size;
}
const char *config_page_icon(const ClassConfig *c, int i) {
    return c->page_icon[i & 3];
}

int config_guild_row(const ClassConfig *c, unsigned skill_hash, int *order) {
    for (int i = 0; i < c->nrules; i++)
        if (c->rules[i].skill_hash == skill_hash) {
            if (order)
                *order = i;
            return c->rules[i].guild_row;
        }
    if (order)
        *order = c->nrules;
    return 0;
}

int config_guild_first_rank(const ClassConfig *c, unsigned skill_hash) {
    for (int i = 0; i < c->nrules; i++)
        if (c->rules[i].skill_hash == skill_hash)
            return c->rules[i].guild_first_rank > 0 ? c->rules[i].guild_first_rank : 1;
    return 1;
}

bool config_skill_hidden(const ClassConfig *c, unsigned skill_hash) {
    for (int i = 0; i < c->nrules; i++)
        if (c->rules[i].skill_hash == skill_hash)
            return c->rules[i].hidden;
    return false;
}

int config_sheet_page(const ClassConfig *c, unsigned skill_hash) {
    for (int i = 0; i < c->nrules; i++)
        if (c->rules[i].skill_hash == skill_hash)
            return c->rules[i].sheet_page ? c->rules[i].sheet_page : c->rules[i].guild_row;
    return 0;
}

bool config_guild_row_centred(const ClassConfig *c, int row) {
    for (int i = 0; i < c->ncentred; i++)
        if (c->centred[i] == row)
            return true;
    return false;
}
