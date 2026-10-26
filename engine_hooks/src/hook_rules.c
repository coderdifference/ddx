// Config hook rules: the global "hooks" section of any engine file (see common.h, "hook_rules.c", and the guide,
// 12-hook-points.md "From a config"). Each rule listens to one point and phase; when its filters pass it changes the
// point's context (before) and runs its actions through the game's code (game_actions.c).
//
// A rule's actions run with a depth count: a point fired while they run (a damage action's hp_damage) skips every rule
// that isn't "nested": true, and so do synthetic contexts, so rules can't loop by default.
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MATCH    4
#define MAX_PATTERNS 8
#define MAX_TARGETS  8

typedef enum {
    A_EFFECT,
    A_ACTION,
    A_REMOVE_BUFFS,
    A_DAMAGE,
    A_HEAL,
    A_STRESS,
    A_TORCH,
    A_QUIRK,
    A_REMOVE_QUIRK,
    A_ITEM,
    A_LOOT,
    A_LOG
} ActKind;
static const char *const k_act_keys[] = {"effect", "action", "remove_buffs", "damage", "heal", "stress",
                                         "torch",  "quirk",  "remove_quirk", "item",   "loot", "log"};
#define NACTS ((int)(sizeof k_act_keys / sizeof k_act_keys[0]))

typedef struct {
    ActKind kind;
    char *arg;                    // the id or name (effect, action, quirk, item, loot, log)
    char *value;                  // damage, heal, stress, torch: a number or formula
    char *type, *source;          // damage: type / source; heal, stress: source; item, loot: type
    char *on;                     // the target ("actor" by default)
    int amount;                   // item: how many
    char *patterns[MAX_PATTERNS]; // remove_buffs
    int npatterns;
} Act;

typedef struct {
    char *file;
    int index;
    int mod; // its file's mod (mods.c): runs only while that mod is active
    char *point;
    HookPhase phase;
    unsigned class_hash; // 0: any actor
    struct {
        char *key, *glob;
    } match[MAX_MATCH];
    int nmatch;
    char *when, *chance;
    bool nested;
    char *amount;      // before: a formula for the new amount ("amount" is the old)
    double scale, add; // before: amount * scale + add
    bool cancel, has_refuse;
    char *set_type; // before: the context's type (loot code, quirk id, trait id)
    unsigned refuse;
    Act *acts;
    int nacts;
} Rule;

static Rule *g_rules;
static int g_nrules, g_cap;
static int g_depth;

static char *dup(const char *s) {
    return s ? strdup(s) : NULL;
}

// a number or a formula string, as text for expr_eval
static char *num_or_formula(const JVal *v) {
    if (!v)
        return NULL;
    if (v->t == J_STR)
        return dup(v->str);
    if (v->t == J_NUM) {
        char b[32];
        snprintf(b, sizeof b, "%.9g", v->num);
        return dup(b);
    }
    if (v->t == J_BOOL)
        return dup(v->b ? "1" : "0");
    return NULL;
}

static const char *str_of(const JVal *o, const char *key) {
    const JVal *v = json_get(o, key);
    return v && v->t == J_STR ? v->str : NULL;
}

static bool parse_act(const JVal *o, Act *a, const char *where) {
    memset(a, 0, sizeof *a);
    int k = -1;
    for (int i = 0; i < NACTS; i++)
        if (json_get(o, k_act_keys[i])) {
            k = i;
            break;
        }
    if (k < 0) {
        logf_("%s: an action without a known verb (effect, action, damage, heal, stress, torch, quirk, "
              "remove_quirk, remove_buffs, item, loot, log)",
              where);
        return false;
    }
    a->kind = (ActKind)k;
    const JVal *v = json_get(o, k_act_keys[k]);
    a->on = dup(str_of(o, "on"));
    a->type = dup(str_of(o, "type"));
    a->source = dup(str_of(o, "source"));
    switch (a->kind) {
    case A_DAMAGE:
    case A_HEAL:
    case A_STRESS:
    case A_TORCH:
        a->value = num_or_formula(v);
        if (!a->value) {
            logf_("%s: %s needs a number or formula", where, k_act_keys[k]);
            return false;
        }
        break;
    case A_REMOVE_BUFFS:
        if (v->t == J_STR)
            a->patterns[a->npatterns++] = dup(v->str);
        for (int i = 0; v->t == J_ARR && i < v->n && a->npatterns < MAX_PATTERNS; i++)
            if (v->items[i].t == J_STR)
                a->patterns[a->npatterns++] = dup(v->items[i].str);
        if (!a->npatterns) {
            logf_("%s: remove_buffs needs a glob or a list of them", where);
            return false;
        }
        break;
    default:
        if (v->t != J_STR) {
            logf_("%s: %s needs a string", where, k_act_keys[k]);
            return false;
        }
        a->arg = dup(v->str);
        break;
    }
    const JVal *n = json_get(o, "amount");
    a->amount = n && n->t == J_NUM && n->num >= 1 ? (int)n->num : 1;
    return true;
}

void hook_rules_parse(const JVal *list, const char *path) {
    if (!list)
        return;
    if (list->t != J_ARR) {
        logf_("config %s: \"hooks\" is a list of rules", path);
        return;
    }
    for (int i = 0; i < list->n; i++) {
        const JVal *o = &list->items[i];
        char where[160];
        snprintf(where, sizeof where, "config %s: hooks[%d]", path, i);
        const char *point = str_of(o, "point");
        if (o->t != J_OBJ || !point) {
            logf_("%s: a rule needs \"point\"", where);
            continue;
        }
        if (g_nrules == g_cap) {
            g_cap = g_cap ? g_cap * 2 : 16;
            g_rules = realloc(g_rules, sizeof *g_rules * (size_t)g_cap);
        }
        Rule *r = &g_rules[g_nrules];
        memset(r, 0, sizeof *r);
        r->file = dup(path);
        r->index = i;
        r->mod = mods_current();
        r->point = dup(point);
        const char *ph = str_of(o, "phase");
        r->phase = ph && !strcmp(ph, "before") ? HOOK_BEFORE : HOOK_AFTER;
        if (ph && strcmp(ph, "before") && strcmp(ph, "after"))
            logf_("%s: phase \"%s\": before or after (after used)", where, ph);
        const char *cls = str_of(o, "class");
        r->class_hash = cls ? name_hash(cls) : 0;
        const JVal *m = json_get(o, "match");
        for (int k = 0; m && m->t == J_OBJ && k < m->n && r->nmatch < MAX_MATCH; k++) {
            if (m->items[k].t != J_STR) {
                logf_("%s: match.%s needs a glob string", where, m->keys[k]);
                continue;
            }
            if (strcmp(m->keys[k], "type") && strcmp(m->keys[k], "source") && strcmp(m->keys[k], "skip"))
                logf_("%s: match.%s: type, source or skip", where, m->keys[k]);
            r->match[r->nmatch].key = dup(m->keys[k]);
            r->match[r->nmatch++].glob = dup(m->items[k].str);
        }
        r->when = dup(str_of(o, "when"));
        r->chance = num_or_formula(json_get(o, "chance"));
        const JVal *nested = json_get(o, "nested");
        r->nested = nested && nested->t == J_BOOL && nested->b;
        r->amount = num_or_formula(json_get(o, "amount"));
        const JVal *sc = json_get(o, "scale"), *ad = json_get(o, "add"), *ca = json_get(o, "cancel"),
                   *rf = json_get(o, "refuse");
        r->scale = sc && sc->t == J_NUM ? sc->num : 1;
        r->add = ad && ad->t == J_NUM ? ad->num : 0;
        r->cancel = ca && ca->t == J_BOOL && ca->b;
        if (rf && rf->t == J_NUM) {
            r->has_refuse = true;
            r->refuse = (unsigned)rf->num;
        }
        r->set_type = dup(str_of(o, "set_type"));
        bool changes = r->amount || sc || ad || r->cancel || r->has_refuse || r->set_type;
        if (changes && r->phase != HOOK_BEFORE)
            logf_("%s: amount / scale / add / cancel / refuse / set_type only "
                  "act in the before phase",
                  where);
        const JVal *d = json_get(o, "do");
        if (d && d->t != J_ARR) {
            logf_("%s: \"do\" is a list of actions", where);
            d = NULL;
        }
        if (d && d->n) {
            r->acts = calloc((size_t)d->n, sizeof *r->acts);
            for (int k = 0; k < d->n; k++) {
                char w2[200];
                snprintf(w2, sizeof w2, "%s.do[%d]", where, k);
                if (d->items[k].t == J_OBJ && parse_act(&d->items[k], &r->acts[r->nacts], w2))
                    r->nacts++;
            }
        }
        if (!changes && !r->nacts) {
            logf_("%s: no changes and no actions, skipped", where);
            continue;
        }
        g_nrules++;
    }
    logf_("config %s: hooks: %d rule(s) in all so far", path, g_nrules);
}

// ---------------------------------------------------------------- running a rule
typedef struct {
    const HookContext *hc;
    CombatContext *cc;
    bool have_cc;
} Vars;

static double lookup(const char *name, bool *ok, void *p) {
    const Vars *v = p;
    const HookContext *c = v->hc;
    if (!strcmp(name, "amount"))
        return c->amount;
    if (!strcmp(name, "result"))
        return c->result;
    if (!strcmp(name, "kind"))
        return c->kind;
    if (!strcmp(name, "hit"))
        return c->hit;
    if (!strcmp(name, "crit"))
        return c->crit;
    if (!strcmp(name, "refuse"))
        return c->refuse;
    if (!strcmp(name, "synthetic"))
        return c->synthetic;
    if (v->have_cc)
        return combat_context_lookup(name, ok, v->cc);
    *ok = false;
    return 0;
}

static double call(const char *name, const ExprArg *args, int n, bool *ok, void *p) {
    const Vars *v = p;
    if (v->have_cc)
        return combat_context_call(name, args, n, ok, v->cc);
    *ok = false;
    return 0;
}

static bool eval(const Rule *r, const char *f, Vars *v, double *out) {
    if (expr_eval_fn(f, lookup, call, v, out))
        return true;
    logf_("hooks: %s rule %d (%s): formula error in \"%s\"", r->file, r->index, r->point, f);
    return false;
}

static bool glob_ok(const char *glob, const char *s) {
    return s && glob_match(glob, s);
}

static int targets(const char *on, const HookContext *c, void **out) {
    if (!on || !strcmp(on, "actor") || !strcmp(on, "self")) {
        out[0] = c->actor;
        return c->actor ? 1 : 0;
    }
    if (!strcmp(on, "target")) {
        out[0] = c->target;
        return c->target ? 1 : 0;
    }
    if (!strcmp(on, "attacker")) {
        out[0] = c->attacker;
        return c->attacker ? 1 : 0;
    }
    return act_out_targets(on, c->actor, game_rng(), out, MAX_TARGETS);
}

static void run_act(const Rule *r, const Act *a, HookContext *c, Vars *v) {
    double x = 0;
    if (a->value && !eval(r, a->value, v, &x))
        return;
    if (a->kind == A_TORCH) {
        game_torch((float)x);
        return;
    }
    if (a->kind == A_ITEM) {
        game_give_item(a->type ? a->type : "trinket", a->arg, a->amount);
        return;
    }
    if (a->kind == A_LOOT) {
        game_queue_loot(a->type, a->arg);
        return;
    }
    if (a->kind == A_LOG) {
        logf_("hooks: %s (%s %s)", a->arg, c->point, hook_phase_name(c->phase));
        return;
    }
    void *t[MAX_TARGETS];
    int n = targets(a->on, c, t);
    if (!n)
        dbgf_("hooks: %s rule %d: no %s for %s", r->file, r->index, a->on ? a->on : "actor", k_act_keys[a->kind]);
    for (int i = 0; i < n; i++) {
        if (!t[i])
            continue;
        switch (a->kind) {
        case A_EFFECT:
            game_apply_effect(c->actor ? c->actor : t[i], t[i], a->arg);
            break;
        case A_ACTION: {
            const ClassConfig *cc = is_hero(t[i]) ? config_for_hero(t[i]) : NULL;
            if (!cc || !config_apply_action(cc, t[i], a->arg))
                logf_("hooks: %s rule %d: no action %s for that hero's class", r->file, r->index, a->arg);
            break;
        }
        case A_REMOVE_BUFFS:
            actor_remove_buffs(t[i], (const char *const *)a->patterns, a->npatterns);
            break;
        case A_DAMAGE:
            game_damage(t[i], (float)x, a->type, a->source, NULL);
            break;
        case A_HEAL:
            game_heal(t[i], (float)x, c->actor, a->source);
            break;
        case A_STRESS:
            game_stress(t[i], (float)x, c->actor, a->source);
            break;
        case A_QUIRK:
            game_add_quirk(t[i], a->arg);
            break;
        case A_REMOVE_QUIRK:
            game_remove_quirk(t[i], a->arg);
            break;
        default:
            break;
        }
    }
}

static void on_point(HookContext *c, void *ud) {
    Rule *r = ud;
    if ((g_depth > 0 || c->synthetic) && !r->nested)
        return;
    if (r->class_hash && (!c->actor || !is_hero(c->actor) || hero_class_hash(c->actor) != r->class_hash))
        return;
    for (int i = 0; i < r->nmatch; i++) {
        const char *s = !strcmp(r->match[i].key, "type")     ? c->type
                        : !strcmp(r->match[i].key, "source") ? c->source
                                                             : c->skip;
        if (!glob_ok(r->match[i].glob, s))
            return;
    }
    static CombatContext ccs[4]; // one per depth (a nested rule builds its own)
    Vars v = {.hc = c, .cc = &ccs[g_depth < 4 ? g_depth : 3]};
    v.have_cc = combat_context_build(v.cc, c->actor);
    double x;
    if (r->when && (!eval(r, r->when, &v, &x) || x == 0))
        return;
    if (r->chance && (!eval(r, r->chance, &v, &x) || game_random() >= x))
        return;
    dbgf_("hooks: %s rule %d fires on %s %s", r->file, r->index, c->point, hook_phase_name(c->phase));
    if (c->phase == HOOK_BEFORE) {
        if (r->amount && eval(r, r->amount, &v, &x))
            c->amount = (float)x;
        c->amount = (float)(c->amount * r->scale + r->add);
        if (r->set_type)
            c->type = r->set_type;
        if (r->has_refuse)
            c->refuse = r->refuse;
        if (r->cancel)
            c->cancel = true;
    }
    g_depth++;
    for (int i = 0; i < r->nacts; i++)
        run_act(r, &r->acts[i], c, &v);
    g_depth--;
}

void hook_rules_register(void) {
    int n = 0;
    for (int i = 0; i < g_nrules; i++) {
        Rule *r = &g_rules[i];
        mods_set_current(r->mod); // the listener is the rule's mod's
        bool ok = hook_on(r->point, r->phase, on_point, r);
        mods_set_current(-1);
        if (ok)
            n++;
        else
            logf_("hooks: %s rule %d: %s %s isn't a hook point and phase", r->file, r->index, r->point,
                  hook_phase_name(r->phase));
    }
    if (g_nrules)
        logf_("hooks: %d of %d rule(s) listening", n, g_nrules);
}
