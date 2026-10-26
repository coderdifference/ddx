// Equipment slots: a hero's equipment drawn as slots placed by a layout file, in place of the game's own drawing
// (design/docs/15-equipment-slots.md, docs/11-ui-library.md "Equipment slots"). See common.h ("equip_slots.c").
//
// Placements: HeroDisplay::App::ShowTrinkets(Hero const&, InventorySystemInterface*, vec2, std::function<ItemValidResult
// (Item const*, ItemClass const*)>, std::function<void(unsigned)> x2, u64, unsigned) draws the hero's trinket inventory
// with Inventory::Display::InventoryButtonGrid, from three callers, told apart by the return address: the character
// sheet (UI::Panel::CharacterDisplay::ShowEquipment), the expedition hero panel (Panel_Hero::ShowSelectedHero) and the
// embark screen's quick trinket select (TownUI::Panel::PartySetupDisplay::ShowQuickSelectTrinkets). For a placement
// the layout has slots for, the hook draws them instead (the function only draws; its std::function arguments are the
// caller's temporaries); otherwise the game's grid.
//
// The trinket kind: slot i is the interface's slot interface i (std::vector<InventorySlotInterface*> at +0x10), an
// EquipmentSlotInterface: a DragContext at +0, a DropContext at +0x10, the item (Item*) at +0x30, the highlight type at
// +0xbc (InventorySlotInterface::GetHighlightType: 1 valid, 2 invalid, set by its OnDragStarted while an item is
// dragged). The slot draws itself and registers the game's object as its drag and drop contexts for its rect, so the
// game's own rules apply (CanEquipTrinket, class requirements, limits, battle locks, swaps, sounds, trinket buffs).
//
// Other kinds (EquipSlotKind, registered by a client): each laid-out slot gets a KindSlot, a DragContext of ours (a
// press starts a drag) and a UiDropTarget (ui_draw.c), registered for its rect with ui_contexts. As the game's
// EquipmentSlotInterface: the DragContext (vtable; +0x08 the drag object's type id, EquipmentDragObject's, so the
// factory makes an EquipmentDragObject with an EquipmentDragData; +0x0c enabled) gets OnPickedUp(pos, object, data):
// InventoryItemDragData::SetItem(data, item, index), EquipmentDragData::SetHero, the trinket unequip sound; then one
// of OnDropRejected, OnDropAccepted (the target took it: the drag's item now is what it handed back, so a different
// valid item goes in with set if the kind accepts it, and the slot empties otherwise; an item handed back that it
// doesn't accept goes to item_store) or OnDropInvalid. Base DragContext vtable: 0-1 destructors, 2 OnPickedUp, 3
// OnDropRejected, 4 OnDropAccepted, 5 OnDropInvalid, 6 GetInternalName. The drop target takes item drags of every
// kind: dropped back on its own slot 2, refused 0 unless the kind accepts it and set takes it; then the slot's old
// item (or a cleared copy: removed) is handed back to the source (item_drag_hand_back), as a trinket slot does.
// An item handed back counts as nothing back when it has the type, id and item id (itemdata_id) of the slot's own.
//
// The game's weapon and armour (HeroDisplay::App::ShowEquipment(Hero const&, vec2), called by the same callers just
// before ShowTrinkets) are skipped where the placement has slots: they're among the layout's slots, placed relative to
// the trinkets. Images take the GUI's current colour (GUI_Colour), and the game's rows end by setting it to white
// (GUI_Colour(rgba 0xffffffff)) after the sheet's gold title; so the slots set white first, as the rows would have. A client
// may watch which hero's equipment is shown (equip_slots_on_show). The embark quick select (ShowQuickSelectTrinkets) is
// the Arena's party setup only (dlc/1117860_arena_mp); the campaign's placements are the sheet and the expedition panel.
//
// Drawing a slot: the empty picture (when empty or while its item is dragged), else the rarity frame
// (Item::GetTrinketClass()+0x324, the rarity's hash, -> UI::CampaignAssets::Instance::GetTrinketRarity: its texture
// handle at +8) under the icon (Inventory::Display::GetIconTexture(Item const*), a counted handle by sret); the
// highlight (overlays/eqp_mouseover.png, eqp_unavailable_mouseover.png: 118 x 187 for a 72 x 144 icon, placed by
// UI::CampaignAssets+0xec8 as Inventory::Display::ShowInventoryItemHighlight does); the item's tooltip on hover. The
// pictures, the highlight's placing and the tooltip's offset are settings: the slot's, else its placement's, else the
// layout's, else the game's.
//
// Windows (MSVC): the same arguments in registers (pos by value: r8 for ShowTrinkets, rdx for ShowEquipment), but
// ShowTrinkets destroys its three std::functions, so a skipped call destroys them in its place. The small getters are
// inlined there (stand-ins in syms_win.c), EquipmentDragObject's type id comes from its GetTypeIdStatic (set up on the
// first call, as items.c's drag data ids), and the DragContext vtable has one destructor (OnPickedUp is slot 1).
#include "common.h"
#include <stdio.h>
#include <string.h>

#define MAX_SLOTS 16

typedef enum { PLACE_SHEET, PLACE_EXPEDITION, PLACE_EMBARK, PLACE_COUNT } Placement;
static const char *const g_place_names[PLACE_COUNT] = {"sheet", "expedition", "embark"};
static const struct {
    const char *symbol;
    uint32_t size;
} g_callers[PLACE_COUNT] = {
    {"_ZN2UI5Panel16CharacterDisplay13ShowEquipmentE4vec2P4HeroPK9HeroClassb", GL(0x7a0, 0x4ad)},
    {"_ZN10Panel_Hero16ShowSelectedHeroEv", GL(0x8c0, 0x650)},
    {"_ZN6TownUI5Panel17PartySetupDisplay23ShowQuickSelectTrinketsE4vec2P4HeroS2_", GL(0x810, 0x4d4)},
};
static uintptr_t g_caller[PLACE_COUNT];

// Mods follow the campaign: each layout, kind, active test and observer is its registering mod's (mods_current) and
// counts only while the campaign applies that mod. The layout is the top applied mod's (g_layout / g_key, picked
// again at each campaign load and close; none: the game's drawing); the layout applies while every applied mod's
// active test says so.
#define MAX_CLIENTS 4
static struct {
    UiLayout *l;
    char key[48];
} g_layouts[MAX_CLIENTS];
static int g_nlayouts;
static ModsList g_layouts_mods;
static UiLayout *g_layout; // the one in use
static const char *g_key = "";
#define MAX_KINDS 8
static EquipSlotKind g_kinds[MAX_KINDS];
static int g_nkinds;
static ModsList g_kinds_mods;
static bool (*g_actives[MAX_CLIENTS])(void);
static int g_nactives;
static ModsList g_actives_mods;
static void (*g_shown[MAX_CLIENTS])(const void *hero);
static int g_nshown;
static ModsList g_shown_mods;

static void pick_layout(void) {
    int i = mods_list_first(&g_layouts_mods);
    g_layout = i >= 0 ? g_layouts[i].l : NULL;
    g_key = i >= 0 ? g_layouts[i].key : "";
}

void equip_slots_layout(UiLayout *l, const char *key) {
    if (!l || g_nlayouts >= MAX_CLIENTS || mods_list_add(&g_layouts_mods) < 0) {
        logf_("equip slots: layout refused");
        return;
    }
    g_layouts[g_nlayouts].l = l;
    snprintf(g_layouts[g_nlayouts++].key, sizeof g_layouts[0].key, "%s", key ? key : "");
    static bool watching;
    if (!watching) {
        watching = true;
        mods_on_campaign(pick_layout);
    }
    pick_layout();
}

bool equip_slot_kind_register(const EquipSlotKind *kind) {
    if (!kind || !kind->id || !kind->item || !kind->accepts || !kind->set || g_nkinds >= MAX_KINDS ||
        mods_list_add(&g_kinds_mods) < 0) {
        logf_("equip slots: kind %s not registered", kind && kind->id ? kind->id : "(none)");
        return false;
    }
    g_kinds[g_nkinds++] = *kind;
    return true;
}

void equip_slots_active(bool (*active)(void)) {
    if (active && g_nactives < MAX_CLIENTS && mods_list_add(&g_actives_mods) >= 0)
        g_actives[g_nactives++] = active;
}
void equip_slots_on_show(void (*shown)(const void *hero)) {
    if (shown && g_nshown < MAX_CLIENTS && mods_list_add(&g_shown_mods) >= 0)
        g_shown[g_nshown++] = shown;
}

static bool layout_active(void) {
    MODS_EACH(g_actives_mods, false, i) if (!g_actives[i]()) return false;
    return true;
}

int equip_slots_items(const void *hero, const uint8_t **out, int max) {
    int n = 0;
    if (hero)
        MODS_EACH(g_kinds_mods, false, k)
    for (int i = 0; i < (g_kinds[k].count > 0 ? g_kinds[k].count : 1) && n < max; i++) {
        const uint8_t *item = g_kinds[k].item(hero, i, g_kinds[k].ud);
        if (item)
            out[n++] = item;
    }
    return n;
}

int equip_slots_unequip(void *hero, void *into, bool (*want)(const uint8_t *item, void *ud), void *ud) {
    static void (*add_copy)(void *inventory, const void *item);           // Inventory::System::AddCopyOfItem
    static void (*copy)(void *dst, const void *src), (*dtor)(void *item); // Inventory::Item's copy and destructor
    if (!add_copy) {
        add_copy = (void (*)(void *, const void *))sym("_ZN9Inventory6System13AddCopyOfItemEPKNS_4ItemE");
        copy = (void (*)(void *, const void *))sym("_ZN9Inventory4ItemC2ERKS0_");
        dtor = (void (*)(void *))sym("_ZN9Inventory4ItemD1Ev");
    }
    int n = 0;
    if (hero && into && add_copy && copy && dtor)
        MODS_EACH(g_kinds_mods, false, k) {
            const EquipSlotKind *kd = &g_kinds[k];
            for (int i = 0; i < (kd->count > 0 ? kd->count : 1); i++) {
                const uint8_t *item = kd->item(hero, i, kd->ud);
                if (!item || (want && !want(item, ud)) || (kd->movable && !kd->movable(hero, i, kd->ud)))
                    continue;
                _Alignas(16)
                    uint8_t held[0x600]; // Inventory::Item is 0x5b0 bytes; the kind's may go when the slot empties
                copy(held, item);
                if (kd->set(hero, i, NULL, kd->ud)) {
                    add_copy(into, held);
                    n++;
                }
                dtor(held);
            }
        }
    return n;
}

static const EquipSlotKind *kind_find(const char *id) {
    MODS_EACH(g_kinds_mods, true, i) if (!strcmp(g_kinds[i].id, id)) return &g_kinds[i];
    return NULL;
}

typedef void (*show_trinkets_fn)(const void *hero, void *iface, Vec2 pos, void *valid, void *f2, void *f3, uint64_t a,
                                 unsigned b);
static show_trinkets_fn o_show_trinkets;
static const uint8_t *(*g_slot_item)(const void *slot);               // InventorySlotInterface::GetItem
static bool (*g_picked_up)(const void *slot);                         // InventorySlotInterface::IsPickedUp
static int (*g_highlight_type)(const void *slot);                     // InventorySlotInterface::GetHighlightType
static bool (*g_item_valid)(const void *item);                        // Inventory::Item::IsValid
static void *(*g_icon)(void *out, const void *item);                  // Inventory::Display::GetIconTexture(Item const*)
static const uint8_t *(*g_trinket_class)(const void *item);           // Inventory::Item::GetTrinketClass
static void *(*g_assets)(void);                                       // UI::CampaignAssets::Current
static const uint8_t *(*g_rarity)(const void *assets, unsigned hash); // CampaignAssets::Instance::GetTrinketRarity
static bool (*g_dragging)(const void *input);                         // InputManager::IsDragging
static void **g_input;                                                // &InputManager::s_Instance
static void **g_rm;                                                   // &ResourceManager::s_Instance
static void (*g_rm_release)(void *rm, const void *handle);
static unsigned g_trinket_hash;
static void (*g_gui_colour)(uint32_t rgba);                             // GUI_Colour(rgba)
static void (*g_drag_set)(void *data, const void *item, unsigned slot); // InventoryItemDragData::SetItem
static void (*g_drag_hero)(void *data, void *hero);                     // EquipmentDragData::SetHero
static void (*g_audio_event)(const char *name, bool, void *callback);   // Audio_Event(char const*, bool, std::function)
static const unsigned *g_drag_object_tid; // EquipmentDragObject::GetTypeIdStatic()::type_id
static const uint8_t *g_drag_object_guard;

static void release(const uint8_t h[16]) {
    const int *n = (const int *)h;
    if (g_rm && *g_rm && g_rm_release && n[0] >= 0 && n[1] >= 0)
        g_rm_release(*g_rm, h);
}

static Placement placement_of(uintptr_t ra) {
    for (int p = 0; p < PLACE_COUNT; p++)
        if (g_caller[p] && ra >= g_caller[p] && ra < g_caller[p] + g_callers[p].size)
            return (Placement)p;
    return PLACE_COUNT;
}

// What a slot shows this frame.
typedef struct {
    const uint8_t *item; // NULL: empty
    bool picked;         // its item is being dragged
    int highlight;       // 0 none, 1 valid, 2 invalid
    void *drag, *drop;   // the contexts for its rect
} SlotState;

static bool trinket_state(void *iface, int index, SlotState *s) {
    void *const *begin = *(void *const *const *)((uint8_t *)iface + IFACE_SLOTS);
    void *const *end = *(void *const *const *)((uint8_t *)iface + IFACE_SLOTS + 8);
    if (index < 0 || index >= end - begin || !begin[index])
        return false;
    void *slot = begin[index];
    const uint8_t *item = g_slot_item(slot);
    s->item = item && g_item_valid(item) ? item : NULL;
    s->picked = g_picked_up(slot);
    s->highlight = g_highlight_type(slot);
    s->drag = slot;
    s->drop = (uint8_t *)slot + SLOT_DROP;
    return true;
}

static bool texture(const char *path, uint8_t out[16]) {
    return path && *path && ui_texture(path, out);
}

// A slot's setting (field: "empty", "highlight.valid", ...): the slot's own, else its placement's, else the layout's
// (the key's), else NULL (the default, quietly).
static const char *setting(const char *path, Placement p, const char *field, char *name, size_t cap) {
    snprintf(name, cap, "%s.%s", path, field);
    if (ui_layout_has(g_layout, name))
        return name;
    snprintf(name, cap, "%s.%s.%s", g_key, g_place_names[p], field);
    if (ui_layout_has(g_layout, name))
        return name;
    snprintf(name, cap, "%s.%s", g_key, field);
    return ui_layout_has(g_layout, name) ? name : NULL;
}
static const char *opt_str(const char *path, Placement p, const char *field, const char *def) {
    char name[192];
    return setting(path, p, field, name, sizeof name) ? ui_layout_str(g_layout, name, def) : def;
}
static Vec2 opt_vec2(const char *path, Placement p, const char *field, Vec2 def) {
    char name[192];
    return setting(path, p, field, name, sizeof name) ? ui_layout_vec2(g_layout, name, def) : def;
}

// a registered type's item through item_draw (its icon and layers); a trinket as the game's grid draws it
static void draw_item(const uint8_t *item, const void *hero, Vec2 pos, Vec2 size) {
    if (item_type_find(*(const unsigned *)(item + ITEM_TYPE_HASH))) {
        item_draw(item, hero, pos, size, ITEM_DRAW_SLOT);
        return;
    }
    // the icon first: a client's icon may decide the frame the game hands out next (GetTrinketRarity)
    _Alignas(16) uint8_t icon[16];
    memset(icon, 0, sizeof icon);
    *(int64_t *)icon = (int64_t)0xFFFFFFFF00000000;
    g_icon(icon, item);
    if (*(const unsigned *)(item + ITEM_TYPE_HASH) == g_trinket_hash && g_trinket_class && g_rarity && g_assets) {
        const uint8_t *cls = g_trinket_class(item);
        void *assets = cls ? g_assets() : NULL;
        const uint8_t *r = assets ? g_rarity(assets, *(const unsigned *)(cls + TRINKETCLASS_RARITY)) : NULL;
        if (r && ((const int *)(r + RARITY_TEXTURE))[0] >= 0)
            ui_image_sized(pos, r + RARITY_TEXTURE, size);
    }
    if (((const int *)icon)[0] >= 0)
        ui_image_sized(pos, icon, size);
    release(icon);
}

// ---------------------------------------------------------------- slots of registered kinds
typedef struct {
    void **vtbl; // the game's DragContext: vtable, drag object type id, enabled
    unsigned drag_type;
    bool drag_enabled;
    uint8_t pad[3];
    UiDropTarget drop; // ctx: this
    const EquipSlotKind *kind;
    int index;
    const void *hero; // the hero it last drew for
    unsigned guid;
    bool picked;             // its item is being dragged
    signed char drag_logged; // the drag state last logged (0 none yet, 1 off, 2 on)
} KindSlot;
static KindSlot g_kslots[PLACE_COUNT][MAX_SLOTS];

// the hero it drew for, if still that hero
static void *ks_hero(const KindSlot *k) {
    return k->hero && is_hero(k->hero) && hero_guid(k->hero) == k->guid ? (void *)k->hero : NULL;
}
static bool ks_movable(const KindSlot *k, const void *hero) {
    return !k->kind->movable || k->kind->movable(hero, k->index, k->kind->ud);
}

// two items the same one: type, id and item id
static bool same_item(const uint8_t *a, const uint8_t *b) {
    if (!a || !b)
        return false;
    return *(const unsigned *)(a + ITEM_TYPE_HASH) == *(const unsigned *)(b + ITEM_TYPE_HASH) &&
           *(const unsigned *)(a + ITEM_ID_HASH) == *(const unsigned *)(b + ITEM_ID_HASH) &&
           itemdata_id(a) == itemdata_id(b);
}

static void ks_nop(void) {}
static const char *ks_name(const KindSlot *k) {
    (void)k;
    return "EquipSlot";
}

static void ks_picked_up(KindSlot *k, const void *pos, void *object, void *data) {
    (void)pos;
    (void)object;
    void *hero = ks_hero(k);
    const uint8_t *item = hero ? k->kind->item(hero, k->index, k->kind->ud) : NULL;
    if (!item || item_drag_kind(data) != ITEM_DRAG_EQUIPMENT) {
        logf_("equip slots: %s %d picked up with %s, nothing carried", k->kind->id, k->index,
              item ? "an unexpected drag" : "no item");
        return;
    }
    g_drag_set(data, item, (unsigned)k->index);
    g_drag_hero(data, hero);
    k->picked = true;
#ifdef _WIN32
    StdFunction callback = {0}; // an empty std::function (passed by address)
    if (g_audio_event)
        g_audio_event("/ui/dungeon/trink_unequip", true, &callback);
#else
    void *callback[6] = {0}; // an empty std::function (passed by address)
    if (g_audio_event)
        g_audio_event("/ui/dungeon/trink_unequip", true, callback);
#endif
}

static void ks_rejected(KindSlot *k, const void *data) {
    (void)data;
    k->picked = false;
}

static void ks_accepted(KindSlot *k, const void *data) {
    k->picked = false;
    void *hero = ks_hero(k);
    if (!hero) {
        logf_("equip slots: %s %d: its hero is gone, the drop is lost", k->kind->id, k->index);
        return;
    }
    const EquipSlotKind *kd = k->kind;
    const uint8_t *back = item_drag_item(data), *mine = kd->item(hero, k->index, kd->ud);
    if (back && (!g_item_valid(back) || same_item(back, mine)))
        back = NULL; // nothing handed back
    if (back && kd->accepts(hero, k->index, back, kd->ud) != 1) {
        const char *where = item_store(back);
        logf_("equip slots: %s %d was handed back %.64s, which it doesn't take: %s %s", kd->id, k->index,
              (const char *)back + ITEM_ID, where ? "put in" : "lost, no room in", where ? where : "anything");
        back = NULL;
    }
    if (!kd->set(hero, k->index, back, kd->ud))
        logf_("equip slots: %s %d refused %s after the drop", kd->id, k->index,
              back ? "the item handed back" : "emptying");
}

static uint64_t ks_drop(UiDropTarget *t, const void *pos, void *drag) {
    (void)pos;
    KindSlot *k = t->ctx;
    if (k->picked) {
        k->picked = false;
        return 2;
    } // back where it came from
    void *hero = ks_hero(k);
    const uint8_t *item = item_drag_item(drag);
    const EquipSlotKind *kd = k->kind;
    if (!hero || !item || !ks_movable(k, hero) || kd->accepts(hero, k->index, item, kd->ud) != 1)
        return 0;
    // the slot's old item, copied: set replaces what item() handed out
    _Alignas(16) static uint8_t old[0x600];
    static void (*copy)(void *, const void *), (*dtor)(void *);
    if (!copy) {
        copy = (void (*)(void *, const void *))sym("_ZN9Inventory4ItemC2ERKS0_");
        dtor = (void (*)(void *))sym("_ZN9Inventory4ItemD1Ev");
    }
    const uint8_t *mine = kd->item(hero, k->index, kd->ud);
    bool had = mine && copy && dtor;
    if (had) {
        memset(old, 0, sizeof old);
        copy(old, mine);
    }
    bool ok = kd->set(hero, k->index, item, kd->ud);
    if (ok)
        item_drag_hand_back(drag, had ? old : NULL);
    if (had)
        dtor(old);
    return ok ? 1 : 0;
}

#ifdef _WIN32
static void *g_ks_vtbl[8] = {(void *)ks_nop,      (void *)ks_picked_up, (void *)ks_rejected,
                             (void *)ks_accepted, (void *)ks_rejected,  (void *)ks_name,
                             (void *)ks_nop,      (void *)ks_nop}; // one destructor
#else
static void *g_ks_vtbl[8] = {(void *)ks_nop,      (void *)ks_nop,      (void *)ks_picked_up, (void *)ks_rejected,
                             (void *)ks_accepted, (void *)ks_rejected, (void *)ks_name,      (void *)ks_nop};
#endif

static bool kind_state(KindSlot *k, const EquipSlotKind *kd, int index, const void *hero, SlotState *s) {
    if (!k->vtbl) {
        k->vtbl = g_ks_vtbl;
        ui_drop_target_init(&k->drop, ks_drop, k);
    }
    if (k->hero != hero || k->kind != kd || k->index != index)
        k->picked = false; // another hero or slot now
    k->kind = kd;
    k->index = index;
    k->hero = hero;
    k->guid = hero_guid(hero);
    s->item = kd->item(hero, index, kd->ud);
    if (s->item && g_item_valid && !g_item_valid(s->item))
        s->item = NULL;
    if (!s->item)
        k->picked = false;
    s->picked = k->picked;
    bool movable = ks_movable(k, hero);
    // the drag object's type id, once the game has given it one (0 is a real id: ids count up from 0 in the order
    // the types are first asked for, and on Windows the library asks at start-up)
    bool typed = g_drag_object_guard && *g_drag_object_guard;
    k->drag_type = typed ? *g_drag_object_tid : 0;
    k->drag_enabled = s->item && movable && !k->picked && typed;
    if (s->item && k->drag_logged != 1 + k->drag_enabled) { // a slot whose item can't be dragged says why
        k->drag_logged = (signed char)(1 + k->drag_enabled);
        logf_("equip slots: %s %d drag %s (movable %d, picked %d, drag type %s%u)", kd->id, index,
              k->drag_enabled ? "on" : "off", movable, k->picked, typed ? "" : "unknown, ", k->drag_type);
    }
    k->drop.accepts = movable ? item_drag_mask(ITEM_DRAG_ANY) : 0;
    k->drop.enabled = movable;
    s->highlight = 0;
    const uint8_t *dragged = k->picked ? NULL : item_drag_item(item_drag_current());
    if (dragged)
        s->highlight = kd->accepts(hero, index, dragged, kd->ud);
    if (s->highlight == 1 && !movable)
        s->highlight = 2;
    s->drag = k->drag_enabled ? (void *)k : NULL;
    s->drop = movable ? (void *)&k->drop : NULL;
    return true;
}

static void draw_slot(const char *path, Placement p, const void *hero, const SlotState *s, Vec2 pos, Vec2 size) {
    uint8_t tex[16];
    if (!s->item || s->picked) {
        if (texture(opt_str(path, p, "empty", ""), tex))
            ui_image_sized(pos, tex, size);
    } else
        draw_item(s->item, hero, pos, size);
    if (s->highlight == 1 || s->highlight == 2) {
        const char *pic = s->highlight == 1
                              ? opt_str(path, p, "highlight.valid", "overlays/eqp_mouseover.png")
                              : opt_str(path, p, "highlight.invalid", "overlays/eqp_unavailable_mouseover.png");
        void *assets = g_assets ? g_assets() : NULL;
        // the game's offset is from the slot's origin, its icon 4 px right of it (inventory_item_layout .icon_offset
        // 4 0); ours is from the icon
        Vec2 def = assets ? *(const Vec2 *)((const uint8_t *)assets + ASSETS_HIGHLIGHT_OFFSET) : (Vec2){-20, -20};
        def.x -= 4;
        static bool logged;
        if (!logged) {
            logged = true;
            logf_("equip slots: the game's highlight offset is %.0f,%.0f", def.x, def.y);
        }
        Vec2 off = opt_vec2(path, p, "highlight.offset", def);
        Vec2 grow = opt_vec2(path, p, "highlight.grow", (Vec2){46, 43}); // the art's size over the 72 x 144 icon's
        if (texture(pic, tex))
            ui_image_sized((Vec2){pos.x + off.x, pos.y + off.y}, tex, (Vec2){size.x + grow.x, size.y + grow.y});
    }
    ui_contexts(s->drag, s->drop, pos, size);
    bool dragging = g_dragging && g_input && *g_input && g_dragging(*g_input);
    if (s->item && !s->picked && !dragging && ui_mouse_in(pos, size)) {
        Vec2 off = opt_vec2(path, p, "tooltip_offset", (Vec2){85, 0});
        ui_item_tooltip(hero, s->item, (Vec2){pos.x + off.x, pos.y + off.y});
    }
}

// The placement's slots, relative to pos. False: none laid out (the game draws).
static bool laid_out(Placement p, const void *hero, const char **keys, int *n) {
    *n = 0;
    if (!g_layout || p == PLACE_COUNT || !is_hero(hero) || !layout_active())
        return false;
    char name[128];
    snprintf(name, sizeof name, "%s.%s.slots", g_key, g_place_names[p]);
    *n = ui_layout_has(g_layout, name) ? ui_layout_keys(g_layout, name, keys, MAX_SLOTS) : 0;
    return *n > 0;
}

static bool draw_placement(Placement p, const void *hero, void *iface, Vec2 pos) {
    const char *keys[MAX_SLOTS];
    int n;
    if (!laid_out(p, hero, keys, &n))
        return false;
    if (g_gui_colour)
        g_gui_colour(0xffffffffu);
    for (int i = 0; i < n; i++) {
        char path[128], f[160];
        snprintf(path, sizeof path, "%s.%s.slots.%s", g_key, g_place_names[p], keys[i]);
        snprintf(f, sizeof f, "%s.kind", path);
        const char *kind = ui_layout_str(g_layout, f, "trinket");
        snprintf(f, sizeof f, "%s.index", path);
        int index = (int)ui_layout_num(g_layout, f, 0);
        snprintf(f, sizeof f, "%s.pos", path);
        Vec2 at = ui_layout_vec2(g_layout, f, (Vec2){0, 0});
        snprintf(f, sizeof f, "%s.size", path);
        Vec2 size = ui_layout_vec2(g_layout, f, (Vec2){72, 144});
        SlotState s = {0};
        const EquipSlotKind *kd = NULL;
        if (!strcmp(kind, "trinket")) {
            if (!iface || !trinket_state(iface, index, &s))
                continue;
        } else if (!(kd = kind_find(kind)) || !kind_state(&g_kslots[p][i], kd, index, hero, &s))
            continue;
        draw_slot(path, p, hero, &s, (Vec2){pos.x + at.x, pos.y + at.y}, size);
    }
    return true;
}

static void h_show_trinkets(const void *hero, void *iface, Vec2 pos, void *valid, void *f2, void *f3, uint64_t a,
                            unsigned b) {
    Placement p = placement_of((uintptr_t)__builtin_return_address(0));
    static bool logged[PLACE_COUNT + 1];
    bool ours = draw_placement(p, hero, iface, pos);
    if (!logged[p]) {
        logged[p] = true;
        logf_("equip slots: %s at %.0f,%.0f %s", p < PLACE_COUNT ? g_place_names[p] : "(unknown caller)", pos.x, pos.y,
              ours ? "drawn from the layout" : "left to the game");
    }
    if (!ours) {
        o_show_trinkets(hero, iface, pos, valid, f2, f3, a, b);
        return;
    }
#ifdef _WIN32
    ui_function_destroy(valid); // the callee's to destroy (std::functions share MSVC's layout)
    ui_function_destroy(f2);
    ui_function_destroy(f3);
#endif
}

typedef void (*show_equipment_fn)(const void *hero, Vec2 pos);
static show_equipment_fn o_show_equipment;

static void h_show_equipment(const void *hero, Vec2 pos) {
    Placement p = placement_of((uintptr_t)__builtin_return_address(0));
    const char *keys[MAX_SLOTS];
    int n;
    bool ours = laid_out(p, hero, keys, &n);
    static bool logged[PLACE_COUNT + 1];
    if (!logged[p]) {
        logged[p] = true;
        logf_("equip slots: %s weapon and armour at %.0f,%.0f %s",
              p < PLACE_COUNT ? g_place_names[p] : "(unknown caller)", pos.x, pos.y,
              ours ? "left to the layout" : "drawn by the game");
    }
    MODS_EACH(g_shown_mods, false, i) g_shown[i](hero);
    if (!ours)
        o_show_equipment(hero, pos);
}

void equip_slots_install(void) {
    if (!g_nlayouts)
        return; // no client: the game's drawing untouched
    g_slot_item = (const uint8_t *(*)(const void *))sym("_ZNK22InventorySlotInterface7GetItemEv");
    g_picked_up = (bool (*)(const void *))sym("_ZNK22InventorySlotInterface10IsPickedUpEv");
    g_highlight_type = (int (*)(const void *))sym("_ZNK22InventorySlotInterface16GetHighlightTypeEv");
    g_item_valid = (bool (*)(const void *))sym("_ZNK9Inventory4Item7IsValidEv");
    g_icon = (void *(*)(void *, const void *))sym("_ZN9Inventory7Display14GetIconTextureEPKNS_4ItemE");
    g_trinket_class = (const uint8_t *(*)(const void *))sym("_ZNK9Inventory4Item15GetTrinketClassEv");
    g_assets = (void *(*)(void))sym("_ZN2UI14CampaignAssets7CurrentEv");
    g_rarity = (const uint8_t *(*)(const void *, unsigned))sym("_ZNK2UI14CampaignAssets8Instance16GetTrinketRarityEj");
    g_dragging = (bool (*)(const void *))sym("_ZNK12InputManager10IsDraggingEv");
    g_input = (void **)sym("_ZN12InputManager10s_InstanceE");
    g_rm = (void **)sym("_ZN15ResourceManager10s_InstanceE");
    g_rm_release = (void (*)(void *, const void *))sym("_ZN15ResourceManager7ReleaseERK19ResourceEntryHandle");
    g_trinket_hash = name_hash("trinket");
    g_gui_colour = (void (*)(uint32_t))sym("_Z10GUI_Colour4rgba");
    g_drag_set =
        (void (*)(void *, const void *, unsigned))sym("_ZN21InventoryItemDragData7SetItemEPKN9Inventory4ItemEj");
    g_drag_hero = (void (*)(void *, void *))sym("_ZN17EquipmentDragData7SetHeroEP4Hero");
    g_audio_event = (void (*)(const char *, bool, void *))sym("_Z11Audio_EventPKcbNSt3__18functionIFvS0_EEE");
#ifdef _WIN32
    const unsigned *(*tid)(void) = (const unsigned *(*)(void))sym("win:EquipmentDragObject::GetTypeIdStatic");
    static const uint8_t ready = 1;
    g_drag_object_tid = tid ? tid() : NULL;
    g_drag_object_guard = &ready;
#else
    g_drag_object_tid = sym("_ZZN19EquipmentDragObject15GetTypeIdStaticEvE7type_id");
    g_drag_object_guard = sym("_ZGVZN19EquipmentDragObject15GetTypeIdStaticEvE7type_id");
#endif
    if (g_nkinds && (!g_drag_set || !g_drag_hero || !g_drag_object_tid || !g_drag_object_guard)) {
        logf_("equip slots: drag symbols missing, %d kind(s) off", g_nkinds);
        g_nkinds = g_kinds_mods.n = 0;
        g_kinds_mods.gen = 0;
    }
    for (int p = 0; p < PLACE_COUNT; p++)
        g_caller[p] = (uintptr_t)sym(g_callers[p].symbol);
    if (!g_slot_item || !g_picked_up || !g_highlight_type || !g_item_valid || !g_icon) {
        logf_("equip slots: symbols missing, off");
        return;
    }
    static const uint8_t prologue[] = PROLOGUE_SUB32(0x18, 0x04); // sub rsp,0x418
    o_show_trinkets =
        (show_trinkets_fn)install_hook("HeroDisplay::App::ShowTrinkets",
                                       sym("_ZN11HeroDisplay3App12ShowTrinketsERK4HeroP24InventorySystemInterface4vec2N"
                                           "St3__18functionIFN9Inventory7Displ"
                                           "ay15ItemValidResultEPKNS9_4ItemEPKNS9_9ItemClassEEEENS8_IFvjEEESL_yj"),
                                       (void *)h_show_trinkets, prologue, sizeof prologue);
    static const uint8_t equipment_prologue[] = PROLOGUE_SUB32(0x48, 0x01); // sub rsp,0x148
    o_show_equipment = (show_equipment_fn)install_hook(
        "HeroDisplay::App::ShowEquipment", sym("_ZN11HeroDisplay3App13ShowEquipmentERK4Hero4vec2"),
        (void *)h_show_equipment, equipment_prologue, sizeof equipment_prologue);
}
