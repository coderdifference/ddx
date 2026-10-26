// Inventory sizes: extra slots for every inventory of a config type, from providers (inv_slots_provider). See
// common.h ("inv_slots.c").
//
// An Inventory::System's slots are a std::vector<Inventory::Item> at +0x08 (0x5b0 bytes each; an empty slot has
// amount (+0x00) <= 0), its config type's hash at +0x20. Inventory::System::Init(type) and Restore(type, data) size
// the vector to the config's max_slots (Inventory::SystemClass::GetSystemConfigClass(type)+0x44, from
// inventory/*.inventory.system_configs.darkest; a later file's entry for a type replaces an earlier one's fields, so
// a mod can change the base). Nothing else reads max_slots for the size: the slot count is the vector's.
//
// The size is applied when a panel shows an inventory: InventorySystemInterface::SetInventorySystem(system, ...)
// builds one slot interface per slot (the Provisioner's OnEnterStart for its store and the party's supplies, the
// dungeon's Panel_Inventory::OnEnterPanel for Raid+0x220), so the system is resized just before (the panel's old slot
// interfaces, which point at the items, are rebuilt by the same call). The party's supplies are copied into the raid
// at embark, size and all. A change of size (a provider's answer changing) shows the next time a panel opens.
// Growing appends empty slots (std::vector<Inventory::Item>::__append); shrinking only drops empty slots at the end,
// never an item, so an inventory stays bigger until its last slots are emptied.
//
// Loading: Restore sizes the vector from max_slots before it reads the items, so a save made with more slots would
// lose the items past them. For the call, max_slots is raised to RESTORE_SLOTS, and the empty slots past the size
// are dropped after. Windows: the config lookup and the vector's resize are inlined (stand-ins in syms_win.c).
#include "common.h"
#include <string.h>

#define RESTORE_SLOTS 256
#define MAX_PROVIDERS 16

static struct {
    unsigned type;
    int (*fn)(void);
} g_prov[MAX_PROVIDERS];
static int g_nprov;
static ModsList g_prov_mods; // their mods: only an applied mod's add slots
static void **g_system_class;
static uint8_t *(*g_config)(const void *system_class, unsigned type);
static void (*g_append)(void *vec, unsigned long n);
static void (*g_destruct_at_end)(void *vec, void *new_end);

void inv_slots_provider(const char *config_type, int (*fn)(void)) {
    if (g_nprov == MAX_PROVIDERS || !config_type || !fn || mods_list_add(&g_prov_mods) < 0) {
        logf_("inv_slots: provider refused");
        return;
    }
    g_prov[g_nprov].type = name_hash(config_type);
    g_prov[g_nprov].fn = fn;
    g_nprov++;
}

static bool provided(unsigned type) {
    for (int i = 0; i < g_nprov; i++)
        if (g_prov[i].type == type)
            return true;
    return false;
}

static int base_of(unsigned type) {
    const uint8_t *c = g_system_class && *g_system_class ? g_config(*g_system_class, type) : NULL;
    return c ? *(const int *)(c + CONFIG_MAX_SLOTS) : -1;
}

static int slots_of(unsigned type) {
    int n = base_of(type);
    if (n < 0)
        return -1;
    MODS_EACH(g_prov_mods, false, i) if (g_prov[i].type == type) {
        int x = g_prov[i].fn();
        if (x > 0)
            n += x;
    }
    return n;
}

int inv_slots(const char *config_type) {
    return g_config ? slots_of(name_hash(config_type)) : -1;
}

// the system's slots to `want`, never dropping an item
static void resize(void *system, int want) {
    uint8_t **v = (uint8_t **)((uint8_t *)system + SYSTEM_ITEMS);
    if (!v[0] || want < 0)
        return;
    int cur = (int)((v[1] - v[0]) / ITEM_SIZE), last = cur;
    while (last > 0 && *(const int *)(v[0] + (size_t)(last - 1) * ITEM_SIZE) <= 0)
        last--;
    int target = want > last ? want : last;
    if (target == cur)
        return;
    if (target > cur)
        g_append(v, (unsigned long)(target - cur));
    else
        g_destruct_at_end(v, v[0] + (size_t)target * ITEM_SIZE);
    logf_("inv_slots: inventory %p: %d -> %d slots (wanted %d)", system, cur, target, want);
}

// ---------------------------------------------------------------- hooks
static void (*o_set)(void *iface, void *system, const void *id, void *fn);
static void h_set(void *iface, void *system, const void *id, void *fn) {
    if (system) {
        unsigned type = *(const unsigned *)((const uint8_t *)system + SYSTEM_CONFIG);
        if (provided(type))
            resize(system, slots_of(type));
    }
    ui_invgrid_reset(iface); // a panel opening: its grid starts at the top
    o_set(iface, system, id, fn);
}

static void (*o_restore)(void *system, unsigned type, void *data);
static void h_restore(void *system, unsigned type, void *data) {
    uint8_t *c = provided(type) && g_system_class && *g_system_class ? g_config(*g_system_class, type) : NULL;
    if (!c) {
        o_restore(system, type, data);
        return;
    }
    int base = *(int *)(c + CONFIG_MAX_SLOTS);
    if (base < RESTORE_SLOTS)
        *(int *)(c + CONFIG_MAX_SLOTS) = RESTORE_SLOTS;
    o_restore(system, type, data);
    *(int *)(c + CONFIG_MAX_SLOTS) = base;
    resize(system, slots_of(type));
}

void inv_slots_install(void) {
    g_system_class = (void **)sym("_ZN9Inventory11SystemClass10s_InstanceE");
    g_config = (uint8_t *(*)(const void *, unsigned))sym("_ZNK9Inventory11SystemClass20GetSystemConfigClassEj");
    g_append =
        (void (*)(void *, unsigned long))sym("_ZNSt3__16vectorIN9Inventory4ItemENS_9allocatorIS2_EEE8__appendEm");
    g_destruct_at_end = (void (*)(void *, void *))sym(
        "_ZNSt3__16vectorIN9Inventory4ItemENS_9allocatorIS2_EEE22__base_destruct_at_endB8ne200100EPS2_");
    if (!g_system_class || !g_config || !g_append || !g_destruct_at_end) {
        logf_("inv_slots: symbols missing, inventories keep their size");
        g_config = NULL;
        return;
    }
    static const uint8_t set[] = PROLOGUE_SUB8(0x78), restore[] = PROLOGUE_SUB32(0xf8, 0x05);
    *(void **)&o_set = install_hook("InventorySystemInterface::SetInventorySystem",
                                    sym("_ZN24InventorySystemInterface18SetInventorySystemEPN9Inventory6SystemERK12Stro"
                                        "ngTypeIdI22InventorySlotInterfaceENSt3__18functionIFvPS4_EEE"),
                                    (void *)h_set, set, sizeof set);
    *(void **)&o_restore =
        install_hook("Inventory::System::Restore", sym("_ZN9Inventory6System7RestoreEjP11RestoreData"),
                     (void *)h_restore, restore, sizeof restore);
    if (!o_set || !o_restore)
        logf_("inv_slots: hooks missing, inventories keep their size");
}
