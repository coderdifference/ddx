// Item type registry: item types beyond the game's compiled-in ones (Inventory::ItemType::k*, each the string_hash of
// its type string). The game loads an inventory_item line of any .type and handles the item inside an expedition (party
// inventory, loot window, discard, save and load), but each function that sorts items by type tests the k* constants
// inline and drops a type it doesn't know (docs/engine-design.md "Native item types spike"). A mod registers its types
// (item_type_register, before item_types_install) and this module owns those functions, giving each registered type
// what its definition asks for. Vanilla types always get the game's own handling.
//
// Home: Estate::System::AddInventorySystem(Inventory::System const&, tag, currency map const*) (the three shorter
// overloads jump to it) moves the party's items into the estate when an expedition ends. It adds the currencies to the
// wallet, then sorts each item (when tag isn't 0, only those with Item::GetHasTag(tag, amount)) by type: kTrinket
// (unless its rarity is darkest_dungeon) into the trinket storage (Estate+0x40) and the expedition's new trinkets
// (Estate+0x190, cleared by Estate::System::OnRaidFinish, read by GetTrinketsAvailableForRemoval), kTrinketUnlock
// counted in the map at Estate+0x160, kEstate and kEstateCurrency into the estate items (Estate+0x100); then it sends a
// RealmInventoryChangedEvent. Any other type is dropped. The hook runs the game's copy, then adds each item of a
// registered type to its home: ITEM_HOME_STASH is Estate::System::GetTrinketStorage(0) (the campaign's stash, +0x40,
// or the arena's, +0xa0, by game mode); ITEM_HOME_CURRENCY adds the amount to its estate currency
// (Estate::System::AddAmount, name_hash of the id), scaled as the game scales its own currencies: the call's currency
// map (std::map<Estate::CurrencyType, float>; nodes: CurrencyType at +0x1c, its hash at +0x5c, the float at +0x60)
// multiplies a listed currency, rounded half away from zero (Estate::SystemClass::ConvertInventorySystemToCurrency
// DefinitionList). Not an heirloom item: the expedition results screen pairs each currency it's given with a fixed
// widget (gold, Shards, the four heirlooms) and crashed on another (RaidResultsDisplay::InitCampaignAnimTweens).
//
// Save: Estate::System::Save writes the trinket storage (Estate+0x40) through Inventory::System::Save(SaveData*,
// std::function<bool (Item const&)>) with the filter Estate::System::Save()::$_0, which keeps only kTrinket items; any
// other type in the stash is lost at the next save. The hook keeps the registered types whose home is the stash too.
//
// Checks: most type tests sit inline in large functions, so they are compare sites (install_cmp_site): each `cmp
// r32, [rip+kTrinket]` listed below is answered by item_type_site: equal for a trinket, and for a registered type that
// joins the site's check. A site is installed only if some registered type joins its check. Every site was checked to
// be followed by a jump on ZF alone. Sites, by check:
//   ITEM_CHECK_SELL  RealmInventory::PreChildrenShowInternal+0x10d5: the hovered item's class type (ItemClass+0x40,
//                    ecx) before its sell price is shown (ItemClass+0xa4, Upgrade::GetAmountWithTagDiscount with
//                    k_TrinketSellBack); jne skips it
//                    RealmInventory::PreChildrenShowInternal()::$_3 +0x2d (the sell mode's validity, ItemValidResult
//                    +0x40): eax = Item::GetTypeHash; jne makes it invalid, else valid when the class's sell price > 0
//   ITEM_CHECK_STASH_DROP  RealmInventorySlotInterface::IsDropItemValidInternal+0x25: valid is `type == kTrinket ||
//                    type == kTrinketUnlock`; this is the second test (eax = Item::GetTypeHash; sete al), so a joining
//                    type is accepted like the Talisman of the Flame without becoming a trinket anywhere else
//
// Pickup sound: InventorySlotInterface::PlayTakeSfx plays event:/ui/dungeon/loot_take_<type> (or _<id> when the class
// flag ItemClass+0x130 is 1) through Audio_Event; FMOD has no event for a new type and logs an error to app.log. The
// hook plays the registered type's sound (a sound file through audio.c, or the event loot_take_<sound>), or none; vanilla types go to the game's copy. Vanilla
// sounds: all, estate, estate_currency, firewood, gold, heirloom, jewelry, journal_page, provision, quest_item, shard,
// snake_scale, spice, supply, trinket, trinket_unlock (master_bank.strings.bank).
//
// Trinket slots: a hero's trinket slot (EquipmentSlotInterface::OnDrop(vec2 const&, DragData*), 0 refused) never
// takes a registered type, so no drop makes one a trinket. (Its OnDragStarted only highlights trinkets anyway.)
//
// A type also needs data the game reads by type string: its inventory_item lines, icons
// (panels/icons_equip/<type>/inv_<type>+<id>.png), strings (str_inventory_title_<type><id>,
// str_inventory_description_<type><id>, str_inventory_type_name_<type>) and a quest_fail_keep_rates entry in a
// campaign/estate/*.estate.json (a retreat removes a type without one).
//
// Windows: the same hooks (syms_win.c); GetAmountOfItems, GetItem and GetHasTag are inlined there (stand-ins), the
// currency map is an MSVC tree, and the sell sites are MSVC's `mov eax, [rip+kTrinket]; cmp [field], eax` pairs (a
// compare site of the pair form, value the field), named "win:item_types <site>" in the table.
#include "common.h"
#include <stdio.h>
#include <string.h>

#define MAX_ITEM_TYPES 16

static ItemTypeDef g_types[MAX_ITEM_TYPES];
static char g_names[MAX_ITEM_TYPES][64];
static char g_sounds[MAX_ITEM_TYPES][128];
static char g_currencies[MAX_ITEM_TYPES][64];
static unsigned g_hashes[MAX_ITEM_TYPES];
static int g_ntypes;
static ModsList g_types_mods; // their mods: a type is known only while its mod is applied (the
                              // top mod's where two register one name)

static const ItemTypeDef *find_own(unsigned type_hash, int owner) {
    for (int i = 0; i < g_ntypes; i++)
        if (g_hashes[i] == type_hash && g_types_mods.owner[i] == owner)
            return &g_types[i];
    return NULL;
}

bool item_type_register(const ItemTypeDef *def) {
    if (!def || !def->type || !*def->type || strlen(def->type) >= sizeof g_names[0]) {
        logf_("item types: a type needs a name under 64 characters");
        return false;
    }
    unsigned h = name_hash(def->type);
    if (def->currency && strlen(def->currency) >= sizeof g_currencies[0]) {
        logf_("item types: %s: currency id too long, not registered", def->type);
        return false;
    }
    if (def->sound && strlen(def->sound) >= sizeof g_sounds[0]) {
        logf_("item types: %s: sound name too long, not registered", def->type);
        return false;
    }
    if (find_own(h, mods_current())) {
        logf_("item types: %s is already registered", def->type);
        return false;
    }
    if (g_ntypes == MAX_ITEM_TYPES || mods_list_add(&g_types_mods) < 0) {
        logf_("item types: too many types, %s not registered", def->type);
        return false;
    }
    strcpy(g_names[g_ntypes], def->type);
    g_types[g_ntypes] = *def;
    g_types[g_ntypes].type = g_names[g_ntypes];
    if (def->sound)
        g_types[g_ntypes].sound = strcpy(g_sounds[g_ntypes], def->sound);
    if (def->currency)
        g_types[g_ntypes].currency = strcpy(g_currencies[g_ntypes], def->currency);
    g_hashes[g_ntypes++] = h;
    return true;
}

const ItemTypeDef *item_type_find(unsigned type_hash) {
    MODS_EACH(g_types_mods, true, i) if (g_hashes[i] == type_hash) return &g_types[i];
    return NULL;
}

int item_types_count(void) {
    return g_ntypes;
}
const ItemTypeDef *item_type_at(int i) {
    return i >= 0 && i < g_ntypes ? &g_types[i] : NULL;
}

typedef struct {
    const char *name, *symbol;
    uint32_t offset;
    uint8_t len, bytes[7];
    unsigned check;
} TypeSite;

static const TypeSite g_sites[] = {
    {"stash sell price",
     "_ZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEv",
     0x10d5,
     6,
     {0x3B, 0x0D, 0xD9, 0xB0, 0x3F, 0x02},
     ITEM_CHECK_SELL},
    {"stash sell mode",
     "_ZNSt3__110__function6__funcIZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEvE3$_3NS_"
     "9allocatorIS5_EEFN9Inventory7Display15ItemValidResultEPKNS8_4ItemEPKNS8_9ItemClassEEEclEOSD_OSG_",
     0x2d,
     6,
     {0x3B, 0x05, 0xA1, 0xEC, 0x3E, 0x02},
     ITEM_CHECK_SELL},
    {"stash drop",
     "_ZNK27RealmInventorySlotInterface23IsDropItemValidInternalERKN9Inventory4ItemE",
     0x25,
     6,
     {0x3B, 0x05, 0x0D, 0x45, 0x13, 0x02},
     ITEM_CHECK_STASH_DROP},
};
#ifdef _WIN32
static const char *const g_win_sites[] = {"win:item_types stash sell price", "win:item_types stash sell mode",
                                          "win:item_types stash drop"}; // g_sites' entries in syms_win.c
#endif

// A site's answer: the vanilla type, or a registered type that joins the site's check.
static bool item_type_site(void *ud, unsigned value, unsigned vanilla) {
    if (value == vanilla)
        return true;
    const ItemTypeDef *def = item_type_find(value);
    return def && (def->joins & ((const TypeSite *)ud)->check);
}

#ifndef ITEM_TYPES_TEST
typedef void (*add_inventory_fn)(void *estate, const void *inventory, unsigned tag, const void *currency_map);
static add_inventory_fn o_add_inventory;
static unsigned (*g_count)(const void *inventory);                    // Inventory::System::GetAmountOfItems
static const uint8_t *(*g_get)(const void *inventory, unsigned i);    // Inventory::System::GetItem
static bool (*g_has_tag)(const void *item, unsigned tag, int amount); // Inventory::Item::GetHasTag
static void *(*g_storage)(void *estate, int type);                    // Estate::System::GetTrinketStorage
static void (*g_add_copy)(void *inventory, const void *item);         // Inventory::System::AddCopyOfItem

static void (*g_add_amount)(void *estate, unsigned currency, int amount); // Estate::System::AddAmount

#ifdef _WIN32
// MSVC's tree: the map holds its head node (parent +8 the root; nil nodes have byte 0x19 set); left 0, right 0x10
static int currency_scaled(const void *currency_map, unsigned currency, int amount) {
    const uint8_t *end = currency_map ? *(const uint8_t *const *)currency_map : NULL, *found = end;
    for (const uint8_t *n = end ? *(const uint8_t *const *)(end + 8) : NULL; n && !n[0x19];) // lower_bound
        if (*(const unsigned *)(n + 0x5c) < currency)
            n = *(const uint8_t *const *)(n + 0x10);
        else {
            found = n;
            n = *(const uint8_t *const *)n;
        }
#else
static int currency_scaled(const void *currency_map, unsigned currency, int amount) {
    const uint8_t *end = currency_map ? (const uint8_t *)currency_map + 8 : NULL, *found = end;
    for (const uint8_t *n = end ? *(const uint8_t *const *)end : NULL; n;) // lower_bound by the hash
        if (*(const unsigned *)(n + 0x5c) < currency)
            n = *(const uint8_t *const *)(n + 8);
        else {
            found = n;
            n = *(const uint8_t *const *)n;
        }
#endif
    if (!found || found == end || *(const unsigned *)(found + 0x5c) != currency)
        return amount;
    float v = (float)amount * *(const float *)(found + 0x60);
    return (int)(v < 0 ? v - 0.5f : v + 0.5f);
}

static void h_add_inventory(void *estate, const void *inventory, unsigned tag, const void *currency_map) {
    o_add_inventory(estate, inventory, tag, currency_map);
    unsigned n = g_count(inventory);
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *item = g_get(inventory, i);
        const ItemTypeDef *def = item ? item_type_find(*(const unsigned *)(item + ITEM_TYPE_HASH)) : NULL;
        if (!def)
            continue;
        if (tag && !g_has_tag(item, tag, *(const int *)(item + ITEM_AMOUNT))) {
            logf_("item types: %s %.64s not tagged %08x, left behind (as the game does)", def->type,
                  (const char *)item + ITEM_ID, tag);
            continue;
        }
        if (def->home == ITEM_HOME_CURRENCY) {
            int amount = *(const int *)(item + ITEM_AMOUNT),
                n2 = def->currency ? currency_scaled(currency_map, name_hash(def->currency), amount) : 0;
            if (n2 && g_add_amount)
                g_add_amount(estate, name_hash(def->currency), n2);
            logf_("item types: %s x%d %s %d %s", def->type, amount, g_add_amount ? "banked as" : "couldn't bank", n2,
                  def->currency ? def->currency : "(no currency)");
            continue;
        }
        void *home = def->home == ITEM_HOME_STASH ? g_storage(estate, 0) : NULL;
        if (home)
            g_add_copy(home, item);
        logf_("item types: %s %.64s x%d %s", def->type, (const char *)item + ITEM_ID,
              *(const int *)(item + ITEM_AMOUNT), home ? "brought home to the stash" : "left behind (no home)");
    }
}

typedef bool (*save_filter_fn)(const void *func, const void *item);
static save_filter_fn o_stash_filter;

static bool h_stash_filter(const void *func, const void *item) {
    if (o_stash_filter(func, item))
        return true;
    const ItemTypeDef *def = item_type_find(*(const unsigned *)((const uint8_t *)item + ITEM_TYPE_HASH));
    return def && def->home == ITEM_HOME_STASH;
}

typedef void (*take_sfx_fn)(void *slot);
static take_sfx_fn o_take_sfx;
static void (*g_audio_event)(const char *name, bool, void *callback); // Audio_Event(char const*, bool, std::function)

static void h_take_sfx(void *slot) {
    const uint8_t *item = *(const uint8_t **)((uint8_t *)slot + SLOT_ITEM);
    const ItemTypeDef *def = item ? item_type_find(*(const unsigned *)(item + ITEM_TYPE_HASH)) : NULL;
    if (!def) {
        o_take_sfx(slot);
        return;
    }
    if (!def->sound)
        return;
    if (strpbrk(def->sound, "/.")) {
        audio_play_file(def->sound, AUDIO_SFX);
        return;
    }
    char name[64];
    snprintf(name, sizeof name, "/ui/dungeon/loot_take_%s", def->sound);
    void *callback[8] = {0}; // an empty std::function (passed by address; 64 bytes on MSVC)
    g_audio_event(name, true, callback);
}

static uint64_t (*o_slot_drop)(void *slot, const void *pos, void *drag);
static uint64_t h_slot_drop(void *slot, const void *pos, void *drag) {
    const uint8_t *item = item_drag_item(drag);
    if (item && item_type_find(*(const unsigned *)(item + ITEM_TYPE_HASH)))
        return 0;
    return o_slot_drop(slot, pos, drag);
}

void item_types_install(void) {
    if (!g_ntypes)
        return; // nothing registered: the game's code untouched
    g_count = (unsigned (*)(const void *))sym("_ZNK9Inventory6System16GetAmountOfItemsEv");
    g_get = (const uint8_t *(*)(const void *, unsigned))sym("_ZNK9Inventory6System7GetItemEj");
    g_has_tag = (bool (*)(const void *, unsigned, int))sym("_ZNK9Inventory4Item9GetHasTagEji");
    g_storage = (void *(*)(void *, int))sym("_ZN6Estate6System17GetTrinketStorageENS0_19ETrinketStorageTypeE");
    g_add_copy = (void (*)(void *, const void *))sym("_ZN9Inventory6System13AddCopyOfItemEPKNS_4ItemE");
    g_add_amount = (void (*)(void *, unsigned, int))sym("_ZN6Estate6System9AddAmountEji");
    void *add = sym("_ZN6Estate6System18AddInventorySystemERKN9Inventory6SystemEjPKNSt3__13mapINS_12CurrencyTypeEfNS5_"
                    "4lessIS7_EENS5_9allocatorINS5_4pairIKS7_fEEEEEE");
    if (!g_count || !g_get || !g_has_tag || !g_storage || !g_add_copy || !add) {
        logf_("item types: symbols missing, %d registered type(s) can't be brought home", g_ntypes);
        return;
    }
    static const uint8_t prologue[] = PROLOGUE_SUB8(0x68); // sub rsp,0x68
    o_add_inventory = (add_inventory_fn)install_hook("Estate::System::AddInventorySystem", add, (void *)h_add_inventory,
                                                     prologue, sizeof prologue);
    // push rbp; mov rbp,rsp; mov rdi,rsi; call Item::GetTypeHash; cmp eax,[rip+kTrinket]
    static const uint8_t filter_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x89, 0xF7, 0xE8, 0x34,
                                              0x32, 0x12, 0x00, 0x3B, 0x05, 0xE2, 0xF6, 0x6C, 0x02};
    static const HookRip filter_rip[] = {{8, 12}, {14, 18}};
    o_stash_filter = (save_filter_fn)install_hook_rip(
        "Estate::System::Save()::$_0",
        sym("_ZNSt3__110__function6__funcIZN6Estate6System4SaveEvE3$_0NS_9allocatorIS4_EEFbRK"
            "N9Inventory4ItemEEEclESA_"),
        (void *)h_stash_filter, filter_prologue, sizeof filter_prologue, filter_rip, 2);
    if (!o_stash_filter)
        logf_("item types: the stash's save filter isn't hooked, registered types won't be saved there");
    g_audio_event = (void (*)(const char *, bool, void *))sym("_Z11Audio_EventPKcbNSt3__18functionIFvS0_EEE");
    // push rbp; mov rbp,rsp; push r15; push r14; push rbx; sub rsp,0x78; mov rbx,rdi
    static const uint8_t sfx_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                           0x53, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x89, 0xFB};
    if (g_audio_event)
        o_take_sfx = (take_sfx_fn)install_hook("InventorySlotInterface::PlayTakeSfx",
                                               sym("_ZN22InventorySlotInterface11PlayTakeSfxEv"), (void *)h_take_sfx,
                                               sfx_prologue, sizeof sfx_prologue);
    if (!o_take_sfx)
        logf_("item types: PlayTakeSfx isn't hooked, registered types play no pickup sound");
    static const uint8_t drop_prologue[] = PROLOGUE_SUB32(0xe8, 0x05); // sub rsp,0x5e8
    o_slot_drop = (uint64_t (*)(void *, const void *, void *))install_hook(
        "EquipmentSlotInterface::OnDrop", sym("_ZN22EquipmentSlotInterface6OnDropERK4vec2P8DragData"),
        (void *)h_slot_drop, drop_prologue, sizeof drop_prologue);
    unsigned joined = 0;
    for (int i = 0; i < g_ntypes; i++)
        joined |= g_types[i].joins;
    for (size_t i = 0; i < sizeof g_sites / sizeof g_sites[0]; i++) {
        const TypeSite *t = &g_sites[i];
#ifdef _WIN32
        uint8_t *fn = (t->check & joined) ? sym(g_win_sites[i]) : NULL; // the site itself
        if (fn)
            install_cmp_site(t->name, fn, t->bytes, t->len, item_type_site, (void *)t);
#else
        uint8_t *fn = (t->check & joined) ? sym(t->symbol) : NULL;
        if (fn)
            install_cmp_site(t->name, fn + t->offset, t->bytes, t->len, item_type_site, (void *)t);
#endif
        else if (t->check & joined)
            logf_("item types: %s: symbol not found, site off", t->name);
    }
    logf_("item types: %d registered", g_ntypes);
}
#endif
