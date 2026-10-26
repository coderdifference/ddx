// Doing things through the game's own code (see common.h, "game_actions.c"): what config hook rules (hook_rules.c)
// and C clients use, so the game's modifiers, popups and hook points apply as for its own sources.
//
// - The rng: the game's global `Rules` (a prng_minstd), which town, camping and debug code pass to these functions.
// - HP: Actor::ApplyHealthDamage(rng, amount, eDamageType const&, unsigned 0, eDamageSourceType const&, bool 0,
//   Actor* attacker) as the debug key and camping call it; types by id: GetDamageTypeFromId /
//   GetDamageSourceTypeFromId. Actor::ApplyHealthHeal(rng, amount, Actor const* healer, Heal::Source type, bool 1)
//   (Heal::Source::GetTypeFromId).
// - Stress: Actor::ApplyStressDamage / ApplyStressHeal(rng, amount, Actor const* source, Stress::Source type)
//   (Stress::Source::GetTypeFromId).
// - Torch: Raid::ChangeTorchlight(change, true) (every caller passes true).
// - Effects: Raid::QueueSideEffect(performer, target, c_string name) then ProcessSideEffectQueue, the path traps and
//   act-outs use (an effect from any .effects.darkest file).
// - Quirks: Hero::AddQuirk(Quirk::Class const*, 1, 0.0f, 1, announce, 1, 1, 1, NULL), as CurioInteractionQuirkResult::
//   Apply (Quirk::Library::s_Instance->GetClass(id)); Hero::RemoveQuirkWithClassId(id hash, true). The 5th argument
//   (r8) announces the quirk on the dungeon screen (RaidDisplay::AddQuirkAnnouncementText: Timeline_Append of the
//   display's script at +0x3108): curios pass 1, the raid start and the town 0. While an expedition loads (raid_start,
//   on the loading thread) that script is unset and announcing crashed, so it's only passed when the display and its
//   script exist.
// - Items: Inventory::Item(type, id, amount, 0, 0, false, {}, TrinketLog) then item_store (party inventory or the
//   estate), after checking Inventory::SystemClass knows the type and id. Into a given inventory:
//   Inventory::System::AddItem(item, true), as the loot generator adds the Farmstead's shard bonus (stacking onto
//   stacks already there).
// - Announcements: the dungeon screen's centre text, as a curio's purge shows "<quirk> Quirk Removed!"
//   (Overlay_Event::InvestigateCurio's purge lambda): RaidDisplay::ShowAnnouncement(FixedLengthString<128> text,
//   type 0, instant_Timeline(), ColourIds::kNotable, render time 0, true, true), Append_Wait(RaidDisplay::T's
//   announcement_times.curio_purge), HideAnnouncement(timeline). instant_Timeline registers itself with the running
//   timelines. Only while the dungeon screen is up (the same test as quirk announcements, below).
// - Loot: Loot::System (Raid+0x5100, as the game's debug key) ::QueueGenerateElement(Loot::Generate::Type const*,
//   FixedLengthString<32> code by value); the type from Loot::Generate::Types::GetTypeById (a local symbol; NULL for an
//   unknown id). The ids: battle, chest (curios), camping, quest, hero_death, threshold.
// Windows: the same names, by syms_win.c (stand-ins for the inlined ones: the type lookups, ApplyHealthDamage, the
// FixedLengthString constructors; GetTypeById takes its id in the second register there).
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITEM_BUFFER 0x600 // Inventory::Item is 0x5b0 bytes (Windows 0x5a8); room to spare
typedef struct {
    uint64_t q[0x91];
} TrinketLog; // Inventory::TrinketLog by value: a FixedLengthString<128>, zeros
typedef struct {
    char s[32];
} Fls32; // FixedLengthString<32> by value

static bool g_ready, g_ok;
static void *g_rules;
static void **g_raid;
static void (*g_damage)(void *actor, void *rng, float amount, const int *type, unsigned, const int *source, bool,
                        void *attacker);
static float (*g_heal)(void *actor, void *rng, float amount, const void *healer, int source, bool);
static float (*g_stress)(void *actor, void *rng, float amount, const void *source, int type);
static float (*g_stress_heal)(void *actor, void *rng, float amount, const void *source, int type);
static int (*g_damage_type)(const char *), (*g_damage_source)(const char *), (*g_heal_source)(const char *),
    (*g_stress_source)(const char *);
static float (*g_torch)(void *raid, float change, bool);
static void (*g_side_effect)(void *raid, void *performer, void *target, const char *effect);
static void (*g_process_side_effects)(void *raid);
static void **g_quirk_lib;
static const void *(*g_quirk_class)(const void *lib, const char *id);
static bool (*g_add_quirk)(void *hero, const void *cls, bool, float, bool, bool, bool, bool, bool, void *timeline);
static void **g_raid_display; // RaidUI::Screen::RaidDisplay::s_Instance
static bool (*g_remove_quirk)(void *hero, unsigned id_hash, bool);
static void (*g_fls64)(void *, const char *), (*g_fls128)(void *, const char *);
static void (*g_item_ctor)(void *, const void *, const void *, int, int, int, bool, const void *, TrinketLog);
static void (*g_item_dtor)(void *);
static void **g_system_class;
static const void *(*g_item_class)(const void *, unsigned, unsigned);
static void (*g_add_item)(void *inventory, void *item, bool stack); // Inventory::System::AddItem(Item&, bool)
static void (*g_add_copy)(void *inventory, const void *item);
static const void *(*g_loot_type)(const char *id);
static void (*g_queue_loot)(void *loot, const void *type, Fls32 code);
typedef struct {
    char s[128];
} Fls128; // FixedLengthString<128> by value
static void (*g_show_announcement)(void *display, Fls128 text, int type, void *timeline, unsigned colour, int render,
                                   bool, bool);
static void (*g_hide_announcement)(void *display, void *timeline);
static void *(*g_instant_timeline)(void);
static const uint8_t *g_display_t; // RaidDisplay::T (the screen's tunables)

#define S(p, name) (*(void **)&(p) = sym(name))
static bool ready(void) {
    if (g_ready)
        return g_ok;
    g_ready = true;
    g_rules = sym("Rules");
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    S(g_damage, "_ZN5Actor17ApplyHealthDamageER11prng_minstdfRK11eDamageTypejRK17eDamageSourceTypebPS_");
    S(g_heal, "_ZN5Actor15ApplyHealthHealER11prng_minstdfPKS_N4Heal6Source5Types5eTypeEb");
    S(g_stress, "_ZN5Actor17ApplyStressDamageER11prng_minstdfPKS_N6Stress6Source5Types5eTypeE");
    S(g_stress_heal, "_ZN5Actor15ApplyStressHealER11prng_minstdfPKS_N6Stress6Source5Types5eTypeE");
    S(g_damage_type, "_Z19GetDamageTypeFromIdPKc");
    S(g_damage_source, "_Z25GetDamageSourceTypeFromIdPKc");
    S(g_heal_source, "_ZN4Heal6Source13GetTypeFromIdEPKc");
    S(g_stress_source, "_ZN6Stress6Source13GetTypeFromIdEPKc");
    S(g_torch, "_ZN4Raid16ChangeTorchlightEfb");
    S(g_side_effect, "_ZN4Raid15QueueSideEffectEP5ActorS1_8c_string");
    S(g_process_side_effects, "_ZN4Raid22ProcessSideEffectQueueEv");
    g_quirk_lib = (void **)sym("_ZN5Quirk7Library10s_InstanceE");
    S(g_quirk_class, "_ZNK5Quirk7Library8GetClassEPKc");
    S(g_add_quirk, "_ZN4Hero8AddQuirkEPKN5Quirk5ClassEbfbbbbbP8Timeline");
    S(g_remove_quirk, "_ZN4Hero22RemoveQuirkWithClassIdEjb");
    g_raid_display = (void **)sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    S(g_fls64, "_ZN17FixedLengthStringILi64EEC1EPKc");
    S(g_fls128, "_ZN17FixedLengthStringILi128EEC1EPKc");
    S(g_item_ctor,
      "_ZN9Inventory4ItemC1ERK17FixedLengthStringILi64EES4_iiibRKNSt3__16vectorIjNS5_9allocatorIjEEEENS_10TrinketLogE");
    S(g_item_dtor, "_ZN9Inventory4ItemD1Ev");
    g_system_class = (void **)sym("_ZN9Inventory11SystemClass10s_InstanceE");
    S(g_item_class, "_ZNK9Inventory11SystemClass12GetItemClassEjj");
    S(g_add_item, "_ZN9Inventory6System7AddItemERNS_4ItemEb");
    S(g_add_copy, "_ZN9Inventory6System13AddCopyOfItemEPKNS_4ItemE");
    S(g_loot_type, "_ZN4Loot8Generate5Types11GetTypeByIdEPKc");
    S(g_queue_loot, "_ZN4Loot6System20QueueGenerateElementEPKNS_8Generate4TypeE17FixedLengthStringILi32EE");
    S(g_show_announcement, "_ZN6RaidUI6Screen11RaidDisplay16ShowAnnouncementE17FixedLengthStringILi128EENS1_"
                           "17eAnnouncementTypeEP8TimelinejNS1_23eAnnouncementRenderTimeEbb");
    S(g_hide_announcement, "_ZN6RaidUI6Screen11RaidDisplay16HideAnnouncementEP8Timeline");
    S(g_instant_timeline, "_Z16instant_Timelinev");
    g_display_t = sym("_ZN6RaidUI6Screen11RaidDisplay1TE");
    g_ok = g_rules && g_raid;
    if (!g_ok)
        logf_("game actions: symbols missing, off");
    return g_ok;
}

static int type_of(int (*from_id)(const char *), const char *id) {
    return id && *id && from_id ? from_id(id) : 0;
}

// RaidDisplay::s_Instance while the dungeon screen can announce: its announcement script is loaded. While an
// expedition loads (raid_start, on the loading thread) it isn't, and announcing then crashed.
static void *dungeon_display(void) {
    uint8_t *d = g_raid_display ? *g_raid_display : NULL;
    return d && *(void *const *)(d + DISPLAY_QUIRK_SCRIPT) ? d : NULL;
}

void *game_rng(void) {
    return ready() ? g_rules : NULL;
}

// the rng's virtual random_uniform (vtable+0x18), as act_outs.c rolls
float game_random(void) {
    void *r = game_rng();
    return r ? (*(float (*const *const *)(void *))r)[3](r) : (float)rand() / ((float)RAND_MAX + 1);
}

// the rng's virtual random_int(min, max), inclusive (the slot differs: RNG_RANDOM_INT_SLOT); rng NULL: the global one
int game_random_int(void *rng, int min, int max) {
    if (!rng)
        rng = game_rng();
    if (!rng)
        return min + (int)((float)rand() / ((float)RAND_MAX + 1) * (float)(max - min + 1));
    return (*(int (*const *const *)(void *, int, int))rng)[RNG_RANDOM_INT_SLOT](rng, min, max);
}

// ResistanceType::k_Data: a row per resistance type, whose {min, max} ClampResistance(type, value, flag) applies
// (flag: Actor+0x127e in the deathblow roll); filled by the game's static initialiser, so read it once the game runs
float *game_resist_clamp(int type, bool flag) {
    static uint8_t *data;
    if (!data)
        data = sym("_ZN14ResistanceTypeL6k_DataE");
    if (!data || type < 0 || type >= RESIST_TYPES)
        return NULL;
    return (float *)(data + (size_t)type * RESIST_DATA_SIZE + RESIST_CLAMP + (flag ? 8 : 0));
}

bool game_damage(void *actor, float amount, const char *type, const char *source, void *attacker) {
    if (!actor || !(amount > 0) || !ready() || !g_damage)
        return false;
    int t = type_of(g_damage_type, type), s = type_of(g_damage_source, source);
    g_damage(actor, g_rules, amount, &t, 0, &s, false, attacker);
    return true;
}

bool game_heal(void *actor, float amount, const void *healer, const char *source) {
    if (!actor || !(amount > 0) || !ready() || !g_heal)
        return false;
    g_heal(actor, g_rules, amount, healer, type_of(g_heal_source, source), true);
    return true;
}

bool game_stress(void *actor, float amount, const void *by, const char *source) {
    if (!actor || amount == 0 || amount != amount || !ready() || !g_stress || !g_stress_heal)
        return false;
    int s = type_of(g_stress_source, source);
    if (amount > 0)
        g_stress(actor, g_rules, amount, by, s);
    else
        g_stress_heal(actor, g_rules, -amount, by, s);
    return true;
}

bool game_torch(float change) {
    if (change == 0 || change != change || !ready() || !g_torch || !*g_raid)
        return false;
    g_torch(*g_raid, change, true);
    return true;
}

bool game_apply_effect(void *performer, void *target, const char *effect) {
    if (!target || !effect || !*effect || !ready() || !g_side_effect || !g_process_side_effects || !*g_raid)
        return false;
    g_side_effect(*g_raid, performer ? performer : target, target, effect);
    g_process_side_effects(*g_raid);
    return true;
}

bool game_add_quirk(void *hero, const char *id) {
    if (!hero || !id || !is_hero(hero) || !ready() || !g_add_quirk || !g_quirk_class || !g_quirk_lib || !*g_quirk_lib)
        return false;
    const void *cls = g_quirk_class(*g_quirk_lib, id);
    if (!cls) {
        logf_("game actions: no quirk %s", id);
        return false;
    }
    bool announce = dungeon_display() != NULL;
    return g_add_quirk(hero, cls, true, 0.0f, true, announce, true, true, true, NULL);
}

bool game_remove_quirk(void *hero, const char *id) {
    if (!hero || !id || !is_hero(hero) || !ready() || !g_remove_quirk)
        return false;
    return g_remove_quirk(hero, name_hash(id), true);
}

// a new Inventory::Item (malloc; free with item_free), NULL if the game doesn't know the type and id
void *game_item_create(const char *type, const char *id, int amount) {
    if (!type || !id || amount < 1 || !ready() || !g_fls64 || !g_fls128 || !g_item_ctor || !g_item_dtor ||
        !g_system_class || !*g_system_class || !g_item_class)
        return NULL;
    if (!g_item_class(*g_system_class, name_hash(type), name_hash(id))) {
        logf_("game actions: no item %s / %s", type, id);
        return NULL;
    }
    uint8_t t[64], i[64];
    static const uint64_t empty_vector[3];
    static TrinketLog log;
    g_fls64(t, type);
    g_fls64(i, id);
    g_fls128(&log, "");
    void *item = malloc(ITEM_BUFFER); // 16-byte aligned on both platforms
    if (!item)
        return NULL;
    memset(item, 0, ITEM_BUFFER);
    g_item_ctor(item, t, i, amount, 0, 0, false, empty_vector, log);
    return item;
}

void game_item_free(void *item) {
    if (!item)
        return;
    g_item_dtor(item);
    free(item);
}

bool game_inventory_copy(void *inventory, const void *item) {
    if (!inventory || !item || !ready() || !g_add_copy)
        return false;
    g_add_copy(inventory, item);
    return true;
}

bool game_give_item(const char *type, const char *id, int amount) {
    void *item = game_item_create(type, id, amount);
    if (!item)
        return false;
    const char *where = item_store(item);
    game_item_free(item);
    if (!where)
        logf_("game actions: no room for %d %s", amount, id);
    return where != NULL;
}

bool game_inventory_add(void *inventory, const char *type, const char *id, int amount) {
    if (!inventory || !ready() || !g_add_item)
        return false;
    void *item = game_item_create(type, id, amount);
    if (!item)
        return false;
    g_add_item(inventory, item, true);
    game_item_free(item);
    return true;
}

bool game_queue_loot(const char *loot_type, const char *code) {
    if (!code || !*code || !ready() || !g_loot_type || !g_queue_loot || !*g_raid)
        return false;
    const void *t = g_loot_type(loot_type && *loot_type ? loot_type : "chest");
    if (!t) {
        logf_("game actions: no loot type %s", loot_type);
        return false;
    }
    Fls32 c;
    memset(&c, 0, sizeof c);
    strncpy(c.s, code, sizeof c.s - 1);
    g_queue_loot((uint8_t *)*g_raid + RAID_LOOT, t, c);
    return true;
}

bool game_announce(const char *text) {
    if (!text || !*text || !ready() || !g_show_announcement || !g_hide_announcement || !g_instant_timeline ||
        !g_display_t)
        return false;
    void *display = dungeon_display();
    if (!display)
        return false;
    Fls128 t;
    snprintf(t.s, sizeof t.s, "%s", text);
    void *timeline = g_instant_timeline();
    if (!timeline)
        return false;
    g_show_announcement(display, t, 0, timeline, name_hash("notable"), 0, true, true);
    void (*wait)(void *, float) = (*(void (***)(void *, float))timeline)[TIMELINE_APPEND_WAIT / 8];
    wait(timeline, *(const float *)(g_display_t + RAID_T_PURGE_TIME));
    g_hide_announcement(display, timeline);
    return true;
}
