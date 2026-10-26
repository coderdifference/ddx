// Game functions and object layout (offsets found in the disassembly; see docs/engine-design.md).
#include "common.h"
#include <string.h>

unsigned (*game_string_hash)(const char *, unsigned);
bool (*game_remove_quirk_with_class_id)(void *hero, unsigned class_hash, bool);
unsigned (*game_hero_get_guid)(const void *hero);
static void *g_hero_vtable;
static void *g_monster_vtable;
static unsigned (*g_monster_get_guid)(const void *monster);

static void (*g_group_remove_buff)(void *group, unsigned guid);
static void (*g_update_buffs_for_target)(void *actor, const void *a, const void *b);
static const void *(*g_find_effect)(const void *rules, const char *name);
static const void *g_rules;

bool game_init(void) {
    game_string_hash = (unsigned (*)(const char *, unsigned))sym("_Z11string_hashPKcj");
    game_remove_quirk_with_class_id = (bool (*)(void *, unsigned, bool))sym("_ZN4Hero22RemoveQuirkWithClassIdEjb");
    game_hero_get_guid = (unsigned (*)(const void *))sym("_ZNK4Hero7GetGuidEv");
    g_group_remove_buff = (void (*)(void *, unsigned))sym("_ZN4Buff5Group10RemoveBuffEj");
    g_update_buffs_for_target =
        (void (*)(void *, const void *, const void *))sym("_ZN5Actor20UpdateBuffsForTargetEPKS_S1_");
    g_find_effect = (const void *(*)(const void *, const char *))sym("_ZNK8RuleBook10FindEffectE8c_string");
    g_rules = sym("Rules"); // the game's global RuleBook
    uint8_t *vt = sym("_ZTV4Hero");
    g_hero_vtable = vt ? vt + 16 : NULL; // vtable pointer = symbol + offset-to-top + RTTI
    vt = sym("_ZTV7Monster");
    g_monster_vtable = vt ? vt + 16 : NULL;
    g_monster_get_guid = (unsigned (*)(const void *))sym("_ZNK7Monster7GetGuidEv");
    logf_("symbols: string_hash %p, RemoveQuirkWithClassId %p, Hero::GetGuid %p, Hero vtable %p, "
          "Monster::GetGuid %p, Monster vtable %p",
          (void *)game_string_hash, (void *)game_remove_quirk_with_class_id, (void *)game_hero_get_guid, g_hero_vtable,
          (void *)g_monster_get_guid, g_monster_vtable);
    return game_string_hash && game_remove_quirk_with_class_id && game_hero_get_guid && g_hero_vtable &&
           g_group_remove_buff && g_update_buffs_for_target && g_find_effect && g_rules;
}

bool is_hero(const void *actor) {
    return actor && g_hero_vtable && *(void *const *)actor == g_hero_vtable;
}
bool is_monster(const void *actor) {
    return actor && g_monster_vtable && g_monster_get_guid && *(void *const *)actor == g_monster_vtable;
}
unsigned hero_guid(const void *hero) {
    return game_hero_get_guid(hero);
}
unsigned monster_guid(const void *monster) {
    return g_monster_get_guid(monster);
}
unsigned name_hash(const char *s) {
    return game_string_hash(s, 0);
}
const void *find_effect(const char *name) {
    return g_find_effect(g_rules, name);
}

unsigned hero_class_hash(const void *hero) {
    const uint8_t *cls = *(const uint8_t *const *)((const uint8_t *)hero + HERO_CLASS);
    return cls ? *(const unsigned *)(cls + CLASS_ID_HASH) : 0;
}

unsigned skill_id_hash(const void *skill) {
    return *(const unsigned *)((const uint8_t *)skill + SKILL_ID_HASH);
}

// Buff::Group is a std::vector<Buff::GroupEntry>. Collect matching guids first, then remove them with the
// game's Buff::Group::RemoveBuff (a guid that's gone is ignored), then recompute the actor's stats as
// Hero::RemoveQuirk does (UpdateBuffsForTarget(NULL, NULL)).
int actor_remove_buffs(void *actor, const char *const *patterns, int npatterns) {
    if (!actor || npatterns <= 0)
        return 0;
    uint8_t *group = (uint8_t *)actor + ACTOR_BUFF_GROUP;
    uint8_t *begin = *(uint8_t **)group, *end = *(uint8_t **)(group + 8);
    if (!begin || end <= begin)
        return 0;
    unsigned guids[256];
    int n = 0;
    for (uint8_t *e = begin; e + BUFF_ENTRY_SIZE <= end && n < 256; e += BUFF_ENTRY_SIZE) {
        char id[64];
        memcpy(id, e + BUFF_ENTRY_INSTANCE + BUFF_ID, sizeof id - 1);
        id[sizeof id - 1] = 0;
        for (int p = 0; p < npatterns; p++)
            if (glob_match(patterns[p], id)) {
                guids[n++] = *(unsigned *)e;
                break;
            }
    }
    for (int i = 0; i < n; i++)
        g_group_remove_buff(group, guids[i]);
    if (n)
        g_update_buffs_for_target(actor, NULL, NULL);
    return n;
}

// Remove only buffs installed under a selected Buff::Source and source id. Trigger buffs use their
// affix id as the source id, keeping this cleanup separate from permanent equipment buffs.
int actor_remove_buffs_by_source_ids(void *actor, unsigned source, const unsigned *source_ids, int nsource_ids) {
    if (!actor || !source_ids || nsource_ids <= 0)
        return 0;
    uint8_t *group = (uint8_t *)actor + ACTOR_BUFF_GROUP;
    uint8_t *begin = *(uint8_t **)group, *end = *(uint8_t **)(group + 8);
    if (!begin || end <= begin)
        return 0;
    unsigned guids[256];
    int n = 0;
    for (uint8_t *e = begin; e + BUFF_ENTRY_SIZE <= end && n < 256; e += BUFF_ENTRY_SIZE) {
        if (*(const unsigned *)(e + BUFF_ENTRY_SOURCE) != source)
            continue;
        unsigned id = *(const unsigned *)(e + BUFF_ENTRY_SOURCE_ID);
        for (int i = 0; i < nsource_ids; i++) {
            if (id == source_ids[i]) {
                guids[n++] = *(const unsigned *)e;
                break;
            }
        }
    }
    for (int i = 0; i < n; i++)
        g_group_remove_buff(group, guids[i]);
    if (n)
        g_update_buffs_for_target(actor, NULL, NULL);
    return n;
}
