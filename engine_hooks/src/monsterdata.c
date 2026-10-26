// Per-monster integer fields, keyed by Monster::GetGuid and saved in Monster's own save block.
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned guid;
    bool used;
    bool present[MAX_FIELDS];
    int value[MAX_FIELDS];
} MonsterData;
static MonsterData *g_monsters;
static size_t g_nmonsters, g_monster_capacity;

static MonsterData *find(unsigned guid, bool create) {
    MonsterData *free_slot = NULL;
    for (size_t i = 0; i < g_nmonsters; i++) {
        if (g_monsters[i].used && g_monsters[i].guid == guid)
            return &g_monsters[i];
        if (!g_monsters[i].used && !free_slot)
            free_slot = &g_monsters[i];
    }
    if (!create)
        return NULL;
    if (!free_slot) {
        if (g_nmonsters == g_monster_capacity) {
            size_t capacity = g_monster_capacity ? g_monster_capacity * 2 : 64;
            if (capacity < g_monster_capacity || capacity > SIZE_MAX / sizeof *g_monsters) {
                logf_("monster data table capacity overflow");
                return NULL;
            }
            MonsterData *grown = realloc(g_monsters, capacity * sizeof *g_monsters);
            if (!grown) {
                logf_("monster data table allocation failed");
                return NULL;
            }
            g_monsters = grown;
            g_monster_capacity = capacity;
        }
        free_slot = &g_monsters[g_nmonsters++];
    }
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->used = true;
    free_slot->guid = guid;
    return free_slot;
}

int monsterdata_get(const void *monster, int field) {
    if (!is_monster(monster) || field < 0 || field >= MAX_FIELDS)
        return 0;
    MonsterData *d = find(monster_guid(monster), false);
    return d && d->present[field] ? d->value[field] : 0;
}

void monsterdata_set(const void *monster, int field, int value) {
    if (!is_monster(monster) || field < 0 || field >= MAX_FIELDS)
        return;
    MonsterData *d = find(monster_guid(monster), true);
    if (!d)
        return;
    d->present[field] = true;
    d->value[field] = value;
}

typedef void (*savedata_add_u32_fn)(void *self, const char *key, const unsigned *value);
typedef bool (*restoredata_get_u32_fn)(void *self, const char *key, unsigned *value);
typedef void (*monster_save_fn)(const void *monster, void *save);
typedef bool (*monster_restore_fn)(void *monster, void *restore);
static monster_save_fn g_save_orig;
static monster_restore_fn g_restore_orig;

static void save_hook(const void *monster, void *save) {
    g_save_orig(monster, save);
    if (!is_monster(monster) || !save)
        return;
    MonsterData *d = find(monster_guid(monster), false);
    if (!d)
        return;
    savedata_add_u32_fn add = (*(savedata_add_u32_fn **)save)[SAVEDATA_ADD_U32 / 8];
    for (int i = 0; i < field_count(); i++) {
        if (!d->present[i])
            continue;
        char key[48];
        snprintf(key, sizeof key, "ddx_actor_%s", field_name(i));
        unsigned v = (unsigned)d->value[i];
        add(save, key, &v);
    }
}

static bool restore_hook(void *monster, void *restore) {
    bool restored = g_restore_orig(monster, restore);
    if (!restored || !restore || !is_monster(monster))
        return restored;
    unsigned guid = monster_guid(monster);
    MonsterData *old = find(guid, false);
    if (old)
        old->used = false;
    restoredata_get_u32_fn get = (*(restoredata_get_u32_fn **)restore)[RESTOREDATA_GET_U32 / 8];
    for (int i = 0; i < field_count(); i++) {
        char key[48];
        snprintf(key, sizeof key, "ddx_actor_%s", field_name(i));
        unsigned v = 0;
        if (!get(restore, key, &v))
            continue;
        MonsterData *d = find(guid, true);
        if (!d)
            return restored;
        d->present[i] = true;
        d->value[i] = (int)v;
        dbgf_("restored monster %u %s = %d", guid, field_name(i), (int)v);
    }
    return restored;
}

void monsterdata_install(void) {
    static const uint8_t save_prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                            0x53, 0x50, 0x48, 0x89, 0xf3, 0x49, 0x89, 0xfe};
    g_save_orig = (monster_save_fn)install_hook("Monster::Save", sym("_ZNK7Monster4SaveEP8SaveData"), (void *)save_hook,
                                                save_prologue, sizeof save_prologue);
    static const uint8_t restore_prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x49, 0x89,
                                               0xf6, 0x48, 0x89, 0xfb, 0xba, 0x01, 0x00, 0x00, 0x00};
    g_restore_orig = (monster_restore_fn)install_hook("Monster::Restore", sym("_ZN7Monster7RestoreEP11RestoreData"),
                                                      (void *)restore_hook, restore_prologue, sizeof restore_prologue);
}
