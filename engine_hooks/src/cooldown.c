// Skill cooldowns (config "cooldowns"; a skill's "cooldown" names one or more, shared by every skill naming it).
//   Battle::Actor_DoSkill(actor, skill, targets, b): every skill an actor performs (player choice, afflicted AI,
//     out-of-turn self skills) -> the skill's cooldowns start (turns + 1, see config_cooldown_start); it also
//     numbers skill uses (skill_use_id) so an action on the performer applies once per use (effects.c), and fires
//     the skill_use point after the game has resolved it.
//   Battle::GetIsSkillBattleValid(actor, skill) -> Battle::SkillValidResult: usable only if not disabled by a
//     buff (0), per-turn uses left (8), per-battle uses left (0x10) and the performer-HP check (0x18). A skill on
//     cooldown gets 0x18 = false: greyed out on the bar and refused, with no misleading uses line (that line
//     only shows for skills with a battle limit). The same check drives the bar, CanBeHeroSkillUsed and the AI.
//   Counting (called from events.c): "own" cooldowns tick when the hero's own turn ends, "actor" ones when any
//     actor's turn starts (monsters too), for every hero; all reset when a battle starts or ends.
#include "common.h"
#include <limits.h>

#ifdef _WIN32
typedef void *(*valid_fn)(const void *battle, void *result, const void *actor, const void *skill); // MSVC: this first
#else
typedef void *(*valid_fn)(void *result, const void *battle, const void *actor, const void *skill);
#endif
static valid_fn g_valid_orig;
typedef uint64_t (*do_skill_fn)(void *battle, void *actor, const void *skill, const void *targets, bool b);
static do_skill_fn g_do_orig;

#ifdef _WIN32
static void *valid_hook(const void *battle, void *result, const void *actor, const void *skill) {
    g_valid_orig(battle, result, actor, skill);
#else
static void *valid_hook(void *result, const void *battle, const void *actor, const void *skill) {
    g_valid_orig(result, battle, actor, skill);
#endif
    const ClassConfig *c = config_for_hero(actor);
    if (c && skill && config_cooldown_left(c, actor, skill_id_hash(skill)) > 0)
        ((uint8_t *)result)[VALID_HP_OK] = 0;
    return result;
}

static unsigned g_use_id;
typedef struct {
    const void *performer;
    const void *skill;
    const void *targets;
    unsigned use_id;
} SkillContext;
#define MAX_SKILL_CONTEXTS 32
static SkillContext g_contexts[MAX_SKILL_CONTEXTS];
static int g_context_depth;

unsigned skill_use_id(void) {
    return g_context_depth > 0 ? g_contexts[g_context_depth - 1].use_id : 0;
}

const void *skill_resolution_performer(void) {
    return g_context_depth > 0 ? g_contexts[g_context_depth - 1].performer : NULL;
}

const void *skill_resolution_skill(void) {
    return g_context_depth > 0 ? g_contexts[g_context_depth - 1].skill : NULL;
}

static int resolution_target_count(const void *targets) {
    if (!targets)
        return 0;
    const void *const *begin = *(const void *const *const *)targets;
    const void *const *end = *(const void *const *const *)((const uint8_t *)targets + sizeof(void *));
    uintptr_t start = (uintptr_t)begin, finish = (uintptr_t)end;
    if (!begin || finish < start || (finish - start) % sizeof(void *) != 0)
        return 0;
    uintptr_t count = (finish - start) / sizeof(void *);
    return count <= INT_MAX ? (int)count : 0;
}

int skill_resolution_target_count(void) {
    return g_context_depth > 0 ? resolution_target_count(g_contexts[g_context_depth - 1].targets) : 0;
}

const void *skill_resolution_target(int index) {
    if (index < 0 || index >= skill_resolution_target_count())
        return NULL;
    const void *const *begin = *(const void *const *const *)g_contexts[g_context_depth - 1].targets;
    return begin[index];
}

static struct {
    const void *actor, *skill;
    unsigned use_id;
} g_last[16];
static int g_last_next;

const void *skill_last_used(const void *actor, unsigned *use_id) {
    for (int i = 0; actor && i < 16; i++)
        if (g_last[i].actor == actor) {
            if (use_id)
                *use_id = g_last[i].use_id;
            return g_last[i].skill;
        }
    return NULL;
}

static void remember_use(const void *actor, const void *skill, unsigned use_id) {
    int k = -1;
    for (int i = 0; i < 16 && k < 0; i++)
        if (g_last[i].actor == actor)
            k = i;
    if (k < 0) {
        k = g_last_next;
        g_last_next = (g_last_next + 1) % 16;
    }
    g_last[k].actor = actor;
    g_last[k].skill = skill;
    g_last[k].use_id = use_id;
}

static uint64_t do_hook(void *battle, void *actor, const void *skill, const void *targets, bool b) {
    combat_log_skill_use(actor, skill, targets, b);
    unsigned use_id = ++g_use_id;
    remember_use(actor, skill, use_id);
    const ClassConfig *c = config_for_hero(actor);
    if (c && skill)
        config_cooldown_start(c, actor, skill_id_hash(skill), true);
    if (g_context_depth >= MAX_SKILL_CONTEXTS) {
        logf_("skill resolution context stack full; context unavailable for use %u", use_id);
        return g_do_orig(battle, actor, skill, targets, b);
    }
    g_contexts[g_context_depth++] = (SkillContext){actor, skill, targets, use_id};
    uint64_t r = g_do_orig(battle, actor, skill, targets, b);
    if (skill && hook_wanted("skill_use")) { // the context is still current: skill_resolution_target works
        int n = resolution_target_count(targets);
        HookContext c = {.battle = battle,
                         .actor = actor,
                         .attacker = actor,
                         .skill = skill,
                         .amount = (float)n,
                         .target = n > 0 ? (void *)skill_resolution_target(0) : NULL,
                         .kind = -1};
        hook_emit("skill_use", HOOK_AFTER, &c);
    }
    g_context_depth--;
    return r;
}

void cooldown_on_turn_end(void *hero) {
    const ClassConfig *c = config_for_hero(hero);
    if (c)
        config_cooldown_tick(c, hero_guid(hero), false);
}

static void tick_actor(unsigned guid, void *ctx) {
    for (int i = 0; config_class_at(i); i++)
        config_cooldown_tick(config_class_at(i), guid, true);
}
void cooldown_on_turn_start(void *actor) {
    herodata_foreach(tick_actor, NULL);
}

void cooldown_on_battle(void *hero) {
    const ClassConfig *c = config_for_hero(hero);
    if (c)
        config_cooldown_reset(c, hero_guid(hero));
    paging_reset(hero);
}

void cooldown_install(void) {
    static const uint8_t do_prologue[] = PROLOGUE_SUB32(0x68, 0x02); // sub rsp,0x268
    g_do_orig = (do_skill_fn)install_hook(
        "Battle::Actor_DoSkill",
        sym("_ZN6Battle13Actor_DoSkillEP5ActorPK16ActorCombatSkillRKNSt3__16vectorIS1_NS5_9allocatorIS1_EEEEb"),
        (void *)do_hook, do_prologue, sizeof do_prologue);
    if (!config_any_cooldowns()) {
        logf_("cooldowns: none configured, validity hook not installed");
        return;
    }
    static const uint8_t valid_prologue[] = PROLOGUE_SUB8(0x38); // sub rsp,0x38
    g_valid_orig = (valid_fn)install_hook("Battle::GetIsSkillBattleValid",
                                          sym("_ZNK6Battle21GetIsSkillBattleValidEPK5ActorPK16ActorCombatSkill"),
                                          (void *)valid_hook, valid_prologue, sizeof valid_prologue);
}
