// Registered item types' prices and stash salvage (ItemTypeDef.price, .salvage_quote, .salvage). See common.h
// ("item_sale.c") and docs/11.
//
// Prices. The game has no price function: wherever a trinket's price matters it looks up the item's
// Inventory::ItemClass (Inventory::SystemClass::GetItemClass(type hash, id hash), or the class cached on the item at
// +0x5a0) and reads the "price" from its data file inline: +0x9c to buy (Upgrade::GetAmountWithTagDiscount(price,
// k_Trinket): the wagon's discount upgrades and town events), +0xa4 to sell (k_TrinketSellBack). Trinket::Library::
// LoadEntriesFile stores the entry's price in both; another type's class (its inventory_item file) its
// purchase_gold_value and sell_gold_value. Every item of one id shares one class, so an item's own price can't live
// there.
//
// So the price is set per item while the game looks at that item, and put back after: a price scope writes the type's
// prices into the class's +0x9c and +0xa4, and popping it restores what was there (scopes nest; items of a type with
// no price callback never get one). Scopes, around the code that reads the price of one item:
//   Inventory::Display::InventoryButton(params, ...)  every item button (wagon stock, estate inventory) and the tooltip
//                                                     it opens (ShowItemTooltip): the item is at params+0
//   the Nomad Wagon's buy lambda (TrinketSupplyDisplay::PreChildrenShowInternal()::$_1, void(unsigned slot)): the item
//                                                     is Inventory::System::GetItem(slot) on the stock at lambda+0x10
//   the estate's sale (stash_view_on_sell: the sell lambda RealmInventory::PreChildrenShowInternal()::$_0, where the
//                                                     sale's gold is worked out; the confirm dialog's lambda only adds it)
//   the stash panel (stash_view_on_panel): after InventoryButtonGrid (ui_invgrid.c's observers) it shows the sell value
//                                                     of the item that grid returned (the one under the cursor); that
//                                                     item's scope is pushed when the grid returns and popped when the
//                                                     panel finishes
//
// Salvage. Each frame, after the stash panel draws (stash_view_on_panel, in the panel's transform), with Ctrl held (and
// Shift not) over an item whose type quotes a salvage:
// - the quote's label where the game puts "Sell for:" (TownUI::Assets+0x1b58), in place of the hint under the grid
//   (hidden by stash_view.c, as Shift's line replaces it), and the quote's icon and amount centred on the game's price
//   position (+0x1b64, as CostDisplay::CurrencyCost centres the coin and amount), each moved by the layout;
// - a Ctrl-click on the item (ui_input.c: pressed and released with Ctrl held, within click_slop units; either button,
//   as macOS makes Ctrl + left click a right click) calls the type's salvage. The "don't ask" flag it gets lasts until
//   the stash closes (a frame without it).
// The input comes from SDL (ui_input.c): the grid's cells take the GUI's press, and start their drag, before we look.
// The item is the one hovered when the press is seen (or the frame before, if a drag took it off the hover), found
// again at the click by its item id (itemdata.c) in the stash's storage; released without moving, the game puts it
// back in its cell.
// Windows (MSVC): InventoryButton returns its 6-byte result through a hidden pointer before params (and destroys its
// std::functions itself; we pass them through); the wagon's buy lambda keeps its stock at +8 (WAGON_BUY_STOCK).
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_SCOPES 32

typedef struct {
    uint8_t *cls;
    int buy, sell;
} Scope;
static Scope g_scopes[MAX_SCOPES];
static int g_nscopes, g_in_realm, g_sell_depth, g_realm_depth;

static void **g_system_class;                                           // &Inventory::SystemClass::s_Instance
static void *(*g_item_class)(const void *, unsigned type, unsigned id); // SystemClass::GetItemClass
static unsigned (*g_id_hash)(const void *);
static void *(*g_get_item)(void *inventory, unsigned slot); // Inventory::System::GetItem
static unsigned (*g_slot_count)(const void *inventory);
static void *(*g_slot_item)(void *inventory, unsigned slot);
static uint8_t *(*g_town_assets)(void);

// the salvage prompt's layout: the top applied mod's (item_sale_layout records each with its mod; picked again at each
// campaign load and close)
#define MAX_LAYOUTS 4
static struct {
    UiLayout *l;
    char key[48];
} g_layouts[MAX_LAYOUTS];
static int g_nlayouts;
static ModsList g_layouts_mods;
static UiLayout *g_layout;
static const char *g_key = "";

static const ItemTypeDef *type_of(const void *item) {
    return item ? item_type_find(*(const unsigned *)((const uint8_t *)item + ITEM_TYPE_HASH)) : NULL;
}

static uint8_t *class_of(const void *item) {
    uint8_t *cls = *(uint8_t *const *)((const uint8_t *)item + ITEM_CLASS);
    if (!cls && g_system_class && *g_system_class)
        cls =
            g_item_class(*g_system_class, *(const unsigned *)((const uint8_t *)item + ITEM_TYPE_HASH), g_id_hash(item));
    return cls;
}

// ---------------------------------------------------------------- prices
// The item's prices into its class until pop_to; false (nothing pushed) for a type with no price callback.
static bool push(const void *item) {
    const ItemTypeDef *def = type_of(item);
    if (!def || !def->price || g_nscopes == MAX_SCOPES)
        return false;
    uint8_t *cls = class_of(item);
    if (!cls)
        return false;
    Scope *s = &g_scopes[g_nscopes++];
    s->cls = cls;
    memcpy(&s->buy, cls + CLASS_BUY, 4);
    memcpy(&s->sell, cls + CLASS_SELL, 4);
    int buy = s->buy, sell = s->sell;
    def->price(item, &buy, &sell, def->ud);
    memcpy(cls + CLASS_BUY, &buy, 4);
    memcpy(cls + CLASS_SELL, &sell, 4);
    return true;
}

static void pop_to(int depth) {
    while (g_nscopes > depth) {
        Scope *s = &g_scopes[--g_nscopes];
        memcpy(s->cls + CLASS_BUY, &s->buy, 4);
        memcpy(s->cls + CLASS_SELL, &s->sell, 4);
    }
}

#ifdef _WIN32
typedef void *(*ButtonFn)(void *result, const void *params, const void *actor, void *fn1, void *fn2, int b1, void *anim,
                          void *name, int b2, void *sprite, void *fn3, int b3);
static ButtonFn o_button;
static void *h_button(void *result, const void *params, const void *actor, void *fn1, void *fn2, int b1, void *anim,
                      void *name, int b2, void *sprite, void *fn3, int b3) {
    int depth = g_nscopes;
    push(params ? *(void *const *)params : NULL);
    void *r = o_button(result, params, actor, fn1, fn2, b1, anim, name, b2, sprite, fn3, b3);
    pop_to(depth);
    return r;
}
#else
typedef uint64_t (*ButtonFn)(const void *params, const void *actor, void *fn1, void *fn2, int b1, void *anim,
                             void *name, int b2, void *sprite, void *fn3, int b3);
static ButtonFn o_button;
static uint64_t h_button(const void *params, const void *actor, void *fn1, void *fn2, int b1, void *anim, void *name,
                         int b2, void *sprite, void *fn3, int b3) {
    int depth = g_nscopes;
    push(params ? *(void *const *)params : NULL);
    uint64_t r = o_button(params, actor, fn1, fn2, b1, anim, name, b2, sprite, fn3, b3);
    pop_to(depth);
    return r;
}
#endif

static void (*o_buy)(void *lambda, unsigned *slot);
static void h_buy(void *lambda, unsigned *slot) {
    int depth = g_nscopes;
    void *inv = *(void **)((uint8_t *)lambda + WAGON_BUY_STOCK);
    if (inv && slot)
        push(g_get_item(inv, *slot));
    o_buy(lambda, slot);
    pop_to(depth);
}

static void sell_before(void *item) {
    g_sell_depth = g_nscopes;
    push(item);
}
static void sell_after(void) {
    pop_to(g_sell_depth);
}

// ---------------------------------------------------------------- salvage
static void *g_hovered, *g_hovered_inv; // the stash item under the cursor this frame, and its storage
static void *g_inv;                     // the stash's storage (the last grid with a hovered item)
static int g_hover_iid;                 // last frame's hovered item, if it could be salvaged
static int g_press_iid;                 // a Ctrl-press began on this item (0: none)
static bool g_prompting, g_dont_ask;
static time_t g_seen;

bool item_sale_prompting(void) {
    return g_prompting;
}

static void pick_layout(void) {
    int i = mods_list_first(&g_layouts_mods);
    g_layout = i >= 0 ? g_layouts[i].l : NULL;
    g_key = i >= 0 ? g_layouts[i].key : "";
}

void item_sale_layout(UiLayout *l, const char *key) {
    if (g_nlayouts == MAX_LAYOUTS || mods_list_add(&g_layouts_mods) < 0) {
        logf_("item sale: layout refused");
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

static char g_k[96];
static const char *k(const char *name) {
    snprintf(g_k, sizeof g_k, "%s.%s", g_key, name);
    return g_k;
}

// the prompt applies: Ctrl (not Shift) over an item whose type quotes a salvage. The click finds the item again by its
// id, so an item saved without one (gear from before ids, or added by a path that never asked) gets one here.
static bool quoting(void *item, ItemSalvageQuote *q) {
    const ItemTypeDef *def = type_of(item);
    unsigned m = ui_modifiers();
    memset(q, 0, sizeof *q);
    return def && def->salvage_quote && def->salvage && (m & UI_MOD_CTRL) && !(m & UI_MOD_SHIFT) &&
           itemdata_ensure_id(item) && def->salvage_quote(item, q, def->ud);
}

static void prompt(const ItemSalvageQuote *q) {
    const uint8_t *assets = g_town_assets();
    Vec2 t = *(const Vec2 *)(assets + ASSETS_SELL_TEXT_POS), p = *(const Vec2 *)(assets + ASSETS_SELL_PRICE_POS);
    Vec2 to = ui_layout_vec2(g_layout, k("text_offset"), (Vec2){0, 0});
    Vec2 po = ui_layout_vec2(g_layout, k("price_offset"), (Vec2){0, 0});
    float is = ui_layout_num(g_layout, k("icon_size"), 24), gap = ui_layout_num(g_layout, k("icon_gap"), 4);
    char font[64], colour[64];
    snprintf(font, sizeof font, "%s", ui_layout_str(g_layout, k("font"), "realm_inventory_trinket_sell_description"));
    snprintf(colour, sizeof colour, "%s",
             ui_layout_str(g_layout, k("colour"), "realm_inventory_trinket_sell_description"));
    const char *amount_font = ui_layout_str(g_layout, k("amount_font"), "realm_inventory_sell_trinket");
    if (q->label)
        ui_text_anchored((Vec2){t.x + to.x, t.y + to.y}, ui_loc(q->label), font, colour, 0, 0);
    char amount[16];
    snprintf(amount, sizeof amount, "%d", q->amount);
    // the icon and amount centred on the price position, across and down
    float x0 = p.x + po.x - (is + gap + ui_text_width(amount_font, amount)) / 2, ym = p.y + po.y;
    uint8_t tex[16];
    if (q->icon && ui_texture(q->icon, tex))
        ui_image_sized((Vec2){x0, ym - is / 2}, tex, (Vec2){is, is});
    ui_text_anchored((Vec2){x0 + is + gap, ym}, amount, amount_font, colour, 3, 0); // 3: middle left
}

// the stash's item with that id now (its slots move)
static void *find(void *inv, int iid) {
    unsigned n = inv && iid ? g_slot_count(inv) : 0;
    for (unsigned i = 0; i < n; i++) {
        void *item = g_slot_item(inv, i);
        if (item && *(const int *)((const uint8_t *)item + ITEM_AMOUNT) > 0 && itemdata_id(item) == iid)
            return item;
    }
    return NULL;
}

static void salvage_frame(void) {
    void *hovered = g_hovered;
    if (g_hovered_inv)
        g_inv = g_hovered_inv;
    g_hovered = g_hovered_inv = NULL;
    time_t now = time(NULL);
    if (now - g_seen > 1)
        g_dont_ask = false; // the stash was closed in between: ask again
    g_seen = now;

    // a Ctrl-click (either button: macOS makes Ctrl + left click a right click): the press on the hovered item, the
    // release near it with Ctrl still held
    ItemSalvageQuote q;
    bool on = hovered && quoting(hovered, &q);
    if (ui_ctrl_pressed() >= 0) // this frame's hovered item, else last frame's (a drag took it)
        g_press_iid = !(ui_modifiers() & UI_MOD_SHIFT) ? (on ? itemdata_id(hovered) : g_hover_iid) : 0;
    if (ui_ctrl_click(ui_layout_num(g_layout, k("click_slop"), 8)) >= 0 && g_press_iid) {
        void *item = find(g_inv, g_press_iid);
        const ItemTypeDef *def = type_of(item);
        logf_("item sale: Ctrl-click salvage on item #%d%s", g_press_iid, def && def->salvage ? "" : " (gone)");
        if (def && def->salvage)
            def->salvage(item, &g_dont_ask, def->ud);
    }
    if (ui_mouse(UI_MOUSE_LEFT)->released || ui_mouse(UI_MOUSE_RIGHT)->released)
        g_press_iid = 0;
    g_hover_iid = on ? itemdata_id(hovered) : 0;
    g_prompting = on;
    if (on)
        prompt(&q);
}

// ---------------------------------------------------------------- the stash panel
static void realm_before(void *panel) {
    (void)panel;
    g_realm_depth = g_nscopes;
    g_in_realm++;
}
static void realm_after(void *panel) {
    (void)panel;
    g_in_realm--;
    salvage_frame(); // drawn over the panel
    pop_to(g_realm_depth);
}

// after every inventory grid (ui_invgrid.c)
static void grid_end(void *iface, void *hovered) {
    if (!g_in_realm || !hovered)
        return;
    push(hovered); // popped when the panel finishes (realm_after)
    g_hovered = hovered;
    g_hovered_inv = iface ? *(void **)((uint8_t *)iface + IFACE_SYSTEM) : NULL;
}

// ---------------------------------------------------------------- install
void item_sale_install(void) {
    bool prices = false, salvage = false;
    for (int i = 0; i < item_types_count(); i++) {
        const ItemTypeDef *d = item_type_at(i);
        prices |= d->price != NULL;
        salvage |= d->salvage_quote && d->salvage;
    }
    if (!prices && !salvage)
        return;
    g_system_class = (void **)sym("_ZN9Inventory11SystemClass10s_InstanceE");
    g_item_class = (void *(*)(const void *, unsigned, unsigned))sym("_ZNK9Inventory11SystemClass12GetItemClassEjj");
    g_id_hash = (unsigned (*)(const void *))sym("_ZNK9Inventory4Item9GetIdHashEv");
    g_get_item = (void *(*)(void *, unsigned))sym("_ZN9Inventory6System7GetItemEj");
    g_slot_count = (unsigned (*)(const void *))sym("_ZNK9Inventory6System17GetTotalSlotCountEv");
    g_slot_item = (void *(*)(void *, unsigned))sym("_ZN9Inventory6System20GetItemFromSlotIndexEj");
    g_town_assets = (uint8_t *(*)(void))sym("_ZN6TownUI6Assets7CurrentEv");
    if (!g_system_class || !g_item_class || !g_id_hash || !g_get_item || !g_slot_count || !g_slot_item ||
        !g_town_assets || !ui_invgrid_ready()) {
        logf_("item sale: symbols missing, off (items keep their class's price)");
        return;
    }
    if (prices) {
        static const uint8_t button[] = PROLOGUE_SUB32(0x98, 0x09), buy[] = PROLOGUE_SUB32(0xA8, 0x00);
        *(void **)&o_button = install_hook(
            "Inventory::Display::InventoryButton",
            sym("_ZN9Inventory7Display15InventoryButtonERKNS0_25InventoryButtonParametersEPK5ActorNSt3__"
                "18functionIFvvEEESA_bPNS0_20TreasureItemAnimVarsEP17FixedLengthStringILi64EEbP11SpineSpriteNS8_IFPKSG_"
                "PKNS_4ItemEjbEEEb"),
            (void *)h_button, button, sizeof button);
        *(void **)&o_buy = install_hook(
            "TrinketSupplyDisplay buy",
            sym("_ZNSt3__110__function6__funcIZN6TownUI5Panel20TrinketSupplyDisplay23PreChildrenShowInternalEvE3$_1NS_"
                "9allocatorIS5_EEFvjEEclEOj"),
            (void *)h_buy, buy, sizeof buy);
        stash_view_on_sell(sell_before, sell_after);
    }
    stash_view_on_panel(realm_before, realm_after);
    ui_invgrid_on(NULL, grid_end);
}
