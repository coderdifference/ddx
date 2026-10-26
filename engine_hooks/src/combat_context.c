// A battle's state for rules and checks: a snapshot of the cheap values (CombatContext) and accessors for the
// rest (stats, buffs, quirks, diseases, equipment), plus expression bindings so JSON conditions can read them.
// Guide: docs/12-hook-points.md "Combat context". Game layout (docs/engine-design.md "Combat context"):
//   Raid: heroes vector<Hero*> at +0x10, monsters vector<Actor*> at +0x4888, Battle at +0x4850, torch float +0x5ec4;
//   Battle: round +0xa8 (Battle::StartRound adds 1), state +0x270 (0: none);
//   Actor: HP float +0x1038, stress float +0x1274, ranks bitmask +0x1018, combat stats (RaidStat, 0x68 bytes each,
//     ActorCombatStat order) from +0x490, buff group +0x460, trait id hash +0x11b8 (else +0x11bc);
//   Hero: class +0x1298 (id string +0x08), quirks +0x12f8 (0x38-byte entries, the quirk's class first: id string +0,
//     hash +0x40, disease flag +0x51); Monster: class +0x13e0 (Monster::GetMonsterClass; id string +0x08 *Unverified*);
//   Trait::Type: id string +0, hash +0x40, overstress type (hash of "affliction" / "virtue") +0x4c.
#include "common.h"
#include <math.h>
#include <string.h>

#define ACTOR_STAT_COUNT  10 // damage_low ... riposte_on_miss_chance (max_hp is 7)
#define ACTOR_STAT_MAX_HP 7

typedef struct {
    const uint8_t *begin, *end;
} Vec;
static void **g_raid, **g_trait_lib;
static const uint8_t *(*g_trait_type)(const void *lib, unsigned hash);
static bool (*g_is_corpse)(const void *actor);
static float (*g_stat_value)(const void *raid_stat, bool clamped);
static float (*g_resolve_level)(const void *hero); // the level plus progress to the next
static const char *(*g_stat_id)(int type);         // ActorCombatStat::GetIdFromType: the k_Data entry, id first
static unsigned g_virtue_hash;

static bool ready(void) {
    if (g_raid)
        return *g_raid != NULL;
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    g_trait_lib = (void **)sym("_ZN5Trait7Library10s_InstanceE");
    *(void **)&g_trait_type = sym("_ZNK5Trait7Library7GetTypeEj");
    *(void **)&g_is_corpse = sym("_ZNK5Actor8IsCorpseEv");
    *(void **)&g_stat_value = sym("_ZNK8RaidStat8GetValueEb");
    *(void **)&g_resolve_level = sym("_ZNK4Hero15GetResolveLevelEv");
    *(void **)&g_stat_id = sym("_ZN15ActorCombatStat13GetIdFromTypeENS_5Types5eTypeE");
    g_virtue_hash = name_hash("virtue");
    if (!g_raid || !g_trait_lib || !g_trait_type || !g_is_corpse || !g_stat_value || !g_resolve_level || !g_stat_id) {
        logf_("combat context: game symbols missing, off");
        static void *none;
        g_raid = &none;
    }
    return *g_raid != NULL;
}

// ---------------------------------------------------------------- accessors
static const uint8_t *trait_of(const void *actor) {
    const uint8_t *a = actor;
    unsigned h = *(const unsigned *)(a + ACTOR_TRAIT);
    if (!h)
        h = *(const unsigned *)(a + ACTOR_TRAIT_ALT);
    return h && ready() ? g_trait_type(*g_trait_lib, h) : NULL;
}

float actor_stat(const void *actor, const char *stat) {
    if (!actor || !stat || !ready())
        return NAN;
    for (int t = 0; t < ACTOR_STAT_COUNT; t++)
        if (!strcmp(g_stat_id(t), stat))
            return g_stat_value((const uint8_t *)actor + ACTOR_STATS + t * ACTOR_STAT_SIZE, true);
    return NAN;
}

int actor_count_buffs(const void *actor, const char *pattern) {
    if (!actor || !pattern)
        return 0;
    const Vec *v = (const Vec *)((const uint8_t *)actor + ACTOR_BUFF_GROUP);
    int n = 0;
    for (const uint8_t *e = v->begin; e && e + BUFF_ENTRY_SIZE <= v->end; e += BUFF_ENTRY_SIZE) {
        char id[64];
        memcpy(id, e + BUFF_ENTRY_ID, sizeof id - 1);
        id[sizeof id - 1] = 0;
        if (glob_match(pattern, id))
            n++;
    }
    return n;
}
bool actor_has_buff(const void *actor, const char *pattern) {
    return actor_count_buffs(actor, pattern) > 0;
}

int actor_marked(const void *actor) {
    static bool (*has_tag)(const void *actor);
    static bool looked;
    if (!looked) {
        looked = true;
        *(void **)&has_tag = sym("_ZNK5Actor6HasTagEv");
    }
    if (!actor || !has_tag)
        return -1;
    return has_tag(actor) ? 1 : 0;
}

// Quirks matching a glob (NULL: any), diseases only or all.
static int count_quirks(const void *hero, const char *pattern, bool diseases) {
    if (!is_hero(hero))
        return 0;
    const Vec *v = (const Vec *)((const uint8_t *)hero + HERO_QUIRKS);
    int n = 0;
    for (const uint8_t *e = v->begin; e && e + QUIRK_SIZE <= v->end; e += QUIRK_SIZE) {
        const char *q = *(const char *const *)e;
        if (!q || (diseases && !q[QUIRKCLASS_DISEASE]))
            continue;
        char id[65];
        memcpy(id, q, 64);
        id[64] = 0;
        if (!pattern || glob_match(pattern, id))
            n++;
    }
    return n;
}
bool hero_has_quirk(const void *hero, const char *pattern) {
    return count_quirks(hero, pattern, false) > 0;
}
bool hero_has_disease(const void *hero, const char *pattern) {
    return count_quirks(hero, pattern, true) > 0;
}

bool hero_has_item(const void *hero, const char *pattern) {
    const uint8_t *items[16];
    int n = is_hero(hero) ? hero_equipped(hero, items, 16) : 0;
    for (int i = 0; i < n; i++) {
        char id[65];
        memcpy(id, items[i] + ITEM_ID, 64);
        id[64] = 0;
        if (glob_match(pattern, id))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------- the snapshot
static void view(CtxActor *v, void *actor, bool hero) {
    memset(v, 0, sizeof *v);
    const uint8_t *a = actor;
    v->actor = actor;
    v->hero = hero;
    const char *cls = *(const char *const *)(a + (hero ? HERO_CLASS : MONSTER_CLASS));
    v->class_id = cls ? cls + CLASS_ID : "";
    unsigned ranks = *(const unsigned *)(a + ACTOR_RANK);
    v->rank = ranks ? __builtin_ctz(ranks) + 1 : 0;
    v->hp = *(const float *)(a + ACTOR_HP);
    v->max_hp = g_stat_value(a + ACTOR_STATS + ACTOR_STAT_MAX_HP * ACTOR_STAT_SIZE, true);
    v->hp_pct = v->max_hp > 0 ? v->hp * 100 / v->max_hp : 0;
    v->corpse = g_is_corpse(actor);
    v->alive = !v->corpse;
    if (!hero)
        return;
    v->stress = *(const float *)(a + ACTOR_STRESS);
    v->deaths_door = v->hp <= 0 && !v->corpse;
    v->resolve_level = (int)g_resolve_level(actor);
    const uint8_t *t = trait_of(actor);
    if (t) {
        v->trait = (const char *)t;
        v->virtuous = *(const unsigned *)(t + TRAIT_OVERSTRESS) == g_virtue_hash;
        v->afflicted = !v->virtuous;
    }
}

bool combat_context_build(CombatContext *c, const void *self) {
    memset(c, 0, sizeof *c);
    if (!ready())
        return false;
    uint8_t *raid = *g_raid;
    c->raid = raid;
    c->battle = raid + RAID_BATTLE;
    c->in_battle = *(const int *)(raid + RAID_BATTLE + BATTLE_STATE) != 0;
    c->round = c->in_battle ? *(const int *)(raid + RAID_BATTLE + BATTLE_ROUND) : 0;
    c->torch = *(const float *)(raid + RAID_TORCH);
    const Vec *h = (const Vec *)(raid + RAID_HEROES), *m = (const Vec *)(raid + RAID_MONSTERS);
    for (void *const *p = (void *const *)h->begin; p && p < (void *const *)h->end && c->nheroes < CTX_MAX_HEROES; p++)
        if (*p)
            view(&c->heroes[c->nheroes++], *p, true);
    for (void *const *p = (void *const *)m->begin;
         c->in_battle && p && p < (void *const *)m->end && c->nmonsters < CTX_MAX_MONSTERS; p++)
        if (*p)
            view(&c->monsters[c->nmonsters++], *p, is_hero(*p));
    for (int i = 0; i < c->nheroes; i++)
        if (c->heroes[i].actor == self)
            c->self = &c->heroes[i];
    for (int i = 0; i < c->nmonsters; i++)
        if (c->monsters[i].actor == self)
            c->self = &c->monsters[i];
    return true;
}

// ---------------------------------------------------------------- expression bindings
// Names: the battle's (torch, round, in_battle, heroes, monsters: alive counts; heroes_at_deaths_door,
// afflicted_heroes, virtuous_heroes, party_stress (total), average_stress, lowest_hero_hp_pct, monsters_hp_pct (the
// living monsters' HP over their max, 0-100)) and self's (hp, max_hp, hp_pct, stress, rank, resolve_level,
// deaths_door, afflicted, virtuous). Calls: on self: stat("id"), has_buff("glob"), buffs("glob"), has_quirk("glob"),
// has_disease() / has_disease("glob"), has_item("glob"), trait("id"), class("glob"); counts: heroes_with_trait("id"),
// heroes_with_quirk("glob"), heroes_with_disease(), heroes_with_buff("glob"), monsters_of_class("glob"),
// monsters_with_buff("glob").
double combat_context_lookup(const char *name, bool *ok, void *ctx) {
    const CombatContext *c = ctx;
    const CtxActor *s = c->self;
    int alive_h = 0, alive_m = 0, dd = 0, aff = 0, vir = 0;
    float stress = 0, low = 100, mhp = 0, mmax = 0;
    for (int i = 0; i < c->nheroes; i++) {
        const CtxActor *h = &c->heroes[i];
        if (!h->alive)
            continue;
        alive_h++;
        dd += h->deaths_door;
        aff += h->afflicted;
        vir += h->virtuous;
        stress += h->stress;
        if (h->hp_pct < low)
            low = h->hp_pct;
    }
    for (int i = 0; i < c->nmonsters; i++)
        if (c->monsters[i].alive) {
            alive_m++;
            mhp += c->monsters[i].hp;
            mmax += c->monsters[i].max_hp;
        }
    if (!strcmp(name, "torch"))
        return c->torch;
    if (!strcmp(name, "round"))
        return c->round;
    if (!strcmp(name, "in_battle"))
        return c->in_battle;
    if (!strcmp(name, "heroes"))
        return alive_h;
    if (!strcmp(name, "monsters"))
        return alive_m;
    if (!strcmp(name, "heroes_at_deaths_door"))
        return dd;
    if (!strcmp(name, "afflicted_heroes"))
        return aff;
    if (!strcmp(name, "virtuous_heroes"))
        return vir;
    if (!strcmp(name, "party_stress"))
        return stress;
    if (!strcmp(name, "average_stress"))
        return alive_h ? stress / alive_h : 0;
    if (!strcmp(name, "lowest_hero_hp_pct"))
        return alive_h ? low : 0;
    if (!strcmp(name, "monsters_hp_pct"))
        return mmax > 0 ? mhp * 100 / mmax : 0;
    if (s) {
        if (!strcmp(name, "hp"))
            return s->hp;
        if (!strcmp(name, "max_hp"))
            return s->max_hp;
        if (!strcmp(name, "hp_pct"))
            return s->hp_pct;
        if (!strcmp(name, "stress"))
            return s->stress;
        if (!strcmp(name, "rank"))
            return s->rank;
        if (!strcmp(name, "resolve_level"))
            return s->resolve_level;
        if (!strcmp(name, "deaths_door"))
            return s->deaths_door;
        if (!strcmp(name, "afflicted"))
            return s->afflicted;
        if (!strcmp(name, "virtuous"))
            return s->virtuous;
    }
    *ok = false;
    return 0;
}

double combat_context_call(const char *name, const ExprArg *a, int n, bool *ok, void *ctx) {
    const CombatContext *c = ctx;
    const CtxActor *s = c->self;
    const char *str = n >= 1 && a[0].is_str ? a[0].str : NULL;
    void *self = s ? s->actor : NULL;
    if (!strcmp(name, "has_disease") && n <= 1)
        return self && hero_has_disease(self, str);
    if (str && n == 1) {
        if (!strcmp(name, "stat")) {
            float v = self ? actor_stat(self, str) : NAN;
            if (isnan(v))
                *ok = false;
            return v;
        }
        if (!strcmp(name, "has_buff"))
            return self && actor_has_buff(self, str);
        if (!strcmp(name, "buffs"))
            return self ? actor_count_buffs(self, str) : 0;
        if (!strcmp(name, "has_quirk"))
            return self && hero_has_quirk(self, str);
        if (!strcmp(name, "has_item"))
            return self && hero_has_item(self, str);
        if (!strcmp(name, "trait"))
            return s && s->trait && !strcmp(s->trait, str);
        if (!strcmp(name, "class"))
            return s && glob_match(str, s->class_id);
        int k = 0;
        if (!strcmp(name, "heroes_with_trait")) {
            for (int i = 0; i < c->nheroes; i++)
                k += c->heroes[i].alive && c->heroes[i].trait && !strcmp(c->heroes[i].trait, str);
            return k;
        }
        if (!strcmp(name, "heroes_with_quirk")) {
            for (int i = 0; i < c->nheroes; i++)
                k += c->heroes[i].alive && hero_has_quirk(c->heroes[i].actor, str);
            return k;
        }
        if (!strcmp(name, "heroes_with_buff")) {
            for (int i = 0; i < c->nheroes; i++)
                k += c->heroes[i].alive && actor_has_buff(c->heroes[i].actor, str);
            return k;
        }
        if (!strcmp(name, "monsters_of_class")) {
            for (int i = 0; i < c->nmonsters; i++)
                k += c->monsters[i].alive && glob_match(str, c->monsters[i].class_id);
            return k;
        }
        if (!strcmp(name, "monsters_with_buff")) {
            for (int i = 0; i < c->nmonsters; i++)
                k += c->monsters[i].alive && actor_has_buff(c->monsters[i].actor, str);
            return k;
        }
    }
    if (!strcmp(name, "heroes_with_disease") && n == 0) {
        int k = 0;
        for (int i = 0; i < c->nheroes; i++)
            k += c->heroes[i].alive && hero_has_disease(c->heroes[i].actor, NULL);
        return k;
    }
    *ok = false;
    return 0;
}
