// Effects recognised by name, in EffectApplyInternal (every effect application passes through it).
//   remove_quirk_<quirk id>   remove that quirk from the target hero
//   ddx_set_<field>_<n>       set the target hero/monster field (n = digits, or m<digits> for negative)
//   ddx_add_<field>_<n>       add to it
//   <client prefix>...        a client's effects (effects_on_prefix, e.g. a client mod client_example*)
//   <action name>             a config action of the target hero's class (config.c); on the performer, once
//                             per skill use
// Config effect_variants first swap the effect for the one its template names (e.g. my_heal ->
// my_heal_2), if that effect exists, using the PERFORMER's fields (the effect's source: Light of Dawn's
// heal lands on other heroes but follows the caster's Holy Power); actions then match the swapped name and
// apply to the effect's TARGET.
// Clients (effects_on_swap) may then swap it for another effect by name (e.g. a client mod client_example copies).
// The effect then applies normally (give these effects no payload).
#include "common.h"

// effects_on_apply observers (common.h)
#define MAX_APPLY 4
static struct {
    EffectApplyEnter enter;
    void (*leave)(void);
} g_apply[MAX_APPLY];
static int g_napply;
static ModsList g_apply_mods; // their mods (only an applied mod's run)
void effects_on_apply(EffectApplyEnter enter, void (*leave)(void)) {
    if ((enter || leave) && g_napply < MAX_APPLY && mods_list_add(&g_apply_mods) >= 0) {
        g_apply[g_napply].enter = enter;
        g_apply[g_napply++].leave = leave;
    }
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

#define MAX_PREFIX_CLIENTS 4
static struct {
    const char *prefix;
    EffectNameHook fn;
} g_clients[MAX_PREFIX_CLIENTS];
static int g_nclients;
static ModsList g_clients_mods; // their mods: only an applied mod's prefix is run (the top's first)

void effects_on_prefix(const char *prefix, EffectNameHook fn) {
    if (!prefix || !*prefix || !fn || g_nclients >= MAX_PREFIX_CLIENTS)
        return;
    // Managed callers release their temporary UTF-8 buffer when registration returns.
    // Registrations live for the host process, so keep an owned copy for that lifetime.
    char *owned = strdup(prefix);
    if (!owned)
        return;
    if (mods_list_add(&g_clients_mods) < 0) {
        free(owned);
        return;
    }
    g_clients[g_nclients++] = (typeof(g_clients[0])){owned, fn};
}

#define MAX_PATCH_CLIENTS 4
#define MAX_PATCHES       8
static EffectPatchHook g_patchers[MAX_PATCH_CLIENTS];
static int g_npatchers;
static ModsList g_patchers_mods; // their mods (only an applied mod's patch; the top mod's last)

void effects_on_patch(EffectPatchHook fn) {
    if (g_npatchers < MAX_PATCH_CLIENTS && mods_list_add(&g_patchers_mods) >= 0)
        g_patchers[g_npatchers++] = fn;
}

#define MAX_SWAP_CLIENTS 4
static EffectSwapHook g_swappers[MAX_SWAP_CLIENTS];
static int g_nswappers;
static ModsList g_swappers_mods; // their mods: the top mod's swap first

void effects_on_swap(EffectSwapHook fn) {
    if (g_nswappers < MAX_SWAP_CLIENTS && mods_list_add(&g_swappers_mods) >= 0)
        g_swappers[g_nswappers++] = fn;
}

static bool run_client(const char *name, void *target, void *performer) {
    MODS_EACH(g_clients_mods, true, i)
    if (starts(name, g_clients[i].prefix)) {
        g_clients[i].fn(name, target, performer);
        return true;
    }
    return false;
}

static void remove_quirk(const char *effect_name, void *hero) {
    const char *quirk = effect_name + strlen("remove_quirk_");
    if (!is_hero(hero)) {
        logf_("%s: target is not a hero, skipped", effect_name);
        return;
    }
    bool removed = game_remove_quirk_with_class_id(hero, name_hash(quirk), true);
    logf_("%s: %s quirk '%s' on hero %u", effect_name, removed ? "removed" : "hero doesn't have", quirk,
          hero_guid(hero));
}

static void field_effect(const char *effect_name, const char *rest, bool add, void *actor) {
    const char *us = strrchr(rest, '_');
    if (!us) {
        logf_("%s: bad name", effect_name);
        return;
    }
    char field[32];
    size_t fl = (size_t)(us - rest);
    if (fl >= sizeof field) {
        logf_("%s: bad field", effect_name);
        return;
    }
    memcpy(field, rest, fl);
    field[fl] = 0;
    int f = field_find(field);
    if (f < 0) {
        logf_("%s: unknown field", effect_name);
        return;
    }
    const char *num = us + 1;
    int n = (*num == 'm') ? -atoi(num + 1) : atoi(num);
    if (is_hero(actor)) {
        unsigned guid = hero_guid(actor);
        int old = herodata_get(guid, f);
        herodata_set(guid, f, add ? old + n : n);
        logf_("%s: hero %u %s %d -> %d", effect_name, guid, field, old, herodata_get(guid, f));
    } else if (is_monster(actor)) {
        unsigned guid = monster_guid(actor);
        int old = monsterdata_get(actor, f);
        monsterdata_set(actor, f, add ? old + n : n);
        logf_("%s: monster %u %s %d -> %d", effect_name, guid, field, old, monsterdata_get(actor, f));
    } else {
        logf_("%s: target is neither a hero nor a monster, skipped", effect_name);
    }
}

// An action on the performer applies once per skill use: a multi-target skill applies its effects per target,
// so a builder hitting two enemies would otherwise gain Holy Power twice.
static bool seen_this_use(const void *hero, const char *name) {
    static struct {
        unsigned use, guid, name;
    } seen[16];
    static int next;
    unsigned use = skill_use_id(), guid = hero_guid(hero), nh = name_hash(name);
    if (!use)
        return false; // not inside a skill (item, event): always applies
    for (int i = 0; i < 16; i++)
        if (seen[i].use == use && seen[i].guid == guid && seen[i].name == nh) {
            dbgf_("%s: hero %u, already applied this skill use", name, guid);
            return true;
        }
    seen[next] = (typeof(seen[0])){use, guid, nh};
    next = (next + 1) % 16;
    return false;
}

// EffectApplyInternal(prng_minstd&, Actor*, Actor*, Effect const*, Inventory::Item*, float, bool,
//                     float, Buff::Source, unsigned, EffectApplyResult&, bool, bool)
typedef uint64_t (*eai_fn)(void *rng, void *actor_a, void *actor_b, const void *effect, void *item, float f1, bool b1,
                           float f2, int source, unsigned u1, void *result, bool b2, bool b3);
static eai_fn g_orig;
static bool (*g_effect_result_bool)(const void *result);
static void **g_raid;
static bool (*g_raid_in_battle)(const void *raid);

static bool combat_active(void) {
    return g_raid && *g_raid && g_raid_in_battle && g_raid_in_battle(*g_raid);
}

static void effect_actor_label(const void *actor, char *kind, size_t kind_n, char *name, size_t name_n,
                               unsigned *guid) {
    *guid = 0;
    if (is_hero(actor)) {
        snprintf(kind, kind_n, "hero");
        *guid = hero_guid(actor);
    } else if (is_monster(actor)) {
        snprintf(kind, kind_n, "monster");
        *guid = monster_guid(actor);
    } else
        snprintf(kind, kind_n, "actor");
    name[0] = 0;
    if (actor) {
        const char *s = (const char *)actor + 8;
        size_t i = 0;
        while (i + 1 < name_n && i < 63 && s[i] >= 0x20 && s[i] != 0x7f) {
            name[i] = s[i];
            i++;
        }
        name[i] = 0;
    }
    if (!name[0])
        snprintf(name, name_n, "(unnamed)");
}

static uint64_t effect_call_log(void *rng, void *actor_a, void *actor_b, const void *effect, void *item, float f1,
                                bool b1, float f2, int source, unsigned u1, void *result, bool b2, bool b3) {
    const char *name = effect ? (const char *)effect : "(null)";
    bool swap = effect && ((const uint8_t *)effect)[EFFECT_SWAP] != 0;
    const void *target = swap ? actor_a : actor_b;
    const void *performer = swap ? actor_b : actor_a;
    char tk[16], tn[64], pk[16], pn[64];
    unsigned tg, pg;
    effect_actor_label(target, tk, sizeof tk, tn, sizeof tn, &tg);
    effect_actor_label(performer, pk, sizeof pk, pn, sizeof pn, &pg);
    combat_logf("EffectApplyInternal attempt in_battle=%u effect=\"%s\" target=%s:\"%s\" guid=%u ptr=%p "
                "performer=%s:\"%s\" guid=%u ptr=%p source=%d f1=%.3f b1=%u f2=%.3f u1=%u b2=%u b3=%u",
                combat_active(), name, tk, tn, tg, target, pk, pn, pg, performer, source, f1, b1, f2, u1, b2, b3);
    buff_origin_enter(target, performer, effect);
    if (effect) { // its popups' details (effects_on_apply)
        const uint8_t *e = effect;
        int poison = *(const int *)(e + EFFECT_DOT_POISON), bleed = *(const int *)(e + EFFECT_DOT_BLEED);
        MODS_EACH(g_apply_mods, false, i)
        if (g_apply[i].enter)
            g_apply[i].enter(target,
                             poison > 0  ? poison
                             : bleed > 0 ? bleed
                                         : 0,
                             *(const int *)(e + EFFECT_DURATION), *(const int *)(e + EFFECT_DURATION_TYPE));
    }
    uint64_t r = g_orig(rng, actor_a, actor_b, effect, item, f1, b1, f2, source, u1, result, b2, b3);
    buff_origin_leave();
    if (effect)
        MODS_EACH(g_apply_mods, false, i) if (g_apply[i].leave) g_apply[i].leave();
    bool any = g_effect_result_bool ? g_effect_result_bool(result) : false;
    const uint8_t *res = result;
    uintptr_t begin = result ? *(const uintptr_t *)(res + 8) : 0;
    uintptr_t end = result ? *(const uintptr_t *)(res + 0x10) : 0;
    size_t n = end >= begin && begin ? (end - begin) / sizeof(void *) : 0;
    combat_logf("EffectApplyInternal result in_battle=%u effect=\"%s\" any=%u flags=%u,%u,%u result_items=%u",
                combat_active(), name, any, result ? res[1] : 0, result ? res[2] : 0, result ? res[3] : 0, (unsigned)n);
    return r;
}

static void buff_id(const void *instance, char out[64]) {
    out[0] = 0;
    if (!instance)
        return;
    const char *id = (const char *)instance + BUFF_ID;
    size_t i = 0;
    while (i < 63 && id[i] >= 0x20 && id[i] != 0x7f) {
        out[i] = id[i];
        i++;
    }
    out[i] = 0;
    if (!out[0])
        snprintf(out, 64, "(unknown)");
}

static void log_buff_change(const char *action, const void *group, unsigned guid, const void *instance) {
    const void *actor = (const uint8_t *)group - ACTOR_BUFF_GROUP;
    char kind[16], name[64], id[64];
    unsigned actor_guid;
    effect_actor_label(actor, kind, sizeof kind, name, sizeof name, &actor_guid);
    buff_id(instance, id);
    combat_logf("Buff %s in_battle=%u actor=%s name=\"%s\" actor_guid=%u actor_ptr=%p id=\"%s\" buff_guid=%u source=%u "
                "source_id=%u",
                action, combat_active(), kind, name, actor_guid, actor, id, guid,
                instance ? *(const unsigned *)((const uint8_t *)instance + 0x54) : 0,
                instance ? *(const unsigned *)((const uint8_t *)instance + 0x58) : 0);
}

static bool (*g_remove_buff_orig)(void *group, const void *predicate);
static void (*g_remove_one_buff_orig)(void *group, unsigned guid);
static void remove_one_buff_hook(void *group, unsigned guid);

// ---------------------------------------------------------------- buff stack limits
// buff_stack_limit(id, max): an actor keeps at most max instances of that buff id from one kind of source (Buff::Source
// at Instance +0x54; gear buffs with the same id are a different source and don't count). Adding one more removes the
// oldest first (the group keeps insertion order), so a new application refreshes the stack instead of adding to it.
#define MAX_STACK_LIMITS 1024
// Each limit is its mod's (mods_current): only an applied mod's count, the highest one's where two limit one id.
static struct {
    unsigned hash;
    int max, owner;
} g_stack_limits[MAX_STACK_LIMITS];
static int g_nstack_limits;

void buff_stack_limit(const char *id, int max) {
    if (!id || !*id || max < 1)
        return;
    unsigned h = name_hash(id);
    int owner = mods_current();
    for (int i = 0; i < g_nstack_limits; i++)
        if (g_stack_limits[i].hash == h && g_stack_limits[i].owner == owner) {
            g_stack_limits[i].max = max;
            return;
        }
    if (g_nstack_limits == MAX_STACK_LIMITS) {
        logf_("buff stacks: limit table full, %s not limited", id);
        return;
    }
    g_stack_limits[g_nstack_limits].hash = h;
    g_stack_limits[g_nstack_limits].owner = owner;
    g_stack_limits[g_nstack_limits++].max = max;
}

static int stack_limit_of(const char *id) {
    if (!g_nstack_limits)
        return 0;
    unsigned h = name_hash(id);
    int best = -1;
    for (int i = 0; i < g_nstack_limits; i++) {
        int o = g_stack_limits[i].owner;
        if (g_stack_limits[i].hash != h || !mods_active(o))
            continue;
        if (best < 0 ||
            (o >= 0 && (g_stack_limits[best].owner < 0 || mods_pos(o) < mods_pos(g_stack_limits[best].owner))))
            best = i;
    }
    return best >= 0 ? g_stack_limits[best].max : 0;
}

static void enforce_stack_limit(void *group, const void *instance) {
    char id[64];
    buff_id(instance, id);
    int max = stack_limit_of(id);
    if (max <= 0 || !g_remove_one_buff_orig)
        return;
    unsigned source = *(const unsigned *)((const uint8_t *)instance + BUFF_SOURCE);
    uint8_t *begin = *(uint8_t **)group, *end = *(uint8_t **)((uint8_t *)group + 8);
    if (!begin || end <= begin)
        return;
    unsigned same[256];
    int n = 0;
    for (uint8_t *e = begin; e + BUFF_ENTRY_SIZE <= end && n < 256; e += BUFF_ENTRY_SIZE) {
        char other[64];
        buff_id(e + 8, other);
        if (!strcmp(other, id) && *(const unsigned *)(e + 8 + BUFF_SOURCE) == source)
            same[n++] = *(unsigned *)e;
    }
    for (int i = 0; i + max <= n; i++) { // the oldest, until the new one makes max
        combat_logf("buff stacks: %s at its limit %d: the oldest (guid %u) makes way", id, max, same[i]);
        remove_one_buff_hook(group, same[i]);
    }
}

static void buff_added(void *group, unsigned guid, const void *instance) {
    log_buff_change("added", group, guid, instance);
    buff_origin_added((const uint8_t *)group - ACTOR_BUFF_GROUP, guid, instance);
    popups_buff_added((const uint8_t *)group - ACTOR_BUFF_GROUP, guid);
}

#ifdef _WIN32
// Windows: Buff::Group::AddBuff is inlined (syms_win.c), so this hooks AddBuffInternal(guid, instance) and does
// AddBuff's work only for the calls from macOS's AddBuff callers (by return address), not for burn stacks.
static void (*g_add_internal_orig)(void *group, unsigned guid, const void *instance);
static const void *g_add_sites[3];
static void add_buff_internal_hook(void *group, unsigned guid, const void *instance) {
    const void *from = __builtin_return_address(0);
    bool add_buff = false;
    for (int i = 0; i < 3; i++)
        add_buff |= g_add_sites[i] && from == g_add_sites[i];
    if (add_buff && instance)
        enforce_stack_limit(group, instance);
    g_add_internal_orig(group, guid, instance);
    if (add_buff)
        buff_added(group, guid, instance);
}
#else
static unsigned (*g_add_buff_orig)(void *group, const void *instance);
static unsigned add_buff_hook(void *group, const void *instance) {
    if (instance)
        enforce_stack_limit(group, instance);
    unsigned guid = g_add_buff_orig(group, instance);
    buff_added(group, guid, instance);
    return guid;
}
#endif

static void remove_one_buff_hook(void *group, unsigned guid) {
    uint8_t *begin = *(uint8_t **)group, *end = *(uint8_t **)((uint8_t *)group + 8);
    char id[64] = "(unknown)";
    if (begin && end >= begin)
        for (uint8_t *entry = begin; entry + BUFF_ENTRY_SIZE <= end; entry += BUFF_ENTRY_SIZE)
            if (*(unsigned *)entry == guid) {
                buff_id(entry + 8, id);
                break;
            }
    g_remove_one_buff_orig(group, guid);
    const void *actor = (const uint8_t *)group - ACTOR_BUFF_GROUP;
    char kind[16], name[64];
    unsigned actor_guid;
    effect_actor_label(actor, kind, sizeof kind, name, sizeof name, &actor_guid);
    combat_logf("Buff removed in_battle=%u actor=%s name=\"%s\" actor_guid=%u actor_ptr=%p id=\"%s\" buff_guid=%u "
                "path=remove_one",
                combat_active(), kind, name, actor_guid, actor, id, guid);
}

typedef struct {
    unsigned guid;
    char id[64];
} RemovedBuff;
static bool remove_buffs_hook(void *group, const void *predicate) {
    RemovedBuff before[256];
    size_t n = 0;
    uint8_t *begin = *(uint8_t **)group, *end = *(uint8_t **)((uint8_t *)group + 8);
    if (begin && end >= begin) {
        for (uint8_t *entry = begin; entry + BUFF_ENTRY_SIZE <= end && n < 256; entry += BUFF_ENTRY_SIZE) {
            before[n].guid = *(unsigned *)entry;
            buff_id(entry + 8, before[n].id);
            n++;
        }
    }
    bool removed = g_remove_buff_orig(group, predicate);
    begin = *(uint8_t **)group;
    end = *(uint8_t **)((uint8_t *)group + 8);
    for (size_t i = 0; i < n; i++) {
        bool remains = false;
        if (begin && end >= begin)
            for (uint8_t *entry = begin; entry + BUFF_ENTRY_SIZE <= end; entry += BUFF_ENTRY_SIZE)
                if (*(unsigned *)entry == before[i].guid) {
                    remains = true;
                    break;
                }
        if (!remains) {
            const void *actor = (const uint8_t *)group - ACTOR_BUFF_GROUP;
            char kind[16], name[64];
            unsigned actor_guid;
            effect_actor_label(actor, kind, sizeof kind, name, sizeof name, &actor_guid);
            combat_logf("Buff removed in_battle=%u actor=%s name=\"%s\" actor_guid=%u actor_ptr=%p id=\"%s\" "
                        "buff_guid=%u path=bulk_remove",
                        combat_active(), kind, name, actor_guid, actor, before[i].id, before[i].guid);
        }
    }
    return removed;
}

static uint64_t hook(void *rng, void *actor_a, void *actor_b, const void *effect, void *item, float f1, bool b1,
                     float f2, int source, unsigned u1, void *result, bool b2, bool b3) {
    if (effect) {
        const char *name = (const char *)effect;
        bool swap = ((const uint8_t *)effect)[EFFECT_SWAP] != 0;
        void *target = swap ? actor_a : actor_b;
        void *performer = swap ? actor_b : actor_a;
        if (starts(name, "remove_quirk_"))
            remove_quirk(name, target);
        else if (starts(name, "ddx_set_"))
            field_effect(name, name + strlen("ddx_set_"), false, target);
        else if (starts(name, "ddx_add_"))
            field_effect(name, name + strlen("ddx_add_"), true, target);
        else if (run_client(name, target, performer)) {
        } else {
            const ClassConfig *pc = config_for_hero(performer);
            char vname[128];
            if (pc && config_effect_variant(pc, performer, name, vname, sizeof vname)) {
                const void *v = find_effect(vname);
                if (v) {
                    effect = v;
                    name = (const char *)v;
                } else
                    logf_("%s: variant effect %s not found, using the original", name, vname);
            }
            MODS_EACH(g_swappers_mods, true, i) { // clients' swaps: the first that names an existing effect
                const char *to = g_swappers[i](effect, target, performer);
                if (!to)
                    continue;
                const void *v = find_effect(to);
                if (v) {
                    effect = v;
                    name = (const char *)v;
                    break;
                }
                logf_("%s: swap effect %s not found, using the original", name, to);
            }
            const ClassConfig *c = config_for_hero(target);
            if (c && !(target == performer && seen_this_use(target, name)))
                config_apply_action(c, target, name);
            // Effect heal formula (performer's fields) and clients' patches: set for this call, then restored.
            EffectPatch patch[MAX_PATCHES];
            float old[MAX_PATCHES];
            int np = 0;
            double h;
            if (pc && config_effect_heal(pc, performer, name, &h)) {
                patch[np++] = (EffectPatch){EFFECT_HEAL, (float)h};
                dbgf_("effect heal formula: %s by hero %u heal %.0f (was %.0f)", name, hero_guid(performer), h,
                      *(const float *)((const uint8_t *)effect + EFFECT_HEAL));
            }
            MODS_EACH(g_patchers_mods, false, i)
            if (np < MAX_PATCHES)
                np += g_patchers[i](effect, target, performer, patch + np, MAX_PATCHES - np);
            if (np) {
                for (int i = 0; i < np; i++) {
                    float *f = (float *)((uint8_t *)effect + patch[i].offset);
                    old[i] = *f;
                    *f = patch[i].value;
                }
                uint64_t r =
                    effect_call_log(rng, actor_a, actor_b, effect, item, f1, b1, f2, source, u1, result, b2, b3);
                for (int i = np - 1; i >= 0; i--)
                    *(float *)((uint8_t *)effect + patch[i].offset) = old[i];
                return r;
            }
        }
    }
    return effect_call_log(rng, actor_a, actor_b, effect, item, f1, b1, f2, source, u1, result, b2, b3);
}

void effects_install(void) {
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    g_raid_in_battle = (bool (*)(const void *))sym("_ZNK4Raid10IsInBattleEv");
    static const uint8_t prologue[] = PROLOGUE_SUB32(0x78, 0x07); // sub rsp,0x778
    g_orig = (eai_fn)install_hook("EffectApplyInternal",
                                  sym("_Z19EffectApplyInternalR11prng_minstdP5ActorS2_"
                                      "PK6EffectPN9Inventory4ItemEfbfN4Buff6SourceEjR17EffectApplyResultbb"),
                                  (void *)hook, prologue, sizeof prologue);
    g_effect_result_bool = (bool (*)(const void *))sym("_ZNK17EffectApplyResultcvbEv");
#ifdef _WIN32
    g_add_sites[0] = sym("win:Buff::Group::AddBuff from Hero::ApplyBuff");
    g_add_sites[1] = sym("win:Buff::Group::AddBuff from Monster::ApplyBuff");
    g_add_sites[2] = sym("win:Buff::Group::AddBuff from Monster companion");
    g_add_internal_orig = (void (*)(void *, unsigned, const void *))install_hook(
        "Buff::Group::AddBuffInternal (AddBuff: combat log)", sym("win:Buff::Group::AddBuffInternal"),
        (void *)add_buff_internal_hook, NULL, 0);
#else
    static const uint8_t add_prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x89,
                                           0xf2, 0x8b, 0x5f, 0x18, 0x8d, 0x43, 0x01};
    g_add_buff_orig = (unsigned (*)(void *, const void *))install_hook(
        "Buff::Group::AddBuff (combat log)", sym("_ZN4Buff5Group7AddBuffERKNS_8InstanceE"), (void *)add_buff_hook,
        add_prologue, sizeof add_prologue);
#endif
    static const uint8_t remove_prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41,
                                              0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
    g_remove_buff_orig = (bool (*)(void *, const void *))install_hook(
        "Buff::Group::RemoveBuffs (combat log)",
        sym("_ZN4Buff5Group11RemoveBuffsENSt3__18functionIFbRKNS_10GroupEntryEEEE"), (void *)remove_buffs_hook,
        remove_prologue, sizeof remove_prologue);
    static const uint8_t remove_one_prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x89, 0xf0, 0x48, 0x8b,
                                                  0x37, 0x48, 0x8b, 0x57, 0x08, 0x48, 0x29, 0xf2};
    g_remove_one_buff_orig = (void (*)(void *, unsigned))install_hook(
        "Buff::Group::RemoveBuff (combat log)", sym("_ZN4Buff5Group10RemoveBuffEj"), (void *)remove_one_buff_hook,
        remove_one_prologue, sizeof remove_one_prologue);
}
