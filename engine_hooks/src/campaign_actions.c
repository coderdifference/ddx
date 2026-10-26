// Small, ownership-preserving campaign operations for native and managed clients.
#include "common.h"

static bool g_resolved;
static void *(*g_campaign)(void), *(*g_roster)(void);
static void *(*g_hero)(void *, unsigned), *(*g_storage)(void *, int);
static int (*g_amount)(const void *, unsigned);
static void (*g_add)(void *, unsigned, int), (*g_save)(void *);

static void resolve(void) {
    if (g_resolved)
        return;
    g_resolved = true;
    *(void **)&g_campaign = sym("_ZN8Campaign7CurrentEv");
    *(void **)&g_roster = sym("_ZN8Campaign13CurrentRosterEv");
    *(void **)&g_hero = sym("_ZN6Roster6System7GetHeroEj");
    *(void **)&g_storage = sym("_ZN6Estate6System17GetTrinketStorageENS0_19ETrinketStorageTypeE");
    *(void **)&g_amount = sym("_ZNK6Estate6System9GetAmountEj");
    *(void **)&g_add = sym("_ZN6Estate6System9AddAmountEji");
    *(void **)&g_save = sym("_ZN6Estate6System4SaveEv");
}

static void *estate(void) {
    resolve();
    void *campaign = g_campaign ? g_campaign() : NULL;
    return campaign ? (uint8_t *)campaign + CAMPAIGN_ESTATE : NULL;
}

void *game_estate_storage(void) {
    void *e = estate();
    return e && g_storage ? g_storage(e, 0) : NULL;
}

int game_estate_amount(unsigned currency) {
    void *e = estate();
    return e && g_amount ? g_amount(e, currency) : 0;
}

bool game_estate_add(unsigned currency, int amount) {
    void *e = estate();
    if (!e || !g_add || !g_save)
        return false;
    if (amount)
        g_add(e, currency, amount);
    g_save(e);
    return true;
}

bool game_estate_pay(const CurrencyCost *costs, int count) {
    void *e = estate();
    if (!e || !g_amount || !g_add || !g_save || count < 0 || (count && !costs))
        return false;
    // Validate the whole payment before mutating balances. Duplicate currencies would bypass this check.
    for (int i = 0; i < count; i++) {
        if (costs[i].amount < 0 || g_amount(e, costs[i].currency) < costs[i].amount)
            return false;
        for (int j = 0; j < i; j++)
            if (costs[j].currency == costs[i].currency)
                return false;
    }
    for (int i = 0; i < count; i++)
        if (costs[i].amount)
            g_add(e, costs[i].currency, -costs[i].amount);
    g_save(e);
    return true;
}

void *game_roster_hero(unsigned guid) {
    resolve();
    void *roster = g_roster ? g_roster() : NULL;
    return roster && g_hero ? g_hero(roster, guid) : NULL;
}

int game_roster_heroes(void **out, int max) {
    resolve();
    const uint8_t *roster = g_roster ? g_roster() : NULL;
    if (!roster || !out || max <= 0)
        return 0;
    const uint8_t *const *begin = *(const uint8_t *const *const *)(roster + ROSTER_ENTRIES);
    const uint8_t *const *end = *(const uint8_t *const *const *)(roster + ROSTER_ENTRIES + 8);
    // Roster entries embed their Hero; return borrowed views and skip status 3 entries.
    int n = 0;
    for (const uint8_t *const *p = begin; begin && p < end && n < max; p++)
        if (*p && *(const int *)(*p + ENTRY_STATUS) != 3)
            out[n++] = (void *)(*p + ROSTER_ENTRY_HERO);
    return n;
}

int game_party_heroes(void **out, int max, bool living_only) {
    CombatContext context;
    if (!out || max <= 0 || !combat_context_build(&context, NULL))
        return 0;
    int n = 0;
    for (int i = 0; i < context.nheroes && n < max; i++)
        if (!living_only || context.heroes[i].alive)
            out[n++] = context.heroes[i].actor;
    return n;
}

int64_t game_inventory_amount(const void *inventory, unsigned type) {
    if (!inventory)
        return 0;
    const uint8_t *begin = *(const uint8_t *const *)((const uint8_t *)inventory + SYSTEM_ITEMS);
    const uint8_t *end = *(const uint8_t *const *)((const uint8_t *)inventory + SYSTEM_ITEMS + 8);
    int64_t amount = 0;
    for (const uint8_t *p = begin; begin && p + ITEM_SIZE <= end; p += ITEM_SIZE)
        if (*(const unsigned *)(p + ITEM_TYPE_HASH) == type && *(const int *)(p + ITEM_AMOUNT) > 0)
            amount += *(const int *)(p + ITEM_AMOUNT);
    return amount;
}
