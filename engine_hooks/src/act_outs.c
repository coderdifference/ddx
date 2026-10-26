// Affliction act-outs (macOS; heroes only): the act_out and act_out_check hook points (hooks.c). Design:
// design/docs/16-act-outs.md; the game's side: docs/engine-design.md "Affliction act-outs".
// Behaviors::Combat_StartTurn rolls a turn-start act-out with Actor::RollActOutCombatStartTurnData (its only caller),
// checks it, then queues its outcome on a timeline that the display plays after the call: nothing of it is applied
// before Combat_StartTurn returns. The three checks, each called only from Combat_StartTurn:
//   Battle::GetIsValidActOut(is_hero, type): not the party's last act-out (no act-out twice in a row);
//   Battle::GetIsActOutCombatStartTurnTypeDisabled(type): a monster in the battle disables the type;
//   Raid::GetActOutCombatStartTurnCount(type) >= the data's raid_limit (only asked when the data has a limit).
// Any refusal: the game sets the last act-out to "nothing" and the turn is a normal one. "nothing" isn't reported.
// Config "act_outs": {"allow_back_to_back": true} (config.c) leaves the repeat check out of the game's verdict.
//   act_out before: the roll picked an act-out; type: its id, kind: its number; cancel: none this turn.
//   act_out_check before: the game's verdict in refuse (ACT_OUT_REPEAT | _DISABLED | _LIMIT, 0: allowed), which
//     listeners may change; cancel refuses. The library asks the game's checks itself, then answers the game's calls
//     with the outcome. after: refuse as decided (ACT_OUT_CANCELLED for a cancel), result 1 allowed, 0 refused.
//   act_out after: Combat_StartTurn queued it (allowed), its outcome still to come; result: the turn result (0 a
//     normal turn, 1 the hero then acts (random_command: the skill chosen for it), 2 the turn is lost).
// Combat_StartTurn as called: rdi unused, rsi the rng, rdx the actor; the turn result in eax.
//
// The roll is the library's (it replaces Actor::RollActOutCombatStartTurnData). The game's: gather the act-out entries
// of the actor's trait (Actor+0x11b8, else +0x11bc: a trait id hash; Trait::Library::GetType, +0xd0: its
// ActOut::Data) and, for a hero unless Hero+0x127e, of each quirk (Hero+0x12f8: 0x38-byte entries, the quirk's class
// first, +0x1c8 its ActOut::Data); an ActOut::Data starts with a vector of CombatStartTurnData (0xb0 bytes each:
// type +0x44, chance +0x8c, class ids +0x90, a vector<unsigned>); keep each entry with a chance > 0 whose class list is
// empty or holds the hero's class; pick one by weight: the rng's virtual random_uniform (vtable+0x18) times the total,
// the first entry whose running sum is above it (none: no act-out, and the rng isn't used). The library's roll is the
// same, except for a source (a trait or a quirk) in config act_outs.chances: its entries' weights are the config's, by
// type, exactly as written (percentages: each source's sum to 100; a type not listed: 0; the data's chance doesn't
// matter), shared among the type's entries the hero may use (attack_friendly has one per class; a type with none
// can't be rolled). All sources' weights then make one pool: chance = weight / the pool's total. Rules (below) multiply
// entries first. Config act_outs.use_max: each act-out type keeps only the entries of the source giving it the most
// weight (its entries of that type summed; a tie: the first source, the trait), so the pool is the sum of each
// act-out's highest weight.
//
// New act-out types (act_out_type_register, config act_outs.types): the game loads each data entry's type with
// ActOut::CombatStartTurn::GetTypeFromId, which answers a registered id with a number from 0x100 (else 15 and an
// assert); GetIdFromType and the other k_Data getters answer those numbers, so the raid's act-out counts save by id
// (Raid::Save) and reload (Raid::Restore), and ValidateActOutBarks (a debug check at SetupRaid) reads no further than
// k_Data. The game's Combat_StartTurn would drop such a number (a plErrorf, a normal turn), so the library runs it:
// run_new does what Combat_StartTurn does for a vanilla type (the checks, SetLastActOut, the count, burndown, the
// source's sound and halo, a wait, the bark "str_<id>_<source>"), then queues the outcome on the display after the bark
// (RaidDisplay::QueueLambda), where the game's timeline would play it. Not sent: HeroActOutStartEvent /
// HeroActOutFinishEvent (the game's listeners: statistics). k_Data is filled by the game's static initializers, which
// run after the library's start: nothing here reads it before the game runs.
#include "common.h"
#include <stdio.h>
#include <string.h>

// Windows (doc 18): MSVC inlined the roll and the three checks into Combat_StartTurn, so two read-write sites there
// stand in for their hooks: after the roll (the entry in rdi, the library's put in its place) and where the checks'
// verdicts meet (the game's, then the outcome written back). The game's own roll has run by then (one rng draw more
// than macOS). The k_Data getters are inlined everywhere and their table is followed by another one, so new type
// numbers can't work: each configured type loads as its "like" vanilla type (see act_outs_install).
#define ACT_OUT_VANILLA_TYPES 15 // ActOut::CombatStartTurn::k_Data's entries

static int (*o_start_turn)(void *unused, void *rng, void *actor);
#ifndef _WIN32
static const void *(*o_roll)(const void *actor, void *rng);
static bool (*o_valid)(const void *battle, bool is_hero, int type);
static bool (*o_disabled)(const void *battle, int type);
static unsigned (*o_count)(const void *raid, int type);
#endif
static const char *(*g_act_out_id)(int type); // ActOut::CombatStartTurn::GetIdFromType
static void **g_raid;
static void **g_trait_lib;                                             // Trait::Library::s_Instance
static const uint8_t *(*g_trait_type)(const void *lib, unsigned hash); // Trait::Library::GetType(unsigned)

// ---------------------------------------------------------------- new act-out types (registry)
#define ACT_OUT_FIRST_NEW 0x100
#define MAX_NEW_TYPES     32
typedef struct {
    ActOutTypeDef def;
    char id[32], like[32];
    int owner;
} NewType; // owner: its mod (mods_current)
static NewType g_types[MAX_NEW_TYPES];
static int g_ntypes;

static const NewType *new_type(int type) {
    int i = type - ACT_OUT_FIRST_NEW;
    return i >= 0 && i < g_ntypes ? &g_types[i] : NULL;
}
// Any type's id: a vanilla one's (k_Data, through the trampoline once GetIdFromType is hooked), a registered one's.
static const char *act_out_name(int type) {
    const NewType *t = new_type(type);
    if (t)
        return t->id;
    return type >= 0 && type < ACT_OUT_VANILLA_TYPES ? g_act_out_id(type) : "?";
}

int act_out_type_register(const ActOutTypeDef *def) {
#ifdef _WIN32
    logf_("act-outs: type %s not registered (new act-out types aren't ported to Windows)",
          def && def->id ? def->id : "?");
    return -1;
#endif
    if (!def || !def->id || !*def->id || strlen(def->id) >= sizeof g_types[0].id) {
        logf_("act-outs: a type without a valid id, not registered");
        return -1;
    }
    int i = 0;
    while (i < g_ntypes && strcmp(g_types[i].id, def->id))
        i++; // registered again: replaced, same number
    if (i == MAX_NEW_TYPES) {
        logf_("act-outs: too many types, %s not registered", def->id);
        return -1;
    }
    NewType *t = &g_types[i];
    t->def = *def;
    snprintf(t->id, sizeof t->id, "%s", def->id);
    snprintf(t->like, sizeof t->like, "%s", def->like ? def->like : "");
    t->def.id = t->id;
    t->def.like = t->like[0] ? t->like : NULL;
    t->owner = mods_current();
    if (i == g_ntypes)
        g_ntypes++;
    logf_("act-outs: type %s is %#x", t->id, ACT_OUT_FIRST_NEW + i);
    return ACT_OUT_FIRST_NEW + i;
}

// The act-out of the Combat_StartTurn in progress: rolled (0: none) and the check's outcome, which the hooked
// checks answer the game with.
static int g_rolled;
static bool g_decided, g_allowed;
static unsigned g_refuse;

#ifdef _WIN32
// The game's verdicts (its inlined checks', in refuse) to the act_out_check listeners.
static void check(void *actor, const void *data, int type, unsigned refuse) {
    uint8_t *raid = *g_raid;
    const void *battle = raid + RAID_BATTLE;
    unsigned limit = *(const unsigned *)((const uint8_t *)data + ACT_OUT_RAID_LIMIT);
    if (config_act_outs()->allow_back_to_back)
        refuse &= ~(unsigned)ACT_OUT_REPEAT;
#else
static void check(void *actor, const void *data, int type) {
    uint8_t *raid = *g_raid;
    const void *battle = raid + RAID_BATTLE;
    unsigned limit = *(const unsigned *)((const uint8_t *)data + ACT_OUT_RAID_LIMIT);
    unsigned refuse = 0;
    if (!config_act_outs()->allow_back_to_back && !o_valid(battle, true, type))
        refuse |= ACT_OUT_REPEAT;
    if (o_disabled(battle, type))
        refuse |= ACT_OUT_DISABLED;
    if (limit && o_count(raid, type) >= limit)
        refuse |= ACT_OUT_LIMIT;
#endif
    HookContext c = {.actor = actor,
                     .raid = raid,
                     .battle = (void *)battle,
                     .kind = type,
                     .type = act_out_name(type),
                     .refuse = refuse,
                     .amount = (float)limit};
    bool go = hook_emit("act_out_check", HOOK_BEFORE, &c);
    g_refuse = go ? c.refuse : c.refuse | ACT_OUT_CANCELLED;
    g_allowed = g_refuse == 0;
    g_decided = true;
}

// ---------------------------------------------------------------- the roll
#define MAX_ENTRIES 256
typedef struct {
    const uint8_t *e[MAX_ENTRIES];
    float w[MAX_ENTRIES];
    const char *src[MAX_ENTRIES]; // the entry's source: the trait's or quirk's id
    int n;
    float total;
    const char *cur; // the source being added
} Table;
typedef struct {
    const uint8_t *begin, *end;
} Vec;

static void add(Table *t, const uint8_t *e, float w) {
    if (w <= 0 || t->n >= MAX_ENTRIES)
        return;
    t->e[t->n] = e;
    t->src[t->n] = t->cur;
    t->w[t->n++] = w;
    t->total += w;
}
static int entry_type(const uint8_t *e) {
    return *(const int *)(e + ACT_OUT_TYPE);
}
static float entry_chance(const uint8_t *e) {
    return *(const float *)(e + ACT_OUT_CHANCE);
}
static bool entry_class_ok(const uint8_t *e, unsigned cls) {
    const unsigned *b = *(const unsigned *const *)(e + ACT_OUT_CLASSES),
                   *end = *(const unsigned *const *)(e + ACT_OUT_CLASSES + 8);
    if (b == end)
        return true;
    for (; b < end; b++)
        if (*b == cls)
            return true;
    return false;
}
// The config's type ids as the game's type numbers (-1: no such act-out).
static int type_of(const char *id) {
    for (int t = 0; t < ACT_OUT_VANILLA_TYPES; t++)
        if (!strcmp(g_act_out_id(t), id))
            return t;
    for (int i = 0; i < g_ntypes; i++)
        if (!strcmp(g_types[i].id, id))
            return ACT_OUT_FIRST_NEW + i;
    return -1;
}
static const ActOutChances *chances_for(unsigned trait) {
    const ActOutConfig *c = config_act_outs();
    for (int i = 0; i < c->ntraits; i++)
        if (c->traits[i].trait_hash == trait)
            return &c->traits[i];
    return NULL;
}

static void add_data(Table *t, const uint8_t *data, unsigned cls, const ActOutChances *cfg) {
    const Vec *v = (const Vec *)data;
    if (!cfg) { // the game's chances
        for (const uint8_t *e = v->begin; e < v->end; e += ACT_OUT_SIZE)
            if (entry_chance(e) > 0 && entry_class_ok(e, cls))
                add(t, e, entry_chance(e));
        return;
    }
    int type[ACT_OUT_MAX_TYPES], count[ACT_OUT_MAX_TYPES]; // per configured act-out: its entries the hero may use
    for (int i = 0; i < cfg->n; i++) {
        type[i] = type_of(cfg->w[i].type);
        count[i] = 0;
        for (const uint8_t *e = v->begin; type[i] >= 0 && e < v->end; e += ACT_OUT_SIZE)
            if (entry_type(e) == type[i] && entry_class_ok(e, cls))
                count[i]++;
    }
    for (const uint8_t *e = v->begin; e < v->end; e += ACT_OUT_SIZE) {
        if (!entry_class_ok(e, cls))
            continue;
        for (int i = 0; i < cfg->n; i++)
            if (count[i] && entry_type(e) == type[i]) {
                add(t, e, cfg->w[i].weight / count[i]);
                break;
            }
    }
}

// For the log: "Reynauld (hero 3)", the actor's display name (Actor+0x08) and guid.
static const char *who(const void *actor) {
    static char buf[96];
    char name[64];
    size_t i = 0;
    const char *s = actor ? (const char *)actor + ACTOR_NAME : "";
    while (i + 1 < sizeof name && s[i] >= 0x20 && s[i] != 0x7f) {
        name[i] = s[i];
        i++;
    }
    name[i] = 0;
    if (is_hero(actor))
        snprintf(buf, sizeof buf, "%s (hero %u)", i ? name : "?", hero_guid(actor));
    else
        snprintf(buf, sizeof buf, "%s", i ? name : "?");
    return buf;
}

// ---------------------------------------------------------------- rules (act_out_rule_register, config act_outs.rules)
#define MAX_RULES 64
static ActOutRule g_rules[MAX_RULES];
static int g_nrules;
static ModsList g_rules_mods; // their mods (only an applied mod's rules weigh)

bool act_out_rule_register(const ActOutRule *r) {
    if (!r || !r->check || g_nrules >= MAX_RULES || mods_list_add(&g_rules_mods) < 0) {
        logf_("act-outs: rule %s not registered", r && r->name ? r->name : "?");
        return false;
    }
    g_rules[g_nrules++] = *r;
    return true;
}

static bool glob_ok(const char *pattern, const char *s) {
    return !pattern || glob_match(pattern, s ? s : "");
}

static float json_rule_check(const CombatContext *ctx, const char *source, const char *act_out, void *ud) {
    const ActOutJsonRule *r = ud;
    double v;
    if (r->when[0] && (!expr_eval_fn(r->when, combat_context_lookup, combat_context_call, (void *)ctx, &v) || v == 0))
        return 1; // a broken condition leaves the weight (the log has the error)
    if (!expr_eval_fn(r->weight, combat_context_lookup, combat_context_call, (void *)ctx, &v))
        return 1;
    return (float)v;
}

// Each entry's weight times every matching rule's factor (the battle's context built once per roll).
static void apply_rules(Table *t, const void *actor) {
    const ActOutConfig *cfg = config_act_outs(); // the campaign's JSON rules (config.c rebuilds them per campaign)
    if ((!g_nrules && !cfg->nrules) || !t->n)
        return;
    static CombatContext ctx;
    if (!combat_context_build(&ctx, actor))
        return;
    t->total = 0;
    int kept = 0;
    for (int i = 0; i < t->n; i++) {
        int type = entry_type(t->e[i]);
        const char *id = act_out_name(type);
        float w = t->w[i];
        MODS_EACH(g_rules_mods, false, r) {
            if (w <= 0)
                break;
            if (!glob_ok(g_rules[r].source, t->src[i]) || !glob_ok(g_rules[r].act_out, id))
                continue;
            float f = g_rules[r].check(&ctx, t->src[i] ? t->src[i] : "", id, g_rules[r].ud);
            if (g_debug && f != 1)
                combat_logf("act-out rule %s for %s: %.32s/%s weight %.3f x %.3f",
                            g_rules[r].name ? g_rules[r].name : "?", who(actor), t->src[i] ? t->src[i] : "", id, w, f);
            w *= f > 0 ? f : 0;
        }
        for (int r = 0; r < cfg->nrules && w > 0; r++) {
            const ActOutJsonRule *j = &cfg->rules[r];
            if (!glob_ok(j->source[0] ? j->source : NULL, t->src[i]) || !glob_ok(j->act_out[0] ? j->act_out : NULL, id))
                continue;
            float f = json_rule_check(&ctx, t->src[i] ? t->src[i] : "", id, (void *)j);
            if (g_debug && f != 1)
                combat_logf("act-out rule %s for %s: %.32s/%s weight %.3f x %.3f", j->file, who(actor),
                            t->src[i] ? t->src[i] : "", id, w, f);
            w *= f > 0 ? f : 0;
        }
        if (w <= 0)
            continue;
        t->e[kept] = t->e[i];
        t->src[kept] = t->src[i];
        t->w[kept++] = w;
        t->total += w;
    }
    t->n = kept;
}

// A JSON rule: when its condition holds (none: always), its weight (a number or a formula over the context).
// use_max: per act-out type, only the entries of the source with the most weight for it stay.
static void keep_max(Table *t) {
    float best[MAX_ENTRIES]; // per entry: its source's weight for its type
    for (int i = 0; i < t->n; i++) {
        best[i] = 0;
        for (int j = 0; j < t->n; j++)
            if (t->src[j] == t->src[i] && entry_type(t->e[j]) == entry_type(t->e[i]))
                best[i] += t->w[j];
    }
    int kept = 0;
    t->total = 0;
    for (int i = 0; i < t->n; i++) {
        bool win = true; // no other source beats it (a tie goes to the earlier source)
        for (int j = 0; j < t->n && win; j++)
            if (t->src[j] != t->src[i] && entry_type(t->e[j]) == entry_type(t->e[i]) &&
                (best[j] > best[i] || (best[j] == best[i] && j < i)))
                win = false;
        if (!win)
            continue;
        t->e[kept] = t->e[i];
        t->src[kept] = t->src[i];
        t->w[kept++] = t->w[i];
        t->total += t->w[i];
    }
    t->n = kept;
}

static void log_pool(const Table *t, const void *actor) {
    char line[1024];
    size_t n = 0;
    for (int i = 0; i < t->n && n < sizeof line - 96; i++) {
        int type = entry_type(t->e[i]);
        n += (size_t)snprintf(line + n, sizeof line - n, "%s%.32s/%s %.2f (%.1f%%)", i ? ", " : "",
                              t->src[i] ? t->src[i] : "?", act_out_name(type), t->w[i],
                              t->total > 0 ? t->w[i] * 100 / t->total : 0);
    }
    combat_logf("act-out pool%s for %s (total %.2f): %s", config_act_outs()->use_max ? " (use_max)" : "", who(actor),
                t->total, line);
}

// The candidates: the trait's and quirks' entries, the config's weights, the rules applied.
// act_outs.chances ids, checked at the first roll (k_Data is empty when the library starts)
static void check_chance_ids(void) {
    static bool done;
    if (done)
        return;
    done = true;
    const ActOutConfig *cfg = config_act_outs();
    for (int i = 0; i < cfg->ntraits; i++) // a misspelt id would silently weigh nothing
        for (int j = 0; j < cfg->traits[i].n; j++)
            if (type_of(cfg->traits[i].w[j].type) < 0)
                logf_("act-outs: act_outs.chances.%s: \"%s\" isn't an act-out", cfg->traits[i].trait,
                      cfg->traits[i].w[j].type);
}

static void gather(Table *tp, const void *actor) {
    check_chance_ids();
    const uint8_t *a = actor;
    bool hero = is_hero(actor);
    unsigned cls = hero ? hero_class_hash(actor) : 0;
    Table t = {.n = 0};
    unsigned trait = *(const unsigned *)(a + ACTOR_TRAIT);
    if (!trait)
        trait = *(const unsigned *)(a + ACTOR_TRAIT_ALT);
    const uint8_t *type = trait ? g_trait_type(*g_trait_lib, trait) : NULL;
    t.cur = (const char *)type; // Trait::Type, Quirk::Class: the id string first
    if (type)
        add_data(&t, type + TRAIT_ACT_OUTS, cls, chances_for(trait));
    if (hero && !a[HERO_NO_QUIRK_ACT_OUTS]) {
        const Vec *q = (const Vec *)(a + HERO_QUIRKS);
        for (const uint8_t *e = q->begin; e < q->end; e += QUIRK_SIZE) {
            const uint8_t *quirk = *(const uint8_t *const *)e;
            t.cur = (const char *)quirk;
            if (quirk)
                add_data(&t, quirk + QUIRKCLASS_ACT_OUTS, cls,
                         chances_for(*(const unsigned *)(quirk + QUIRKCLASS_HASH)));
        }
    }
    apply_rules(&t, actor);
    *tp = t;
}

static float uniform(void *rng) {
    return (*(float (*const *const *)(void *))rng)[3](rng);
} // vtable+0x18

static const void *roll(const void *actor, void *rng) {
    static Table t;
    gather(&t, actor);
    if (config_act_outs()->use_max)
        keep_max(&t);
    if (!t.n)
        return NULL;
    if (g_debug)
        log_pool(&t, actor);
    float x = uniform(rng) * t.total, run = 0;
    for (int i = 0; i < t.n; i++)
        if ((run += t.w[i]) > x)
            return t.e[i];
    return t.e[t.n - 1];
}

static const uint8_t *g_forced; // per-source rolls: the act-out the game's next roll returns
static bool g_forcing;

#ifdef _WIN32
// After the game's inlined roll (its pick in rdi and [rbp-0x68]): the library's pick, the act_out before point.
static void m_roll(Regs *r) {
    const void *actor = *(void *const *)(r->rbp - 0x40), *data = (const void *)r->rdi;
    void *rng = *(void *const *)(r->rbp - 0x50);
    if (g_forcing)
        data = g_forced;
    else if (g_trait_type)
        data = roll(actor, rng);
    int type = data ? *(const int *)((const uint8_t *)data + ACT_OUT_TYPE) : 0;
    if (type) {
        HookContext c = {.actor = (void *)actor, .kind = type, .type = act_out_name(type)};
        if (!hook_emit("act_out", HOOK_BEFORE, &c))
            data = NULL;
        else
            g_rolled = type;
    }
    r->rdi = (uint64_t)data;
    *(const void **)(r->rbp - 0x68) = data;
}
// Where the inlined checks meet: r14b (not the last act-out), dl (no monster disables it), al (under the raid limit),
// each 1 to allow. The listeners' outcome goes back in all three.
static void m_checks(Regs *r) {
    const uint8_t *data = (const uint8_t *)r->rdi;
    if (!g_rolled || !data || *(const int *)(data + ACT_OUT_TYPE) != g_rolled || !*g_raid)
        return;
    unsigned refuse = ((r->r14 & 0xff) ? 0 : ACT_OUT_REPEAT) | ((r->rdx & 0xff) ? 0 : ACT_OUT_DISABLED) |
                      ((r->rax & 0xff) ? 0 : ACT_OUT_LIMIT);
    check(*(void *const *)(r->rbp - 0x40), data, g_rolled, refuse);
    uint64_t ok = g_allowed;
    r->r14 = (r->r14 & ~0xffull) | ok;
    r->rdx = (r->rdx & ~0xffull) | ok;
    r->rax = (r->rax & ~0xffull) | ok;
}
#else
static const void *h_roll(const void *actor, void *rng) {
    const void *data = g_forcing ? g_forced : g_trait_type ? roll(actor, rng) : o_roll(actor, rng);
    int type = data ? *(const int *)((const uint8_t *)data + ACT_OUT_TYPE) : 0;
    if (!type)
        return data;
    HookContext c = {.actor = (void *)actor, .kind = type, .type = act_out_name(type)};
    if (!hook_emit("act_out", HOOK_BEFORE, &c))
        return NULL;
    g_rolled = type;
    if (*g_raid)
        check((void *)actor, data, type);
    return data;
}

// The game's checks, answered with the decision for the act-out in progress (the game's own answer otherwise).
static bool h_valid(const void *battle, bool is_hero, int type) {
    return g_decided && type == g_rolled ? g_allowed : o_valid(battle, is_hero, type);
}
static bool h_disabled(const void *battle, int type) {
    return g_decided && type == g_rolled ? !g_allowed : o_disabled(battle, type);
}
static unsigned h_count(const void *raid, int type) {
    return g_decided && type == g_rolled ? (g_allowed ? 0 : UINT32_MAX) : o_count(raid, type);
}
#endif

// One act-out through the game's Combat_StartTurn (its roll: the library's, or g_forced); *performed: it passed the
// checks and was queued.
static int run_one(void *unused, void *rng, void *actor, int *performed) {
    *performed = 0;
    g_rolled = 0;
    g_decided = false;
    int r = o_start_turn(unused, rng, actor);
    int type = g_rolled;
    bool decided = g_decided, allowed = g_allowed;
    g_rolled = 0;
    g_decided = false;
    if (!type)
        return r;
    const char *id = act_out_name(type);
    if (decided) {
        HookContext c = {.actor = actor, .kind = type, .type = id, .refuse = g_refuse, .result = allowed ? 1.0f : 0.0f};
        hook_emit("act_out_check", HOOK_AFTER, &c);
    }
    const uint8_t *raid = *g_raid; // a refused act-out leaves "nothing" as the last
    if (!raid || *(const int *)(raid + RAID_PARTY + PARTY_LAST_ACT_OUT) != type)
        return r;
    *performed = type;
    HookContext c = {.actor = actor, .kind = type, .type = id, .result = (float)r};
    hook_emit("act_out", HOOK_AFTER, &c);
    return r;
}

// ---------------------------------------------------------------- new types: the library's Combat_StartTurn
#define HALO_TYPES          10 // Combat_StartTurn queues a halo for types 0-9
#define HEAL_SOURCE_ACT_OUT 7  // Heal::Source as heal_self's outcome passes it

#ifndef _WIN32                                                // (new types: macOS only)
static void (*g_set_last)(void *battle, bool hero, int type); // Battle::SetLastActOut(bool, CombatStartTurn type)
static void (*g_count_act_out)(void *raid, int type);         // Raid::CountActOutCombatStartTurn
static int (*g_result_of)(int type);                          // the k_Data getters (trampolines once hooked)
static float (*g_bark_chance_of)(int type);
static float (*g_burndown_of)(int type); // GetExtendBurndownTime (0 past the table)
static void (*g_add_burndown)(void *battle, float seconds, bool);
static const char *(*g_sound_of)(const void *source); // ActOut::Display::GetAudioEventIdFromSourceIdentifier
static int (*g_halo_of)(const void *source);          // ... GetHaloTypeFromSourceIdentifier
static void (*g_audio)(const char *event, bool, StdFunction *done); // Audio_Event(char const*, bool, function)
static void **g_display;                                            // RaidUI::Screen::RaidDisplay::s_Instance
static const uint8_t *g_display_t;                                  // RaidDisplay::T (its timings)
static void (*g_queue_lambda)(void *display, StdFunction *fn);
static void (*g_queue_halo)(void *display, void *actor, int halo, bool);
static void (*g_queue_wait)(void *display, float seconds);
static void (*g_queue_bark)(void *display, void *actor, const char *id, bool, bool, SourceId source);
#endif
static void (*g_heal)(void *actor, void *rng, float amount, const void *healer, int source, bool);
static void (*g_side_effect)(void *raid, void *actor, void *target, const char *effect); // Raid::QueueSideEffect
static void (*g_process_side_effects)(void *raid);

#ifndef _WIN32 // (new types: macOS only)
static int like_of(const NewType *t) {
    return t->def.like ? type_of(t->def.like) : -1;
}
static int new_result(const NewType *t) {
    int l = like_of(t);
    return t->def.result >= 0 ? t->def.result : l >= 0 && l < ACT_OUT_VANILLA_TYPES ? g_result_of(l) : 0;
}
static float new_bark_chance(const NewType *t) {
    int l = like_of(t);
    return t->def.bark_chance >= 0 ? t->def.bark_chance : l >= 0 && l < ACT_OUT_VANILLA_TYPES ? g_bark_chance_of(l) : 1;
}
static float new_burndown(const NewType *t) {
    int l = like_of(t);
    return t->def.burndown >= 0 ? t->def.burndown : l >= 0 && l < ACT_OUT_VANILLA_TYPES ? g_burndown_of(l) : 0;
}

static void play_halo(const uint8_t *d) {
    void *actor;
    int halo;
    memcpy(&actor, d, sizeof actor);
    memcpy(&halo, d + sizeof actor, sizeof halo);
    if (*g_display)
        g_queue_halo(*g_display, actor, halo, true);
}

// Outcomes waiting on the display: a ring, each played once (the display plays a turn's queue long before 16 more).
#define MAX_PENDING 16
typedef struct {
    ActOutRun run;
    const NewType *t;
} Pending;
static Pending g_pending[MAX_PENDING];
static unsigned g_npending;

static void play_outcome(const uint8_t *d) {
    const Pending *p;
    memcpy(&p, d, sizeof p);
    if (g_debug)
        combat_logf("act-out %s outcome for %s", p->t->id, who(p->run.actor));
    p->t->def.outcome(&p->run, p->t->def.ud);
}

static void queue_call(void (*fn)(const uint8_t *), const void *data, size_t n) {
    StdFunction f;
    ui_function(&f, fn, data, n);
    g_queue_lambda(*g_display, &f); // by value: the display keeps a copy (ours lives in f's buffer)
}

// A registered type, rolled: what Combat_StartTurn does for a vanilla one, the outcome the type's.
static int run_new(void *rng, void *actor, const uint8_t *data, int *performed) {
    *performed = 0;
    int type = entry_type(data);
    const NewType *t = new_type(type);
    uint8_t *raid = *g_raid;
    if (!t || !raid || !*g_display)
        return 0;
    void *battle = raid + RAID_BATTLE;
    HookContext b = {.actor = actor, .kind = type, .type = t->id};
    if (!hook_emit("act_out", HOOK_BEFORE, &b))
        return 0;
    check(actor, data, type);
    bool allowed = g_allowed;
    g_decided = false;
    HookContext c = {.actor = actor, .kind = type, .type = t->id, .refuse = g_refuse, .result = allowed ? 1.0f : 0.0f};
    hook_emit("act_out_check", HOOK_AFTER, &c);
    if (!allowed) {
        g_set_last(battle, true, 0);
        return 0;
    }

    int result = new_result(t);
    g_count_act_out(raid, type);
    float burndown = new_burndown(t);
    if (burndown > 0)
        g_add_burndown(battle, burndown, true);
    const char *sound = g_sound_of(data + ACT_OUT_SOURCE);
    if (sound && g_audio) {
        StdFunction none;
        memset(&none, 0, sizeof none);
        g_audio(sound, true, &none);
    }
    int halo = g_halo_of(data + ACT_OUT_SOURCE);
    if (halo < HALO_TYPES) {
        uint8_t d[16] = {0};
        memcpy(d, &actor, sizeof actor);
        memcpy(d + sizeof actor, &halo, sizeof halo);
        queue_call(play_halo, d, sizeof d);
    }
    g_queue_wait(*g_display, *(const float *)(g_display_t + RAID_DISPLAY_WAIT));
    const char *source = (const char *)data + ACT_OUT_SOURCE_ID;
    if (uniform(rng) < new_bark_chance(t)) {
        char bark[128];
        snprintf(bark, sizeof bark, "str_%s_%s", t->id, source);
        SourceId s;
        memcpy(&s, data + ACT_OUT_SOURCE, sizeof s);
        g_queue_bark(*g_display, actor, bark, true, true, s);
    }
    if (t->def.outcome && mods_active(t->owner)) { // its mod applied (JSON types: the campaign's config has it)
        Pending *p = &g_pending[g_npending++ % MAX_PENDING];
        *p = (Pending){.t = t,
                       .run = {.actor = actor,
                               .data = data,
                               .rng = rng,
                               .id = t->id,
                               .source = source,
                               .number = *(const float *)(data + ACT_OUT_NUMBER),
                               .string = (const char *)data + ACT_OUT_STRING}};
        queue_call(play_outcome, &p, sizeof p);
    }
    g_set_last(battle, true, type);
    *performed = type;
    HookContext a = {.actor = actor, .kind = type, .type = t->id, .result = (float)result};
    hook_emit("act_out", HOOK_AFTER, &a);
    return result;
}

#endif

// A rolled entry (NULL: none): a registered type through run_new, any other through the game's Combat_StartTurn.
static int perform(void *unused, void *rng, void *actor, const uint8_t *entry, int *performed) {
#ifndef _WIN32
    if (entry && new_type(entry_type(entry)))
        return run_new(rng, actor, entry, performed);
#endif
    g_forced = entry;
    g_forcing = true;
    int r = run_one(unused, rng, actor, performed);
    g_forcing = false;
    return r;
}

// ---------------------------------------------------------------- outcome helpers (also for C clients)
void act_out_heal(void *target, void *actor, void *rng, float share) {
    float max_hp = actor_stat(target, "max_hp");
    if (!g_heal || !target || !(max_hp > 0) || !(share > 0))
        return;
    g_heal(target, rng, max_hp * share, actor, HEAL_SOURCE_ACT_OUT, true);
}

void act_out_side_effect(void *actor, void *target, const char *effect) {
    if (!g_side_effect || !*g_raid || !target || !effect || !*effect)
        return;
    g_side_effect(*g_raid, actor, target, effect);
    g_process_side_effects(*g_raid);
}

int act_out_targets(const char *name, void *actor, void *rng, void **out, int max) {
    static CombatContext ctx;
    if (!name || max <= 0 || !combat_context_build(&ctx, actor))
        return 0;
    if (!strcmp(name, "self")) {
        out[0] = actor;
        return 1;
    }
    const CtxActor *pick[CTX_MAX_HEROES];
    int n = 0;
    bool others = !strcmp(name, "ally_random");
    for (int i = 0; i < ctx.nheroes; i++)
        if (ctx.heroes[i].alive && !(others && ctx.heroes[i].actor == actor))
            pick[n++] = &ctx.heroes[i];
    if (!n)
        return 0;
    if (!strcmp(name, "party")) {
        int k = 0;
        for (int i = 0; i < n && k < max; i++)
            out[k++] = pick[i]->actor;
        return k;
    }
    if (!strcmp(name, "party_random") || others) {
        int i = (int)(uniform(rng) * n);
        out[0] = pick[i < n ? i : n - 1]->actor;
        return 1;
    }
    bool pct = !strcmp(name, "party_lowest_hp_pct");
    if (!pct && strcmp(name, "party_lowest_hp")) {
        logf_("act-outs: target \"%s\" unknown", name);
        return 0;
    }
    const CtxActor *low = pick[0]; // a tie: the first in the party's order
    for (int i = 1; i < n; i++)
        if (pct ? pick[i]->hp_pct < low->hp_pct : pick[i]->hp < low->hp)
            low = pick[i];
    out[0] = low->actor;
    return 1;
}

#ifndef _WIN32
// config act_outs.types: "heal" (number_value: a share of the target's max HP) or "effect" (string_value) on the target
// A JSON type's outcome: the campaign's entry for its id (config.c rebuilds them per campaign; none: nothing).
static void json_outcome(const ActOutRun *r, void *ud) {
    (void)ud;
    const ActOutConfig *cfg = config_act_outs();
    const ActOutJsonType *j = NULL;
    for (int i = 0; i < cfg->ntypes && !j; i++)
        if (!strcmp(cfg->types[i].id, r->id))
            j = &cfg->types[i];
    if (!j)
        return;
    void *to[CTX_MAX_HEROES];
    int n = act_out_targets(j->target, r->actor, r->rng, to, CTX_MAX_HEROES);
    for (int i = 0; i < n; i++) {
        if (g_debug) {
            char by[96];
            snprintf(by, sizeof by, "%s", who(r->actor));
            combat_logf("act-out %s: %s from %s on %s (%g, \"%s\")", r->id, j->outcome, by, who(to[i]), r->number,
                        r->string);
        }
        if (!strcmp(j->outcome, "heal"))
            act_out_heal(to[i], r->actor, r->rng, r->number);
        else
            act_out_side_effect(r->actor, to[i], r->string);
    }
}

// The k_Data getters, answering registered types (the game asks them only in Combat_StartTurn, which never sees one,
// and in ValidateActOutBarks, Raid::Save, Raid::Restore and the data loads).
static int (*o_type_from_id)(const char *id);
static const char *(*o_id_from_type)(int type);
static int (*o_result_of)(int type);
static float (*o_bark_chance_of)(int type);
static bool (*o_validate_barks)(int type);
static int h_type_from_id(const char *id) {
    for (int i = 0; id && i < g_ntypes; i++)
        if (!strcmp(g_types[i].id, id))
            return ACT_OUT_FIRST_NEW + i;
    return o_type_from_id(id);
}
static const char *h_id_from_type(int type) {
    if (type >= 0 && type < ACT_OUT_VANILLA_TYPES)
        return o_id_from_type(type);
    const NewType *t = new_type(type);
    return t ? t->id : "unknown"; // an unregistered id in the data (15): never past k_Data
}
static int h_result_of(int type) {
    const NewType *t = new_type(type);
    return t ? new_result(t) : type >= 0 && type < ACT_OUT_VANILLA_TYPES ? o_result_of(type) : 0;
}
static float h_bark_chance_of(int type) {
    const NewType *t = new_type(type);
    return t ? new_bark_chance(t) : type >= 0 && type < ACT_OUT_VANILLA_TYPES ? o_bark_chance_of(type) : 0;
}
static bool h_validate_barks(int type) {
    return type >= 0 && type < ACT_OUT_VANILLA_TYPES && o_validate_barks(type);
}

#endif

// ---------------------------------------------------------------- per-source rolls (config act_outs.per_source)
// Each source (the trait, then each quirk, in the hero's order) gives one act-out or none, rolled once on its own table
// (its weights, nothing included; the rules applied), and they run one at a time, each fully played out before the
// next source rolls: the next roll (and its rules) sees what the earlier act-outs did (a move, a buff, stress), and its
// setup (a target, a rank) starts from there. How: Combat_StartTurn is called by bs_before_turn_Bark's handler, each
// frame the display is idle, until the battle steps on; one act-out runs per call (through the game's own
// Combat_StartTurn, its roll forced: its own act_out points, checks, bark and queued outcome) and, while sources are
// left, the call returns 1 and act_outs_hold() (asked by events.c's Battle::IncrementState hook) keeps the battle in
// that state, so the game calls again once the act-out has played. If the game steps without calling (the hero can no
// longer act: stunned, dead), the hold lets go and the rest is dropped.
// ignore_command doesn't end the turn: it's flagged, and the turn is lost once all sources have rolled.
// retreat_from_combat stops there (the earlier ones have run; the turn ends with the retreat). Otherwise the turn
// result is the highest (1: the hero then acts). max_per_turn (0: no limit): no more sources roll once that many have
// run. Two sources may give the same act-out: both run (the no-repeat check refuses the second unless
// allow_back_to_back).
#define ACT_OUT_IGNORE_COMMAND 3
#define ACT_OUT_RETREAT        5
#define TURN_LOST              2
#define MAX_SOURCES            16

// ---------------------------------------------------------------- prerequisites (config act_outs.prerequisites)
// Per hero: a turn and a battle number (its per-source turns; battle_start), and per source, the turn and battle it
// last fired (gave an act-out that ran) and last rolled (anything, nothing included). raid_start clears the hero's
// records, so whatever is recorded is from this expedition. Not saved: a reload starts the expedition afresh.
#define MAX_HEROES_TRACKED 32
#define MAX_SOURCE_RECORDS 256
typedef struct {
    unsigned hero;
    int turn, battle;
} HeroClock;
typedef struct {
    unsigned hero;
    char source[64];
    bool fired, rolled;
    int fired_turn, fired_battle, rolled_turn, rolled_battle;
} SourceRecord;
static HeroClock g_clock[MAX_HEROES_TRACKED];
static SourceRecord g_rec[MAX_SOURCE_RECORDS];
static int g_nclock, g_nrec;

static HeroClock *clock_of(unsigned hero) {
    for (int i = 0; i < g_nclock; i++)
        if (g_clock[i].hero == hero)
            return &g_clock[i];
    if (g_nclock == MAX_HEROES_TRACKED) {
        memmove(g_clock, g_clock + 1, sizeof g_clock - sizeof *g_clock);
        g_nclock--;
    }
    g_clock[g_nclock] = (HeroClock){.hero = hero};
    return &g_clock[g_nclock++];
}
static SourceRecord *record_of(unsigned hero, const char *source, bool create) {
    for (int i = 0; i < g_nrec; i++)
        if (g_rec[i].hero == hero && !strcmp(g_rec[i].source, source))
            return &g_rec[i];
    if (!create)
        return NULL;
    if (g_nrec == MAX_SOURCE_RECORDS) {
        memmove(g_rec, g_rec + 1, sizeof g_rec - sizeof *g_rec);
        g_nrec--;
    }
    SourceRecord *r = &g_rec[g_nrec++];
    memset(r, 0, sizeof *r);
    r->hero = hero;
    snprintf(r->source, sizeof r->source, "%s", source);
    return r;
}
static void on_battle_start(HookContext *c, void *ud) {
    if (is_hero(c->actor))
        clock_of(hero_guid(c->actor))->battle++;
}
static void on_raid_start(HookContext *c, void *ud) {
    if (!is_hero(c->actor))
        return;
    unsigned h = hero_guid(c->actor);
    int kept = 0;
    for (int i = 0; i < g_nrec; i++)
        if (g_rec[i].hero != h)
            g_rec[kept++] = g_rec[i];
    g_nrec = kept;
}
static void note_roll(unsigned hero, const char *source, bool fired) {
    const HeroClock *k = clock_of(hero);
    SourceRecord *r = record_of(hero, source, true);
    r->rolled = true;
    r->rolled_turn = k->turn;
    r->rolled_battle = k->battle;
    if (fired) {
        r->fired = true;
        r->fired_turn = k->turn;
        r->fired_battle = k->battle;
    }
}

static bool requirement_met(unsigned hero, const ActOutPrereq *q) {
    const SourceRecord *r = record_of(hero, q->source, false);
    if (!r)
        return false;
    const HeroClock *k = clock_of(hero);
    bool f = r->fired, ro = q->nothing_counts && r->rolled;
    switch (q->within) {
    case ACT_OUT_WITHIN_TURN:
        return (f && r->fired_turn == k->turn) || (ro && r->rolled_turn == k->turn);
    case ACT_OUT_WITHIN_BATTLE:
        return (f && r->fired_battle == k->battle) || (ro && r->rolled_battle == k->battle);
    default:
        return f || ro;
    }
}
// May the source roll now? (no prerequisites: yes)
static bool source_ready(unsigned hero, const char *source) {
    const ActOutConfig *cfg = config_act_outs();
    for (int i = 0; i < cfg->nprereqs; i++) {
        const ActOutPrereqs *p = &cfg->prereqs[i];
        if (strcmp(p->source, source))
            continue;
        int met = 0;
        for (int j = 0; j < p->n; j++)
            met += requirement_met(hero, &p->req[j]);
        return p->any ? met > 0 : met == p->n;
    }
    return true;
}

// ---------------------------------------------------------------- the turn in progress
static struct {
    bool active, called; // a turn in progress; Combat_StartTurn called since the last hold
    const void *actor;
    unsigned hero;
    const char *src[MAX_SOURCES]; // the sources, as gathered when the turn started
    bool done[MAX_SOURCES];
    int nsrc, ran, result;
    bool pass;
} g_seq;

// The first source not yet rolled whose prerequisites are met (-1: none; then none ever this turn, as only a roll
// can meet one).
static int next_ready(void) {
    for (int s = 0; s < g_seq.nsrc; s++)
        if (!g_seq.done[s] && source_ready(g_seq.hero, g_seq.src[s] ? g_seq.src[s] : ""))
            return s;
    return -1;
}

bool act_outs_hold(void) {
    if (!g_seq.active || next_ready() < 0)
        return false;
    if (!g_seq.called) {
        g_seq.active = false;
        return false;
    } // the game moved on without asking: let go
    g_seq.called = false;
    return true;
}

static int per_source(void *unused, void *rng, void *actor) {
    static Table t;
    const ActOutConfig *cfg = config_act_outs();
    if (!g_seq.active || g_seq.actor != actor) { // a new turn: its sources
        gather(&t, actor);
        memset(&g_seq, 0, sizeof g_seq);
        g_seq.active = true;
        g_seq.actor = actor;
        g_seq.hero = is_hero(actor) ? hero_guid(actor) : 0;
        clock_of(g_seq.hero)->turn++;
        for (int i = 0; i < t.n; i++) {
            bool seen = false;
            for (int k = 0; k < g_seq.nsrc && !seen; k++)
                seen = g_seq.src[k] == t.src[i];
            if (!seen && g_seq.nsrc < MAX_SOURCES)
                g_seq.src[g_seq.nsrc++] = t.src[i];
        }
        if (g_debug && !g_seq.nsrc)
            combat_logf("act-out rolls (per source) for %s: no sources", who(actor));
    }
    g_seq.called = true;
    for (int s; (s = next_ready()) >= 0;) {
        g_seq.done[s] = true;
        const char *src = g_seq.src[s];
        gather(&t, actor); // now: the rules see what the earlier act-outs did
        float total = 0, run = 0;
        for (int i = 0; i < t.n; i++)
            if (t.src[i] == src)
                total += t.w[i];
        if (total <= 0)
            continue;
        float x = uniform(rng) * total;
        const uint8_t *got = NULL;
        for (int i = 0; i < t.n && !got; i++)
            if (t.src[i] == src && (run += t.w[i]) > x)
                got = t.e[i];
        if (!got)
            for (int i = t.n - 1; i >= 0 && !got; i--)
                if (t.src[i] == src)
                    got = t.e[i];
        int type = entry_type(got);
        if (g_debug)
            combat_logf("act-out roll (per source %d of %d) for %s: %.32s: %s", s + 1, g_seq.nsrc, who(actor),
                        src ? src : "?", act_out_name(type));
        if (type == 0) {
            note_roll(g_seq.hero, src ? src : "", false);
            continue;
        } // nothing: this source gives none
        int performed;
        int r = perform(unused, rng, actor, got, &performed);
        note_roll(g_seq.hero, src ? src : "", performed != 0);
        if (!performed)
            continue; // cancelled or refused: as if it hadn't been rolled
        if (performed == ACT_OUT_RETREAT) {
            g_seq.active = false;
            return r;
        } // the retreat ends it
        if (performed == ACT_OUT_IGNORE_COMMAND)
            g_seq.pass = true;
        else if (r > g_seq.result && r != TURN_LOST)
            g_seq.result = r;
        if (cfg->max_per_turn > 0 && ++g_seq.ran >= cfg->max_per_turn)
            for (int k = 0; k < g_seq.nsrc; k++)
                g_seq.done[k] = true;
        if (next_ready() >= 0)
            return 1; // more to roll once this one has played (act_outs_hold)
        break;
    }
    if (g_debug)
        for (int k = 0; k < g_seq.nsrc; k++)
            if (!g_seq.done[k])
                combat_logf("act-out source %.32s for %s: prerequisites not met, no roll",
                            g_seq.src[k] ? g_seq.src[k] : "?", who(actor));
    g_seq.active = false;
    return g_seq.pass ? TURN_LOST : g_seq.result;
}

static int h_start_turn(void *unused, void *rng, void *actor) {
    if (config_act_outs()->per_source && g_trait_type)
        return per_source(unused, rng, actor);
    int performed;
    if (!g_trait_type)
        return run_one(unused, rng, actor, &performed); // the game's roll (no new types)
    return perform(unused, rng, actor, roll(actor, rng), &performed);
}

#define HOOK(orig, name, mangled, detour, ...)                                                                         \
    do {                                                                                                               \
        static const uint8_t pro[] = __VA_ARGS__;                                                                      \
        *(void **)&orig = install_hook(name, sym(mangled), (void *)detour, pro, sizeof pro);                           \
    } while (0)

#ifndef _WIN32
// New types: the game's functions run_new calls, and the k_Data getters. Without all of them no type is registered
// (the data's ids then load as the game's unknown type, and the game drops them).
static void install_types(void) {
    *(void **)&g_set_last = sym("_ZN6Battle13SetLastActOutEbN6ActOut15CombatStartTurn5Types5eTypeE");
    *(void **)&g_count_act_out = sym("_ZN4Raid26CountActOutCombatStartTurnEN6ActOut15CombatStartTurn5Types5eTypeE");
    *(void **)&g_result_of = sym("_ZN6ActOut15CombatStartTurn25GetTurnResultTypeFromTypeENS0_5Types5eTypeE");
    *(void **)&g_bark_chance_of = sym("_ZN6ActOut15CombatStartTurn21GetBarkChanceFromTypeENS0_5Types5eTypeE");
    *(void **)&g_burndown_of = sym("_ZN6ActOut15CombatStartTurn21GetExtendBurndownTimeENS0_5Types5eTypeE");
    *(void **)&g_add_burndown = sym("_ZN6Battle15AddBurndownTimeEfb");
    *(void **)&g_sound_of = sym("_ZN6ActOut7Display35GetAudioEventIdFromSourceIdentifierERKNS_16SourceIdentifierE");
    *(void **)&g_halo_of = sym("_ZN6ActOut7Display31GetHaloTypeFromSourceIdentifierERKNS_16SourceIdentifierE");
    *(void **)&g_audio = sym("_Z11Audio_EventPKcbNSt3__18functionIFvS0_EEE");
    g_display = (void **)sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    g_display_t = sym("_ZN6RaidUI6Screen11RaidDisplay1TE");
    *(void **)&g_queue_lambda = sym("_ZN6RaidUI6Screen11RaidDisplay11QueueLambdaENSt3__18functionIFvvEEE");
    *(void **)&g_queue_halo = sym("_ZN6RaidUI6Screen11RaidDisplay9QueueHaloEP5ActorN4Halo5Types5eTypeEb");
    *(void **)&g_queue_wait = sym("_ZN6RaidUI6Screen11RaidDisplay9QueueWaitEf");
    *(void **)&g_queue_bark =
        sym("_ZN6RaidUI6Screen11RaidDisplay9QueueBarkEP5Actor8c_stringbbN6ActOut16SourceIdentifierE");
    *(void **)&g_heal = sym("_ZN5Actor15ApplyHealthHealER11prng_minstdfPKS_N4Heal6Source5Types5eTypeEb");
    *(void **)&g_side_effect = sym("_ZN4Raid15QueueSideEffectEP5ActorS1_8c_string");
    *(void **)&g_process_side_effects = sym("_ZN4Raid22ProcessSideEffectQueueEv");
    if (!g_set_last || !g_count_act_out || !g_result_of || !g_bark_chance_of || !g_burndown_of || !g_add_burndown ||
        !g_sound_of || !g_halo_of || !g_display || !g_display_t || !g_queue_lambda || !g_queue_halo || !g_queue_wait ||
        !g_queue_bark || !g_heal || !g_side_effect || !g_process_side_effects || !g_trait_type) {
        logf_("act-outs: symbols for new types not found, %d type(s) off", g_ntypes);
        g_ntypes = 0;
        return;
    }
    // each: push rbp; mov rbp,rsp; mov eax,edi; lea r,[rax+4*rax]; shl r,4 (then a rip-relative lea, not moved)
    HOOK(o_type_from_id, "ActOut::CombatStartTurn::GetTypeFromId (act-out types)",
         "_ZN6ActOut15CombatStartTurn13GetTypeFromIdEPKc", h_type_from_id,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x49, 0x89, 0xFE, 0x31, 0xDB, 0x31, 0xF6});
    HOOK(o_id_from_type, "ActOut::CombatStartTurn::GetIdFromType (act-out types)",
         "_ZN6ActOut15CombatStartTurn13GetIdFromTypeENS0_5Types5eTypeE", h_id_from_type,
         {0x55, 0x48, 0x89, 0xE5, 0x89, 0xF8, 0x48, 0x8D, 0x0C, 0x80, 0x48, 0xC1, 0xE1, 0x04});
    HOOK(o_result_of, "ActOut::CombatStartTurn::GetTurnResultTypeFromType (act-out types)",
         "_ZN6ActOut15CombatStartTurn25GetTurnResultTypeFromTypeENS0_5Types5eTypeE", h_result_of,
         {0x55, 0x48, 0x89, 0xE5, 0x89, 0xF8, 0x48, 0x8D, 0x04, 0x80, 0x48, 0xC1, 0xE0, 0x04});
    HOOK(o_bark_chance_of, "ActOut::CombatStartTurn::GetBarkChanceFromType (act-out types)",
         "_ZN6ActOut15CombatStartTurn21GetBarkChanceFromTypeENS0_5Types5eTypeE", h_bark_chance_of,
         {0x55, 0x48, 0x89, 0xE5, 0x89, 0xF8, 0x48, 0x8D, 0x04, 0x80, 0x48, 0xC1, 0xE0, 0x04});
    HOOK(o_validate_barks, "ActOut::CombatStartTurn::GetDoesValidateBarksFromType (act-out types)",
         "_ZN6ActOut15CombatStartTurn28GetDoesValidateBarksFromTypeENS0_5Types5eTypeE", h_validate_barks,
         {0x55, 0x48, 0x89, 0xE5, 0x89, 0xF8, 0x48, 0x8D, 0x04, 0x80, 0x48, 0xC1, 0xE0, 0x04});
    if (!o_type_from_id || !o_id_from_type || !o_result_of || !o_bark_chance_of || !o_validate_barks) {
        logf_("act-outs: a k_Data getter isn't hooked: new types are unsafe (check the log above)");
        if (!o_type_from_id)
            g_ntypes = 0; // nothing in the data gets a new number then
    }
    if (o_id_from_type)
        g_act_out_id = o_id_from_type; // vanilla ids from here on through the trampolines
    if (o_result_of)
        g_result_of = o_result_of;
    if (o_bark_chance_of)
        g_bark_chance_of = o_bark_chance_of;
    logf_("act-outs: %d new type(s)", g_ntypes);
}

#else
// Windows: each configured type loads as its "like" vanilla type (ActOut::CombatStartTurn::GetTypeFromId is a real
// function there): e.g. heal_lowest_ally acts as heal_self. A type with no "like" stays unknown (the game's assert,
// then an invalid entry).
static const char *like_id(const char *id) {
    const ActOutConfig *cfg = config_act_outs();
    for (int i = 0; id && i < cfg->ntypes; i++)
        if (!strcmp(cfg->types[i].id, id) && cfg->types[i].like[0])
            return cfg->types[i].like;
    return NULL;
}
static int (*o_type_from_id)(const char *id);
static int h_type_like(const char *id) {
    const char *like = like_id(id);
    return o_type_from_id(like ? like : id);
}
// The act-out data loader (traits' and quirks' "combat_start_turn_act_outs") inlines the same lookup on a local copy of
// the type string (rbp + 0xe0, a FixedLengthString<64>), hashed right after this site: the like type's name goes there.
static void loader_type(const Regs *r) {
    char *s = (char *)(r->rbp + 0xe0);
    const char *like = like_id(s);
    if (like)
        snprintf(s, 64, "%s", like);
}
static void install_like(const ActOutConfig *cfg) {
    if (!cfg->ntypes)
        return;
    for (int i = 0; i < cfg->ntypes; i++)
        logf_("act-outs: %s acts as %s on Windows (new act-out types aren't ported)", cfg->types[i].id,
              cfg->types[i].like[0] ? cfg->types[i].like : "nothing: no \"like\"");
    *(void **)&o_type_from_id =
        install_hook("ActOut::CombatStartTurn::GetTypeFromId (act-out types)",
                     sym("_ZN6ActOut15CombatStartTurn13GetTypeFromIdEPKc"), (void *)h_type_like, NULL, 0);
    void *site = sym("win:ActOut data loader: CombatStartTurn type lookup");
    if (site)
        install_mid_hook("ActOut data loader (act-out types)", site, loader_type);
}
#endif

void act_outs_install(void) {
    static const char *const points[] = {"act_out", "act_out_check"};
    const ActOutConfig *cfg = config_act_outs();
#ifdef _WIN32
    install_like(cfg);
#else
    for (int i = 0; i < cfg->ntypes; i++) {
        const ActOutJsonType *j = &cfg->types[i];
        bool known = !j->outcome[0] || !strcmp(j->outcome, "heal") || !strcmp(j->outcome, "effect");
        if (!known)
            logf_("act-outs: act_outs.types.%s: outcome \"%s\": heal or effect (none used)", j->id, j->outcome);
        act_out_type_register(&(ActOutTypeDef){.id = j->id,
                                               .like = j->like[0] ? j->like : NULL,
                                               .result = j->result,
                                               .bark_chance = j->bark_chance,
                                               .burndown = j->burndown,
                                               .outcome = known && j->outcome[0] ? json_outcome : NULL});
    }
#endif
    if (!hook_any_wanted(points, 2) && !cfg->allow_back_to_back && !cfg->ntraits && !g_nrules && !cfg->nrules &&
        !g_ntypes)
        return;
    if (g_nrules || cfg->nrules)
        logf_("act-outs: %d rule(s), %d from configs", g_nrules + cfg->nrules, cfg->nrules);
    if (cfg->nprereqs) {
        if (!cfg->per_source)
            logf_("act-outs: act_outs.prerequisites apply only with per_source (ignored)");
        if (!hook_on("battle_start", HOOK_AFTER, on_battle_start, NULL) ||
            !hook_on("raid_start", HOOK_AFTER, on_raid_start, NULL))
            logf_("act-outs: prerequisites: battle_start / raid_start listeners failed (battle and expedition spans "
                  "off)");
    }

    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    *(void **)&g_act_out_id = sym("_ZN6ActOut15CombatStartTurn13GetIdFromTypeENS0_5Types5eTypeE");
    if (!g_raid || !g_act_out_id) {
        logf_("act-outs: symbols not found, act_out and act_out_check off");
        return;
    }
    g_trait_lib = (void **)sym("_ZN5Trait7Library10s_InstanceE");
    *(void **)&g_trait_type = g_trait_lib ? sym("_ZNK5Trait7Library7GetTypeEj") : NULL;
    if (!g_trait_type)
        logf_("act-outs: Trait::Library not found, the game rolls (no act_outs.chances)");
    for (int i = 0; i < cfg->ntraits; i++) { // percentages: each source's should add up to 100
        float s = 0;
        for (int j = 0; j < cfg->traits[i].n; j++)
            s += cfg->traits[i].w[j].weight;
        if (s < 99.5f || s > 100.5f)
            logf_("act-outs: act_outs.chances.%s adds up to %.2f, not 100 (used as written)", cfg->traits[i].trait, s);
    }
#ifdef _WIN32
    // the two sites first: Combat_StartTurn's hook relies on them (its per-source rolls force the roll's pick)
    if (!install_mid_hook_rw("Combat_StartTurn: the roll (act_out)", sym("win:Combat_StartTurn act-out roll"),
                             m_roll) ||
        !install_mid_hook_rw("Combat_StartTurn: the checks (act_out_check)", sym("win:Combat_StartTurn act-out checks"),
                             m_checks)) {
        logf_("act-outs: a Combat_StartTurn site isn't hooked, act_out and act_out_check off");
        return;
    }
    HOOK(o_start_turn, "Behaviors::Combat_StartTurn (act_out)", "_ZN9Behaviors16Combat_StartTurnER11prng_minstdP5Actor",
         h_start_turn, {0});
#else
    // the checks first: the roll hook asks them, so it goes in only once they're all in
    // GetIsValidActOut: push rbp; mov rbp,rsp; test esi,esi; je +0x16; lea rax,[rip+..] (a rel8 jump, a disp32)
    static const uint8_t valid_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x85, 0xF6, 0x74, 0x16,
                                        0x48, 0x8D, 0x05, 0xD9, 0xA6, 0x7E, 0x02};
    *(void **)&o_valid =
        install_hook_rip("Battle::GetIsValidActOut (act_out_check)",
                         sym("_ZNK6Battle16GetIsValidActOutEbN6ActOut15CombatStartTurn5Types5eTypeE"), (void *)h_valid,
                         valid_pro, sizeof valid_pro, (const HookRip[]){{7, 8}, {11, 15}}, 2);
    HOOK(o_disabled, "Battle::GetIsActOutCombatStartTurnTypeDisabled (act_out_check)",
         "_ZNK6Battle38GetIsActOutCombatStartTurnTypeDisabledEN6ActOut15CombatStartTurn5Types5eTypeE", h_disabled,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50, 0x4C, 0x8B, 0x77, 0x38});
    // mov rdx,[rdi+0x5ed8]; xor eax,eax; test rdx,rdx; je +0x37 (a rel8 jump)
    static const uint8_t count_pro[] = {0x48, 0x8B, 0x97, 0xD8, 0x5E, 0x00, 0x00,
                                        0x31, 0xC0, 0x48, 0x85, 0xD2, 0x74, 0x37};
    *(void **)&o_count =
        install_hook_rip("Raid::GetActOutCombatStartTurnCount (act_out_check)",
                         sym("_ZNK4Raid29GetActOutCombatStartTurnCountEN6ActOut15CombatStartTurn5Types5eTypeE"),
                         (void *)h_count, count_pro, sizeof count_pro, (const HookRip[]){{13, 14}}, 1);
    if (!o_valid || !o_disabled || !o_count) {
        logf_("act-outs: a check isn't hooked, act_out and act_out_check off");
        return;
    }
    HOOK(o_start_turn, "Behaviors::Combat_StartTurn (act_out)", "_ZN9Behaviors16Combat_StartTurnER11prng_minstdP5Actor",
         h_start_turn, PROLOGUE_SUB32(0x98, 0x04));
    if (!o_start_turn)
        return;
    HOOK(o_roll, "Actor::RollActOutCombatStartTurnData (act_out)",
         "_ZNK5Actor29RollActOutCombatStartTurnDataER11prng_minstd", h_roll,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00});
    if (g_ntypes)
        install_types();
#endif
}
