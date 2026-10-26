// The value hook points (hooks.c): every HP, stress and torch change, in and out of combat, at the game's own
// chokepoints. before: amount is the game's input (callbacks may change it, or cancel); after: result is what the game
// did. Design: design/docs/13-expedition-hooks.md; offsets: docs/engine-design.md "Value hook points".
//   hp_damage      Actor:: and Monster::ApplyHealthDamageInternal, the virtual every HP loss goes through (hits, DoTs,
//                  burn, traps, hunger, obstacles, reflect, riposte...; DoTs call it directly, not ApplyHealthDamage)
//   hp_heal        Actor::ApplyHealthHeal
//   stress_damage  Actor::ApplyStressDamage, and Actor::ApplyRawStressDamage when called directly (missed attacks,
//                  skill side effects, monster spawns: source "raw", no modifiers)
//   stress_heal    Actor::ApplyStressHeal
//   torch_change   Raid::ChangeTorchlight
// Windows: same functions and prototypes; the id lookups and create_Timeline are stand-ins (syms_win.c).
#include "common.h"

#define DAMAGE_TYPES   20 // eDamageType: unknown .. burn
#define DAMAGE_SOURCES 14 // eDamageSourceType: unknown .. trinket
#define STRESS_SOURCES 22 // Stress::Source::Types::eType
#define HEAL_SOURCES   15 // Heal::Source::Types::eType

static const char *(*g_damage_type_id)(int);
static const char *(*g_damage_source_id)(int);
static const char *(*g_stress_source_id)(int);
static const char *g_heal_types;
static void *(*g_create_timeline)(void);

typedef void *(*HpDamageFn)(void *actor, void *prng, float amount, const int *type, unsigned u, const int *source,
                            bool b, void *source_actor);
static HpDamageFn g_hp_damage_original, g_monster_hp_damage_original;
static float (*g_hp_heal_original)(void *actor, void *prng, float amount, const void *source_actor, int source, bool b);
static float (*g_stress_damage_original)(void *actor, void *prng, float amount, const void *source_actor, int source);
static float (*g_stress_raw_original)(void *actor, void *prng, float amount);
static float (*g_stress_heal_original)(void *actor, void *prng, float amount, const void *source_actor, int source);
static float (*g_torch_original)(void *raid, float amount, bool b);
static int g_depth;              // value points being emitted (a callback dealing damage: synthetic)
static int g_stress_depth;       // inside ApplyStressDamage (its raw call isn't a point of its own)
static void *g_monster_hp_actor; // inside Monster::ApplyHealthDamageInternal, which may pass it to Actor's

static const char *id_or_null(const char *(*lookup)(int), int v, int n) {
    return lookup && v >= 0 && v < n ? lookup(v) : NULL;
}

static float field(const void *p, int offset) {
    return p ? *(const float *)((const uint8_t *)p + offset) : 0;
}

// before, the game, after; false: cancelled (the game wasn't called)
static bool emit_before(const char *point, HookContext *c, bool signed_amount) {
    c->synthetic = g_depth > 0;
    g_depth++;
    bool go = hook_emit(point, HOOK_BEFORE, c);
    g_depth--;
    if (c->amount < 0 && !signed_amount)
        c->amount = 0; // damage, heal and stress amounts aren't signed
    return go;
}

static void emit_after(const char *point, HookContext *c) {
    g_depth++;
    hook_emit(point, HOOK_AFTER, c);
    g_depth--;
}

static void *hp_damage(HpDamageFn original, void *actor, void *prng, float amount, const int *type, unsigned u,
                       const int *source, bool b, void *source_actor) {
    int t = type ? *type : -1, s = source ? *source : -1;
    HookContext c = {.actor = actor,
                     .target = actor,
                     .attacker = source_actor,
                     .amount = amount,
                     .kind = s,
                     .source = id_or_null(g_damage_source_id, s, DAMAGE_SOURCES),
                     .type = id_or_null(g_damage_type_id, t, DAMAGE_TYPES)};
    // cancelled: the callers queue (or ignore) the timeline the game returns, and some don't check it for NULL
    if (!emit_before("hp_damage", &c, false))
        return g_create_timeline ? g_create_timeline() : NULL;
    float hp = field(actor, ACTOR_HP);
    void *timeline = original(actor, prng, c.amount, type, u, source, b, source_actor);
    c.result = hp - field(actor, ACTOR_HP);
    emit_after("hp_damage", &c);
    return timeline;
}

static void *hp_damage_hook(void *actor, void *prng, float amount, const int *type, unsigned u, const int *source,
                            bool b, void *source_actor) {
    if (actor == g_monster_hp_actor) // Monster's, passed on: already a point
        return g_hp_damage_original(actor, prng, amount, type, u, source, b, source_actor);
    return hp_damage(g_hp_damage_original, actor, prng, amount, type, u, source, b, source_actor);
}

static void *monster_hp_damage_hook(void *actor, void *prng, float amount, const int *type, unsigned u,
                                    const int *source, bool b, void *source_actor) {
    void *outer = g_monster_hp_actor; // its effects may damage another monster
    g_monster_hp_actor = actor;
    void *r = hp_damage(g_monster_hp_damage_original, actor, prng, amount, type, u, source, b, source_actor);
    g_monster_hp_actor = outer;
    return r;
}

static float hp_heal_hook(void *actor, void *prng, float amount, const void *source_actor, int source, bool b) {
    HookContext c = {
        .actor = actor,
        .target = actor,
        .attacker = (void *)source_actor,
        .amount = amount,
        .kind = source,
        .source = g_heal_types && source >= 0 && source < HEAL_SOURCES ? g_heal_types + source * HEAL_TYPE_SIZE : NULL};
    if (!emit_before("hp_heal", &c, false))
        return 0;
    c.result = g_hp_heal_original(actor, prng, c.amount, source_actor, source, b);
    emit_after("hp_heal", &c);
    return c.result;
}

static float stress_damage_hook(void *actor, void *prng, float amount, const void *source_actor, int source) {
    HookContext c = {.actor = actor,
                     .target = actor,
                     .attacker = (void *)source_actor,
                     .amount = amount,
                     .kind = source,
                     .source = id_or_null(g_stress_source_id, source, STRESS_SOURCES)};
    if (!emit_before("stress_damage", &c, false))
        return 0;
    g_stress_depth++;
    c.result = g_stress_damage_original(actor, prng, c.amount, source_actor, source);
    g_stress_depth--;
    emit_after("stress_damage", &c);
    return c.result;
}

static float stress_raw_hook(void *actor, void *prng, float amount) {
    if (g_stress_depth)
        return g_stress_raw_original(actor, prng, amount);
    HookContext c = {.actor = actor, .target = actor, .amount = amount, .kind = -1, .source = "raw"};
    if (!emit_before("stress_damage", &c, false))
        return 0;
    c.result = g_stress_raw_original(actor, prng, c.amount);
    emit_after("stress_damage", &c);
    return c.result;
}

static float stress_heal_hook(void *actor, void *prng, float amount, const void *source_actor, int source) {
    HookContext c = {.actor = actor,
                     .target = actor,
                     .attacker = (void *)source_actor,
                     .amount = amount,
                     .kind = source,
                     .source = id_or_null(g_stress_source_id, source, STRESS_SOURCES)};
    if (!emit_before("stress_heal", &c, false))
        return 0;
    float stress = field(actor, ACTOR_STRESS);
    float r = g_stress_heal_original(actor, prng, c.amount, source_actor, source);
    c.result = stress - field(actor, ACTOR_STRESS);
    emit_after("stress_heal", &c);
    return r;
}

static float torch_hook(void *raid, float amount, bool b) {
    HookContext c = {.raid = raid, .amount = amount, .kind = -1};
    if (!emit_before("torch_change", &c, true))
        return 0;
    c.result = g_torch_original(raid, c.amount, b);
    emit_after("torch_change", &c);
    return c.result;
}

void value_hooks_install(void) {
    g_damage_type_id = (const char *(*)(int))sym("_Z19GetIdFromDamageType11eDamageType");
    g_damage_source_id = (const char *(*)(int))sym("_Z25GetIdFromDamageSourceType17eDamageSourceType");
    g_stress_source_id = (const char *(*)(int))sym("_ZN6Stress6Source13GetIdFromTypeENS0_5Types5eTypeE");
    g_heal_types = (const char *)sym("_ZN4Heal6SourceL10k_TypeDataE");
    if (hook_wanted("hp_damage")) {
        g_create_timeline = (void *(*)(void))sym("_Z15create_Timelinev");
        static const uint8_t p[] = PROLOGUE_SUB32(0xd8, 0x00), m[] = PROLOGUE_SUB32(0xf8, 0x01);
        *(void **)&g_hp_damage_original = install_hook(
            "Actor::ApplyHealthDamageInternal (hp_damage)",
            sym("_ZN5Actor25ApplyHealthDamageInternalER11prng_minstdfRK11eDamageTypejRK17eDamageSourceTypebPS_"),
            (void *)hp_damage_hook, p, sizeof p);
        *(void **)&g_monster_hp_damage_original = install_hook(
            "Monster::ApplyHealthDamageInternal (hp_damage)",
            sym("_ZN7Monster25ApplyHealthDamageInternalER11prng_minstdfRK11eDamageTypejRK17eDamageSourceTypebP5Actor"),
            (void *)monster_hp_damage_hook, m, sizeof m);
    }
    if (hook_wanted("hp_heal")) {
        static const uint8_t p[] = PROLOGUE_SUB8(0x78);
        *(void **)&g_hp_heal_original =
            install_hook("Actor::ApplyHealthHeal (hp_heal)",
                         sym("_ZN5Actor15ApplyHealthHealER11prng_minstdfPKS_N4Heal6Source5Types5eTypeEb"),
                         (void *)hp_heal_hook, p, sizeof p);
    }
    if (hook_wanted("stress_damage")) {
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x30};
        *(void **)&g_stress_damage_original =
            install_hook("Actor::ApplyStressDamage (stress_damage)",
                         sym("_ZN5Actor17ApplyStressDamageER11prng_minstdfPKS_N6Stress6Source5Types5eTypeE"),
                         (void *)stress_damage_hook, p, sizeof p);
        static const uint8_t r[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                    0x53, 0x48, 0x81, 0xec, 0xa8, 0x00, 0x00, 0x00};
        *(void **)&g_stress_raw_original =
            install_hook("Actor::ApplyRawStressDamage (stress_damage)",
                         sym("_ZN5Actor20ApplyRawStressDamageER11prng_minstdf"), (void *)stress_raw_hook, r, sizeof r);
    }
    if (hook_wanted("stress_heal")) {
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                    0x53, 0x48, 0x83, 0xec, 0x18, 0x41, 0x89, 0xce};
        *(void **)&g_stress_heal_original =
            install_hook("Actor::ApplyStressHeal (stress_heal)",
                         sym("_ZN5Actor15ApplyStressHealER11prng_minstdfPKS_N6Stress6Source5Types5eTypeE"),
                         (void *)stress_heal_hook, p, sizeof p);
    }
    if (hook_wanted("torch_change")) {
        static const uint8_t p[] = PROLOGUE_SUB8(0x38);
        *(void **)&g_torch_original =
            install_hook("Raid::ChangeTorchlight (torch_change)", sym("_ZN4Raid16ChangeTorchlightEfb"),
                         (void *)torch_hook, p, sizeof p);
    }
}
