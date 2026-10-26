// The expedition hook points (hooks.c): DoT ticks, steps, entering areas and hunger. The damage a DoT deals is also an
// hp_damage point (type bleed / poisoned / burn); dot_tick is the tick itself, in battle or in a corridor.
// Windows: ScoutArea and Party::CanGoHungry are inlined, so scout hooks Map::NagivateMap for the two calls ScoutArea
// made (by return address) and hunger_check is a mid-hook where the inlined answer is tested (doc 18).
// Design: design/docs/13-expedition-hooks.md; offsets: docs/engine-design.md "Expedition hook points".
//   dot_tick      Raid::ApplyDot (an actor's HP DoTs, stress DoTs and DoT durations, at its round start or a step);
//                 Raid::ApplyBurnDot (source "burn": an actor's burn stacks, at its round start in battle)
//   step          Party::OnNewTileStep
//   area_enter    Party::OnAreaVisit
//   hunger_check  Party::CanGoHungry
//   raid_ready    once each time the dungeon screen is set up (RaidUI::Screen::RaidDisplay::OnPostRaidStart, called by
//                 Darkest::PostRaidStart for a new expedition and a loaded save alike), on the first frame the player can
//                 act: RaidDisplay::GetCanProcessInput() const returns true (the raid started, no queued display
//                 actions, no overlay, shared panel layer, camp, quest presentation or loot window, not a battle tile,
//                 input active; UpdateInAreaState asks it every frame out of battle). A save loaded mid-battle isn't
//                 asked during the battle, so there it fires on the first hero's turn_start instead (events.c calls
//                 expedition_raid_ready_turn); source "explore" or "battle".
//   scout         ScoutArea(Area const*, unsigned, unsigned depth, AreaKnowledge const&, bool), when it scouts (knowledge
//                 2): Party::OnAreaVisit's roll on entering a room (depth 2, 4 on a critical scout; source "room"), or
//                 Party::OnRaidStart (source "raid_start"); a scouting curio (source "curio") fires from prop_hooks.c, as
//                 it doesn't call ScoutArea. Calls with knowledge 1 only mark areas seen. The chance is
//                 rolled inline in OnAreaVisit (the heroes' scouting stat + a base + darkness), so it can't be changed.
//   affliction_roll  Behaviors::GenerateAfflictionOrVirtueForHero (its only caller: Actor::ApplyRawStressDamage, when
//                 stress reaches 100 with no trait and no pending affliction; it then queues the display's resolve
//                 test, RaidDisplay::QueueOverstressedActor, which applies what was picked). The game picks virtue or
//                 affliction (a virtue chance from the rules, the hero's resolve buffs, a flashback battle), then one by
//                 weight among the generated traits (Trait+0x44 is_generated; virtues: a base weight + the hero's history
//                 of it + generate_chance_modifier), and sets it pending: Actor::SetPendingVirtue (Actor+0x11c4) or
//                 SetPendingAffliction(hash, level) (+0x11c0, the level +0x11b4, rolled from the rules' table at
//                 _Rules+0x220 with _Rules' rng). Before: type (a trait id) gives that trait instead (the game's roll
//                 skipped); cancel: nothing is picked (the display then has nothing to apply).
#include "common.h"

static void **g_raid; // &Raid::s_Instance
static void *raid(void) {
    return g_raid ? *g_raid : NULL;
}

static void (*g_dot_original)(void *raid, void *actor, bool battle, const void *filter);
static void (*g_burn_original)(void *raid, void *actor);
static void (*g_step_original)(void *party);
static void (*g_area_original)(void *party, void *area, bool b);
static bool g_in_area_visit; // inside Party::OnAreaVisit (scout's source)

// ---------------------------------------------------------------- raid_ready
static bool g_ready_pending;
static void (*g_post_raid_start_original)(void *display);
static bool (*g_can_input_original)(const void *display);
static bool (*g_in_battle)(const void *raid);

static void raid_ready(const char *source) {
    g_ready_pending = false;
    HookContext c = {.raid = raid(), .kind = -1, .source = source};
    hook_emit("raid_ready", HOOK_AFTER, &c);
}

static void post_raid_start_hook(void *display) {
    g_post_raid_start_original(display);
    g_ready_pending = true;
}

static bool can_input_hook(const void *display) {
    bool r = g_can_input_original(display);
    if (r && g_ready_pending)
        raid_ready("explore");
    return r;
}

void expedition_raid_ready_turn(const void *actor) {
    if (g_ready_pending && actor && is_hero(actor) && raid() && g_in_battle && g_in_battle(raid()))
        raid_ready("battle");
}

// cancel: no HP or stress DoT this tick, and the actor's DoTs don't count down
static void dot_hook(void *r, void *actor, bool battle, const void *filter) {
    HookContext c = {.raid = r, .actor = actor, .target = actor, .kind = -1, .source = battle ? "battle" : "corridor"};
    if (!hook_emit("dot_tick", HOOK_BEFORE, &c))
        return;
    float hp = actor ? *(const float *)((const uint8_t *)actor + ACTOR_HP) : 0;
    g_dot_original(r, actor, battle, filter);
    c.result = actor ? hp - *(const float *)((const uint8_t *)actor + ACTOR_HP) : 0;
    hook_emit("dot_tick", HOOK_AFTER, &c);
}

// cancel: no burn damage this tick, and the stacks don't decay or spread
static void burn_hook(void *r, void *actor) {
    HookContext c = {.raid = r, .actor = actor, .target = actor, .kind = -1, .source = "burn"};
    if (!hook_emit("dot_tick", HOOK_BEFORE, &c))
        return;
    float hp = actor ? *(const float *)((const uint8_t *)actor + ACTOR_HP) : 0;
    g_burn_original(r, actor);
    c.result = actor ? hp - *(const float *)((const uint8_t *)actor + ACTOR_HP) : 0;
    hook_emit("dot_tick", HOOK_AFTER, &c);
}

static void step_hook(void *party) {
    g_step_original(party);
    HookContext c = {.raid = raid(), .kind = -1};
    hook_emit("step", HOOK_AFTER, &c);
}

static void area_hook(void *party, void *area, bool b) {
    g_in_area_visit = true;
    g_area_original(party, area, b);
    g_in_area_visit = false;
    int kind = area ? *(const int *)((const uint8_t *)area + AREA_KIND) : -1;
    HookContext c = {.raid = raid(),
                     .area = area,
                     .kind = kind,
                     .source = kind == 0  ? "room"
                               : kind > 0 ? "corridor"
                                          : NULL};
    hook_emit("area_enter", HOOK_AFTER, &c);
}

#ifdef _WIN32
// cancel: the area isn't scouted. ScoutArea(area, a, depth, knowledge, crit) is inlined into Party::OnAreaVisit and
// OnRaidStart as Map::NagivateMap(Raid+0x448, area, a, depth, its lambda by value: knowledge at +8, crit at +0xc;
// MSVC: the impl pointer at +0x38, the callee destroys it). Only those two calls are ScoutArea's.
static uint64_t (*g_nav_original)(void *map, const void *area, unsigned a, unsigned depth, StdFunction *fn);
static const void *g_nav_sites[2];
static uint64_t nav_hook(void *map, const void *area, unsigned a, unsigned depth, StdFunction *fn) {
    const void *from = __builtin_return_address(0);
    const uint8_t *lambda = fn ? fn->f : NULL;
    if ((from != g_nav_sites[0] && from != g_nav_sites[1]) || !lambda || *(const int *)(lambda + 8) < 2)
        return g_nav_original(map, area, a, depth, fn);
    HookContext c = {
        .raid = raid(), .area = (void *)area, .kind = (int)depth, .source = g_in_area_visit ? "room" : "raid_start"};
    if (!hook_emit("scout", HOOK_BEFORE, &c)) {
        ui_function_destroy(fn); // the skipped callee's to destroy
        return 0;
    }
    uint64_t r = g_nav_original(map, area, a, depth, fn);
    hook_emit("scout", HOOK_AFTER, &c);
    return r;
}

// Party::CanGoHungry is inlined into HandleTileContents' $_1: where its two paths join, bpl holds the answer
static void hunger_site(Regs *r) {
    HookContext c = {.raid = raid(), .kind = -1};
    bool hungry = hook_emit("hunger_check", HOOK_BEFORE, &c) && (r->rbp & 0xff);
    c.result = hungry;
    hook_emit("hunger_check", HOOK_AFTER, &c);
    r->rbp = (r->rbp & ~(uint64_t)0xff) | hungry;
}
#else
static bool (*g_hunger_original)(void *party);
static uint64_t (*g_scout_original)(const void *area, unsigned a, unsigned depth, const int *knowledge, bool b);

// cancel: the area isn't scouted
static uint64_t scout_hook(const void *area, unsigned a, unsigned depth, const int *knowledge, bool b) {
    if (!knowledge || *knowledge < 2)
        return g_scout_original(area, a, depth, knowledge, b);
    HookContext c = {
        .raid = raid(), .area = (void *)area, .kind = (int)depth, .source = g_in_area_visit ? "room" : "raid_start"};
    if (!hook_emit("scout", HOOK_BEFORE, &c))
        return 0;
    uint64_t r = g_scout_original(area, a, depth, knowledge, b);
    hook_emit("scout", HOOK_AFTER, &c);
    return r;
}

static bool hunger_hook(void *party) {
    HookContext c = {.raid = raid(), .kind = -1};
    bool hungry = hook_emit("hunger_check", HOOK_BEFORE, &c) && g_hunger_original(party);
    c.result = hungry;
    hook_emit("hunger_check", HOOK_AFTER, &c);
    return hungry;
}
#endif

// ---------------------------------------------------------------- affliction_roll
static void (*g_generate_original)(void *self, void *actor);
static void **g_trait_lib;
static const uint8_t *(*g_trait_by_id)(const void *lib, const char *id);     // Trait::Library::GetType(char const*)
static const uint8_t *(*g_trait_by_hash_fn)(const void *lib, unsigned hash); // Trait::Library::GetType(unsigned)
static void (*g_set_pending_virtue)(void *actor, unsigned hash);
static void (*g_set_pending_affliction)(void *actor, const unsigned *hash, int level);
static const unsigned *(*g_roll_level)(const void *table, void *rng, float); // RandomTable<unsigned>::Roll<prng_minstd>
static uint8_t *g_rules;

static const uint8_t *g_trait_by_hash(unsigned hash) {
    return g_trait_by_hash_fn(*g_trait_lib, hash);
} // id first

static void generate_hook(void *self, void *actor) {
    const uint8_t *a = actor;
    if (!a || *(const unsigned *)(a + ACTOR_TRAIT) || *(const unsigned *)(a + ACTOR_TRAIT + 4)) {
        g_generate_original(self, actor); // the game's early return
        return;
    }
    HookContext c = {.raid = raid(), .actor = actor, .kind = -1};
    if (!hook_emit("affliction_roll", HOOK_BEFORE, &c))
        return;
    const uint8_t *forced = c.type ? g_trait_by_id(*g_trait_lib, c.type) : NULL;
    if (c.type && !forced)
        logf_("affliction_roll: no trait \"%s\", the game picks", c.type);
    unsigned v0 = *(const unsigned *)(a + ACTOR_PENDING_VIRTUE), f0 = *(const unsigned *)(a + ACTOR_PENDING_AFFLICTION);
    if (forced) {
        unsigned hash = name_hash(c.type);
        if (*(const unsigned *)(forced + TRAIT_OVERSTRESS) == name_hash("virtue"))
            g_set_pending_virtue(actor, hash);
        else
            g_set_pending_affliction(actor, &hash, (int)*g_roll_level(g_rules + RULES_AFFLICTION_LEVELS, g_rules, 0));
    } else {
        g_generate_original(self, actor);
    }
    unsigned v = *(const unsigned *)(a + ACTOR_PENDING_VIRTUE), f = *(const unsigned *)(a + ACTOR_PENDING_AFFLICTION);
    const uint8_t *given = f != f0 && f ? g_trait_by_hash(f) : v != v0 && v ? g_trait_by_hash(v) : NULL;
    HookContext after = {.raid = c.raid,
                         .actor = actor,
                         .kind = -1,
                         .type = given ? (const char *)given : NULL,
                         .source = !given         ? NULL
                                   : f != f0 && f ? "affliction"
                                                  : "virtue"};
    hook_emit("affliction_roll", HOOK_AFTER, &after);
}

void expedition_hooks_install(void) {
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    if (hook_wanted("dot_tick")) {
        static const uint8_t p[] = PROLOGUE_SUB8(0x48);
        *(void **)&g_dot_original = install_hook(
            "Raid::ApplyDot (dot_tick)", sym("_ZN4Raid8ApplyDotEP5ActorbRKNSt3__18functionIFbPKN4Buff8InstanceEEEE"),
            (void *)dot_hook, p, sizeof p);
        // push rbp; mov rbp,rsp; push r14; push rbx; sub rsp,0x50; mov rax,[rip+disp32] (disp32 at 14, ends at 18)
        static const uint8_t b[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x83,
                                    0xec, 0x50, 0x48, 0x8b, 0x05, 0xe6, 0x97, 0x96, 0x00};
        *(void **)&g_burn_original =
            install_hook_rip("Raid::ApplyBurnDot (dot_tick)", sym("_ZN4Raid12ApplyBurnDotEP5Actor"), (void *)burn_hook,
                             b, sizeof b, (const HookRip[]){{14, 18}}, 1);
    }
    if (hook_wanted("step")) {
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                    0x53, 0x48, 0x83, 0xec, 0x18, 0x48, 0x8b, 0x1f};
        *(void **)&g_step_original = install_hook("Party::OnNewTileStep (step)", sym("_ZN5Party13OnNewTileStepEv"),
                                                  (void *)step_hook, p, sizeof p);
    }
    if (hook_wanted("area_enter") || hook_wanted("scout")) { // scout: tells a room's roll from the raid start
        static const uint8_t p[] = PROLOGUE_SUB8(0x58);
        *(void **)&g_area_original = install_hook(
            "Party::OnAreaVisit (area_enter)", sym("_ZN5Party11OnAreaVisitEP4Areab"), (void *)area_hook, p, sizeof p);
    }
    if (hook_wanted("scout")) {
#ifdef _WIN32
        g_nav_sites[0] = sym("win:Map::NagivateMap from Party::OnAreaVisit");
        g_nav_sites[1] = sym("win:Map::NagivateMap from Party::OnRaidStart");
        *(void **)&g_nav_original =
            install_hook("Map::NagivateMap (scout)", sym("win:Map::NagivateMap"), (void *)nav_hook, NULL, 0);
#else
        // push rbp; mov rbp,rsp; push r14; push rbx; sub rsp,0x40; mov eax,edx; mov edx,esi
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48,
                                    0x83, 0xec, 0x40, 0x89, 0xd0, 0x89, 0xf2};
        *(void **)&g_scout_original = install_hook("ScoutArea (scout)", sym("_Z9ScoutAreaPK4AreajjRK13AreaKnowledgeb"),
                                                   (void *)scout_hook, p, sizeof p);
#endif
    }
    if (hook_wanted("affliction_roll")) {
        g_trait_lib = (void **)sym("_ZN5Trait7Library10s_InstanceE");
        *(void **)&g_trait_by_id = sym("_ZNK5Trait7Library7GetTypeEPKc");
        *(void **)&g_trait_by_hash_fn = sym("_ZNK5Trait7Library7GetTypeEj");
        *(void **)&g_set_pending_virtue = sym("_ZN5Actor16SetPendingVirtueEj");
        *(void **)&g_set_pending_affliction = sym("_ZN5Actor20SetPendingAfflictionERKji");
        *(void **)&g_roll_level = sym("_ZNK11RandomTableIjE4RollI11prng_minstdEERKjRT_f");
        g_rules = sym("Rules"); // the game's _Rules
        if (!g_trait_lib || !g_trait_by_id || !g_trait_by_hash_fn || !g_set_pending_virtue ||
            !g_set_pending_affliction || !g_roll_level || !g_rules) {
            logf_("affliction_roll: symbols not found, off");
        } else {
            static const uint8_t p[] = PROLOGUE_SUB32(0x98, 0x00);
            *(void **)&g_generate_original = install_hook(
                "Behaviors::GenerateAfflictionOrVirtueForHero (affliction_roll)",
                sym("_ZN9Behaviors33GenerateAfflictionOrVirtueForHeroEP5Actor"), (void *)generate_hook, p, sizeof p);
        }
    }
    if (hook_wanted("raid_ready")) {
        *(void **)&g_in_battle = sym("_ZNK4Raid10IsInBattleEv");
        static const uint8_t p[] = PROLOGUE_SUB32(0x78, 0x05);
        *(void **)&g_post_raid_start_original = install_hook("RaidDisplay::OnPostRaidStart (raid_ready)",
                                                             sym("_ZN6RaidUI6Screen11RaidDisplay15OnPostRaidStartEv"),
                                                             (void *)post_raid_start_hook, p, sizeof p);
        // push rbp; mov rbp,rsp; push r15; push r14; push rbx; push rax; mov rbx,rdi; lea r15,[rip+disp32] (disp at 16)
        static const uint8_t q[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50,
                                    0x48, 0x89, 0xfb, 0x4c, 0x8d, 0x3d, 0x24, 0x24, 0x34, 0x02};
        if (g_post_raid_start_original)
            *(void **)&g_can_input_original =
                install_hook_rip("RaidDisplay::GetCanProcessInput (raid_ready)",
                                 sym("_ZNK6RaidUI6Screen11RaidDisplay18GetCanProcessInputEv"), (void *)can_input_hook,
                                 q, sizeof q, (const HookRip[]){{16, 20}}, 1);
    }
    if (hook_wanted("hunger_check")) {
#ifdef _WIN32
        install_mid_hook_rw("Party::CanGoHungry (hunger_check)", sym("win:Party::CanGoHungry result"), hunger_site);
#else
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48,
                                    0x83, 0xec, 0x18, 0x83, 0xbf, 0x88, 0x02, 0x00, 0x00, 0x00};
        *(void **)&g_hunger_original = install_hook("Party::CanGoHungry (hunger_check)",
                                                    sym("_ZN5Party11CanGoHungryEv"), (void *)hunger_hook, p, sizeof p);
#endif
    }
}
