// Hook points: the library's one registry of named moments in the game (portable: no game symbols here).
// Every point is declared in k_points below, with the phases it fires in. Clients register a callback for a point and
// phase with hook_on (or "*" for every point that's installed anyway; hook_on_all: every point, all installed) before the library's *_install functions run; a module installs its game
// hooks only for points something listens to (hook_wanted; config "debug": true installs and logs them all). The
// module that owns a point builds a HookContext and calls hook_emit.
//   before: fired before the game acts; the context is the game's input, and callbacks may change it (amount, ...) or
//           set cancel. hook_emit returns false when cancelled.
//   after:  fired once the game has acted; changes are ignored.
// Each listener belongs to the mod registering it (mods_current; -1 the library). A point's own listeners run, then the
// "*" listeners; within each, the library's first, then the campaign's active mods from the lowest in its mod list to
// the highest (so the top mod's before changes stand: a higher mod wins), each mod's in registration order. A mod not
// applied to the loaded campaign (or no campaign) isn't called. hook_wanted counts every mod's (installation stays
// the superset of all campaigns).
// Design: design/docs/13-expedition-hooks.md; guide: docs/12-hook-points.md.
#include "common.h"
#include <string.h>

typedef struct {
    const char *name;
    unsigned phases;
    const char *about;
} HookPoint;

#define B (1u << HOOK_BEFORE)
#define A (1u << HOOK_AFTER)
static const HookPoint k_points[] = {
    // expedition and per-hero moments (events.c); actor: the hero (turn_start, turn_end: any actor)
    {"raid_start", A, "a hero starts an expedition"},
    {"raid_finish", A, "a hero's expedition ends"},
    {"battle_start", A, "a hero enters a battle"},
    {"battle_end", A, "a hero's battle ends"},
    {"battle_retreat", A, "a hero retreats from battle"},
    {"turn_start", A,
     "an actor's combat turn starts (after its DoT ticks, before it acts; skip \"stun\": a stunned turn)"},
    {"turn_end", A, "an actor's combat turn ends (after its burn tick; stunned actors too)"},
    {"round_start", A, "a combat round starts (battle; kind: its number, 1 the first), before its first turn"},
    {"round_end", A, "a combat round ends, per hero"},
    {"camp_start", A, "a hero starts camping"},
    // affliction act-outs (act_outs.c): actor: the hero; type: the act-out's id, kind: its number
    {"act_out", B | A,
     "an afflicted hero's turn-start act-out (before: rolled, cancel: none; after: queued, "
     "not yet applied, result: the turn result, 2 lost)"},
    {"act_out_check", B | A,
     "the game's checks of a rolled act-out (refuse: repeat 1, disabled 2, limit 4, may be "
     "changed; cancel refuses; amount: raid_limit; after: result 1 allowed)"},
    // the resolve test (expedition_hooks.c): actor: the hero
    {"affliction_roll", B | A,
     "a hero's resolve test picks an affliction or virtue (before: type may name the trait to "
     "give, cancel: none; after: type: the trait given, source: affliction or virtue)"},
    // combat skills (cooldown.c, Battle::Actor_DoSkill): actor (= attacker), skill; amount: its target count,
    // target: the first (skill_resolution_target(i) for each, during the callback)
    {"skill_use", A, "an actor has used a combat skill (its own effects are applied or queued)"},
    // combat, per target of each attack (combat_hooks.c): attacker, target, skill, amount (damage), hit, crit
    {"hit", A, "an attack hits a target"},
    {"miss", A, "an attack misses a target"},
    {"dodge", A, "an attack misses a hero (the hero dodged)"},
    {"crit", A, "an attack crits a target"},
    {"kill", A, "an attack kills a living target"},
    {"hit_taken", A, "a hero is hit"},
    {"crit_taken", A, "a hero is crit"},
    {"deathblow_resist", A, "a hero at Death's Door resists a deathblow (actor)"},
    // every HP, stress and torch change, in and out of combat (value_hooks.c): actor (target), attacker (the source
    // actor, if any), amount (before: the game's input, may be changed), result (after), kind / source / type
    {"hp_damage", B | A,
     "an actor takes HP damage (amount before resistances; type: attack, bleed, poisoned, trap...)"},
    {"hp_heal", B | A, "an actor is healed (amount before modifiers)"},
    {"stress_damage", B | A, "an actor gains stress (amount before resistances, darkness and difficulty)"},
    {"stress_heal", B | A, "an actor's stress is healed (amount before modifiers)"},
    {"torch_change", B | A, "the torch changes (amount: the change; result: the clamped change applied)"},
    // DoTs, movement and hunger (expedition_hooks.c)
    {"dot_tick", B | A,
     "an actor's DoTs tick (source: battle, corridor or burn; cancel skips the tick; result: HP lost)"},
    {"step", A, "the party steps onto a new corridor tile"},
    {"raid_ready", A,
     "the expedition is on screen and the player is about to act (once per entry; source: explore or battle)"},
    {"area_enter", A, "the party enters an area (area; source: room or corridor)"},
    {"hunger_check", B | A, "the party reaches a hunger tile (cancel: no hunger; result: 1 if the party goes hungry)"},
    {"scout", B | A,
     "an area is scouted (area; source: room, raid_start or curio; kind: depth, 4 critical; cancel: not scouted)"},
    // curios, obstacles, doors and traps (prop_hooks.c): actor: the hero; prop; kind / source: the result type
    {"curio_result", A, "the game picks an interaction's outcome (curio, item on a curio, obstacle, door, prisoner)"},
    {"curio_apply", B | A,
     "an interaction's outcome happens (cancel: skipped, for loot, quirk, effect, scouting, purge, disease)"},
    {"trap", B | A,
     "a trap is resolved (amount: the hero's chance, may be changed; source: disarm or walk; result: 1 safe)"},
    // loot (loot_hooks.c)
    {"loot_queue", B | A,
     "a loot table code is queued (source: battle, curio...; type: the code, may be changed; cancel)"},
    {"loot_item", B | A, "a given item is queued as loot (item; cancel)"},
    {"loot_generated", B | A,
     "a queued loot code is generated, once per code (source; type: the code; inventory: the "
     "result; kind: its position in the batch, amount: the batch's code count; before: codes "
     "queued now are generated into the same window; cancel: none)"},
    // heroes and camping (hero_hooks.c): actor: the hero
    {"quirk_add", B | A,
     "a hero gains a quirk or disease (type: its id, may be changed; cancel: not added; result: 1 added)"},
    {"quirk_remove", B | A, "a hero loses a quirk (type: its id, kind: its index; cancel: kept; result: 1 removed)"},
    {"deaths_door", A, "a hero reaches Death's Door (type, source: the damage's type and source)"},
    {"heart_attack", B | A, "a hero has a heart attack at max stress (cancel: none, the stress stays)"},
    {"death", A, "a hero dies"},
    {"camp_skill", B | A,
     "a camping skill is used (target: on whom; type: its id; amount: its cost; cancel: no effect, "
     "points spent; result: 1 performed)"},
    {"camp_end", A, "a hero's camp ends"},
    {"item_used", A, "a hero uses an item on an expedition (item)"},
    {"trinket_triggered", A, "a hero's trinket effect triggers (the game names no trinket)"},
    // barks (barks.c): actor: who barks
    {"bark", A,
     "an actor's bark is queued (text: as shown; type: its string id; source: the bark type, neutral, "
     "afflicted, virtued or cursed; curio trigger barks too, prop: the curio)"},
#ifdef HOOKS_TEST_POINTS
    {"test_value", B | A, "offline tests only (engine_hooks/tests/test_hooks.c)"},
#endif
};
#undef B
#undef A
#define NPOINTS ((int)(sizeof k_points / sizeof *k_points))

#define MAX_LISTENERS 256
// point -1: all points ("*"); wants: an all-points listener that counts for hook_wanted (hook_on_all); owner: its mod
typedef struct {
    int point;
    HookPhase phase;
    HookCallback callback;
    void *userdata;
    bool wants;
    int owner;
} Listener;
static Listener g_listeners[MAX_LISTENERS];
static int g_nlisteners;
static int g_dispatch[MAX_LISTENERS], g_ndispatch; // the listeners called, in dispatch order (rebuilt when dirty)
static bool g_dirty = true;

static void mark_dirty(void) {
    g_dirty = true;
}

static void rebuild_order(void) {
    int owners[MAX_LISTENERS];
    for (int i = 0; i < g_nlisteners; i++)
        owners[i] = g_listeners[i].owner;
    g_ndispatch = mods_order(owners, g_nlisteners, false, g_dispatch);
    g_dirty = false;
}

static int point_find(const char *name) {
    for (int i = 0; name && i < NPOINTS; i++)
        if (!strcmp(k_points[i].name, name))
            return i;
    return -1;
}

bool hook_point_exists(const char *name) {
    return point_find(name) >= 0;
}

int hook_point_count(void) {
    return NPOINTS;
}

const char *hook_point_name(int i) {
    return i >= 0 && i < NPOINTS ? k_points[i].name : NULL;
}

const char *hook_point_about(int i) {
    return i >= 0 && i < NPOINTS ? k_points[i].about : NULL;
}

bool hook_point_has_phase(int i, HookPhase phase) {
    return i >= 0 && i < NPOINTS && (k_points[i].phases >> phase & 1);
}

const char *hook_phase_name(HookPhase phase) {
    return phase == HOOK_BEFORE ? "before" : "after";
}

bool hook_on(const char *point, HookPhase phase, HookCallback callback, void *userdata) {
    if (!point || !callback || (phase != HOOK_BEFORE && phase != HOOK_AFTER))
        return false;
    int p = -1;
    if (strcmp(point, "*")) {
        p = point_find(point);
        if (p < 0) {
            logf_("hooks: no point named %s", point);
            return false;
        }
        if (!hook_point_has_phase(p, phase)) {
            logf_("hooks: %s has no %s phase", point, hook_phase_name(phase));
            return false;
        }
    }
    if (g_nlisteners >= MAX_LISTENERS) {
        logf_("hooks: more than %d listeners, %s not added", MAX_LISTENERS, point);
        return false;
    }
    static bool watching;
    if (!watching) {
        watching = true;
        mods_on_campaign(mark_dirty);
    }
    g_listeners[g_nlisteners++] = (Listener){p, phase, callback, userdata, false, mods_current()};
    g_dirty = true;
    return true;
}

bool hook_on_all(HookPhase phase, HookCallback callback, void *userdata) {
    if (!hook_on("*", phase, callback, userdata))
        return false;
    g_listeners[g_nlisteners - 1].wants = true;
    return true;
}

bool hook_wanted(const char *point) {
    if (g_debug)
        return true;
    int p = point_find(point);
    for (int i = 0; p >= 0 && i < g_nlisteners; i++)
        if (g_listeners[i].point == p || (g_listeners[i].point < 0 && g_listeners[i].wants))
            return true;
    return false;
}

bool hook_any_wanted(const char *const *points, int n) {
    for (int i = 0; i < n; i++)
        if (hook_wanted(points[i]))
            return true;
    return false;
}

bool hook_emit(const char *point, HookPhase phase, HookContext *ctx) {
    if (!ctx)
        return true;
    int p = point_find(point);
    if (p < 0 || !hook_point_has_phase(p, phase)) {
        dbgf_("hooks: %s %s isn't declared", point ? point : "(null)", hook_phase_name(phase));
        return true;
    }
    ctx->point = k_points[p].name;
    ctx->phase = phase;
    ctx->cancel = false;
    // The point's listeners, then the "*" ones (so a log sees what the game gets). Snapshot the count: a callback
    // can't extend the dispatch in progress.
    if (g_dirty)
        rebuild_order();
    int n = g_ndispatch;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < n; k++) {
            const Listener *l = &g_listeners[g_dispatch[k]];
            if (l->phase == phase && l->point == (pass ? -1 : p))
                l->callback(ctx, l->userdata);
        }
    return phase != HOOK_BEFORE || !ctx->cancel;
}
