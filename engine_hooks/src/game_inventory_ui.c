// Native drag/drop interface ownership and layout stay behind semantic inventory UI operations.
#include "common.h"
static bool (*full_swap)(const void *);
static void (*next_accepted)(void *, void *);
static void accepted(void *slot, void *drag) {
    uint8_t *mine = slot ? *(uint8_t **)((uint8_t *)slot + SLOT_ITEM) : NULL;
    const void *back = item_drag_item(drag);
    unsigned old = 0, changed = 0;
    bool patch = mine && back && full_swap && full_swap(mine) && full_swap(back);
    if (patch) {
        old = *(unsigned *)(mine + ITEM_TYPE_HASH);
        // A different type hash forces the native drop path to swap the entire item instead of stacking.
        changed = ~old;
        *(unsigned *)(mine + ITEM_TYPE_HASH) = changed;
    }
    next_accepted(slot, drag);
    uint8_t *now = slot ? *(uint8_t **)((uint8_t *)slot + SLOT_ITEM) : NULL;
    // The drop may replace the slot item; restore only the sentinel hash on the resulting item.
    if (patch && now && *(unsigned *)(now + ITEM_TYPE_HASH) == changed)
        *(unsigned *)(now + ITEM_TYPE_HASH) = old;
}
bool game_inventory_full_swap(bool (*predicate)(const void *)) {
    if (!predicate || full_swap)
        return false;
    if (!game_hook_wrap("InventoryItemSlotInterface::OnDropAccepted", (void *)accepted, (void **)&next_accepted))
        return false;
    full_swap = predicate;
    return true;
}
void *game_inventory_cell_inventory(void *cell) {
    return cell ? *(void **)((uint8_t *)cell + CELL_INVENTORY) : NULL;
}
void *game_inventory_cell_item(void *cell) {
    return cell ? *(void **)((uint8_t *)cell + CELL_ITEM) : NULL;
}
void *game_stash_empty_cell(void *realm) {
    static unsigned (*slots)(const void *), (*count)(const void *);
    static uint8_t *(*item)(void *, unsigned);
    static void *(*at)(void *, unsigned);
    if (!slots)
        *(void **)&slots = sym("_ZNK9Inventory6System17GetTotalSlotCountEv");
    if (!count)
        *(void **)&count = sym("_ZNK24InventorySystemInterface12GetSlotCountEv");
    if (!item)
        *(void **)&item = sym("_ZN9Inventory6System20GetItemFromSlotIndexEj");
    if (!at)
        *(void **)&at = sym("_ZN24InventorySystemInterface16GetSlotInterfaceEj");
    void *inv = game_estate_storage(), *iface = realm ? *(void **)((uint8_t *)realm + REALM_INTERFACE) : NULL;
    if (!inv || !iface || !slots || !count || !item || !at)
        return NULL;
    unsigned n = slots(inv), m = count(iface);
    for (unsigned i = 0; i < n && i < m; i++)
        if (*(int *)item(inv, i) <= 0)
            return at(iface, i);
    return NULL;
}
bool ui_realm_header_split(const char *title, const char *blank, const char *texture, bool (*placement)(Vec2 *)) {
    const uint8_t *show = sym("_ZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEv");
    const uint8_t *caller = call_site_return(show, GL(0x577, 0x567), NULL);
    return caller && ui_name_frame_split(caller, title, blank, texture, placement);
}
