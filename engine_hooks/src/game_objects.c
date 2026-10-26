// Semantic game-object access for clients. No native layout is part of the managed contract.
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
void *game_current_raid(void) {
    static void **p;
    if (!p)
        p = sym("_ZN4Raid10s_InstanceE");
    return p ? *p : NULL;
}
const char *game_actor_name(const void *a) {
    return a ? (const char *)a + ACTOR_NAME : "";
}
unsigned game_actor_rank(const void *a) {
    return a ? *(const unsigned *)((const uint8_t *)a + ACTOR_RANK) : 0;
}
float game_actor_stress(const void *a) {
    return a ? *(const float *)((const uint8_t *)a + ACTOR_STRESS) : 0;
}
void game_actor_set_stress(void *a, float v) {
    if (a)
        *(float *)((uint8_t *)a + ACTOR_STRESS) = v;
}
bool game_actor_is_corpse(const void *a) {
    static bool (*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK5Actor8IsCorpseEv");
    return a && fn && fn(a);
}
float game_hero_resolve(const void *a) {
    static float (*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK4Hero15GetResolveLevelEv");
    return a && fn ? fn(a) : 0;
}
bool game_hero_suppresses_death_loot(const void *a) {
    return a && *((const uint8_t *)a + HERO_NO_QUIRK_ACT_OUTS);
}
int game_item_amount(const void *item) {
    return item ? *(const int *)((const uint8_t *)item + ITEM_AMOUNT) : 0;
}
const char *game_item_type(const void *item) {
    return item ? (const char *)item + ITEM_TYPE_STRING : "";
}
const char *game_item_id(const void *item) {
    return item ? (const char *)item + ITEM_ID : "";
}
unsigned game_item_rarity(const void *item) {
    static void *(*find)(unsigned);
    if (!find)
        *(void **)&find = sym("_ZN7Trinket7Library15GetTrinketClassEj");
    static void **library;
    static void *(*item_class)(const void *, unsigned, unsigned);
    if (!library)
        library = sym("_ZN9Inventory11SystemClass10s_InstanceE");
    if (!item_class)
        *(void **)&item_class = sym("_ZNK9Inventory11SystemClass12GetItemClassEjj");
    const uint8_t *bytes = item;
    // ITEM_CLASS is populated lazily by the game; resolve through the library even for uncached items.
    const uint8_t *cls = bytes && library && *library && item_class
                             ? item_class(*library, *(const unsigned *)(bytes + ITEM_TYPE_HASH),
                                          *(const unsigned *)(bytes + ITEM_ID_HASH))
                             : NULL;
    const uint8_t *trinket = cls && find ? find(*(const unsigned *)(cls + ITEMCLASS_TRINKET_ID)) : NULL;
    return trinket ? *(const unsigned *)(trinket + TRINKETCLASS_RARITY) : 0;
}
int game_inventory_count(const void *inv) {
    const uint8_t *const *v = inv ? (const uint8_t *const *)((const uint8_t *)inv + SYSTEM_ITEMS) : NULL;
    return v && v[0] && v[1] >= v[0] ? (int)((v[1] - v[0]) / ITEM_SIZE) : 0;
}
void *game_inventory_item(void *inv, int i) {
    return i >= 0 && i < game_inventory_count(inv) ? *(uint8_t **)((uint8_t *)inv + SYSTEM_ITEMS) + i * ITEM_SIZE
                                                   : NULL;
}
void game_inventory_remove(void *inv, const void *item) {
    static void (*fn)(void *, const void *);
    if (!fn)
        *(void **)&fn = sym("_ZN9Inventory6System10RemoveItemEPKNS_4ItemE");
    if (inv && item && fn)
        fn(inv, item);
}
void *game_raid_random(void *battle) {
    return battle ? (uint8_t *)battle + BATTLE_RNG : NULL;
}
int game_raid_difficulty(const void *raid) {
    static int (*fn)(void *);
    if (!fn)
        *(void **)&fn = sym("_ZN5Quest12RaidInstance13GetDifficultyEv");
    void *q = raid ? *(void *const *)((const uint8_t *)raid + RAID_QUEST) : NULL;
    return q && fn ? fn(q) : 0;
}
bool game_raid_in_battle(const void *raid) {
    static bool (*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK4Raid10IsInBattleEv");
    return raid && fn && fn(raid);
}
void *game_raid_actor(void *raid, int i) {
    static unsigned (*count)(const void *);
    static void *(*at)(const void *, unsigned);
    if (!count)
        *(void **)&count = sym("_ZNK4Raid17GetNumberOfActorsEv");
    if (!at)
        *(void **)&at = sym("_ZNK4Raid17GetActorFromIndexEj");
    return raid && count && at && i >= 0 && (unsigned)i < count(raid) ? at(raid, (unsigned)i) : NULL;
}
int game_raid_actor_count(const void *raid) {
    static unsigned (*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK4Raid17GetNumberOfActorsEv");
    return raid && fn ? (int)fn(raid) : 0;
}
void *game_quest_reward(void *vector, int i) {
    uint8_t **v = vector;
    return v && v[0] && i >= 0 && v[0] + (i + 1) * QUEST_SIZE <= v[1] ? v[0] + i * QUEST_SIZE + QUEST_REWARDS : NULL;
}
void *game_party_selected(void *party) {
    return party ? *(void **)((uint8_t *)party + PARTY_SELECTED) : NULL;
}
void *game_roster_element_hero(void *element) {
    uint8_t *e = element ? *(uint8_t **)((uint8_t *)element + ROSTER_ELEMENT_ENTRY) : NULL;
    return e ? e + ROSTER_ENTRY_HERO : NULL;
}
unsigned game_buff_source(const void *b) {
    return b ? *(const unsigned *)((const uint8_t *)b + BUFF_SOURCE) : 0;
}
unsigned game_buff_source_id(const void *b) {
    return b ? *(const unsigned *)((const uint8_t *)b + BUFF_SOURCE_ID) : 0;
}
const char *game_buff_id(const void *b) {
    return b ? (const char *)b + BUFF_ID : "";
}
bool game_actor_add_equipment_buff(void *hero, unsigned buff, unsigned source) {
    static void (*fn)(void *, unsigned, unsigned, int);
    if (!fn)
        *(void **)&fn = sym("_ZZN5Actor18UpdateTrinketBuffsEvENK3$_5clEjjN4Buff6SourceE");
    if (!hero || !fn)
        return false;
#ifdef _WIN32
    fn(&hero, buff, source, 6);
#else
    fn(hero, buff, source, 6);
#endif
    return true;
}
float game_effect_stress_heal(const void *e) {
    return e ? *(const float *)((const uint8_t *)e + EFFECT_HEALSTRESS) : 0;
}
bool game_effect_is_dot(const void *e) {
    return e && (*(const int *)((const uint8_t *)e + EFFECT_DOT_BLEED) ||
                 *(const int *)((const uint8_t *)e + EFFECT_DOT_POISON));
}
unsigned game_effect_stress_heal_field(void) {
    return EFFECT_HEALSTRESS;
}
void *game_skill_effect(void *skill, int i) {
    void ***v = skill ? (void ***)((uint8_t *)skill + SKILL_BASE_EFFECTS) : NULL;
    return v && v[0] && v[1] >= v[0] && i >= 0 && i < v[1] - v[0] && i < 1024 ? v[0][i] : NULL;
}
void *game_supply_inventory(void *supply) {
    static void *(*fn)(void *);
    if (!fn)
        *(void **)&fn = sym("_ZN8Building5Store17BaseTrinketSupply18GetInventorySystemEv");
    return supply && fn ? fn(supply) : NULL;
}
bool game_raid_queue_death_item(void *raid, const void *item) {
    static void (*queue)(void *, const void *), (*set_type)(void *, const void *);
    static void *death_type;
    if (!queue)
        *(void **)&queue = sym("_ZN4Loot6System14QueueOwnedItemERKN9Inventory4ItemE");
    if (!set_type)
        *(void **)&set_type = sym("_ZN4Loot6System15SetGenerateTypeEPKNS_8Generate4TypeE");
    if (!death_type) {
        uint8_t *death = sym("_ZN4Hero21OnDeathFinishInternalEv");
        unsigned off = GL(0x145, 0x1a6);
        if (death && death[off] == 0x48 && death[off + 1] == 0x8d && death[off + 2] == GL(0x35, 0x0d))
            death_type = death + off + 7 + *(int *)(death + off + 3);
    }
    if (!raid || !item || !queue)
        return false;
    queue((uint8_t *)raid + RAID_LOOT, item);
    if (set_type && death_type)
        set_type((uint8_t *)raid + RAID_LOOT, death_type);
    return true;
}
bool game_buff_description(unsigned hash, const void *hero, char *out, size_t cap) {
    static void **library;
    static void *(*get)(void *, unsigned);
    static bool (*has)(const void *);
    static char *(*describe)(char *, const void *, const void *);
    if (!library)
        library = sym("_ZN4Buff7Library10s_InstanceE");
    if (!get)
        *(void **)&get = sym("_ZN4Buff7Library7GetBuffEj");
    if (!has)
        *(void **)&has = sym("_ZN4Buff7Display17GetHasDescriptionEPKNS_8InstanceE");
    if (!describe)
        *(void **)&describe = sym("_ZN4Buff7Display14GetDescriptionEPKNS_8InstanceEPK5Actor");
    void *b = library && *library && get ? get(*library, hash) : NULL;
    if (!b || !has || !has(b) || !describe || !out || !cap)
        return false;
    _Alignas(16) char text[512] = {0};
    describe(text, b, hero);
    snprintf(out, cap, "%s", text);
    return true;
}
void *game_current_raid_display(void) {
    static void **p;
    if (!p)
        p = sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    return p ? *p : NULL;
}
int game_attack_target_count(const void *attack) {
    const uint8_t *const *v = attack ? (const uint8_t *const *)((const uint8_t *)attack + ATTACK_TARGETS) : NULL;
    return v && v[0] && v[1] >= v[0] ? (int)((v[1] - v[0]) / 0x20) : 0;
}
int game_hero_negative_quirk(const void *hero, int selected) {
    const uint8_t *const *v = hero ? (const uint8_t *const *)((const uint8_t *)hero + HERO_QUIRKS) : NULL;
    if (!v || !v[0] || v[1] < v[0])
        return -1;
    int n = 0;
    for (const uint8_t *p = v[0]; p + QUIRK_SIZE <= v[1]; p += QUIRK_SIZE) {
        const uint8_t *cls = *(const uint8_t *const *)p;
        if (cls && !cls[QUIRKCLASS_POSITIVE] && n++ == selected)
            return (int)((p - v[0]) / QUIRK_SIZE);
    }
    return -1;
}
int game_raid_tab(const void *display) {
    return display ? *(const int *)((const uint8_t *)display + GL(0x16b8, 0x1768)) : 0;
}
void game_raid_set_tab(void *display, int tab) {
    if (display)
        *(int *)((uint8_t *)display + GL(0x16b8, 0x1768)) = tab;
}
bool game_raid_reorder_available(void *panel) {
    static float (*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK10TweenFloatcvfEv");
    return panel && fn && fn((uint8_t *)panel + GL(0x10, 8)) > 0.5f;
}
void game_raid_reorder(void *display) {
    static void (*fn)(void *);
    if (!fn)
        *(void **)&fn = sym("_ZN6RaidUI6Screen11RaidDisplay21HeroesRerankToDefaultEv");
    if (display && fn)
        fn(display);
}
bool ui_hotspot_is_party_reorder(const void *p) {
    return p && *(const uint64_t *)((const uint8_t *)p + 8) == 0x72746162ULL;
}
void game_actor_remove_affliction(void *actor) {
    static void (*fn)(void *);
    if (!fn)
        *(void **)&fn = sym("_ZN5Actor16RemoveAfflictionEv");
    if (actor && fn)
        fn(actor);
}
void game_actor_remove_virtue(void *actor) {
    static void (*fn)(void *);
    if (!fn)
        *(void **)&fn = sym("_ZN5Actor12RemoveVirtueEv");
    if (actor && fn)
        fn(actor);
}
bool game_hero_remove_quirk(void *hero, unsigned index) {
    static bool (*fn)(void *, unsigned, bool);
    if (!fn)
        *(void **)&fn = sym("_ZN4Hero11RemoveQuirkEjb");
    return hero && fn && fn(hero, index, true);
}
int game_inventory_slot_count(const void *inv) {
    static unsigned (*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK9Inventory6System17GetTotalSlotCountEv");
    return inv && fn ? (int)fn(inv) : 0;
}
void *game_inventory_slot(void *inv, int i) {
    static void *(*fn)(void *, unsigned);
    if (!fn)
        *(void **)&fn = sym("_ZN9Inventory6System20GetItemFromSlotIndexEj");
    return inv && i >= 0 && i < game_inventory_slot_count(inv) && fn ? fn(inv, (unsigned)i) : NULL;
}
void game_inventory_remove_slot(void *inv, unsigned i) {
    static void (*fn)(void *, unsigned);
    if (!fn)
        *(void **)&fn = sym("_ZN9Inventory6System10RemoveSlotEj");
    if (inv && fn)
        fn(inv, i);
}
void game_roster_save(void) {
    static void *(*get)(void);
    static void (*save)(void *, bool);
    if (!get)
        *(void **)&get = sym("_ZN8Campaign13CurrentRosterEv");
    if (!save)
        *(void **)&save = sym("_ZN6Roster6System4SaveEb");
    void *r = get ? get() : NULL;
    if (r && save)
        save(r, false);
}
bool game_upgrade_purchased(unsigned tree, int level, unsigned guid) {
    typedef struct {
        unsigned tree;
        uint8_t code, pad[3];
        unsigned guid;
    } Purchase;
    static bool (*fn)(const Purchase *);
    if (!fn)
        *(void **)&fn = sym("_ZN7Upgrade14GetIsPurchasedERKNS_18PurchaseIdentifierE");
    Purchase p = {.tree = tree, .code = (uint8_t)('a' + level), .guid = guid};
    return fn && level >= 0 && level < 26 && fn(&p);
}
void ui_inventory_highlight(int kind) {
    static void (*fn)(int);
    if (!fn)
        *(void **)&fn =
            sym("_ZN9Inventory7Display26ShowInventoryItemHighlightEN22InventorySlotInterface13HighlightTypeE");
    if (fn)
        fn(kind);
}
float game_actor_resistance(const void *actor, int type) {
    static float (*fn)(const void *, int);
    if (!fn)
        *(void **)&fn = sym("_ZNK5Actor20GetCurrentResistanceEN15ResistanceTypes15eResistanceTypeE");
    return actor && fn ? fn(actor, type) : 0;
}
float game_resistance_maximum(int type, bool flag) {
    float *v = game_resist_clamp(type, flag);
    return v ? v[1] : 0;
}
void game_resistance_set_maximum(int type, bool flag, float value) {
    float *v = game_resist_clamp(type, flag);
    if (v)
        v[1] = value;
}
bool game_actor_health_heal(void *target, void *battle, float amount, const void *source, const char *id) {
    static float (*fn)(void *, void *, float, const void *, int, bool);
    static int (*type)(const char *);
    if (!fn)
        *(void **)&fn = sym("_ZN5Actor15ApplyHealthHealER11prng_minstdfPKS_N4Heal6Source5Types5eTypeEb");
    if (!type)
        *(void **)&type = sym("_ZN4Heal6Source13GetTypeFromIdEPKc");
    int t = type && id ? type(id) : -1;
    if (!target || !battle || !fn || t < 0 || t >= 15)
        return false;
    fn(target, game_raid_random(battle), amount, source, t, true);
    return true;
}
bool game_actor_stress_heal(void *target, void *battle, float amount, const void *source, const char *id) {
    static float (*fn)(void *, void *, float, const void *, int);
    static int (*type)(const char *);
    if (!fn)
        *(void **)&fn = sym("_ZN5Actor15ApplyStressHealER11prng_minstdfPKS_N6Stress6Source5Types5eTypeE");
    if (!type)
        *(void **)&type = sym("_ZN6Stress6Source13GetTypeFromIdEPKc");
    int t = type && id ? type(id) : -1;
    if (!target || !battle || !fn || t < 0 || t >= 24)
        return false;
    fn(target, game_raid_random(battle), amount, source, t);
    return true;
}
unsigned game_actor_capabilities(void) {
    static bool ready;
    static unsigned mask;
    if (ready)
        return mask;
    ready = true;
    if (sym("_ZN5Actor15ApplyHealthHealER11prng_minstdfPKS_N4Heal6Source5Types5eTypeEb") &&
        sym("_ZN4Heal6Source13GetTypeFromIdEPKc"))
        mask |= 1;
    if (sym("_ZN5Actor15ApplyStressHealER11prng_minstdfPKS_N6Stress6Source5Types5eTypeE") &&
        sym("_ZN6Stress6Source13GetTypeFromIdEPKc"))
        mask |= 2;
    if (sym("_ZNK5Actor20GetCurrentResistanceEN15ResistanceTypes15eResistanceTypeE"))
        mask |= 4;
    if (sym("_ZN4Raid10s_InstanceE"))
        mask |= 8;
    if (sym("_ZZN5Actor18UpdateTrinketBuffsEvENK3$_5clEjjN4Buff6SourceE"))
        mask |= 16;
    return mask;
}

void *game_raid_battle(void *raid) {
    return raid ? (uint8_t *)raid + RAID_BATTLE : NULL;
}
int game_battle_random_int(void *battle, int low, int high) {
    return game_random_int(game_raid_random(battle), low, high);
}

bool game_upgrades_available(void) {
    return sym("_ZN7Upgrade14GetIsPurchasedERKNS_18PurchaseIdentifierE") != NULL;
}
int game_skill_effect_count(void *skill) {
    void ***v = skill ? (void ***)((uint8_t *)skill + SKILL_BASE_EFFECTS) : NULL;
    return v && v[0] && v[1] >= v[0] && v[1] - v[0] <= 1024 ? (int)(v[1] - v[0]) : 0;
}
int game_inventory_item_count(const void *inventory) {
    static unsigned (*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK9Inventory6System16GetAmountOfItemsEv");
    return inventory && fn ? (int)fn(inventory) : 0;
}
