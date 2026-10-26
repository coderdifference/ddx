// The combat hook points (hooks.c): per target of each attack, from Battle::ApplyResult's two per-target calls,
// Battle::ApplyHit (hit, hit_taken, crit, crit_taken, kill) and Battle::ApplyMiss (miss, and dodge when the target is a
// hero: the game's "Dodge!"), and deathblow_resist from Hero::OnDeathDoorDeathAvoided; helpers that act through the
// game's combat code.
#include "common.h"
#include <string.h>

static bool g_followup;
static bool g_side_effect_filter_ready;
static CombatFollowupEffectFilter g_followup_filter;
static void *g_followup_filter_data;
static void (*g_apply_hit_original)(void *battle, const void *attack, const void *target_data);
static void (*g_apply_miss_original)(void *battle, const void *attack, const void *target_data);
static void **g_raid; // Raid::s_Instance; the Battle is the raid's, at RAID_BATTLE

static void *raid_battle(void) {
    return g_raid && *g_raid ? (uint8_t *)*g_raid + RAID_BATTLE : NULL;
}
static bool (*g_actor_death_started)(const void *actor);
static void (*g_death_door_avoided_original)(void *hero);
static void (*g_apply_side_effect_original)(void *battle, void *actor, const void *target_data, const void *effect,
                                            void *effects, bool b1, bool b2);

// the context of one attack target (Attack: attacker +0x20, skill +0x28; TargetData: target +0, amount +8, hit +0x10,
// crit +0x11, corpse +0x18)
static HookContext target_context(void *battle, const void *attack, const void *td) {
    const uint8_t *a = attack, *t = td;
    return (HookContext){.battle = battle,
                         .attack = attack,
                         .target_data = td,
                         .attacker = *(void *const *)(a + 0x20),
                         .skill = *(const void *const *)(a + 0x28),
                         .target = *(void *const *)t,
                         .amount = *(const float *)(t + 0x08),
                         .kind = -1,
                         .hit = t[0x10] != 0,
                         .crit = t[0x11] != 0,
                         .synthetic = false};
}

// A missed target never reaches ApplyHit: ApplyResult calls ApplyMiss for it (2026-10-19: heroes dodged 16 times in a
// log with no miss / dodge point while those came from ApplyHit's hit flag). actor: the attacker for miss, the hero
// for dodge. ApplyMiss doesn't use its this: ApplyResult leaves rdi as PlayTargetVfx left it (garbage; Riposting's
// RNG read through it crashed), so the context's battle is the raid's.
static void apply_miss_hook(void *unused_this, const void *attack, const void *td) {
    if (!g_apply_miss_original)
        return;
    g_apply_miss_original(unused_this, attack, td);
    void *battle = raid_battle();
    if (!attack || !td || g_followup || !battle)
        return;
    HookContext c = target_context(battle, attack, td);
    c.hit = c.crit = false;
    c.actor = c.attacker;
    hook_emit("miss", HOOK_AFTER, &c);
    if (c.target && is_hero(c.target)) {
        c.actor = c.target;
        hook_emit("dodge", HOOK_AFTER, &c);
    }
}

static void apply_hit_hook(void *battle, const void *attack, const void *td) {
    if (!g_apply_hit_original)
        return;
    g_apply_hit_original(battle, attack, td);
    if (!attack || !td || g_followup)
        return;
    static bool checked; // once: is ApplyHit's this the raid's Battle (what misses use)?
    if (!checked && raid_battle()) {
        checked = true;
        logf_("combat hooks: ApplyHit battle %p, raid + 0x%x %p: %s", battle, RAID_BATTLE, raid_battle(),
              battle == raid_battle() ? "same" : "DIFFERENT (miss / dodge contexts would be wrong)");
    }
    const uint8_t *t = td;
    HookContext c = target_context(battle, attack, td);
    void *target = c.target;
    // actor: the attacker for its own points (hit, crit, kill), the target for the points it takes.
    c.actor = c.attacker;
    hook_emit("hit", HOOK_AFTER, &c);
    if (target && is_hero(target)) {
        c.actor = target;
        hook_emit("hit_taken", HOOK_AFTER, &c);
        if (c.crit)
            hook_emit("crit_taken", HOOK_AFTER, &c);
    }
    c.actor = c.attacker;
    if (c.crit)
        hook_emit("crit", HOOK_AFTER, &c);
    // TargetData marks corpse selections separately. A living target reaching zero HP
    // during this native hit is a per-target kill trigger.
    if (!t[0x18] && target && g_actor_death_started && g_actor_death_started(target))
        hook_emit("kill", HOOK_AFTER, &c);
}

// A generated follow-up passes through the game's normal hit/side-effect pipeline. Its
// client-supplied filter decides which skill effects are replayed on the secondary target.
static void apply_side_effect_hook(void *battle, void *actor, const void *td, const void *effect, void *effects,
                                   bool b1, bool b2) {
    if (g_followup && g_followup_filter && !g_followup_filter(effect, g_followup_filter_data))
        return;
    if (g_apply_side_effect_original)
        g_apply_side_effect_original(battle, actor, td, effect, effects, b1, b2);
}

static void death_door_avoided_hook(void *hero) {
    if (g_death_door_avoided_original)
        g_death_door_avoided_original(hero);
    if (!hero)
        return;
    HookContext context = {.actor = hero, .kind = -1};
    hook_emit("deathblow_resist", HOOK_AFTER, &context);
}

bool combat_apply_followup_hit(const HookContext *source, void *target, float scale, bool crit,
                               CombatFollowupEffectFilter filter, void *userdata) {
    if (!source || !source->battle || !source->attack || !source->target_data || !target || !g_apply_hit_original ||
        !g_side_effect_filter_ready || scale < 0.0f)
        return false;
    uint8_t td[0x20];
    memcpy(td, source->target_data, sizeof td);
    *(void **)td = target;
    *(float *)(td + 0x08) = source->amount * scale;
    td[0x10] = 1; // successful synthetic hit
    td[0x11] = crit ? 1 : 0;
    td[0x18] = 0; // this is a living actor, not a corpse target
    bool was_followup = g_followup;
    CombatFollowupEffectFilter old_filter = g_followup_filter;
    void *old_filter_data = g_followup_filter_data;
    g_followup = true;
    g_followup_filter = filter;
    g_followup_filter_data = userdata;
    g_apply_hit_original(source->battle, source->attack, td);
    g_followup = was_followup;
    g_followup_filter = old_filter;
    g_followup_filter_data = old_filter_data;
    return true;
}

// Battle::Actor_ApplySideEffect(performer, TargetData, effect, ...): its actor is the effect's performer (EffectApply's
// first actor, the game passes the attacker); the target is always the TargetData's (+0), so a copy carries ours.
bool combat_apply_effect(const HookContext *source, void *performer, void *target, const void *effect) {
    if (!source || !source->battle || !performer || !target || !source->target_data || !effect ||
        !g_apply_side_effect_original)
        return false;
    uint8_t td[0x20];
    memcpy(td, source->target_data, sizeof td);
    *(void **)td = target;
    td[0x18] = 0; // a living actor, not a corpse target
    // The native method writes its pending effect pointers into this vector. Inline capacity
    // avoids an allocation and leaves nothing to destroy when the call returns.
    const void *storage[8];
    const void **vector[3] = {storage, storage, storage + 8};
    g_apply_side_effect_original(source->battle, performer, td, effect, vector, true, false);
    return true;
}

bool combat_apply_side_effect(const HookContext *source, const void *effect) {
    return source && combat_apply_effect(source, source->attacker, source->target, effect);
}

void combat_hooks_install(void) {
    static const char *const hit_points[] = {"hit", "miss", "dodge", "crit", "kill", "hit_taken", "crit_taken"};
    static const char *const miss_points[] = {"miss", "dodge"};
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    if (hook_any_wanted(miss_points, 2)) {
        static const uint8_t miss_prologue[] = PROLOGUE_SUB32(0x88, 0x00); // sub rsp,0x88
        g_apply_miss_original = (void (*)(void *, const void *, const void *))install_hook(
            "Battle::ApplyMiss (combat hooks)", sym("_ZN6Battle9ApplyMissEPKNS_6AttackERKNS0_10TargetDataE"),
            (void *)apply_miss_hook, miss_prologue, sizeof miss_prologue);
    }
    bool hits = hook_any_wanted(hit_points, 7);
    if (hits) {
        static const uint8_t hit_prologue[] = PROLOGUE_SUB32(0x68, 0x01); // sub rsp,0x168
        g_apply_hit_original = (void (*)(void *, const void *, const void *))install_hook(
            "Battle::ApplyHit (combat hooks)", sym("_ZN6Battle8ApplyHitEPKNS_6AttackERKNS0_10TargetDataE"),
            (void *)apply_hit_hook, hit_prologue, sizeof hit_prologue);
        g_actor_death_started = (bool (*)(const void *))sym("_ZNK5Actor22GetWasDeathStartCalledEv");
        if (!g_actor_death_started)
            logf_("combat hooks: Actor::GetWasDeathStartCalled unavailable; kill point off");
        // the follow-up filter; the helpers take a hit point's context, so they're only needed with the hit points
        static const uint8_t side_effect_prologue[] = PROLOGUE_SUB8(0x48);
        g_apply_side_effect_original =
            (void (*)(void *, void *, const void *, const void *, void *, bool, bool))install_hook(
                "Battle::Actor_ApplySideEffect (follow-up filter)",
                sym("_ZN6Battle21Actor_ApplySideEffectEP5ActorRKNS_6Attack10TargetDataEPK6EffectRNSt3__16vectorIS8_NS9_"
                    "9allocatorIS8_EEEEbb"),
                (void *)apply_side_effect_hook, side_effect_prologue, sizeof side_effect_prologue);
        g_side_effect_filter_ready = g_apply_side_effect_original != NULL;
    } else
        logf_("combat hooks: no listeners on the hit points, Battle::ApplyHit not hooked");
    if (hook_wanted("deathblow_resist")) {
        static const uint8_t avoided_prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x81,
                                                   0xec, 0x88, 0x00, 0x00, 0x00, 0x8b, 0x9f, 0x68, 0x12, 0x00, 0x00};
        g_death_door_avoided_original = (void (*)(void *))install_hook(
            "Hero::OnDeathDoorDeathAvoided (deathblow_resist)", sym("_ZN4Hero23OnDeathDoorDeathAvoidedEv"),
            (void *)death_door_avoided_hook, avoided_prologue, sizeof avoided_prologue);
    }
}
