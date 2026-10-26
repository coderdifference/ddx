// Who may use an item, in both places the game asks.
// Expedition: DungeonController::CanHeroUseItem(hero, type hash, id hash) hard-codes the vanilla rules per item id (dog
// treats: Houndmaster, bandage: bleeding, ...); a mod item has none, so any hero could use it. Its callers
// (CanUsePartyInventoryItem, ControlledHeroCanUseItem) build the CanUseItemResult (byte 0: applies, byte 1: usable) from
// the bool with the controlled hero.
// Town: inventory items the character panel's hero can't use are drawn darkened. That's the realm inventory's validity
// lambda (TownUI::Panel::RealmInventory::PreChildrenShowInternal()::$_2, a std::function operator()(Item const*&&,
// ItemClass const*&&) returning ItemValidResult by sret: valid bool at +0x40), which checks the character panel's hero
// (TownUI::Screen::TownDisplay::GetCharacterPanelHero) against a trinket's class requirements and leaves every other
// item valid. With no character sheet open it has no hero and leaves everything valid; a town hero fallback
// (items_town_hero) answers GetCharacterPanelHero inside the lambda instead, so the stash darkens what that hero can't
// use, vanilla trinkets by the game's own rules too.
// This module owns the three functions. The answer is the game's, then a registered item type's can_use
// (item_types.c), then each rule (items_on_use) in turn; in an expedition, last, the class configs' rules: for an item
// listed in a class config's "items", only heroes of that class whose conditions hold may, and not during a battle if
// the item says "combat": false.
//
// Item drags (item_drag_*): a drag's DragData is an InventoryItemDragData (the party inventory, loot), an
// EquipmentDragData (from a hero's slot: plus the hero) or a RealmInventoryItemDragData (the stash). Each class has its
// own type id (DragData vtable slot 0 GetTypeId, against <class>::GetTypeIdStatic()::type_id once its guard is set):
// the bit a DropContext's accept mask uses. All three are InventoryItemDragData underneath (GetItem, SetItem,
// GetSlotIndex). The current drag: InputManager+0x1b0 (the DragObject), its DragData* at +8, only while
// InputManager::IsDragging (a finished drag's object stays there). A target's answer is
// SetItem on the drag: the item it hands back (the source puts it where the dragged item was), or a cleared copy of the
// dragged item (the source removes it).
// item_store: AddCopyOfItem into the party inventory (Raid+0x220, on an expedition, if GetIsRoomForItem) or the estate's
// trinket storage (Estate::System::GetTrinketStorage(Campaign::Current()+0xe20, 0)).
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// cleared, 0x40 the answer

typedef bool (*can_use_fn)(const void *self, const void *hero, unsigned type_hash, unsigned id_hash);
static can_use_fn g_orig;
static void **g_raid;                         // &Raid::s_Instance
static bool (*g_in_battle)(const void *raid); // Raid::IsInBattle
static ItemUseRule g_rules[4];
static int g_nrules;
static ModsList g_rules_mods; // their mods (only an applied mod's rule; the top mod's last word)
#define MAX_TOWN_HEROES 4
static const void *(*g_town_heroes[MAX_TOWN_HEROES])(void);
static int g_ntown_heroes;
static ModsList g_town_heroes_mods; // their mods: the top mod's answer first

void items_on_use(ItemUseRule rule) {
    if (g_nrules < (int)(sizeof g_rules / sizeof g_rules[0]) && mods_list_add(&g_rules_mods) >= 0)
        g_rules[g_nrules++] = rule;
    else
        logf_("items: too many item use rules");
}

void items_town_hero(const void *(*fallback)(void)) {
    if (fallback && g_ntown_heroes < MAX_TOWN_HEROES && mods_list_add(&g_town_heroes_mods) >= 0)
        g_town_heroes[g_ntown_heroes++] = fallback;
}

// the town hero fallbacks' answer (NULL: none, or none of their mods applied)
static const void *town_hero(void) {
    MODS_EACH(g_town_heroes_mods, true, i) {
        const void *h = g_town_heroes[i]();
        if (h)
            return h;
    }
    return NULL;
}

static void **g_town_display;                                 // &TownUI::Screen::TownDisplay::s_Instance
static const void *(*g_panel_hero)(const void *town_display); // TownDisplay::GetCharacterPanelHero (the original)

const void *items_active_hero(void) {
    const void *hero = NULL;
    if (!g_town_display)
        g_town_display = sym("_ZN6TownUI6Screen11TownDisplay10s_InstanceE");
    if (!g_panel_hero)
        g_panel_hero = (const void *(*)(const void *))sym("_ZNK6TownUI6Screen11TownDisplay21GetCharacterPanelHeroEv");
    if (g_town_display && *g_town_display && g_panel_hero)
        hero = g_panel_hero(*g_town_display);
    return hero ? hero : town_hero();
}

static bool decide(const ItemUseQuery *q, bool ok) {
    const ItemTypeDef *def = item_type_find(q->type_hash);
    if (def && def->can_use)
        ok = def->can_use(q, ok, def->ud);
    MODS_EACH(g_rules_mods, false, i) ok = g_rules[i](q, ok);
    return ok;
}

static bool hook(const void *self, const void *hero, unsigned type_hash, unsigned id_hash) {
    bool ok = g_orig(self, hero, type_hash, id_hash);
    if (!hero)
        return ok;
    void *raid = g_raid ? *g_raid : NULL;
    bool in_battle = raid && g_in_battle && g_in_battle(raid);
    ok = decide(&(ItemUseQuery){.hero = hero, .type_hash = type_hash, .id_hash = id_hash, .in_battle = in_battle}, ok);
    return ok && config_item_allowed(hero, id_hash, in_battle) != 0;
}

static bool g_in_town_valid; // inside the town validity lambda

static const void *h_panel_hero(const void *town_display) {
    const void *hero = g_panel_hero(town_display);
    return hero || !g_in_town_valid ? hero : town_hero();
}

// The lambda returns ItemValidResult through a hidden pointer: first on macOS, after this (the closure) with MSVC.
#ifdef _WIN32
static void *(*o_town_valid)(void *self, uint8_t *out, const uint8_t *const *item, const void *const *cls);
static void *h_town_valid(void *self, uint8_t *out, const uint8_t *const *item, const void *const *cls) {
#else
static void *(*o_town_valid)(uint8_t *out, void *self, const uint8_t *const *item, const void *const *cls);
static void *h_town_valid(uint8_t *out, void *self, const uint8_t *const *item, const void *const *cls) {
#endif
    // The vanilla lambda looks up the trinket class of every item with a panel hero (Item::GetTrinketClass, else
    // Trinket::Library::GetTrinketClass(ItemClass+0x84)) and reads +0x278 unchecked: NULL for a registered type, so
    // those never reach it. Their answer starts at true and is the type's can_use.
    if (item && *item && item_type_find(*(const unsigned *)(*item + ITEM_TYPE_HASH))) {
        out[0] = out[0x3f] = 0;
        out[VALID_RESULT_OK] = 1;
        g_in_town_valid = true;
        const void *hero = g_town_display && *g_town_display && g_panel_hero ? h_panel_hero(*g_town_display) : NULL;
        g_in_town_valid = false;
        if (hero && is_hero(hero))
            out[VALID_RESULT_OK] = decide(&(ItemUseQuery){.hero = hero,
                                                          .item = *item,
                                                          .type_hash = *(const unsigned *)(*item + ITEM_TYPE_HASH),
                                                          .id_hash = *(const unsigned *)(*item + ITEM_ID_HASH),
                                                          .in_town = true},
                                          true);
        return out;
    }
    g_in_town_valid = true;
#ifdef _WIN32
    void *r = o_town_valid(self, out, item, cls);
#else
    void *r = o_town_valid(out, self, item, cls);
#endif
    const void *hero = g_town_display && *g_town_display && g_panel_hero ? h_panel_hero(*g_town_display) : NULL;
    g_in_town_valid = false;
    if (hero && is_hero(hero) && item && *item)
        out[VALID_RESULT_OK] = decide(&(ItemUseQuery){.hero = hero,
                                                      .item = *item,
                                                      .type_hash = *(const unsigned *)(*item + ITEM_TYPE_HASH),
                                                      .id_hash = *(const unsigned *)(*item + ITEM_ID_HASH),
                                                      .in_town = true},
                                      out[VALID_RESULT_OK]);
    return r;
}

// ---------------------------------------------------------------- item drags
#define ITEM_BUFFER 0x600        // Inventory::Item is 0x5b0 bytes; room to spare
static const unsigned *g_tid[3]; // inventory, equipment, stash drag data type ids
static const uint8_t *g_tid_guard[3];
static const uint8_t *const *g_input;                  // &InputManager::s_Instance
static bool (*g_dragging)(const void *input);          // InputManager::IsDragging
static const uint8_t *(*g_drag_get)(const void *drag); // InventoryItemDragData::GetItem
static void (*g_drag_set)(void *drag, const void *item, unsigned slot);
static unsigned (*g_drag_slot)(const void *drag);
static void (*g_item_copy)(void *dst, const void *src);
static void (*g_item_dtor)(void *item);
static void (*g_item_clear)(void *item);
static void *(*g_campaign)(void);
static void *(*g_trinket_storage)(void *estate, int type);
static bool (*g_has_room)(const void *inventory, const void *item);
static void (*g_add_copy)(void *inventory, const void *item);

static bool drag_ready(void) {
    static int state; // 0 not looked up, 1 ready, -1 missing
    if (state)
        return state > 0;
    static const char *const tids[3][2] = {{"_ZZN21InventoryItemDragData15GetTypeIdStaticEvE7type_id",
                                            "_ZGVZN21InventoryItemDragData15GetTypeIdStaticEvE7type_id"},
                                           {"_ZZN17EquipmentDragData15GetTypeIdStaticEvE7type_id",
                                            "_ZGVZN17EquipmentDragData15GetTypeIdStaticEvE7type_id"},
                                           {"_ZZN26RealmInventoryItemDragData15GetTypeIdStaticEvE7type_id",
                                            "_ZGVZN26RealmInventoryItemDragData15GetTypeIdStaticEvE7type_id"}};
    state = 1;
#ifdef _WIN32
    // The statics aren't separate symbols there: each class's GetTypeIdStatic() returns its type id (set up on the first
    // call, as the game's own GetTypeId does).
    static const char *const fns[3] = {"win:InventoryItemDragData::GetTypeIdStatic",
                                       "win:EquipmentDragData::GetTypeIdStatic",
                                       "win:RealmInventoryItemDragData::GetTypeIdStatic"};
    static const uint8_t ready = 1;
    (void)tids;
    for (int i = 0; i < 3; i++) {
        const unsigned *(*fn)(void) = (const unsigned *(*)(void))sym(fns[i]);
        g_tid[i] = fn ? fn() : NULL;
        g_tid_guard[i] = &ready;
        if (!g_tid[i])
            state = -1;
    }
#else
    for (int i = 0; i < 3; i++) {
        g_tid[i] = sym(tids[i][0]);
        g_tid_guard[i] = sym(tids[i][1]);
        if (!g_tid[i] || !g_tid_guard[i])
            state = -1;
    }
#endif
    g_input = sym("_ZN12InputManager10s_InstanceE");
    g_dragging = (bool (*)(const void *))sym("_ZNK12InputManager10IsDraggingEv");
    g_drag_get = (const uint8_t *(*)(const void *))sym("_ZNK21InventoryItemDragData7GetItemEv");
    g_drag_set =
        (void (*)(void *, const void *, unsigned))sym("_ZN21InventoryItemDragData7SetItemEPKN9Inventory4ItemEj");
    g_drag_slot = (unsigned (*)(const void *))sym("_ZNK21InventoryItemDragData12GetSlotIndexEv");
    g_item_copy = (void (*)(void *, const void *))sym("_ZN9Inventory4ItemC2ERKS0_");
    g_item_dtor = (void (*)(void *))sym("_ZN9Inventory4ItemD1Ev");
    g_item_clear = (void (*)(void *))sym("_ZN9Inventory4Item5ClearEv");
    g_campaign = (void *(*)(void))sym("_ZN8Campaign7CurrentEv");
    g_trinket_storage = (void *(*)(void *, int))sym("_ZN6Estate6System17GetTrinketStorageENS0_19ETrinketStorageTypeE");
    g_has_room = (bool (*)(const void *, const void *))sym("_ZNK9Inventory6System16GetIsRoomForItemEPKNS_4ItemE");
    g_add_copy = (void (*)(void *, const void *))sym("_ZN9Inventory6System13AddCopyOfItemEPKNS_4ItemE");
    if (!g_input || !g_dragging || !g_drag_get || !g_drag_set || !g_drag_slot || !g_item_copy || !g_item_dtor ||
        !g_item_clear || !g_campaign || !g_trinket_storage || !g_has_room || !g_add_copy || !g_raid)
        state = -1;
    if (state < 0)
        logf_("items: item drag symbols missing, item drags unknown");
    return state > 0;
}

unsigned item_drag_kind(const void *drag) {
    if (!drag || !drag_ready())
        return 0;
    const unsigned *t = ((const unsigned *(*)(const void *))(*(void *const *const *)drag)[0])(drag);
    for (int i = 0; t && i < 3; i++)
        if (*g_tid_guard[i] && *t == *g_tid[i])
            return 1u << i;
    return 0;
}

const uint8_t *item_drag_item(const void *drag) {
    return item_drag_kind(drag) ? g_drag_get(drag) : NULL;
}

uint32_t item_drag_mask(unsigned kinds) {
    uint32_t m = 0;
    for (int i = 0; i < 3 && drag_ready(); i++)
        if ((kinds & (1u << i)) && *g_tid_guard[i] && *g_tid[i] < 32)
            m |= 1u << *g_tid[i];
    return m;
}

void *item_drag_current(void) {
    const uint8_t *im = drag_ready() ? *g_input : NULL;
    const uint8_t *object = im && g_dragging(im) ? *(const uint8_t *const *)(im + INPUT_DRAG) : NULL;
    return object ? *(void *const *)(object + 8) : NULL;
}

void item_drag_hand_back(void *drag, const uint8_t *item) {
    if (!item_drag_kind(drag))
        return;
    if (item) {
        g_drag_set(drag, item, g_drag_slot(drag));
        return;
    }
    void *empty = malloc(ITEM_BUFFER); // 16-aligned on both platforms
    if (!empty)
        return;
    memset(empty, 0, ITEM_BUFFER);
    g_item_copy(empty, g_drag_get(drag));
    g_item_clear(empty);
    g_drag_set(drag, empty, g_drag_slot(drag));
    g_item_dtor(empty);
    free(empty);
}

const char *item_store(const uint8_t *item) {
    if (!item || !drag_ready())
        return NULL;
    if (*g_raid) {
        void *party = (uint8_t *)*g_raid + RAID_PARTY_INVENTORY;
        if (g_has_room(party, item)) {
            g_add_copy(party, item);
            return "the party inventory";
        }
    }
    void *campaign = g_campaign();
    void *estate = campaign ? g_trinket_storage((uint8_t *)campaign + CAMPAIGN_ESTATE, 0) : NULL;
    if (!estate)
        return NULL;
    g_add_copy(estate, item);
    return "the estate";
}

// ---------------------------------------------------------------- an item's name and colour (items_on_display_name)
#define MAX_NAMERS 4
static ItemDisplayName g_namers[MAX_NAMERS];
static int g_nnamers;
static ModsList g_namers_mods;                             // their mods: the top mod's name first
static GameLocString (*g_item_title)(const uint8_t *item); // Inventory::Display::GetTitle

void items_on_display_name(ItemDisplayName fn) {
    if (fn && g_nnamers < MAX_NAMERS && mods_list_add(&g_namers_mods) >= 0)
        g_namers[g_nnamers++] = fn;
}

void items_display_name(const uint8_t *item, char *name, size_t cap, char *colour, size_t colour_cap) {
    if (!item || !cap || !colour_cap)
        return;
    MODS_EACH(g_namers_mods, true, i) if (g_namers[i](item, name, cap, colour, colour_cap)) return;
    if (!g_item_title)
        g_item_title = (GameLocString (*)(const uint8_t *))sym("_ZN9Inventory7Display8GetTitleERKNS_4ItemE");
    GameLocString t = g_item_title ? g_item_title(item) : (GameLocString){NULL, 0};
    snprintf(name, cap, "%s", t.text && t.text[0] ? t.text : (const char *)item + ITEM_ID);
    snprintf(colour, colour_cap, "%s", "white");
}

void items_install(void) {
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    g_in_battle = (bool (*)(const void *))sym("_ZNK4Raid10IsInBattleEv");
    static const uint8_t prologue[] = PROLOGUE_SUB8(0x28); // sub rsp,0x28
    g_orig = (can_use_fn)install_hook("DungeonController::CanHeroUseItem",
                                      sym("_ZNK17DungeonController14CanHeroUseItemEPK4Herojj"), (void *)hook, prologue,
                                      sizeof prologue);
    g_town_display = sym("_ZN6TownUI6Screen11TownDisplay10s_InstanceE");
    void *panel_hero = sym("_ZNK6TownUI6Screen11TownDisplay21GetCharacterPanelHeroEv");
    if (!g_nrules && !g_ntown_heroes && !item_types_count())
        return; // nothing changes a town answer
#ifdef _WIN32
    // Inlined everywhere on Windows: the stand-in reads the panel; the fallback hero answers only our own checks
    if (g_ntown_heroes)
        logf_("items: town hero fallbacks apply to the library's checks only (Windows)");
    g_panel_hero = (const void *(*)(const void *))panel_hero;
#else
    // push rbp; mov rbp,rsp; push rbx; push rax; mov rbx,rdi; add rbx,0x32f0
    static const uint8_t panel_hero_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48, 0x89,
                                                  0xFB, 0x48, 0x81, 0xC3, 0xF0, 0x32, 0x00, 0x00};
    if (g_ntown_heroes && panel_hero) // only needed for a fallback
        *(void **)&g_panel_hero = install_hook("TownUI::Screen::TownDisplay::GetCharacterPanelHero", panel_hero,
                                               (void *)h_panel_hero, panel_hero_prologue, sizeof panel_hero_prologue);
    else
        g_panel_hero = (const void *(*)(const void *))panel_hero;
#endif
    // push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; push rax
    static const uint8_t town_valid_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41,
                                                  0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
    *(void **)&o_town_valid = install_hook(
        "RealmInventory item validity",
        sym("_ZNSt3__110__function6__funcIZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEvE3$_2NS_"
            "9allocatorIS5_EEFN9Inventory7Display15ItemValidResultEPKNS8_4ItemEPKNS8_9ItemClassEEEclEOSD_OSG_"),
        (void *)h_town_valid, town_valid_prologue, sizeof town_valid_prologue);
}
