// The per-hero hook points (raid_start ... camp_start, hooks.c): each hooks a per-hero game function and emits its
// point after it (on Windows, where MSVC inlined most of them, at the inlined code). turn_start and turn_end are any
// actor's, monsters too: they come from the battle's state machine (below). The same hooks drive cooldown counting
// (cooldown.c).
#include "common.h"
#include <string.h>

enum {
    EV_RAID_START,
    EV_RAID_FINISH,
    EV_BATTLE_START,
    EV_BATTLE_END,
    EV_BATTLE_RETREAT,
    EV_TURN_START,
    EV_TURN_END,
    EV_ROUND_END,
    EV_CAMP_START,
    EV_COUNT
};
const char *const k_event_names[EV_COUNT] = {"raid_start", "raid_finish",    "battle_start",
                                             "battle_end", "battle_retreat", "turn_start",
                                             "turn_end",   "round_end",      "camp_start"};

static void fire_skip(int ev, void *actor, const char *skip) {
    HookContext c = {.actor = actor, .kind = -1, .skip = skip};
    hook_emit(k_event_names[ev], HOOK_AFTER, &c);
}
static void fire(int ev, void *actor) {
    fire_skip(ev, actor, NULL);
}

// Hook functions: one original pointer per event, typed by signature. Every hook returns what the
// original returned (in case a function we think is void returns a value).
static uint64_t (*o_raid_start)(void *), (*o_raid_finish)(void *), (*o_battle_retreat)(void *);
static uint64_t (*o_round_end)(void *), (*o_camp_start)(void *);
static uint64_t (*o_battle_start)(void *, bool);
static uint64_t (*o_battle_end)(void *, bool, bool);

static uint64_t h_raid_start(void *h) {
    uint64_t r = o_raid_start(h);
    fire(EV_RAID_START, h);
    return r;
}
static uint64_t h_raid_finish(void *h) {
    uint64_t r = o_raid_finish(h);
    fire(EV_RAID_FINISH, h);
    return r;
}
static uint64_t h_battle_retreat(void *h) {
    uint64_t r = o_battle_retreat(h);
    fire(EV_BATTLE_RETREAT, h);
    return r;
}
static uint64_t h_round_end(void *h) {
    uint64_t r = o_round_end(h);
    fire(EV_ROUND_END, h);
    return r;
}
static uint64_t h_camp_start(void *h) {
    uint64_t r = o_camp_start(h);
    fire(EV_CAMP_START, h);
    return r;
}
static uint64_t h_battle_start(void *h, bool b) {
    uint64_t r = o_battle_start(h, b);
    cooldown_on_battle(h);
    fire(EV_BATTLE_START, h);
    return r;
}
static uint64_t h_battle_end(void *h, bool a, bool b) {
    uint64_t r = o_battle_end(h, a, b);
    cooldown_on_battle(h);
    fire(EV_BATTLE_END, h);
    return r;
}
// skip: the turn is skipped, and why ("stun"; HookContext.skip), NULL: the actor acts
static void turn_start(void *actor, const char *skip) {
    cooldown_on_turn_start(actor);
    expedition_raid_ready_turn(actor); // a save loaded mid-battle: ready on the first hero's turn
    fire_skip(EV_TURN_START, actor, skip);
}
static void turn_end(void *actor) {
    cooldown_on_turn_end(actor);
    fire(EV_TURN_END, actor);
}

#define HOOK(orig, name, mangled, detour, ...)                                                                         \
    do {                                                                                                               \
        static const uint8_t pro[] = __VA_ARGS__;                                                                      \
        *(void **)&orig = install_hook(name, sym(mangled), (void *)detour, pro, sizeof pro);                           \
    } while (0)

// RaidDisplay::UpdateBattleState runs the battle's state machine (BattleState, the game's _g_battlestate_names:
// bs_before_turn_start 17 ... bs_prepare_next_turn 47, the same on both platforms) for the current actor, hero or
// monster, stepping with Battle::IncrementState (one step per call; the multiplayer lock leaves the state as it was).
// A turn starts on the step out of bs_before_turn_wait_for_drag into bs_before_turn_Bark (after the DoT ticks and the
// stun check, before the hero's turn-start act-outs: bs_before_turn_Bark's handler calls Behaviors::Combat_StartTurn,
// and a lost turn (ignore_command) jumps from there to bs_after_turn) and ends on the step out of
// bs_after_turn_actor_dot (after the burn tick and Hero::OnTurnEnd). Battle::SetState's jumps back into bs_turn_start
// (a move or friendly skill sending the actor back to choose) aren't steps, so a turn starts once. A stunned actor's
// turn: bs_before_turn_update_stun removes the stun and jumps (SetState) to bs_after_turn, so turn_start fires on that
// jump, skip "stun", and the turn ends as any other.
enum {
    BS_BEFORE_TURN_UPDATE_STUN = 22,
    BS_BEFORE_TURN_WAIT_FOR_DRAG = 23,
    BS_BEFORE_TURN_BARK = 24,
    BS_AFTER_TURN = 34,
    BS_AFTER_TURN_ACTOR_DOT = 39
};
static int battle_state(const void *battle) {
    return *(const int *)((const uint8_t *)battle + BATTLE_STATE);
}
static void *(*g_current_actor)(const void *battle);

// A state change from -> to that a turn event follows (both platforms' hooks call it).
static void state_changed(void *battle, int from, int to) {
    bool step = to == from + 1;
    bool starts = step && from == BS_BEFORE_TURN_WAIT_FOR_DRAG, ends = step && from == BS_AFTER_TURN_ACTOR_DOT;
    bool stunned = from == BS_BEFORE_TURN_UPDATE_STUN && to == BS_AFTER_TURN;
    if (!starts && !ends && !stunned)
        return;
    void *actor = g_current_actor ? g_current_actor(battle) : NULL;
    if (!actor)
        return;
    if (ends)
        turn_end(actor);
    else
        turn_start(actor, stunned ? "stun" : NULL);
}

// Battle::SetState: on macOS the stunned actor's jump (bs_before_turn_update_stun -> bs_after_turn); on Windows also
// the steps out of bs_after_turn_actor_dot and bs_before_turn_Bark (the inlined IncrementState calls SetState(state + 1)
// there). The
// multiplayer lock leaves the state as it was.
static uint64_t (*o_set_state)(void *battle, int state);
static uint64_t h_set_state(void *battle, int state) {
    int from = battle_state(battle);
#ifdef _WIN32
    // the bark step (the inlined IncrementState's SetState(state + 1), UpdateBattleState 0x759a35)
    if (from == BS_BEFORE_TURN_BARK && state == from + 1 && act_outs_hold())
        return 0; // more act-outs first
#endif
    uint64_t r = o_set_state(battle, state);
    state_changed(battle, from, battle_state(battle));
    return r;
}

#ifdef _WIN32
// Windows: MSVC inlined Battle::IncrementState and several per-hero functions (Hero::OnBattleCreate, OnBattleFinish,
// OnRoundEnd, OnCampStart, OnBattleAfterRetreat) into their callers, so mid-function hooks run those events where the
// inlined code runs (syms_win.c; a hero loop's step, after that hero's part). UpdateBattleState writes two of the turn
// steps itself (the Raid in rsi; the hook runs before the write, after the lock check): the step out of
// bs_before_turn_wait_for_drag, and the jump to bs_after_turn that the stun case shares with the bark's lost turn
// (state_changed tells them apart by the state left). battle_end: RaidDisplay::OnBattleFinish (its heroes' loop runs
// once per battle) and UpdateBattleState's $_18, macOS's two callers of Hero::OnBattleFinish in a normal battle.
// act_outs_hold on the bark step: in h_set_state. Not yet on Windows: Wave::WaveLogic's calls (wave battles).
static void *raid_battle_of(const Regs *r) {
    return (uint8_t *)r->rsi + RAID_BATTLE;
}
static void m_turn_start(const Regs *r) {
    void *battle = raid_battle_of(r);
    state_changed(battle, battle_state(battle), BS_BEFORE_TURN_BARK);
}
static void m_after_turn(const Regs *r) {
    void *battle = raid_battle_of(r);
    state_changed(battle, battle_state(battle), BS_AFTER_TURN);
}

static void m_battle_start(void *hero) {
    cooldown_on_battle(hero);
    fire(EV_BATTLE_START, hero);
}
static void m_battle_end(void *hero) {
    cooldown_on_battle(hero);
    fire(EV_BATTLE_END, hero);
}
static void m_create_step(const Regs *r) {
    m_battle_start((void *)r->rdi);
}
static void m_resume_step(const Regs *r) {
    m_battle_start((void *)r->rsi);
}
static void m_finish_step(const Regs *r) {
    m_battle_end((void *)r->rbx);
}
static void m_round_end_step(const Regs *r) {
    fire(EV_ROUND_END, (void *)r->rdi);
}
static void m_camp_step(const Regs *r) {
    fire(EV_CAMP_START, (void *)r->rdi);
}
static void m_retreat_step(const Regs *r) {
    fire(EV_BATTLE_RETREAT, (void *)r->rbx);
}

static uint64_t (*o_display_finish)(void *display, bool victory);
static uint64_t h_display_finish(void *display, bool victory) {
    bool ran = ((const uint8_t *)display)[RAIDDISPLAY_BATTLE_FINISHED] != 0;
    uint64_t r = o_display_finish(display, victory);
    void **raid = sym("_ZN4Raid10s_InstanceE");
    uint8_t *rd = raid ? *raid : NULL;
    void **b = rd ? *(void ***)(rd + RAID_HEROES) : NULL, **e = rd ? *(void ***)(rd + RAID_HEROES + 8) : NULL;
    for (void **h = b; !ran && h && h < e; h++)
        m_battle_end(*h);
    return r;
}
#else
// macOS: Battle::IncrementState, the steps.
static uint64_t (*o_increment_state)(void *battle);
static uint64_t h_increment_state(void *battle) {
    int from = battle_state(battle);
    if (from == BS_BEFORE_TURN_BARK && act_outs_hold())
        return 0; // more act-outs to run first (act_outs.c)
    uint64_t r = o_increment_state(battle);
    state_changed(battle, from, battle_state(battle));
    return r;
}
#endif

void events_install(void) {
    if (!config_any_cooldowns() && !hook_any_wanted(k_event_names, EV_COUNT) && !config_act_outs()->per_source &&
        !hook_wanted("raid_ready")) {
        logf_("events: no listeners or cooldowns, hooks not installed");
        return;
    }
    *(void **)&g_current_actor = sym("_ZNK6Battle15GetCurrentActorEv");
#ifdef _WIN32
    HOOK(o_raid_start, "Hero::OnRaidStart", "_ZN4Hero11OnRaidStartEv", h_raid_start, {0});
    HOOK(o_raid_finish, "Hero::OnRaidFinish", "_ZN4Hero12OnRaidFinishEv", h_raid_finish, {0});
    install_mid_hook("Battle::Create (Hero::OnBattleCreate)", sym("win:Battle::Create hero loop"), m_create_step);
    install_mid_hook("Raid::ResumeBattle (Hero::OnBattleCreate)", sym("win:Raid::ResumeBattle hero loop"),
                     m_resume_step);
    HOOK(o_display_finish, "RaidDisplay::OnBattleFinish (Hero::OnBattleFinish)", "win:RaidDisplay::OnBattleFinish",
         h_display_finish, {0});
    install_mid_hook("RaidDisplay::UpdateBattleState $_18 (Hero::OnBattleFinish)",
                     sym("win:UpdateBattleState battle finish hero loop"), m_finish_step);
    install_mid_hook("Battle::NextInitiative (Hero::OnRoundEnd)", sym("win:Battle::NextInitiative hero loop"),
                     m_round_end_step);
    install_mid_hook("Party::OnStartCamp (Hero::OnCampStart)", sym("win:Party::OnStartCamp hero loop"), m_camp_step);
    install_mid_hook("Battle::CampaignPartyRetreat $_0 (Hero::OnBattleAfterRetreat)",
                     sym("win:CampaignPartyRetreat hero loop"), m_retreat_step);
    HOOK(o_set_state, "Battle::SetState (turn_end, turn_start skipped by stun)", "_ZN6Battle8SetStateE11BattleState",
         h_set_state, {0});
    install_mid_hook("RaidDisplay::UpdateBattleState (turn_start)", sym("win:UpdateBattleState turn start"),
                     m_turn_start);
    install_mid_hook("RaidDisplay::UpdateBattleState (turn_start skipped by stun)",
                     sym("win:UpdateBattleState after turn"), m_after_turn);
    (void)h_round_end;
    (void)h_camp_start;
    (void)h_battle_start;
    (void)h_battle_end;
    (void)h_battle_retreat;
#else
    // Expected bytes: whole instructions, no RIP-relative operands or branches (from the disassembly).
    HOOK(o_raid_start, "Hero::OnRaidStart", "_ZN4Hero11OnRaidStartEv", h_raid_start,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00});
    HOOK(o_raid_finish, "Hero::OnRaidFinish", "_ZN4Hero12OnRaidFinishEv", h_raid_finish,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xFB, 0xC6, 0x87, 0x79, 0x12, 0x00, 0x00, 0x00});
    HOOK(o_battle_start, "Hero::OnBattleCreate", "_ZN4Hero14OnBattleCreateEb", h_battle_start,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x41, 0x89, 0xF6, 0x48, 0x89, 0xFB});
    HOOK(o_battle_end, "Hero::OnBattleFinish", "_ZN4Hero14OnBattleFinishEbb", h_battle_end,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x41, 0x89, 0xD6, 0x48, 0x89, 0xFB, 0x85, 0xF6});
    HOOK(o_battle_retreat, "Hero::OnBattleAfterRetreat", "_ZN4Hero20OnBattleAfterRetreatEv", h_battle_retreat,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0xFB});
    if (g_current_actor) {
        HOOK(o_increment_state, "Battle::IncrementState (turn_start, turn_end)", "_ZN6Battle14IncrementStateEv",
             h_increment_state, {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x89, 0xFB});
        // test esi,esi; je +0x13; cmp esi,0x29; jg +0x13; cmp byte [rdi+0x2da],0 (two rel8 jumps, past the bytes)
        static const uint8_t set_pro[] = {0x85, 0xF6, 0x74, 0x0F, 0x83, 0xFE, 0x29, 0x7F,
                                          0x0A, 0x80, 0xBF, 0xDA, 0x02, 0x00, 0x00, 0x00};
        static const HookRip set_rel[] = {{3, 4}, {8, 9}};
        *(void **)&o_set_state =
            install_hook_rip("Battle::SetState (turn_start skipped by stun)", sym("_ZN6Battle8SetStateE11BattleState"),
                             (void *)h_set_state, set_pro, sizeof set_pro, set_rel, 2);
    } else
        logf_("events: Battle::GetCurrentActor not found, turn_start and turn_end off");
    HOOK(o_round_end, "Hero::OnRoundEnd", "_ZN4Hero10OnRoundEndEv", h_round_end,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x89, 0xFB});
    HOOK(o_camp_start, "Hero::OnCampStart", "_ZN4Hero11OnCampStartEv", h_camp_start,
         {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x89, 0xFB});
#endif
}
