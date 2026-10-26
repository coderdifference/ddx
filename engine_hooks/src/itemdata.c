// Per-item integer fields for inventory items that never stack (trinkets), saved in the item's own save block as
// ddx_<field>, like herodata.c and monsterdata.c. Same global field names.
//
// An Inventory::Item has no guid: it's a value, copied, moved and reassigned between inventory slots, so it can't
// be keyed by address. The library gives it an id (iid) carried in its tags map, which every game copy path copies,
// and saves it as ddx_iid.
//
// Inventory::Item (macOS): amount int 0x00, type FixedLengthString<64> 0x04 (hash 0x44), id FixedLengthString<64>
// 0x48 (hash 0x88), tags std::map<unsigned, unsigned> 0x90 (libc++: begin 0x90, root 0x98, size 0xa0; node: left
// 0x00, right 0x08, parent 0x10, key 0x1c, value 0x20). Item::Save writes id, type, amount, an Arena "unlocked" tag,
// triggers / quest uses, used during quest, added buffs and the trinket log, but no other tags;
// Item::Item(RestoreData*) calls Item::Restore. SaveData / RestoreData virtuals as herodata.c.
//
// A tag's value is a COUNT of the stack's units that carry it: Inventory::System::AddToItem(dest, src, amount) moves
// min(value, amount) of each tag from src to dest (a moved trinket's tag values all become 1: the first version,
// with the iid as the value, lost every id on the way back from a raid). So the iid is the tag's KEY,
// IID_TAG_BASE + iid, with value 1; a key whose count a move took to 0 stays behind and doesn't count. That only
// works for items that move one at a time, so ids are refused on items whose stack limit isn't 1 (trinkets: 1, set
// by Trinket::Library::LoadEntriesFile, and fixed). The "quest_rewards" and "provision" inventories ignore stack
// limits, so two copies of one trinket there would share a slot and its tags.
//
// Ids are unique within a profile: new ones are above every id restored or assigned since the library loaded. The
// table isn't cleared between profiles; an item's entry is replaced when it's restored.
// Windows (doc 18): the same Item layout; the tags are an MSVC map (itemdata_id walks it), AddTag / RemoveTag /
// GetItemClass are syms_win.c stand-ins (inlined there).
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IID_TAG_BASE 0xC0000000u
#define IID_MAX      0x3fffffff

typedef void (*savedata_add_u32_fn)(void *self, const char *key, const unsigned *value);
typedef bool (*restoredata_get_u32_fn)(void *self, const char *key, unsigned *value);

static void (*g_add_tag)(void *item, unsigned tag, int amount);
static void (*g_remove_tag)(void *item, unsigned tag);
static void **g_item_classes; // &Inventory::SystemClass::s_Instance
static const uint8_t *(*g_get_item_class)(const void *self, unsigned type_hash, unsigned id_hash);
static void (*o_save)(const void *item, void *save);
static void (*o_restore)(void *item, void *restore);
static bool g_wanted;
#define MAX_SAVE_HOOKS 4
static ItemSaveHook g_on_save[MAX_SAVE_HOOKS]; // itemdata_on_save: only an applied mod's run
static int g_non_save;
static ModsList g_on_save_mods;
static int g_next_iid = 1;

// ---------------------------------------------------------------- the table (by iid)
typedef struct {
    int iid;
    bool present[MAX_FIELDS];
    int value[MAX_FIELDS];
} ItemData;
static ItemData *g_items;
static size_t g_nitems, g_capacity;

static ItemData *find(int iid, bool create) {
    if (iid <= 0)
        return NULL;
    for (size_t i = 0; i < g_nitems; i++)
        if (g_items[i].iid == iid)
            return &g_items[i];
    if (!create)
        return NULL;
    if (g_nitems == g_capacity) {
        size_t capacity = g_capacity ? g_capacity * 2 : 256;
        ItemData *grown = realloc(g_items, capacity * sizeof *g_items);
        if (!grown) {
            logf_("item data table full");
            return NULL;
        }
        g_items = grown;
        g_capacity = capacity;
    }
    ItemData *d = &g_items[g_nitems++];
    memset(d, 0, sizeof *d);
    d->iid = iid;
    return d;
}

// ---------------------------------------------------------------- the iid tag
// the item's iid: the smallest tag key >= IID_TAG_BASE whose count is > 0 (a lower bound, then in-order successors)
#ifdef _WIN32
// MSVC's tree: ITEM_TAGS holds the head node (its parent is the root; nil nodes have byte 0x19 set); nodes: left 0,
// parent 8, right 0x10, key 0x1c, value 0x20
#define NODE(p, off) (*(const uint8_t *const *)((p) + (off)))
int itemdata_id(const void *item) {
    if (!item)
        return 0;
    const uint8_t *head = *(const uint8_t *const *)((const uint8_t *)item + ITEM_TAGS);
    if (!head)
        return 0;
    const uint8_t *n = NODE(head, 8), *best = head;
    while (!n[0x19]) {
        if (*(const unsigned *)(n + 0x1c) >= IID_TAG_BASE) {
            best = n;
            n = NODE(n, 0);
        } else
            n = NODE(n, 0x10);
    }
    for (int guard = 0; best != head && guard < 64; guard++) {
        if (*(const int *)(best + 0x20) > 0)
            return (int)(*(const unsigned *)(best + 0x1c) - IID_TAG_BASE);
        const uint8_t *r = NODE(best, 0x10);
        if (!r[0x19]) {
            while (!NODE(r, 0)[0x19])
                r = NODE(r, 0);
            best = r;
            continue;
        }
        const uint8_t *p = NODE(best, 8);
        while (!p[0x19] && best == NODE(p, 0x10)) {
            best = p;
            p = NODE(p, 8);
        }
        best = p; // the head: past the last node
    }
    return 0;
}
#undef NODE
#else
int itemdata_id(const void *item) {
    if (!item)
        return 0;
    const uint8_t *end = (const uint8_t *)item + ITEM_TAG_END;
    const uint8_t *n = *(const uint8_t *const *)end, *best = NULL;
    while (n) {
        if (*(const unsigned *)(n + 0x1c) >= IID_TAG_BASE) {
            best = n;
            n = *(const uint8_t *const *)n;
        } // left
        else
            n = *(const uint8_t *const *)(n + 8); // right
    }
    for (int guard = 0; best && guard < 64; guard++) {
        if (*(const int *)(best + 0x20) > 0)
            return (int)(*(const unsigned *)(best + 0x1c) - IID_TAG_BASE);
        const uint8_t *r = *(const uint8_t *const *)(best + 8);
        if (r) {
            while (*(const uint8_t *const *)r)
                r = *(const uint8_t *const *)r;
            best = r;
            continue;
        }
        const uint8_t *p = *(const uint8_t *const *)(best + 0x10);
        while (p && p != end && *(const uint8_t *const *)(p + 8) == best) {
            best = p;
            p = *(const uint8_t *const *)(p + 0x10);
        }
        best = p && p != end ? p : NULL; // past the last node: none
    }
    return 0;
}
#endif

static void set_iid(void *item, int iid) {
    for (int had, guard = 0; (had = itemdata_id(item)) && guard < 8; guard++)
        g_remove_tag(item, IID_TAG_BASE + (unsigned)had);
    g_add_tag(item, IID_TAG_BASE + (unsigned)iid, 1);
    if (iid >= g_next_iid)
        g_next_iid = iid + 1;
}

// only items that move one at a time can carry an iid
static bool single(const void *item) {
#ifdef ITEMDATA_TEST
    (void)item;
    return true;
#else
    if (!g_item_classes || !*g_item_classes || !g_get_item_class)
        return false;
    const uint8_t *c = g_get_item_class(*g_item_classes, *(const unsigned *)((const uint8_t *)item + ITEM_TYPE_HASH),
                                        *(const unsigned *)((const uint8_t *)item + ITEM_ID_HASH));
    return c && *(const int *)(c + ITEMCLASS_STACK_LIMIT) == 1 && !c[ITEMCLASS_STACK_MODIFIABLE];
#endif
}

int itemdata_ensure_id(void *item) {
    int iid = itemdata_id(item);
    if (iid || !item || !g_add_tag || !g_remove_tag)
        return iid;
    if (!single(item)) {
        logf_("item data: %.64s can stack, no id", (const char *)item + 0x48);
        return 0;
    }
    if (g_next_iid > IID_MAX)
        return 0;
    set_iid(item, g_next_iid);
    return itemdata_id(item);
}

// ---------------------------------------------------------------- fields
int itemdata_get(const void *item, int field) {
    ItemData *d = find(itemdata_id(item), false);
    return (d && field >= 0 && d->present[field]) ? d->value[field] : 0;
}
unsigned itemdata_type_hash(const void *item) {
    return item ? *(const unsigned *)((const uint8_t *)item + ITEM_TYPE_HASH) : 0;
}
unsigned itemdata_item_hash(const void *item) {
    return item ? *(const unsigned *)((const uint8_t *)item + ITEM_ID_HASH) : 0;
}
bool itemdata_has(const void *item, int field) {
    ItemData *d = find(itemdata_id(item), false);
    return d && field >= 0 && d->present[field];
}
bool itemdata_set(void *item, int field, int value) {
    ItemData *d = field >= 0 ? find(itemdata_ensure_id(item), true) : NULL;
    if (!d)
        return false;
    d->present[field] = true;
    d->value[field] = value;
    return true;
}

// ---------------------------------------------------------------- save / restore
static void h_save(const void *item, void *save) {
    o_save(item, save);
    if (!item || !save)
        return;
    MODS_EACH(g_on_save_mods, false, i) g_on_save[i]((void *)item);
    int iid = itemdata_id(item);
    if (iid <= 0)
        return;
    savedata_add_u32_fn add = (*(savedata_add_u32_fn **)save)[SAVEDATA_ADD_U32 / 8];
    unsigned v = (unsigned)iid;
    add(save, "ddx_iid", &v);
    ItemData *d = find(iid, false);
    for (int i = 0; d && i < field_count(); i++) {
        if (!d->present[i])
            continue;
        char key[48];
        snprintf(key, sizeof key, "ddx_%s", field_name(i));
        v = (unsigned)d->value[i];
        add(save, key, &v);
    }
}

static void h_restore(void *item, void *restore) {
    o_restore(item, restore);
    if (!item || !restore)
        return;
    restoredata_get_u32_fn get = (*(restoredata_get_u32_fn **)restore)[RESTOREDATA_GET_U32 / 8];
    unsigned v = 0;
    if (!get(restore, "ddx_iid", &v) || !v || v > IID_MAX)
        return;
    int iid = (int)v;
    set_iid(item, iid);
    ItemData *d = find(iid, true);
    if (!d)
        return;
    memset(d->present, 0, sizeof d->present); // replace any entry from another profile
    for (int i = 0; i < field_count(); i++) {
        char key[48];
        snprintf(key, sizeof key, "ddx_%s", field_name(i));
        if (!get(restore, key, &v))
            continue;
        d->present[i] = true;
        d->value[i] = (int)v;
    }
    dbgf_("item data: restored %.64s iid %d", (const char *)item + 0x48, iid);
}

void itemdata_enable(void) {
    g_wanted = true;
}
void itemdata_on_save(ItemSaveHook fn) {
    if (fn && g_non_save < MAX_SAVE_HOOKS && mods_list_add(&g_on_save_mods) >= 0)
        g_on_save[g_non_save++] = fn;
}

#ifdef ITEMDATA_TEST
void itemdata_test_init(void (*add)(void *, unsigned, int), void (*remove)(void *, unsigned)) {
    g_add_tag = add;
    g_remove_tag = remove;
}
#endif

void itemdata_install(void) {
    if (!g_wanted)
        return;
    g_add_tag = (void (*)(void *, unsigned, int))sym("_ZN9Inventory4Item6AddTagEji");
    g_remove_tag = (void (*)(void *, unsigned))sym("_ZN9Inventory4Item9RemoveTagEj");
    g_item_classes = (void **)sym("_ZN9Inventory11SystemClass10s_InstanceE");
    g_get_item_class =
        (const uint8_t *(*)(const void *, unsigned, unsigned))sym("_ZNK9Inventory11SystemClass12GetItemClassEjj");
    if (!g_add_tag || !g_remove_tag || !g_item_classes || !g_get_item_class) {
        logf_("item data: symbols missing, off");
        return;
    }
#ifndef _WIN32 // (local constants: no Windows addresses; the same values)
    static const char *const game_tags[] = {"_ZN9Inventory8ItemTagsL15k_ArenaUnlockedE",
                                            "_ZN9Inventory8ItemTagsL10k_CantSellE",
                                            "_ZN9Inventory8ItemTagsL18k_TrinketRetentionE"};
    for (int i = 0; i < 3; i++) {
        const unsigned *t = sym(game_tags[i]);
        if (t && *t >= IID_TAG_BASE)
            logf_("item data: game tag %s = %08x is in the iid range", game_tags[i], *t);
    }
#endif
    // push rbp; mov rbp,rsp; push r14; push rbx; sub rsp,0x10; mov r14,rsi
    static const uint8_t save_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53,
                                            0x48, 0x83, 0xEC, 0x10, 0x49, 0x89, 0xF6};
    *(void **)&o_save = install_hook("Inventory::Item::Save", sym("_ZNK9Inventory4Item4SaveEP8SaveData"),
                                     (void *)h_save, save_prologue, sizeof save_prologue);
    // push rbp; mov rbp,rsp; push r15; push r14; push rbx; sub rsp,0x9d8
    static const uint8_t restore_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                               0x53, 0x48, 0x81, 0xEC, 0xD8, 0x09, 0x00, 0x00};
    *(void **)&o_restore = install_hook("Inventory::Item::Restore", sym("_ZN9Inventory4Item7RestoreEP11RestoreData"),
                                        (void *)h_restore, restore_prologue, sizeof restore_prologue);
}
