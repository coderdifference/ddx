// Per-hero integer fields (by hero guid), saved in the hero's block of the game save as ddx_<field>.
// Hero::Save / Hero::Restore hooks; the game's SaveData_JSON / RestoreData_JSON virtuals do the I/O.
#include "common.h"
#include <stdio.h>
#include <string.h>

#define MAX_HEROES 512
typedef struct {
    unsigned guid;
    bool used;
    bool present[MAX_FIELDS];
    int value[MAX_FIELDS];
} HeroData;
static HeroData g_heroes[MAX_HEROES];

static HeroData *find(unsigned guid, bool create) {
    HeroData *free_slot = NULL;
    for (int i = 0; i < MAX_HEROES; i++) {
        if (g_heroes[i].used && g_heroes[i].guid == guid)
            return &g_heroes[i];
        if (!g_heroes[i].used && !free_slot)
            free_slot = &g_heroes[i];
    }
    if (!create || !free_slot) {
        if (create)
            logf_("hero data table full");
        return NULL;
    }
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->used = true;
    free_slot->guid = guid;
    return free_slot;
}

int herodata_get(unsigned guid, int field) {
    HeroData *d = find(guid, false);
    return (d && field >= 0 && d->present[field]) ? d->value[field] : 0;
}
bool herodata_has(unsigned guid, int field) {
    HeroData *d = find(guid, false);
    return d && field >= 0 && d->present[field];
}
void herodata_set(unsigned guid, int field, int value) {
    HeroData *d = find(guid, true);
    if (!d || field < 0)
        return;
    d->present[field] = true;
    d->value[field] = value;
}

void herodata_foreach(void (*fn)(unsigned guid, void *ctx), void *ctx) {
    for (int i = 0; i < MAX_HEROES; i++)
        if (g_heroes[i].used)
            fn(g_heroes[i].guid, ctx);
}

// SaveData_JSON / RestoreData_JSON virtual slots (MSVC orders the overloads differently)
typedef void (*savedata_add_u32_fn)(void *self, const char *key, const unsigned *value);
typedef bool (*restoredata_get_u32_fn)(void *self, const char *key, unsigned *value);

typedef void (*hero_save_fn)(const void *hero, void *save);
typedef bool (*hero_restore_fn)(void *hero, void *restore, bool b); // false: not restored (the roster drops it)
static hero_save_fn g_save_orig;
static hero_restore_fn g_restore_orig;

static void save_hook(const void *hero, void *save) {
    g_save_orig(hero, save); // runs inside the hero's object: our keys join it
    HeroData *d = find(hero_guid(hero), false);
    if (!d || !save)
        return;
    savedata_add_u32_fn add = (*(savedata_add_u32_fn **)save)[SAVEDATA_ADD_U32 / 8];
    for (int i = 0; i < field_count(); i++) {
        if (!d->present[i])
            continue;
        char key[48];
        snprintf(key, sizeof key, "ddx_%s", field_name(i));
        unsigned v = (unsigned)d->value[i];
        add(save, key, &v);
    }
}

// Clients told when a hero's fields are restored: Hero::Restore's own work (e.g. Actor::UpdateTrinketBuffs) runs
// before the fields are set, so anything built from them must be rebuilt here.
#define MAX_RESTORE_CLIENTS 4
static void (*g_restore_clients[MAX_RESTORE_CLIENTS])(void *hero);
static int g_nrestore_clients;
static ModsList g_restore_mods; // their mods (only an applied mod's run)

void herodata_on_restore(void (*fn)(void *hero)) {
    if (fn && g_nrestore_clients < MAX_RESTORE_CLIENTS && mods_list_add(&g_restore_mods) >= 0)
        g_restore_clients[g_nrestore_clients++] = fn;
}

static bool restore_hook(void *hero, void *restore, bool b) {
    bool restored = g_restore_orig(hero, restore, b);
    unsigned guid = hero_guid(hero);
    HeroData *old = find(guid, false);
    if (old)
        old->used = false; // drop stale data (other save, other campaign)
    if (!restore)
        return restored;
    restoredata_get_u32_fn get = (*(restoredata_get_u32_fn **)restore)[RESTOREDATA_GET_U32 / 8];
    for (int i = 0; i < field_count(); i++) {
        char key[48];
        snprintf(key, sizeof key, "ddx_%s", field_name(i));
        unsigned v = 0;
        if (!get(restore, key, &v))
            continue;
        herodata_set(guid, i, (int)v);
        dbgf_("restored hero %u %s = %d", guid, field_name(i), (int)v);
    }
    MODS_EACH(g_restore_mods, false, i) g_restore_clients[i](hero);
    return restored;
}

void herodata_install(void) {
    static const uint8_t save_prologue[] = PROLOGUE_SUB32(0x88, 0x00); // sub rsp,0x88
    g_save_orig = (hero_save_fn)install_hook("Hero::Save", sym("_ZNK4Hero4SaveEP8SaveData"), (void *)save_hook,
                                             save_prologue, sizeof save_prologue);
    static const uint8_t restore_prologue[] = PROLOGUE_SUB32(0xB8, 0x01); // sub rsp,0x1b8
    g_restore_orig = (hero_restore_fn)install_hook("Hero::Restore", sym("_ZN4Hero7RestoreEP11RestoreDatab"),
                                                   (void *)restore_hook, restore_prologue, sizeof restore_prologue);
}
