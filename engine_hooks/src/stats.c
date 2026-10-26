// Skill numbers from hero fields (config skill "dmg" and "heal" formulas).
// Heal: Battle::Actor_DoSkillFriendly(battle, performer, skill, targets, bool) rolls random_int(.heal min, max)
// from the skill (+0x364 / +0x368) synchronously; the steps it queues don't read them. For a configured skill
// with a heal formula we write the value into min and max for that call, then restore them.
// Hero::CalculateDerivedStatsInternal(hero, other) computes a hero's combat stats; inside it,
// ActorCombatSkill::GetDamageModifier(other) returns the skill's .dmg (base, +0x43c) plus a rank bonus
// (.rank_damage_modifiers, by `other`'s rank). We remember the hero for the duration of the first call and,
// in the second, replace the base with the hero's formula: result - base + formula.
#include "common.h"
#include <string.h>

static const void *g_hero; // the hero whose stats are being computed
static uint64_t (*o_calc)(void *hero, const void *other);
static float (*o_get_dmg)(const void *skill, const void *actor);
static StatsFriendlyHealMinHook g_friendly_min[4];
static int g_nfriendly_min;
void stats_on_friendly_heal_min(StatsFriendlyHealMinHook fn) {
    if (fn && g_nfriendly_min < 4)
        g_friendly_min[g_nfriendly_min++] = fn;
}

// stats_on_calc clients (a client mod gear rows): only an applied mod's run, befores the top mod's last, afters in reverse
#define MAX_CALC 4
static struct {
    StatsCalcHook before, after;
} g_calc[MAX_CALC];
static int g_ncalc;
static ModsList g_calc_mods;
void stats_on_calc(StatsCalcHook before, StatsCalcHook after) {
    if ((before || after) && g_ncalc < MAX_CALC && mods_list_add(&g_calc_mods) >= 0)
        g_calc[g_ncalc++] = (typeof(g_calc[0])){before, after};
}
static void calc_before(void *hero) {
    MODS_EACH(g_calc_mods, false, i) if (g_calc[i].before) g_calc[i].before(hero);
}
static void calc_after(void *hero) {
    for (int k = mods_list_order(&g_calc_mods, false) - 1; k >= 0; k--) {
        int i = g_calc_mods.order[k];
        if (g_calc[i].after)
            g_calc[i].after(hero);
    }
}
void stats_rows_begin(void *hero) {
    calc_before(hero);
}
void stats_rows_end(void *hero) {
    calc_after(hero);
}

static uint64_t h_calc(void *hero, const void *other) {
    const void *prev = g_hero;
    g_hero = hero;
    calc_before(hero);
    uint64_t r = o_calc(hero, other);
    calc_after(hero);
    g_hero = prev;
    return r;
}

static float h_get_dmg(const void *skill, const void *actor) {
    float v = o_get_dmg(skill, actor);
    const ClassConfig *c = g_hero ? config_for_hero(g_hero) : NULL;
    double f;
    if (c && skill && config_skill_dmg(c, g_hero, skill_id_hash(skill), &f)) {
        float base = *(const float *)((const uint8_t *)skill + SKILL_BASE_DMG);
        float was = v;
        v = v - base + (float)f;
        static unsigned last_guid, last_skill;
        static float last_v = -999;
        if (hero_guid(g_hero) != last_guid || skill_id_hash(skill) != last_skill || v != last_v) { // log changes only
            dbgf_("dmg formula: hero %u skill %08x modifier %.2f (base %.2f, game %.2f)", hero_guid(g_hero),
                  skill_id_hash(skill), v, base, was);
            last_guid = hero_guid(g_hero);
            last_skill = skill_id_hash(skill);
            last_v = v;
        }
    }
    return v;
}

static uint64_t (*o_do_friendly)(void *battle, void *performer, void *skill, const void *targets, bool b);

static uint64_t h_do_friendly(void *battle, void *performer, void *skill, const void *targets, bool b) {
    const ClassConfig *c = config_for_hero(performer);
    double f;
    int *mn = (int *)((uint8_t *)skill + SKILL_HEAL_MIN), *mx = (int *)((uint8_t *)skill + SKILL_HEAL_MAX);
    int old_min = *mn, old_max = *mx;
    if (c && skill && config_skill_heal(c, performer, skill_id_hash(skill), &f)) {
        int v = (int)(f + 0.5);
        *mn = v;
        *mx = v;
        dbgf_("heal formula: hero %u skill heal %d (was %d-%d)", hero_guid(performer), v, old_min, old_max);
    }
    int add = 0;
    for (int i = 0; i < g_nfriendly_min; i++)
        add += g_friendly_min[i](performer, skill);
    *mn += add;
    uint64_t r = o_do_friendly(battle, performer, skill, targets, b);
    *mn = old_min;
    *mx = old_max;
    return r;
}

void stats_install(void) {
    bool formulas = config_any_formulas();
    if (!formulas && !g_ncalc && !g_nfriendly_min) {
        logf_("stats: no formulas configured, hooks not installed");
        return;
    }
    if (formulas || g_ncalc) {
        static const uint8_t calc_prologue[] = PROLOGUE_SUB8(0x38); // sub rsp,0x38
        *(void **)&o_calc =
            install_hook("Hero::CalculateDerivedStatsInternal", sym("_ZN4Hero29CalculateDerivedStatsInternalEPK5Actor"),
                         (void *)h_calc, calc_prologue, sizeof calc_prologue);
    }
    static const uint8_t friendly_prologue[] = PROLOGUE_SUB32(0x18, 0x01); // sub rsp,0x118
    *(void **)&o_do_friendly = install_hook(
        "Battle::Actor_DoSkillFriendly",
        sym("_ZN6Battle21Actor_DoSkillFriendlyEP5ActorPK16ActorCombatSkillRKNSt3__16vectorIS1_NS5_9allocatorIS1_EEEEb"),
        (void *)h_do_friendly, friendly_prologue, sizeof friendly_prologue);
    if (!formulas)
        return;

#ifdef _WIN32
    // Windows: GetDamageModifier starts with an 8-byte movss, a plain hook
    *(void **)&o_get_dmg =
        install_hook("ActorCombatSkill::GetDamageModifier", sym("_ZNK16ActorCombatSkill17GetDamageModifierEPK5Actor"),
                     (void *)h_get_dmg, NULL, 0);
    return;
#endif
    // GetDamageModifier starts: movss 0x43c(%rdi),%xmm0 ; test %rsi,%rsi ; je +0x79 ; push %rbp
    // The short je can't be copied, so the trampoline rewrites it as jne-over + absolute jmp.
    uint8_t *fn = sym("_ZNK16ActorCombatSkill17GetDamageModifierEPK5Actor");
    if (!fn)
        return;
    static const uint8_t expected[] = {0xF3, 0x0F, 0x10, 0x87, 0x3C, 0x04, 0x00, 0x00, // movss 0x43c(%rdi),%xmm0
                                       0x48, 0x85, 0xF6,                               // test %rsi,%rsi
                                       0x74, 0x79,                                     // je +0x79
                                       0x55};                                          // push %rbp
    uint64_t je_target = (uint64_t)fn + 13 + 0x79, resume = (uint64_t)fn + 14;
    uint8_t t[64];
    size_t n = 0;
    memcpy(t + n, expected, 11);
    n += 11; // movss ; test
    t[n++] = 0x75;
    t[n++] = 14; // jne +14 (skip the absolute jmp)
    t[n++] = 0xFF;
    t[n++] = 0x25;
    memset(t + n, 0, 4);
    n += 4; // jmp [rip+0] -> je target
    memcpy(t + n, &je_target, 8);
    n += 8;
    t[n++] = 0x55; // push %rbp
    t[n++] = 0xFF;
    t[n++] = 0x25;
    memset(t + n, 0, 4);
    n += 4; // jmp [rip+0] -> rest of the original
    memcpy(t + n, &resume, 8);
    n += 8;
    *(void **)&o_get_dmg = install_hook_with_trampoline("ActorCombatSkill::GetDamageModifier", fn, (void *)h_get_dmg,
                                                        expected, sizeof expected, t, n);
}
