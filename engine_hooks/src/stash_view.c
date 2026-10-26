// The estate stash (the game's "realm inventory") as tabs: each tab shows the items it claims, in its own sort order,
// in place of the game's single sorted grid. Clients register tabs (stash_tab_register: an icon, a tooltip string id,
// which items it holds, up to STASH_MAX_SORTS sorts) and the stash shows them as tabs along its top edge, with the open
// tab's sort buttons in the game's sort button row. An item belongs to the first tab (in registration order) that
// claims it; a tab with no `has` takes every item no other tab claims. The storage itself is untouched: every item
// stays in the one trinket storage, so a drop always lands in its own tab, whichever tab is open. With no tab
// registered (or the client's active test false), the stash is the game's.
//
// The grid (TownUI::Panel::RealmInventory::PreChildrenShowInternal -> Inventory::Display::InventoryButtonGrid(
// InventorySystemInterface*, Actor const*, GridLayout const&, vec2, InventoryButtonGridParameters const&,
// InventoryButtonGridDisplayInfo*)) draws cell i from the interface's slot interfaces (a std::vector at +0x10 / +0x18 /
// +0x20, one per storage slot; Inventory::System* at +0x08), up to the storage's highest filled slot rounded up to a
// row and at most GetSlotCount(), each with Inventory::Display::InventoryButtonInGrid(actor, item, i, ..., the slot
// interface as DragContext, +0x10 as DropContext, ...). So around the stash's call (a ui_invgrid.c observer) that
// vector points at an array of ours: the open tab's items in its order, no gaps. Each cell is still its slot's own
// interface, so dragging, dropping, the tooltip and the darkening work on the real slot; only the index i is the
// position in our list. That index reaches the stash's click callback, PreChildrenShowInternal()::$_0(unsigned) (sell:
// RemoveSlot(i)), so the hook on it maps it back to the real slot first.
//
// Scrolling is ui_invgrid.c's (a scroll panel with the normal scroll bar; stash_view_layout's scroll key): the game's
// own is switched off. RealmInventory scrolls by a Widget::ContentAxisInfo at +0x218 (position +0x10, target +0x18;
// PreChildrenShowInternal translates the grid by -position inside its clip, the rect {x, y, w, h} at +0x540) and draws
// a ScrollBarWidget at +0x240. After each ActiveUpdate (and before each draw) the position and target are set to 0, so
// the wheel (ContentAxisInfo::ProcessScrollInput, from PostChildrenShowInternal) never moves it, and the bar's
// ScrollBarWidget::RenderInternal is skipped, so it's neither drawn nor dragged. Our viewport is the game's: from the
// grid's top to the rect's bottom. The game's scroll range (+0x518, the item count) is set to the open tab's count.
//
// The sort row: RealmInventory::DrawSortButton(ResourceHandle<Texture> const&, char const* tooltip id, unsigned sort
// type, std::function<void()> click) draws each sort button (alphabetical, rarity, class; class and alphabetical in
// languages where alphabetical is off). DrawSortAndUnequipButtons lays the row out from sort_button_pos
// (TownUI::Assets+0x1be8, +0x1bcc with a hero) leftwards by sort_button_spacing (+0x1bf0): the unequip button N spacings
// left of it (N sort buttons), then each sort button one spacing further right, drawn at the current transform's
// origin. The hook draws, at the first sort button and none of the others, the open tab's sorts, ending `reserved`
// spacings left of sort_button_pos (the layout's sort_row_reserved: places kept clear at the row's right end, 1 for
// the unequip button, below). Sort type -1: the game draws no
// "current sort" overlay; our sorts never touch the storage's order.
//
// The unequip button: DrawSortAndUnequipButtons draws the game's "Unequip all trinkets" with
// RealmInventory::DrawUnequipButton(vec2 pos) (a GUI_ImageButton at pos, N spacings left of sort_button_pos; texture
// TownUI::Assets+0x1cc8; its click asks with a ConfirmDialog, then ExecuteUnequipTrinkets). The hook draws the open
// tab's button instead (none if the tab turns it off), at sort_button_pos (the row's right end, the first reserved
// place), as a DrawSortButton (the game's button tooltip, from the tab's string id), whose click asks with ui_confirm.
// Yes does what ExecuteUnequipTrinkets does, for the tab's items only: Roster::System::UnequipAllTrinkets walks the
// roster's Roster::Entry* vector (+0x20 / +0x28), skips an entry with status +0x1520 == 3, and for the hero
// (entry+0x10) moves each filled slot of the trinket inventory (Hero+0x1200) to the storage (AddCopyOfItem, then
// RemoveSlotItem(i, amount): the slots don't shift), sets entry+0x159c when one moved and calls
// Actor::UpdateTrinketBuffs; then ExecuteUnequipTrinkets sets the panel's refresh flag (+0x528), saves the roster
// (Roster::System::Save(false)) and the estate (Estate::System::Save, Campaign::Current()+0xe20). Here only the
// trinkets of the tab move, and the registered equipment slot kinds' items of the tab (equip_slots_unequip).
//
// The tabs are drawn after the panel (RealmInventory::PreChildrenShowInternal, panel coordinates), over the layout's
// strip_background (if any, at strip_pos, strip_size): a background per
// state (the layout's picture, with _hover and _on variants) as a GUI_ImageButton (ImageButtonParameters' constructor;
// a widget takes the mouse, so the town's buildings behind a tab, GUI_HotSpots, aren't hovered through it; a plain
// picture let them be); the panel's rect is grown to take them in (strip, below), and the tab's icon (with optional _hover and _on variants) as a DrawSortButton (so it gets
// the game's button tooltip, from the tab's string id). A click anywhere on the tab opens it.
//
// Drops: the game's own drop area (RealmInventory::DrawTrinketArea, below; arena mode only in the game) covers the
// layout's drop_pos / drop_size in any mode:
// an item dropped there goes to the first empty slot, and shows in its own tab, whichever is open, even an empty one.
// (A drop target of our own drawn after the panel never got the drop: nothing drawn after PreChildrenShowInternal gets
// input there.)
//
// This module owns RealmInventory::ActiveUpdate, DrawSortButton, DrawUnequipButton, PreChildrenShowInternal, the sell callback
// PreChildrenShowInternal()::$_0 and ScrollBarWidget::RenderInternal; clients get the panel and the sale through
// stash_view_on_panel and stash_view_on_sell. It also owns the hint under the grid (the game's
// realm_inventory_trinket_sell_instruction): the open tab's instruction, if it has one, and none while item_sale.c's
// salvage prompt shows in its place.
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TABS    8
#define MAX_VIEW    4096
#define MAX_CLIENTS 4

typedef struct {
    StashTabDef def;
    char id[32], icon[160], tooltip[64], instruction[64];
    char sort_icon[STASH_MAX_SORTS][160], sort_tooltip[STASH_MAX_SORTS][64];
    char unequip_icon[160], unequip_tooltip[64], unequip_question[64], unequip_yes[64], unequip_no[64];
    int nsorts, sort; // its sorts, and the one chosen
} Tab;
// Mods follow the campaign: every tab, active test, layout and client is its registering mod's (mods_current) and
// counts only while the campaign applies that mod. g_tabs is the campaign's tabs (the top mod's first, each mod's in
// registration order), rebuilt from g_all at each campaign load and close, a tab's chosen sort kept; the layout is the
// top applied mod's; the tabs apply while every applied mod's active test says so.
static Tab g_all[MAX_TABS]; // every registered tab
static int g_nall;
static ModsList g_all_mods;
static Tab g_tabs[MAX_TABS];    // the campaign's
static int g_tab_src[MAX_TABS]; // each one's index in g_all
static int g_ntabs, g_tab;
static bool (*g_actives[MAX_CLIENTS])(void);
static int g_nactives;
static ModsList g_actives_mods;
static struct {
    UiLayout *tabs, *scroll;
    char key[48], scroll_key[48];
} g_layouts[MAX_CLIENTS];
static int g_nlayouts;
static ModsList g_layouts_mods;
static UiLayout *g_layout; // the tabs and the sort row (the top applied mod's)
static const char *g_key = "tabs";

static struct {
    void (*before)(void *realm);
    void (*after)(void *realm);
} g_panel_clients[MAX_CLIENTS];
static struct {
    void (*before)(void *item);
    void (*after)(void);
} g_sell_clients[MAX_CLIENTS];
static int g_npanel, g_nsell;
static ModsList g_panel_mods, g_sell_mods;

static void on_campaign(void) {
    for (int k = 0; k < g_ntabs; k++)
        g_all[g_tab_src[k]].sort = g_tabs[k].sort;
    g_ntabs = g_tab = 0;
    MODS_EACH(g_all_mods, true, i) {
        g_tab_src[g_ntabs] = i;
        g_tabs[g_ntabs++] = g_all[i];
    }
    int l = mods_list_first(&g_layouts_mods);
    g_layout = l >= 0 ? g_layouts[l].tabs : NULL;
    g_key = l >= 0 ? g_layouts[l].key : "tabs";
}

static void watch_campaigns(void) {
    static bool watching;
    if (!watching) {
        watching = true;
        mods_on_campaign(on_campaign);
    }
}

static bool tabs_active(void) {
    MODS_EACH(g_actives_mods, false, i) if (!g_actives[i]()) return false;
    return true;
}

static void (*g_transform_push)(void), (*g_transform_pop)(void);
static void (*g_transform_translate)(const Vec2 *);
static uint8_t *(*g_town_assets)(void);
static uint8_t *(*g_slot_item)(void *inventory, unsigned slot);
static void **g_item_classes;
static const uint8_t *(*g_item_class)(const void *, unsigned type, unsigned id);
static const uint8_t *(*g_trinket_class)(unsigned id);
static void *(*g_campaign)(void), *(*g_roster)(void);
static void *(*g_trinket_storage)(void *estate, int type);
static unsigned (*g_slot_count)(const void *inventory);
static void (*g_add_copy)(void *inventory, const void *item);
static void (*g_remove_slot_item)(void *inventory, unsigned slot, int amount);
static void (*g_update_trinket_buffs)(void *hero);
static void (*g_roster_save)(void *roster, bool flag);
static void (*g_estate_save)(void *estate);
static void (*g_button_params)(void *self, Vec2 pos, const void *texture, uint64_t id, bool enabled,
                               const void *tooltip);

static void (*o_active_update)(void *realm, float dt);
static void (*o_sort_button)(void *realm, const void *texture, const char *tooltip, unsigned type, void *click);
static void (*o_unequip_button)(void *realm, Vec2 pos);
static void (*o_realm)(void *realm);
static void (*o_sell)(void *lambda, unsigned *slot);
static void (*o_bar_render)(const void *bar, unsigned flags);
static void (*o_trinket_area)(void *realm);
static void (*o_base_pos)(void *realm, Vec2 *pos), (*o_base_size)(void *realm, Vec2 *size);
static bool g_installed, g_scrolls;

static void *g_realm;                  // the stash panel (its last update)
static void *g_view[MAX_VIEW];         // the open tab's slot interfaces, in its order
static unsigned g_view_slot[MAX_VIEW]; // and their storage slots
static unsigned g_view_n;
static int g_sort_buttons, g_last_sort_buttons = 3; // the game's sort buttons this frame and last frame
static void **g_saved[3];
static void *g_swapped;

static bool on(void) {
    return g_installed && g_ntabs && tabs_active();
}

// ---------------------------------------------------------------- registration
bool stash_tab_register(const StashTabDef *def) {
    if (!def || !def->id || g_nall == MAX_TABS || g_installed || mods_list_add(&g_all_mods) < 0) {
        logf_("stash view: tab %s refused", def && def->id ? def->id : "?");
        return false;
    }
    watch_campaigns();
    Tab *t = &g_all[g_nall++];
    memset(t, 0, sizeof *t);
    t->def = *def;
    snprintf(t->id, sizeof t->id, "%s", def->id);
    snprintf(t->icon, sizeof t->icon, "%s", def->icon ? def->icon : "");
    snprintf(t->tooltip, sizeof t->tooltip, "%s", def->tooltip ? def->tooltip : "");
    snprintf(t->instruction, sizeof t->instruction, "%s", def->instruction ? def->instruction : "");
    snprintf(t->unequip_icon, sizeof t->unequip_icon, "%s", def->unequip.icon ? def->unequip.icon : "");
    snprintf(t->unequip_tooltip, sizeof t->unequip_tooltip, "%s",
             def->unequip.tooltip ? def->unequip.tooltip : "str_unequip_all_trinkets");
    snprintf(t->unequip_question, sizeof t->unequip_question, "%s",
             def->unequip.question ? def->unequip.question : "str_unequip_all_trinkets_description");
    snprintf(t->unequip_yes, sizeof t->unequip_yes, "%s",
             def->unequip.yes ? def->unequip.yes : "str_unequip_all_trinkets_yes");
    snprintf(t->unequip_no, sizeof t->unequip_no, "%s",
             def->unequip.no ? def->unequip.no : "str_unequip_all_trinkets_no");
    for (int i = 0; i < STASH_MAX_SORTS && def->sorts[i].key; i++, t->nsorts++) {
        snprintf(t->sort_icon[i], sizeof t->sort_icon[i], "%s", def->sorts[i].icon ? def->sorts[i].icon : "");
        snprintf(t->sort_tooltip[i], sizeof t->sort_tooltip[i], "%s",
                 def->sorts[i].tooltip ? def->sorts[i].tooltip : "");
    }
    return true;
}

void stash_view_active(bool (*active)(void)) {
    if (active && g_nactives < MAX_CLIENTS && mods_list_add(&g_actives_mods) >= 0)
        g_actives[g_nactives++] = active;
}

void stash_view_layout(UiLayout *tabs, const char *key, UiLayout *scroll, const char *scroll_key) {
    if (g_nlayouts == MAX_CLIENTS || g_installed || mods_list_add(&g_layouts_mods) < 0) {
        logf_("stash view: layout refused");
        return;
    }
    watch_campaigns();
    g_layouts[g_nlayouts].tabs = tabs;
    g_layouts[g_nlayouts].scroll = scroll;
    snprintf(g_layouts[g_nlayouts].key, sizeof g_layouts[0].key, "%s", key ? key : "tabs");
    snprintf(g_layouts[g_nlayouts++].scroll_key, sizeof g_layouts[0].scroll_key, "%s",
             scroll_key ? scroll_key : "stash");
}

void stash_view_on_panel(void (*before)(void *realm), void (*after)(void *realm)) {
    if (g_npanel < MAX_CLIENTS && mods_list_add(&g_panel_mods) >= 0) {
        g_panel_clients[g_npanel].before = before;
        g_panel_clients[g_npanel].after = after;
        g_npanel++;
    }
}

void stash_view_on_sell(void (*before)(void *item), void (*after)(void)) {
    if (g_nsell < MAX_CLIENTS && mods_list_add(&g_sell_mods) >= 0) {
        g_sell_clients[g_nsell].before = before;
        g_sell_clients[g_nsell].after = after;
        g_nsell++;
    }
}

const char *stash_view_tab(void) {
    return on() ? g_tabs[g_tab].id : NULL;
}

// the hint under the grid: hidden while item_sale.c's salvage prompt takes its place, else the open tab's (if it has one)
static const char *instruction(void) {
    if (item_sale_prompting())
        return "";
    return on() && g_tabs[g_tab].instruction[0] ? g_tabs[g_tab].instruction : NULL;
}

// ---------------------------------------------------------------- trinket sort keys
static const char *const g_trinket_rarities[] = {"very_common", "common",    "uncommon",      "rare",
                                                 "very_rare",   "ancestral", "crimson_court", "kickstarter",
                                                 "trophy",      "madman",    "comet",         "shard"};

static const uint8_t *trinket_class(const uint8_t *item) {
    if (!g_item_classes || !*g_item_classes || !g_item_class || !g_trinket_class)
        return NULL;
    const uint8_t *cls = g_item_class(*g_item_classes, *(const unsigned *)(item + ITEM_TYPE_HASH),
                                      *(const unsigned *)(item + ITEM_ID_HASH));
    return cls ? g_trinket_class(*(const unsigned *)(cls + ITEMCLASS_TRINKET_ID)) : NULL;
}

static int trinket_rank(const uint8_t *item) {
    const uint8_t *tc = trinket_class(item);
    unsigned h = tc ? *(const unsigned *)(tc + TRINKETCLASS_RARITY) : 0;
    for (int i = 0; i < (int)(sizeof g_trinket_rarities / sizeof *g_trinket_rarities); i++)
        if (name_hash(g_trinket_rarities[i]) == h)
            return i;
    return 3;
}

void stash_key_trinket_rarity(const uint8_t *item, StashKey *k, void *ud) {
    (void)ud;
    k->k[0] = -trinket_rank(item); // rarest first, then by id
}

void stash_key_trinket_class(const uint8_t *item, StashKey *k, void *ud) {
    (void)ud;
    const uint8_t *tc = trinket_class(item);
    const unsigned *b = tc ? *(const unsigned *const *)(tc + TRINKETCLASS_CLASSES) : NULL;
    const unsigned *e = tc ? *(const unsigned *const *)(tc + TRINKETCLASS_CLASSES + 8) : NULL;
    k->k[0] = b && e > b ? *b : 0; // any hero first, then by the first class's id hash
    k->k[1] = -trinket_rank(item);
}

// ---------------------------------------------------------------- the view
typedef struct {
    StashKey key;
    unsigned slot;
} Entry;
static Entry g_entries[MAX_VIEW];

static int by_key(const void *pa, const void *pb) {
    const Entry *a = pa, *b = pb;
    for (int i = 0; i < 3; i++)
        if (a->key.k[i] != b->key.k[i])
            return a->key.k[i] < b->key.k[i] ? -1 : 1;
    int c = strcmp(a->key.name, b->key.name);
    return c ? c : (int)a->slot - (int)b->slot;
}

// the tab an item belongs to: the first that claims it, else the first with no test (-1: none)
static int tab_of(const uint8_t *item) {
    int rest = -1;
    for (int i = 0; i < g_ntabs; i++) {
        if (!g_tabs[i].def.has) {
            if (rest < 0)
                rest = i;
            continue;
        }
        if (g_tabs[i].def.has(item, g_tabs[i].def.ud))
            return i;
    }
    return rest;
}

// the open tab's items of the storage behind iface, in its order (g_view, g_view_slot, g_view_n)
static void build_view(void *iface) {
    void *inv = *(void **)((uint8_t *)iface + IFACE_SYSTEM);
    void **b = *(void ***)((uint8_t *)iface + IFACE_SLOTS), **e = *(void ***)((uint8_t *)iface + IFACE_SLOTS + 8);
    const Tab *t = &g_tabs[g_tab];
    StashKeyFn key = t->nsorts ? t->def.sorts[t->sort].key : NULL;
    unsigned n = 0;
    for (unsigned i = 0; b + i < e && n < MAX_VIEW; i++) {
        uint8_t *item = g_slot_item(inv, i);
        if (!item || *(const int *)(item + ITEM_AMOUNT) <= 0 || tab_of(item) != g_tab)
            continue;
        Entry *en = &g_entries[n++];
        memset(&en->key, 0, sizeof en->key);
        snprintf(en->key.name, sizeof en->key.name, "%.64s", (const char *)item + ITEM_ID);
        en->slot = i;
        if (key)
            key(item, &en->key, t->def.ud);
    }
    qsort(g_entries, n, sizeof *g_entries, by_key);
    for (unsigned i = 0; i < n; i++) {
        g_view[i] = b[g_entries[i].slot];
        g_view_slot[i] = g_entries[i].slot;
    }
    g_view_n = n;
}

static bool is_stash(const void *iface) {
    return iface && g_realm && iface == *(void *const *)((uint8_t *)g_realm + REALM_INTERFACE) && on();
}

static void grid_begin(void *iface) {
    g_swapped = NULL;
    if (!is_stash(iface))
        return;
    build_view(iface);
    void ***v = (void ***)((uint8_t *)iface + IFACE_SLOTS);
    g_saved[0] = v[0];
    g_saved[1] = v[1];
    g_saved[2] = v[2];
    v[0] = g_view;
    v[1] = g_view + g_view_n;
    v[2] = g_view + g_view_n;
    g_swapped = iface;
}

static void grid_end(void *iface, void *hovered) {
    (void)hovered;
    if (!g_swapped || iface != g_swapped)
        return;
    void ***v = (void ***)((uint8_t *)iface + IFACE_SLOTS);
    v[0] = g_saved[0];
    v[1] = g_saved[1];
    v[2] = g_saved[2];
    g_swapped = NULL;
}

// the panel, and its item count (the scroll range) as our view's: after each update and before each draw, so even
// the frame it opens on (drawn before its first update) shows our list, not the game's
static void prepare(void *realm) {
    g_realm = realm;
    if (!realm || !on())
        return;
    void *iface = *(void **)((uint8_t *)realm + REALM_INTERFACE);
    if (iface) {
        build_view(iface); // the count for the scroll range; the grid rebuilds as it draws
        *(unsigned *)((uint8_t *)realm + REALM_ITEM_COUNT) = g_view_n;
    }
    if (g_scrolls) { // the game's scrolling stays at the top: ours scrolls
        *(float *)((uint8_t *)realm + REALM_AXIS_POS) = 0;
        *(float *)((uint8_t *)realm + REALM_AXIS_TARGET) = 0;
    }
}

// ---------------------------------------------------------------- buttons
// path with a suffix before its extension ("a/b.png", "_on": "a/b_on.png")
static void variant(char *out, size_t n, const char *path, const char *suffix) {
    const char *dot = strrchr(path, '.');
    int stem = dot ? (int)(dot - path) : (int)strlen(path);
    snprintf(out, n, "%.*s%s%s", stem, path, suffix, dot ? dot : "");
}

static void key_path(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s.%s", g_key, name);
}

static Vec2 lv(const char *name, Vec2 fallback) {
    char k[96];
    key_path(k, sizeof k, name);
    return ui_layout_vec2(g_layout, k, fallback);
}

static float ln(const char *name, float fallback) {
    char k[96];
    key_path(k, sizeof k, name);
    return ui_layout_num(g_layout, k, fallback);
}

static const char *ls(const char *name, const char *fallback) {
    char k[96];
    key_path(k, sizeof k, name);
    return ui_layout_str(g_layout, k, fallback);
}

static void on_tab(const uint8_t *data) {
    if (data[0] >= g_ntabs || data[0] == g_tab)
        return;
    g_tab = data[0];
    void *iface = g_realm ? *(void **)((uint8_t *)g_realm + REALM_INTERFACE) : NULL;
    if (iface)
        ui_invgrid_reset(iface); // a new tab starts at the top
}

static void on_sort(const uint8_t *data) {
    if (data[0] < g_tabs[g_tab].nsorts)
        g_tabs[g_tab].sort = data[0];
}

// a button drawn by the game's DrawSortButton at pos (current transform): its tooltip and click are the game's
static void game_button(void *realm, Vec2 pos, const uint8_t tex[16], const char *tooltip, void (*fn)(const uint8_t *),
                        uint8_t arg) {
    StdFunction click;
    ui_function(&click, fn, &arg, 1);
    g_transform_push();
    g_transform_translate(&pos);
    o_sort_button(realm, tex, tooltip, 0xFFFFFFFFu, &click);
    g_transform_pop();
}

// ---------------------------------------------------------------- drops
// The game's own drop area: RealmInventory::DrawTrinketArea draws an invisible GUI_ImageButton (id 'trnk', the white
// texture, colour 0) at TownUI::Assets+0x23a0 (pos) sized +0x23a8, whose DropContext is the panel's
// TrinketDropAreaInventoryInterface (RealmInventory+0xc0, a RealmInventorySlotInterface): its OnDrop puts the item in
// the storage's first empty slot (Inventory::System::SwitchEquippedItems), after IsDropItemValidInternal (trinkets,
// and registered types that join ITEM_CHECK_STASH_DROP). It's drawn before the grid, so a cell still takes a drop on
// it. Its rect is the layout's drop_pos / drop_size for the call (the game's is for the vanilla panel), or with
// drop_area "view" the grid's viewport (RealmInventory+0x540, the clip the grid draws in).
static bool g_area_drawn; // this frame's panel has its drop area

// the game's drop area with the layout's rect (the transform: the panel's)
static void draw_area(void *realm) {
    uint8_t *assets = g_town_assets();
    Vec2 *pos = (Vec2 *)(assets + ASSETS_DROP_POS), *size = (Vec2 *)(assets + ASSETS_DROP_SIZE);
    Vec2 was_pos = *pos, was_size = *size;
    if (!strcmp(ls("drop_area", "layout"), "view")) { // the grid's own viewport (its clip, panel coordinates)
        const float *view = (const float *)((const uint8_t *)realm + REALM_VIEW);
        *pos = (Vec2){view[0], view[1]};
        *size = (Vec2){view[2], view[3]};
    } else {
        *pos = lv("drop_pos", was_pos);
        *size = lv("drop_size", was_size);
    }
    o_trinket_area(realm);
    ui_colour(0xFFFFFFFFu); // it draws in colour 0 (invisible): the panel after it is drawn white
    *pos = was_pos;
    *size = was_size;
    g_area_drawn = true;
}

// the game's own call: only in arena mode (App+0x54b) with a hero assigned (RealmInventory+0x510); the panel has it
// already (h_realm draws it first, in any mode)
static void h_trinket_area(void *realm) {
    if (g_area_drawn)
        return;
    o_trinket_area(realm);
}

// the tabs along the panel's top edge (panel coordinates; after the panel, so the open tab covers its border)
static void draw_tabs(void *realm) {
    Vec2 pos = lv("pos", (Vec2){24, -42}), size = lv("size", (Vec2){56, 44}), step = lv("spacing", (Vec2){60, 0});
    Vec2 icon_at = lv("icon_offset", (Vec2){12, 6}), open_shift = lv("open_offset", (Vec2){0, 0});
    const char *bg = ls("background", "campaign/town/realm_inventory/stash_tab.png");
    const char *strip_bg = ls("strip_background", "");
    char path[192];
    uint8_t strip_tex[16];
    if (strip_bg[0] && ui_texture(strip_bg, strip_tex)) // behind the tabs, so they stand out from what's behind
        ui_image_sized(lv("strip_pos", (Vec2){0, pos.y}), strip_tex,
                       lv("strip_size", (Vec2){size.x * g_ntabs, -pos.y}));
    for (int i = 0; i < g_ntabs; i++) {
        Tab *t = &g_tabs[i];
        bool open = i == g_tab;
        Vec2 at = {pos.x + i * step.x + (open ? open_shift.x : 0), pos.y + i * step.y + (open ? open_shift.y : 0)};
        bool hover = !open && ui_mouse_in(at, size);
        variant(path, sizeof path, bg, open ? "_on" : hover ? "_hover" : "");
        uint8_t tex[16];
        if (ui_texture(path, tex) || ui_texture(bg, tex)) {
            // a button, not a picture: like the game's widgets it takes the mouse, so what's behind the tab (the town's
            // buildings) isn't hovered or clicked through it; a click on it opens the tab
            uint8_t params[UI_PARAMS_SIZE];
            memset(params, 0, sizeof params);
            g_button_params(params, at, tex, 0x73746230ull + (uint64_t)i, true, NULL); // 'stb0' + i
            StdFunction click;
            uint8_t a = (uint8_t)i;
            ui_function(&click, on_tab, &a, 1);
            ui_image_button(params, &click);
        }
        variant(path, sizeof path, t->icon, open ? "_on" : hover ? "_hover" : ""); // the icon's state, if drawn
        if (t->icon[0] && (ui_texture(path, tex) || ui_texture(t->icon, tex)))
            game_button(realm, (Vec2){at.x + icon_at.x, at.y + icon_at.y}, tex, t->tooltip, on_tab, (uint8_t)i);
    }
}

static void sort_buttons(void *realm);

static void h_sort_button(void *realm, const void *texture, const char *tooltip, unsigned type, void *click) {
    if (!on() || !tooltip) {
        o_sort_button(realm, texture, tooltip, type, click);
        return;
    }
    if (!g_sort_buttons++)
        sort_buttons(realm); // the open tab's are drawn at the first; the game's are not drawn
#ifdef _WIN32
    ui_function_destroy(click); // not passed on: the callee's to destroy (MSVC)
#endif
}

static void sort_buttons(void *realm) {
    uint8_t *assets = g_town_assets ? g_town_assets() : NULL;
    if (!assets)
        return;
    Vec2 step = *(const Vec2 *)(assets + ASSETS_SORT_SPACING);
    Vec2 size = lv("sort_button_size", (Vec2){32, 32});
    float reserved = ln("sort_row_reserved", 1);
    Tab *t = &g_tabs[g_tab];
    if (t->nsorts < 2)
        return; // nothing to choose
    for (int i = 0; i < t->nsorts; i++) {
        // the current origin is the first sort button's place, sort_button_pos - (N - 1) spacings; this button goes
        // reserved + (nsorts - 1 - i) spacings left of sort_button_pos
        float k = (float)(g_last_sort_buttons - 1) - (reserved + (float)(t->nsorts - 1 - i));
        Vec2 d = {k * step.x, k * step.y};
        bool chosen = i == t->sort;
        g_transform_push();
        g_transform_translate(&d);
        bool hover = !chosen && ui_mouse_in((Vec2){0, 0}, size);
        g_transform_pop();
        char path[192];
        uint8_t tex[16];
        variant(path, sizeof path, t->sort_icon[i], chosen ? "_on" : hover ? "_hover" : "");
        if (!ui_texture(path, tex) && !ui_texture(t->sort_icon[i], tex))
            continue;
        game_button(realm, d, tex, t->sort_tooltip[i], on_sort, (uint8_t)i);
    }
}

// ---------------------------------------------------------------- unequip all
static bool in_tab(const uint8_t *item, void *ud) {
    return tab_of(item) == (int)(intptr_t)ud;
}

// every hero's equipped items of the tab into the stash, then saved (as ExecuteUnequipTrinkets)
static void unequip_tab(int tab) {
    void *campaign = g_campaign(), *roster = g_roster();
    void *storage = campaign ? g_trinket_storage((uint8_t *)campaign + CAMPAIGN_ESTATE, 0) : NULL;
    if (!storage || !roster) {
        logf_("stash view: no roster or stash, nothing unequipped");
        return;
    }
    void *const *b = *(void *const *const *)((uint8_t *)roster + ROSTER_ENTRIES);
    void *const *e = *(void *const *const *)((uint8_t *)roster + ROSTER_ENTRIES + 8);
    int heroes = 0, items = 0;
    for (void *const *p = b; b && p < e; p++) {
        uint8_t *entry = *p;
        if (!entry || *(const int *)(entry + ENTRY_STATUS) == 3)
            continue;
        uint8_t *hero = entry + ENTRY_HERO;
        void *trinkets = hero + ACTOR_TRINKETS;
        int moved = 0;
        for (unsigned i = 0; i < g_slot_count(trinkets); i++) {
            uint8_t *item = g_slot_item(trinkets, i);
            int amount = item ? *(const int *)(item + ITEM_AMOUNT) : 0;
            if (amount <= 0 || tab_of(item) != tab)
                continue;
            g_add_copy(storage, item);
            g_remove_slot_item(trinkets, i, amount);
            moved++;
        }
        if (moved) {
            entry[ENTRY_TRINKETS_CHANGED] = 1;
            g_update_trinket_buffs(hero);
        }
        moved += equip_slots_unequip(hero, storage, in_tab, (void *)(intptr_t)tab);
        items += moved;
        heroes += moved > 0;
    }
    if (g_realm)
        *(int *)((uint8_t *)g_realm + REALM_REFRESH) = 1;
    g_roster_save(roster, false);
    g_estate_save((uint8_t *)campaign + CAMPAIGN_ESTATE);
    logf_("stash view: unequipped %d %s item(s) from %d hero(es)", items, g_tabs[tab].id, heroes);
}

static void on_unequip_answer(const char *key, const uint8_t *data) {
    if (!strcmp(key, "yes") && data[0] < g_ntabs)
        unequip_tab(data[0]);
}

static void on_unequip(const uint8_t *data) {
    if (data[0] >= g_ntabs)
        return;
    const Tab *t = &g_tabs[data[0]];
    const UiConfirmOption answers[] = {{"yes", t->unequip_yes, on_unequip_answer}, {"no", t->unequip_no, NULL}};
    ui_confirm(t->unequip_question, answers, 2, data, 1);
}

// the open tab's button at sort_button_pos (pos is N spacings left of it); none if the tab turns it off
static void h_unequip_button(void *realm, Vec2 pos) {
    if (!on()) {
        o_unequip_button(realm, pos);
        return;
    }
    Tab *t = &g_tabs[g_tab];
    uint8_t *assets = g_town_assets();
    if (t->def.unequip.off || !assets)
        return;
    Vec2 step = *(const Vec2 *)(assets + ASSETS_SORT_SPACING);
    Vec2 at = {pos.x + g_last_sort_buttons * step.x, pos.y + g_last_sort_buttons * step.y};
    uint8_t tex[16];
    const uint8_t *picture = assets + ASSETS_UNEQUIP_TEXTURE;
    if (t->unequip_icon[0]) {
        char path[192];
        g_transform_push();
        g_transform_translate(&at);
        bool hover = ui_mouse_in((Vec2){0, 0}, lv("sort_button_size", (Vec2){32, 32}));
        g_transform_pop();
        variant(path, sizeof path, t->unequip_icon, hover ? "_hover" : "");
        if (ui_texture(path, tex) || ui_texture(t->unequip_icon, tex))
            picture = tex;
    }
    game_button(realm, at, picture, t->unequip_tooltip, on_unequip, (uint8_t)g_tab);
}

// ---------------------------------------------------------------- hooks
static void h_active_update(void *realm, float dt) {
    g_realm = realm;
    o_active_update(realm, dt);
    prepare(realm);
}

// The tab strip: the panel's rect (RealmInventory::UpdateBasePos, its position at +0x530, and UpdateBaseSize, its
// background's size) grows upward by the strip's height (the tabs' top above the panel: -pos.y), and the panel's
// drawing (PreChildrenShowInternal) is moved back down by as much, so it stays where it was. The tabs are
// then inside the panel's widget: the game's input reaches them (clicks, the icon's tooltip), and the town behind
// them isn't hovered (a widget only gets input inside its rect; drawn outside it, the tabs were clicked through).
// PostChildrenShowInternal (the close button) isn't moved: its transform isn't the grown rect's (moved, it sat a
// strip lower).
static float strip(void) {
    if (!on() || !g_layout || !o_base_pos || !o_base_size)
        return 0; // both, or none
    float y = lv("pos", (Vec2){0, 0}).y;
    return y < 0 ? -y : 0;
}

static void h_base_pos(void *realm, Vec2 *pos) {
    o_base_pos(realm, pos);
    if (pos)
        pos->y -= strip();
}

static void h_base_size(void *realm, Vec2 *size) {
    o_base_size(realm, size);
    if (size)
        size->y += strip();
}

static void push_strip(void) {
    Vec2 d = {0, strip()};
    g_transform_push();
    g_transform_translate(&d);
}

static void h_realm(void *realm) {
    push_strip();
    prepare(realm); // the open tab's order from its first frame
    g_area_drawn = false;
    if (on() && g_layout && o_trinket_area)
        draw_area(realm); // drops anywhere on the panel, in any mode
    MODS_EACH(g_panel_mods, false, i) if (g_panel_clients[i].before) g_panel_clients[i].before(realm);
    g_sort_buttons = 0;
    o_realm(realm);
    if (g_sort_buttons)
        g_last_sort_buttons = g_sort_buttons;
    if (on() && g_layout)
        draw_tabs(realm);
    for (int k = mods_list_order(&g_panel_mods, false) - 1; k >= 0; k--) {
        int i = g_panel_mods.order[k];
        if (g_panel_clients[i].after)
            g_panel_clients[i].after(realm);
    }
    g_transform_pop();
}

static void h_sell(void *lambda, unsigned *slot) {
    unsigned real = slot ? *slot : 0;
    if (slot && on() && g_realm && real < g_view_n)
        real = g_view_slot[real]; // the stash shows our order
    if (slot)
        slot = &real;
    void *inv = *(void **)((uint8_t *)lambda + LAMBDA_INVENTORY);
    void *item = inv && slot ? g_slot_item(inv, *slot) : NULL;
    logf_("stash sell: slot %u, %.64s", slot ? *slot : 0, item ? (const char *)item + ITEM_ID : "(no item)");
    MODS_EACH(g_sell_mods, false, i) if (g_sell_clients[i].before) g_sell_clients[i].before(item);
    o_sell(lambda, slot);
    for (int k = mods_list_order(&g_sell_mods, false) - 1; k >= 0; k--) {
        int i = g_sell_mods.order[k];
        if (g_sell_clients[i].after)
            g_sell_clients[i].after();
    }
}

static void h_bar_render(const void *bar, unsigned flags) {
    if (g_scrolls && g_realm && bar == (const uint8_t *)g_realm + REALM_BAR && on())
        return; // ours instead
    o_bar_render(bar, flags);
}

// our viewport: from the grid's top to the bottom of the game's clip rect (same transform); none (the game's grid and
// scrolling) while the tabs are off, as h_bar_render and prepare leave the game's scrolling then
static float stash_height(const void *iface, Vec2 grid_pos) {
    (void)iface;
    if (!g_realm || !on())
        return 0;
    const float *view = (const float *)((const uint8_t *)g_realm + REALM_VIEW);
    return view[1] + view[3] - grid_pos.y;
}

// ---------------------------------------------------------------- install
void stash_view_install(void) {
    if (!g_nall && !g_npanel && !g_nsell)
        return; // no clients: the stash is the game's
    g_slot_item = (uint8_t *(*)(void *, unsigned))sym("_ZN9Inventory6System20GetItemFromSlotIndexEj");
    g_item_classes = (void **)sym("_ZN9Inventory11SystemClass10s_InstanceE");
    g_item_class =
        (const uint8_t *(*)(const void *, unsigned, unsigned))sym("_ZNK9Inventory11SystemClass12GetItemClassEjj");
    g_trinket_class = (const uint8_t *(*)(unsigned))sym("_ZN7Trinket7Library15GetTrinketClassEj");
    g_transform_push = (void (*)(void))sym("_Z14Transform_Pushv");
    g_transform_pop = (void (*)(void))sym("_Z13Transform_Popv");
    g_transform_translate = (void (*)(const Vec2 *))sym("_Z19Transform_TranslateRK4vec2");
    g_town_assets = (uint8_t *(*)(void))sym("_ZN6TownUI6Assets7CurrentEv");
    g_button_params = (void (*)(void *, Vec2, const void *, uint64_t, bool, const void *))sym(
        "_ZN21ImageButtonParametersC2E4vec214ResourceHandleI7TextureEybP20GuiTooltipParameters");
    if (!g_slot_item || !g_button_params || !g_transform_push || !g_transform_pop || !g_transform_translate ||
        !g_town_assets) {
        logf_("stash view: symbols missing, the stash stays vanilla");
        return;
    }
    static const uint8_t realm[] = PROLOGUE_SUB32(0xB8, 0x04), sell[] = PROLOGUE_SUB32(0x18, 0x07);
    *(void **)&o_realm = install_hook("RealmInventory::PreChildrenShowInternal",
                                      sym("_ZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEv"),
                                      (void *)h_realm, realm, sizeof realm);
    *(void **)&o_sell =
        install_hook("RealmInventory sell",
                     sym("_ZNSt3__110__function6__funcIZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEvE3$_"
                         "0NS_9allocatorIS5_EEFvjEEclEOj"),
                     (void *)h_sell, sell, sizeof sell);
    ui_text_substitute("realm_inventory_trinket_sell_instruction", instruction);
    if (!g_nall) {
        logf_("stash view: no tabs, panel and sale clients only");
        return;
    }
    if (!o_realm || !o_sell || !ui_invgrid_ready()) {
        logf_("stash view: hooks missing, the stash keeps its own order");
        return;
    }
    static const uint8_t sort_button[] = PROLOGUE_SUB32(0x68, 0x0A);
    // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; sub rsp,0x10
    static const uint8_t update[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                     0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x10};
    *(void **)&o_active_update = install_hook("TownUI::Panel::RealmInventory::ActiveUpdate",
                                              sym("_ZN6TownUI5Panel14RealmInventory12ActiveUpdateEf"),
                                              (void *)h_active_update, update, sizeof update);
    *(void **)&o_sort_button = install_hook(
        "TownUI::Panel::RealmInventory::DrawSortButton",
        sym("_ZN6TownUI5Panel14RealmInventory14DrawSortButtonERK14ResourceHandleI7TextureEPKcjNSt3__18functionIFvvEEE"),
        (void *)h_sort_button, sort_button, sizeof sort_button);
    if (!o_active_update || !o_sort_button) {
        logf_("stash view: hooks missing, the stash keeps its own order");
        return;
    }
    g_campaign = (void *(*)(void))sym("_ZN8Campaign7CurrentEv");
    g_roster = (void *(*)(void))sym("_ZN8Campaign13CurrentRosterEv");
    g_trinket_storage = (void *(*)(void *, int))sym("_ZN6Estate6System17GetTrinketStorageENS0_19ETrinketStorageTypeE");
    g_slot_count = (unsigned (*)(const void *))sym("_ZNK9Inventory6System17GetTotalSlotCountEv");
    g_add_copy = (void (*)(void *, const void *))sym("_ZN9Inventory6System13AddCopyOfItemEPKNS_4ItemE");
    g_remove_slot_item = (void (*)(void *, unsigned, int))sym("_ZN9Inventory6System14RemoveSlotItemEji");
    g_update_trinket_buffs = (void (*)(void *))sym("_ZN5Actor18UpdateTrinketBuffsEv");
    g_roster_save = (void (*)(void *, bool))sym("_ZN6Roster6System4SaveEb");
    g_estate_save = (void (*)(void *))sym("_ZN6Estate6System4SaveEv");
    if (g_campaign && g_roster && g_trinket_storage && g_slot_count && g_add_copy && g_remove_slot_item &&
        g_update_trinket_buffs && g_roster_save && g_estate_save) {
        // push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp,0xa98
        static const uint8_t unequip[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                          0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x98, 0x0A, 0x00, 0x00};
        *(void **)&o_unequip_button = install_hook("TownUI::Panel::RealmInventory::DrawUnequipButton",
                                                   sym("_ZN6TownUI5Panel14RealmInventory17DrawUnequipButtonE4vec2"),
                                                   (void *)h_unequip_button, unequip, sizeof unequip);
    }
    if (!o_unequip_button)
        logf_("stash view: the game's unequip button stays (trinkets only)");
    ui_invgrid_on(grid_begin, grid_end);
    // Both are tiny: a hook takes 14 bytes of whole instructions. UpdateBasePos: push rbp; mov rbp,rsp;
    // mov rax,[rdi+0x530]; mov [rsi],rax (then pop rbp; ret). UpdateBaseSize: push rbp; mov rbp,rsp; push rbx; push rax;
    // mov rbx,rsi; call TownUI::Assets::Current (rel32 at 10, moved like a RIP-relative operand).
    static const uint8_t base_pos[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x87,
                                       0x30, 0x05, 0x00, 0x00, 0x48, 0x89, 0x06},
                         base_size[] = {0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48,
                                        0x89, 0xF3, 0xE8, 0xD2, 0x29, 0xE9, 0xFF};
    static const HookRip size_call[] = {{10, 14}};
    *(void **)&o_base_pos = install_hook("TownUI::Panel::RealmInventory::UpdateBasePos",
                                         sym("_ZN6TownUI5Panel14RealmInventory13UpdateBasePosER4vec2"),
                                         (void *)h_base_pos, base_pos, sizeof base_pos);
    *(void **)&o_base_size = install_hook_rip("TownUI::Panel::RealmInventory::UpdateBaseSize",
                                              sym("_ZN6TownUI5Panel14RealmInventory14UpdateBaseSizeER4vec2"),
                                              (void *)h_base_size, base_size, sizeof base_size, size_call, 1);
    if (!o_base_pos || !o_base_size)
        logf_("stash view: the panel's rect isn't grown, tabs above it get no input");
    static const uint8_t area[] = PROLOGUE_SUB32(0x78, 0x04); // sub rsp,0x478
    *(void **)&o_trinket_area = install_hook("TownUI::Panel::RealmInventory::DrawTrinketArea",
                                             sym("_ZN6TownUI5Panel14RealmInventory15DrawTrinketAreaEv"),
                                             (void *)h_trinket_area, area, sizeof area);
    if (!o_trinket_area)
        logf_("stash view: the drop area keeps the game's rect");
    g_installed = true;
    static const uint8_t bar[] = PROLOGUE_SUB32(0x48, 0x0f); // sub rsp,0xf48
    *(void **)&o_bar_render =
        install_hook("ScrollBarWidget::RenderInternal", sym("_ZNK15ScrollBarWidget14RenderInternalEj"),
                     (void *)h_bar_render, bar, sizeof bar);
    for (int i = 0; o_bar_render && i < g_nlayouts; i++) { // each layout's scrolling is its mod's
        if (!g_layouts[i].scroll)
            continue;
        int was = mods_current();
        mods_set_current(g_layouts_mods.owner[i]);
        ui_invgrid_scroll("trinket_storage", g_layouts[i].scroll, g_layouts[i].scroll_key);
        mods_set_current(was);
        g_scrolls = true;
    }
    if (g_scrolls)
        ui_invgrid_scroll_height("trinket_storage", stash_height);
    else
        logf_("stash view: the stash keeps the game's scrolling");
    on_campaign();
    logf_("stash view: %d tab(s)", g_nall);
}
