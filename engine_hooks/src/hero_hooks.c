// The hero and camping hook points (hooks.c): quirks, Death's Door, heart attacks, deaths, camping skills, the end of a
// camp, and two bus events (items used, trinkets triggered).
// Design: design/docs/13-expedition-hooks.md step 5; offsets: docs/engine-design.md "Hero and camping points".
//   quirk_add          Hero::AddQuirk(Quirk::Class const*, bool resist, float, bool x5, Timeline*), every quirk and
//                      disease gained (curios, battle, camping, the estate); returns whether it was added. resist: the
//                      game rolls the hero's disease resistance (a random roll + the float, against the resistance).
//   quirk_remove       Hero::RemoveQuirk(unsigned index, bool), every quirk removed (treatment, transitions, a quirk
//                      replaced because the hero has too many); returns whether it was removed
//   deaths_door        Hero::OnDeathDoorReached(eDamageType const&, unsigned, eDamageSourceType const&)
//   heart_attack       Actor::QueueHeartAttack(prng&, bool), from Actor::ApplyRawStressDamage at max stress (and the
//                      raid start or a retreat with max stress); it queues the bark, then Death's Door or death
//   death              Hero::OnDeathStartInternal (it dispatches HeroDeathEvent). Not Actor::OnDeathStart: battles also
//                      call that to unload their monsters.
//   camp_skill         Camping::PerformCampingSkill(Camp&, Party&, Loot::System&, unsigned performer, unsigned slot,
//                      unsigned target): performer and target are indices into the party's heroes, slot the
//                      performer's camping skill slot (the 5-argument overload jumps into this one)
//   camp_end           Camping::Camp::FinishCamp(Progression::System const&, Party&), per hero (as camp_start)
//   item_used          the bus's HeroUsedItemEvent (DungeonController::HeroUseItem: +0x10 the hero, +0x18 the item)
//   trinket_triggered  the bus's TrinketTriggeredEvent (+0x10 the hero; the event names no trinket)
// Windows (doc 18): Camp::FinishCamp is inlined into RaidDisplay::FinishCamp, so camp_end follows that;
// HeroUsedItemEvent's dispatch is inlined into HeroUseItem, so item_used is a site there after it; trinket_triggered
// hooks the bus's Dispatcher::Dispatch<TrinketTriggeredEvent> (out of line; its seven callers are all the senders).
#include "common.h"

typedef struct {
    void **begin, **end;
} PtrVec;

static void **g_raid;                                                 // &Raid::s_Instance
static void **g_quirk_lib;                                            // &Quirk::Library::s_Instance
static const char *(*g_quirk_by_id)(const void *lib, const char *id); // Quirk::Library::GetClass(char const*)
static const char *(*g_damage_type_id)(int);
static const char *(*g_damage_source_id)(int);
static const uint8_t *(*g_camp_skill_class)(const void *hero, unsigned slot); // Hero::GetSelectedCampingSkillClass
static void (*g_deduct_points)(void *camp, int points);                       // Camping::Camp::DeductCampingSkillPoints
static void (*g_log_skill)(void *camp, uint64_t hero_and_a, uint32_t b); // Camping::Camp::LogSkill(SkillLogEntryId)

static bool (*g_add_quirk_original)(void *hero, const char *cls, bool resist, float roll, bool b2, bool b3, bool b4,
                                    bool b5, bool b6, void *timeline);
static bool (*g_remove_quirk_original)(void *hero, unsigned index, bool b);
static void (*g_deaths_door_original)(void *hero, const int *type, unsigned u, const int *source);
static void (*g_heart_attack_original)(void *actor, void *prng, bool b);
static void (*g_death_original)(void *hero);
static void (*g_camp_skill_original)(void *camp, void *party, void *loot, unsigned performer, unsigned slot,
                                     unsigned target);
#ifdef _WIN32
static void *(*g_display_finish_camp_original)(void *display);
#else
static void (*g_finish_camp_original)(void *camp, const void *progression, void *party);
static void (*g_item_used_original)(void *list, void *event);
#endif
static void (*g_trinket_original)(void *list, void *event);

static void *raid(void) {
    return g_raid ? *g_raid : NULL;
}

static const char *id_or_null(const char *(*lookup)(int), int v) {
    return lookup && v >= 0 ? lookup(v) : NULL;
}

static void *party_hero(void *party, unsigned i) {
    const PtrVec *v = party;
    return v && v->begin + i < v->end ? v->begin[i] : NULL;
}

// type: the quirk's id (a Quirk::Class starts with it); before: may be changed to another quirk's id; cancel: not added
static bool add_quirk_hook(void *hero, const char *cls, bool resist, float roll, bool b2, bool b3, bool b4, bool b5,
                           bool b6, void *timeline) {
    if (!cls)
        return g_add_quirk_original(hero, cls, resist, roll, b2, b3, b4, b5, b6, timeline);
    HookContext c = {.raid = raid(), .actor = hero, .kind = -1, .type = cls};
    if (!hook_emit("quirk_add", HOOK_BEFORE, &c)) {
        HookContext after = {.raid = c.raid, .actor = hero, .kind = -1, .type = cls};
        hook_emit("quirk_add", HOOK_AFTER, &after);
        return false;
    }
    if (c.type != cls) {
        const char *other = c.type ? g_quirk_by_id(*g_quirk_lib, c.type) : NULL;
        if (other)
            cls = other;
        else
            logf_("quirk_add: no quirk \"%s\", the game's is added", c.type ? c.type : "(null)");
    }
    bool added = g_add_quirk_original(hero, cls, resist, roll, b2, b3, b4, b5, b6, timeline);
    HookContext after = {.raid = c.raid, .actor = hero, .kind = -1, .type = cls, .result = added};
    hook_emit("quirk_add", HOOK_AFTER, &after);
    return added;
}

// type: the quirk's id; cancel: kept
static bool remove_quirk_hook(void *hero, unsigned index, bool b) {
    const PtrVec *q = (const PtrVec *)((const uint8_t *)hero + HERO_QUIRKS);
    const uint8_t *entry = (const uint8_t *)q->begin + (size_t)index * QUIRK_SIZE;
    const char *cls = entry < (const uint8_t *)q->end ? *(const char *const *)entry : NULL;
    HookContext c = {.raid = raid(), .actor = hero, .kind = (int)index, .type = cls};
    bool removed = false;
    if (hook_emit("quirk_remove", HOOK_BEFORE, &c))
        removed = g_remove_quirk_original(hero, index, b);
    HookContext after = {.raid = c.raid, .actor = hero, .kind = (int)index, .type = cls, .result = removed};
    hook_emit("quirk_remove", HOOK_AFTER, &after);
    return removed;
}

// type / source: the damage type and source that brought the hero to Death's Door
static void deaths_door_hook(void *hero, const int *type, unsigned u, const int *source) {
    g_deaths_door_original(hero, type, u, source);
    HookContext c = {.raid = raid(),
                     .actor = hero,
                     .kind = source ? *source : -1,
                     .type = type ? id_or_null(g_damage_type_id, *type) : NULL,
                     .source = source ? id_or_null(g_damage_source_id, *source) : NULL};
    hook_emit("deaths_door", HOOK_AFTER, &c);
}

// cancel: no heart attack (the hero keeps their stress)
static void heart_attack_hook(void *actor, void *prng, bool b) {
    HookContext c = {.raid = raid(), .actor = actor, .kind = -1};
    if (!hook_emit("heart_attack", HOOK_BEFORE, &c))
        return;
    g_heart_attack_original(actor, prng, b);
    hook_emit("heart_attack", HOOK_AFTER, &c);
}

static void death_hook(void *hero) {
    g_death_original(hero);
    HookContext c = {.raid = raid(), .actor = hero, .kind = -1};
    hook_emit("death", HOOK_AFTER, &c);
}

// actor: the performer, target: the hero it's used on, type: the skill's id, amount: its respite cost. cancel: the
// skill does nothing, but its points are spent and it's logged as used, as when the game's hero refuses it.
static void camp_skill_hook(void *camp, void *party, void *loot, unsigned performer, unsigned slot, unsigned target) {
    void *hero = party_hero(party, performer);
    const uint8_t *skill = hero ? g_camp_skill_class(hero, slot) : NULL;
    if (!skill) {
        g_camp_skill_original(camp, party, loot, performer, slot, target);
        return;
    }
    HookContext c = {.raid = raid(),
                     .actor = hero,
                     .target = party_hero(party, target),
                     .kind = -1,
                     .type = (const char *)skill + CAMP_SKILL_ID,
                     .amount = (float)*(const int *)(skill + CAMP_SKILL_COST)};
    if (hook_emit("camp_skill", HOOK_BEFORE, &c)) {
        g_camp_skill_original(camp, party, loot, performer, slot, target);
        c.result = 1;
    } else {
        g_deduct_points(camp, *(const int *)(skill + CAMP_SKILL_COST));
        g_log_skill(camp,
                    (uint64_t)*(const uint32_t *)((const uint8_t *)hero + HERO_ROSTER_ID) |
                        (uint64_t)*(const uint32_t *)(skill + CAMP_SKILL_LOG_A) << 32,
                    *(const uint32_t *)(skill + CAMP_SKILL_LOG_B));
        c.result = 0;
    }
    c.amount = (float)*(const int *)(skill + CAMP_SKILL_COST);
    hook_emit("camp_skill", HOOK_AFTER, &c);
}

static void camp_end(void *party) {
    for (unsigned i = 0; party_hero(party, i); i++) {
        HookContext c = {.raid = raid(), .actor = party_hero(party, i), .kind = -1};
        hook_emit("camp_end", HOOK_AFTER, &c);
    }
}

// the event: HeroUsedItemEvent (EVENT_ACTOR the hero, EVENT_ITEM the item, still in its slot)
static void item_used(const uint8_t *e) {
    HookContext c = {.raid = raid(),
                     .actor = e ? *(void *const *)(e + EVENT_ACTOR) : NULL,
                     .item = e ? *(const void *const *)(e + EVENT_ITEM) : NULL,
                     .kind = -1};
    if (c.actor)
        hook_emit("item_used", HOOK_AFTER, &c);
}

#ifdef _WIN32
static void *display_finish_camp_hook(void *display) {
    void *r = g_display_finish_camp_original(display);
    void *rd = raid();
    if (rd)
        camp_end((uint8_t *)rd + RAID_PARTY);
    return r;
}

// after the inlined dispatch, before the inlined RemoveSlotItem; the event on HeroUseItem's frame (rbp + 0x2a0)
static void item_used_site(const Regs *r) {
    item_used((const uint8_t *)r->rbp + 0x2a0);
}
#else
static void finish_camp_hook(void *camp, const void *progression, void *party) {
    g_finish_camp_original(camp, progression, party);
    camp_end(party);
}

static void item_used_hook(void *list, void *event) {
    g_item_used_original(list, event);
    item_used(event);
}
#endif

// macOS: the bus's ListenerListData::Dispatch (the list, the event); Windows: Dispatcher::Dispatch (the bus, the event)
static void trinket_hook(void *list, void *event) {
    g_trinket_original(list, event);
    HookContext c = {
        .raid = raid(), .actor = event ? *(void *const *)((const uint8_t *)event + EVENT_ACTOR) : NULL, .kind = -1};
    if (c.actor)
        hook_emit("trinket_triggered", HOOK_AFTER, &c);
}

void hero_hooks_install(void) {
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    if (hook_wanted("quirk_add")) {
        g_quirk_lib = (void **)sym("_ZN5Quirk7Library10s_InstanceE");
        *(void **)&g_quirk_by_id = sym("_ZNK5Quirk7Library8GetClassEPKc");
        if (!g_quirk_lib || !g_quirk_by_id)
            logf_("quirk_add: symbols not found, off");
        else {
            static const uint8_t p[] = PROLOGUE_SUB32(0x08, 0x01);
            *(void **)&g_add_quirk_original =
                install_hook("Hero::AddQuirk (quirk_add)", sym("_ZN4Hero8AddQuirkEPKN5Quirk5ClassEbfbbbbbP8Timeline"),
                             (void *)add_quirk_hook, p, sizeof p);
        }
    }
    if (hook_wanted("quirk_remove")) {
        static const uint8_t p[] = PROLOGUE_SUB8(0x28);
        *(void **)&g_remove_quirk_original =
            install_hook("Hero::RemoveQuirk (quirk_remove)", sym("_ZN4Hero11RemoveQuirkEjb"), (void *)remove_quirk_hook,
                         p, sizeof p);
    }
    if (hook_wanted("deaths_door")) {
        *(void **)&g_damage_type_id = sym("_Z19GetIdFromDamageType11eDamageType");
        *(void **)&g_damage_source_id = sym("_Z25GetIdFromDamageSourceType17eDamageSourceType");
        // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; sub rsp,0x30
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x30};
        *(void **)&g_deaths_door_original =
            install_hook("Hero::OnDeathDoorReached (deaths_door)",
                         sym("_ZN4Hero18OnDeathDoorReachedERK11eDamageTypejRK17eDamageSourceType"),
                         (void *)deaths_door_hook, p, sizeof p);
    }
    if (hook_wanted("heart_attack")) {
        static const uint8_t p[] = PROLOGUE_SUB32(0x18, 0x02);
        *(void **)&g_heart_attack_original =
            install_hook("Actor::QueueHeartAttack (heart_attack)", sym("_ZN5Actor16QueueHeartAttackER11prng_minstdb"),
                         (void *)heart_attack_hook, p, sizeof p);
    }
    if (hook_wanted("death")) {
        // push rbp; mov rbp,rsp; push r15; push r14; push rbx; sub rsp,0x18; mov ebx,[rdi+0x12f4]
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48,
                                    0x83, 0xec, 0x18, 0x8b, 0x9f, 0xf4, 0x12, 0x00, 0x00};
        *(void **)&g_death_original =
            install_hook("Hero::OnDeathStartInternal (death)", sym("_ZN4Hero20OnDeathStartInternalEv"),
                         (void *)death_hook, p, sizeof p);
    }
    if (hook_wanted("camp_skill")) {
        *(void **)&g_camp_skill_class = sym("_ZNK4Hero28GetSelectedCampingSkillClassEj");
        *(void **)&g_deduct_points = sym("_ZN7Camping4Camp24DeductCampingSkillPointsEi");
        *(void **)&g_log_skill = sym("_ZN7Camping4Camp8LogSkillENS_15SkillLogEntryIdE");
        if (!g_camp_skill_class || !g_deduct_points || !g_log_skill)
            logf_("camp_skill: symbols not found, off");
        else {
            static const uint8_t p[] = PROLOGUE_SUB32(0xd8, 0x02);
            *(void **)&g_camp_skill_original =
                install_hook("Camping::PerformCampingSkill (camp_skill)",
                             sym("_ZN7Camping19PerformCampingSkillERNS_4CampER5PartyRN4Loot6SystemEjjj"),
                             (void *)camp_skill_hook, p, sizeof p);
        }
    }
    if (hook_wanted("camp_end")) {
#ifdef _WIN32
        static const uint8_t p[] = {0}; // the table has the prologue
        *(void **)&g_display_finish_camp_original =
            install_hook("RaidDisplay::FinishCamp (camp_end)", sym("win:RaidDisplay::FinishCamp"),
                         (void *)display_finish_camp_hook, p, sizeof p);
#else
        // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; mov r14,rdx
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x49, 0x89, 0xd6};
        *(void **)&g_finish_camp_original = install_hook(
            "Camping::Camp::FinishCamp (camp_end)", sym("_ZN7Camping4Camp10FinishCampERKN11Progression6SystemER5Party"),
            (void *)finish_camp_hook, p, sizeof p);
#endif
    }
#ifdef _WIN32
    if (hook_wanted("item_used"))
        install_mid_hook("DungeonController::HeroUseItem (item_used)", sym("win:HeroUseItem after HeroUsedItemEvent"),
                         item_used_site);
    if (hook_wanted("trinket_triggered")) {
        static const uint8_t p[] = {0}; // the table has the prologue
        *(void **)&g_trinket_original = install_hook(
            "TrinketTriggeredEvent dispatch (trinket_triggered)",
            sym("_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE8DispatchI21TrinketTriggeredEventEEvPT_"),
            (void *)trinket_hook, p, sizeof p);
    }
#else
    // the bus's dispatch specializations (combat_events.c hooks others with the same prologue)
    static const uint8_t dispatch[] = PROLOGUE_SUB8(0x18);
    if (hook_wanted("item_used"))
        *(void **)&g_item_used_original = install_hook("HeroUsedItemEvent dispatch (item_used)",
                                                       sym("_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16L"
                                                           "istenerListData8DispatchI17HeroUsedItemEventEEvPT_"),
                                                       (void *)item_used_hook, dispatch, sizeof dispatch);
    if (hook_wanted("trinket_triggered"))
        *(void **)&g_trinket_original = install_hook("TrinketTriggeredEvent dispatch (trinket_triggered)",
                                                     sym("_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16Lis"
                                                         "tenerListData8DispatchI21TrinketTriggeredEventEEvPT_"),
                                                     (void *)trinket_hook, dispatch, sizeof dispatch);
#endif
}
