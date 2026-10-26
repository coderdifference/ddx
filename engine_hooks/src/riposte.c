// Ripostes with the hero's own skills (config "riposte": {"skills": ["melee"], "no_skill_bark": "<string id>"}).
// Vanilla: Battle::RiposteAttempt(attack) walks the attack's targets; for each living, unstunned target whose
// Actor::GetRiposteSkill() (its class's riposte_skill, NULL for most hero classes) is set, against a skill that can
// be riposted, it sets SetRiposteEnabled(true) (Hero::CalculateDerivedStatsInternal then takes accuracy and damage from
// GetRiposteSkill() instead of the current skill), rolls riposte_on_hit / riposte_on_miss chance against the battle's
// rng, and on success queues a Battle::Attack in Battle+0x258 (performer, skill, type 0xd), sets the skill as current
// (SetCurrentCombatSkill(skill, attacker, true)), rolls the hit (Attack::AddTarget, the skill's accuracy, crit and
// launch-free), adds the riposte buffs' riposte_effect, plays the skill's sound and resolves it (ApplyResult). No
// launch or target rank check: riposte1 has .launch 1234 .target 1234.
// Here, for a hero, the GetRiposteSkill call inside RiposteAttempt is answered with one of the hero's own skills:
// usable now (skills_usable: selected, valid in its mode, allowed by its class config), of a configured .type, single
// target, not a move skill, launchable from its rank and able to target the attacker (ActorCombatSkill::
// GetIsSkillTargetValid: ranks, sides, guard) and battle-valid (Battle::GetIsSkillBattleValid: not disabled by a
// buff, per-turn and per-battle uses left, the performer-HP check, cooldown.c's cooldowns). One: that one; several:
// one at random (the battle's rng). None: NULL, so the game skips the riposte; the bark plays if the riposte would
// otherwise have fired (alive, not stunned, the attack can be riposted, the chance roll passed).
// The chosen skill then stands in for the class riposte skill everywhere the game reads it: GetRiposteSkill returns
// it for that hero (derived stats, Hero::UpdateStanceAnimation's riposte stance anim at skill+0x9c), and its
// ActorCombatSkill+0x3ad flag (Get/SetIsRiposteSkill, set on every vanilla riposte skill) is on while the riposte
// plays, so the game treats it as one: Actor::GetSkillEffectsInternal adds trinkets' riposte additional effects (type
// 5) instead of the attack ones, RaidDisplay::UpdateBattleState skips its target indicators for the hero's current
// skill, and Battle::StartInitiative clears the hero's current skill and reselects (SelectCombatAbility(-1)). The flag
// is set only on a skill the game doesn't already flag, cleared after RiposteAttempt if no riposte with it was queued,
// otherwise at the next turn_start (after StartInitiative) or battle_end; the chosen skill stays the hero's
// GetRiposteSkill until the next battle starts. Skill objects are class data shared by every
// hero of the class at that level, so heroes riposting with the same skill share the flag.
// A skill with a .move of its own moves the hero as on its turn: the riposte path never moves the performer (only
// Actor_DoSkill / Actor_DoAttackSkill / Actor_DoSkillFriendly do), so for each queued riposte with such a skill the
// hook on Battle::PostSkillSideEffects (the attack's timeline step that applies the ripostes' side effects) builds
// Battle::CreatePerformerSkillMoveTimeline(hero, skill), as Actor_DoAttackSkill does for its performer, and queues it
// (RaidDisplay::QueueTimeline), so the move plays after the attack and its ripostes.
// A riposte counts as a use of the skill, as on the hero's turn (only a free action): after RiposteAttempt, each queued
// hero riposte adds one battle-limited use (Actor::IncrementBattleLimitedCombatSkillUses(skill+0x4c) when the skill
// has a .per_battle_limit, skill+0x98, as PostSkillSideEffects does for the attacker) and starts the skill's
// cooldowns (config_cooldown_start, off turn). Per-turn limits (.per_turn_limit) count the battle's history for the
// current round and turn, so a riposte on another actor's turn doesn't take from them. Monsters keep their own riposte skills.
// Windows: GetRiposteSkill is inlined in its three callers (RiposteAttempt, Hero::CalculateDerivedStatsInternal,
// Hero::UpdateStanceAnimation), so a read-write site after each inlined copy swaps the skill register instead.
#include "common.h"
#include <stdio.h>
#include <string.h>

#define RIPOSTE_CALL 0x11c // RiposteAttempt + this: call Actor::GetRiposteSkill
#define MAX_SKILLS   32
#define MAX_HEROES   16
#define MAX_FLAGGED  16

typedef struct {
    uint8_t *begin, *end, *cap;
} Vec;

#ifndef _WIN32
static void *(*o_riposte_skill)(const void *actor);
#endif
static void (*o_attempt)(void *battle, void *attack);
#ifdef _WIN32
static void (*g_valid)(const void *battle, void *result, const void *actor, const void *skill); // MSVC: this first
#else
static void (*g_valid)(void *result, const void *battle, const void *actor, const void *skill);
#endif
static bool (*g_valid_bool)(const void *result);
static bool (*g_target_valid)(const void *skill, const void *performer, const void *target);
static bool (*g_multi)(const void *skill);
static bool (*g_move)(const void *skill);
static bool (*g_riposte_enabled)(const void *actor);
static void (*g_set_riposte_enabled)(void *actor, bool on);
static bool (*g_can_be_riposted)(const void *skill);
static bool (*g_stunned)(const void *actor);
static float (*g_uniform)(void *rng);
static void *(*g_move_timeline)(void *battle, void *actor, const void *skill);
static void (*g_queue_timeline)(void *display, void *timeline, bool);
static void (*o_post_effects)(void *battle, const void *skill);
static void (*g_add_battle_use)(void *actor, unsigned skill_hash);
static void **g_display;
#ifndef _WIN32
static const uint8_t *g_site_ret; // return address of RiposteAttempt's GetRiposteSkill call
#endif

static void *g_battle; // the RiposteAttempt running (else NULL)
static const uint8_t *g_attack;
// the skill each hero last riposted with (its GetRiposteSkill), and the skills whose riposte flag we set
static struct {
    const void *hero;
    uint8_t *skill;
} g_chosen[MAX_HEROES];
static uint8_t *g_flagged[MAX_FLAGGED];
static int g_nflagged;
// queued hero ripostes whose skill moves the hero, moved at the attack's PostSkillSideEffects
static struct {
    void *hero;
    const uint8_t *skill;
} g_moves[MAX_HEROES];
static int g_nmoves;

static bool type_allowed(const uint8_t *skill) {
    const RiposteConfig *r = config_riposte();
    for (int i = 0; i < r->ntypes; i++)
        if (!strcmp((const char *)skill + SKILL_TYPE, r->types[i]))
            return true;
    return false;
}

// NULL: the skill can riposte; else the first check it fails (for the log).
static const char *unusable(const void *hero, const void *attacker, const uint8_t *skill) {
    if (!type_allowed(skill))
        return "type";
    if (g_multi(skill))
        return "multi-target";
    if (g_move(skill))
        return "move skill";
    if (!g_target_valid(skill, hero, attacker))
        return "rank or target";
    uint8_t result[VALID_RESULT_SIZE] = {0};
#ifdef _WIN32
    g_valid(g_battle, result, hero, skill);
#else
    g_valid(result, g_battle, hero, skill);
#endif
    return g_valid_bool(result) ? NULL : "battle-invalid";
}

static void set_chosen(const void *hero, uint8_t *skill) {
    int free_slot = -1;
    for (int i = 0; i < MAX_HEROES; i++) {
        if (g_chosen[i].hero == hero) {
            g_chosen[i].skill = skill;
            return;
        }
        if (free_slot < 0 && !g_chosen[i].hero)
            free_slot = i;
    }
    if (!skill)
        return;
    if (free_slot < 0)
        free_slot = 0; // full (stale heroes): reuse
    g_chosen[free_slot].hero = hero;
    g_chosen[free_slot].skill = skill;
}

static uint8_t *chosen(const void *hero) {
    for (int i = 0; i < MAX_HEROES; i++)
        if (g_chosen[i].hero == hero)
            return g_chosen[i].skill;
    return NULL;
}

static void flag(uint8_t *skill) {
    if (skill[SKILL_IS_RIPOSTE] || g_nflagged >= MAX_FLAGGED)
        return; // vanilla riposte skill, or already ours
    skill[SKILL_IS_RIPOSTE] = 1;
    g_flagged[g_nflagged++] = skill;
}

static void unflag_from(int first) {
    for (int i = first; i < g_nflagged; i++)
        g_flagged[i][SKILL_IS_RIPOSTE] = 0;
    if (first < g_nflagged)
        g_nflagged = first;
}

static bool target_hit(const void *hero) {
    const Vec *t = (const Vec *)(g_attack + ATTACK_TARGETS);
    for (const uint8_t *e = t->begin; e && e + TARGET_SIZE <= t->end; e += TARGET_SIZE)
        if (*(const void *const *)(e + TARGET_ACTOR) == hero)
            return e[TARGET_HIT];
    return false;
}

// No skill qualifies: bark if the game would have ripostes here (its checks after GetRiposteSkill, and the roll).
static void no_skill(void *hero) {
    const char *bark = config_riposte()->no_skill_bark;
    const void *skill = *(const void *const *)(g_attack + ATTACK_SKILL);
    bool dead = (*(bool (*const *const *)(const void *))hero)[ACTOR_IS_DEAD / 8](hero);
    bool stunned = g_stunned(hero), can = skill && g_can_be_riposted(skill), hit = target_hit(hero);
    // The riposte chance stats count only while riposte is enabled (SetRiposteEnabled reruns
    // Actor::UpdateBuffsInternal; the game enables it before reading them), so read them the same way.
    bool was = g_riposte_enabled(hero);
    if (!was)
        g_set_riposte_enabled(hero, true);
    float chance = actor_stat(hero, hit ? "riposte_on_hit_chance" : "riposte_on_miss_chance");
    if (!was)
        g_set_riposte_enabled(hero, false);
    if (dead || stunned || !can || !(chance > 0)) { // the game wouldn't riposte either
        combat_logf("riposte: %p has no skill that can hit back; no riposte anyway (dead %d, stunned %d, attack can "
                    "be riposted %d, hit %d, chance %.2f)",
                    hero, dead, stunned, can, hit, chance);
        return;
    }
    float roll = g_uniform((uint8_t *)g_battle + BATTLE_RNG);
    combat_logf("riposte: %p has no skill that can hit back (chance %.2f, roll %.2f)", hero, chance, roll);
    if (chance >= roll && bark[0])
        bark_queue(hero, bark);
}

static uint8_t *choose(void *hero) {
    const void *attacker = *(const void *const *)(g_attack + ATTACK_PERFORMER);
    void *skills[MAX_SKILLS];
    uint8_t *ok[MAX_SKILLS];
    int n = skills_usable(hero, skills, MAX_SKILLS), nok = 0;
    char why[512] = "";
    for (int i = 0; i < n; i++) {
        const char *r = unusable(hero, attacker, skills[i]);
        if (!r) {
            ok[nok++] = skills[i];
            continue;
        }
        size_t l = strlen(why);
        snprintf(why + l, sizeof why - l, "%s%s: %s", l ? ", " : "", (const char *)skills[i] + SKILL_ID, r);
    }
    if (!nok) {
        combat_logf("riposte: %p none of %d skills can riposte (%s)", hero, n, why);
        set_chosen(hero, NULL);
        no_skill(hero);
        return NULL;
    }
    int pick = nok == 1 ? 0 : (int)(g_uniform((uint8_t *)g_battle + BATTLE_RNG) * (float)nok);
    if (pick >= nok)
        pick = nok - 1;
    uint8_t *skill = ok[pick];
    set_chosen(hero, skill);
    flag(skill);
    combat_logf("riposte: %p could riposte with %s (%d of %d skills usable; the game rolls its chance next)", hero,
                (const char *)skill + SKILL_ID, nok, n);
    return skill;
}

// riposte_on observers (common.h)
#define MAX_RIPOSTE_OBSERVERS 4
static struct {
    void (*scope)(bool);
    void (*skill)(const void *, const void *);
} g_robs[MAX_RIPOSTE_OBSERVERS];
static int g_nrobs;
static ModsList g_robs_mods; // their mods (only an applied mod's observe)
void riposte_on(void (*scope)(bool on), void (*skill)(const void *actor, const void *skill)) {
    if ((scope || skill) && g_nrobs < MAX_RIPOSTE_OBSERVERS && mods_list_add(&g_robs_mods) >= 0) {
        g_robs[g_nrobs].scope = scope;
        g_robs[g_nrobs++].skill = skill;
    }
}
static void riposte_scope(bool on) {
    MODS_EACH(g_robs_mods, false, i) if (g_robs[i].scope) g_robs[i].scope(on);
}
static void riposte_skill_chosen(const void *actor, const void *skill) {
    MODS_EACH(g_robs_mods, false, i) if (g_robs[i].skill) g_robs[i].skill(actor, skill);
}

// RiposteAttempt's GetRiposteSkill call for a hero, when a campaign's mods ask for it: our choice (none: a campaign
// whose mods don't ask)
static bool choosing(const void *actor) {
    return is_hero(actor) && config_riposte()->ntypes;
}
static void *choose_at_site(void *actor) {
    void *skill = choose(actor);
    riposte_skill_chosen(actor, skill); // observers (the expedition log's riposte line)
    return skill;
}

#ifdef _WIN32
// The inlined GetRiposteSkill copies: the hero's chosen skill in place of its class's (in the register given)
static void site_attempt(Regs *r) { // RiposteAttempt: hero rsi, skill r15
    void *actor = (void *)r->rsi;
    if (!g_attack)
        return;
    if (choosing(actor)) {
        r->r15 = (uint64_t)choose_at_site(actor);
        return;
    }
    uint8_t *skill = is_hero(actor) ? chosen(actor) : NULL;
    if (skill)
        r->r15 = (uint64_t)skill;
    riposte_skill_chosen(actor, (const void *)r->r15);
}
static void site_derived(Regs *r) { // CalculateDerivedStatsInternal: hero rbx, skill rsi
    uint8_t *skill = is_hero((void *)r->rbx) ? chosen((void *)r->rbx) : NULL;
    if (skill)
        r->rsi = (uint64_t)skill;
}
static void site_stance(Regs *r) { // UpdateStanceAnimation: hero rbx, skill rdi
    uint8_t *skill = is_hero((void *)r->rbx) ? chosen((void *)r->rbx) : NULL;
    if (skill)
        r->rdi = (uint64_t)skill;
}
#else
static void *h_riposte_skill(const void *actor) {
    bool site = g_attack && __builtin_return_address(0) == g_site_ret;
    if (site && choosing(actor))
        return choose_at_site((void *)actor);
    uint8_t *skill = is_hero(actor) ? chosen(actor) : NULL;
    void *r = skill ? skill : o_riposte_skill(actor);
    if (site)
        riposte_skill_chosen(actor, r);
    return r;
}
#endif

static bool queued(const void *battle, const uint8_t *skill) {
    const Vec *v = (const Vec *)((const uint8_t *)battle + BATTLE_RIPOSTES);
    for (const uint8_t *a = v->begin; a && a + ATTACK_SIZE <= v->end; a += ATTACK_SIZE)
        if (*(const uint8_t *const *)(a + ATTACK_SKILL) == skill)
            return true;
    return false;
}

// The riposte counts as a use of the skill: battle-limited uses and cooldowns, as on the hero's turn.
static void spend_use(void *hero, const uint8_t *skill) {
    if (*(const uint32_t *)(skill + SKILL_BATTLE_LIMIT) && g_add_battle_use)
        g_add_battle_use(hero, *(const uint32_t *)(skill + SKILL_ID_HASH));
    const ClassConfig *c = config_for_hero(hero);
    if (c)
        config_cooldown_start(c, hero, skill_id_hash(skill), false);
}

static void h_attempt(void *battle, void *attack) {
    if (g_attack) {
        o_attempt(battle, attack);
        return;
    } // re-entered: leave it to the outer call
    int first = g_nflagged;
    const Vec *v = (const Vec *)((const uint8_t *)battle + BATTLE_RIPOSTES);
    size_t before = v->begin ? (size_t)(v->end - v->begin) : 0; // only this attempt's ripostes count below
    g_battle = battle;
    g_attack = attack;
    riposte_scope(true);
    o_attempt(battle, attack);
    riposte_scope(false);
    g_battle = NULL;
    g_attack = NULL;
    for (const uint8_t *a = v->begin ? v->begin + before : NULL; a && a + ATTACK_SIZE <= v->end; a += ATTACK_SIZE) {
        void *hero = *(void *const *)(a + ATTACK_PERFORMER);
        const uint8_t *skill = *(const uint8_t *const *)(a + ATTACK_SKILL);
        combat_logf("riposte: %p ripostes with %s", hero, (const char *)skill + SKILL_ID);
        if (!hero || !skill || !is_hero(hero) || chosen(hero) != skill)
            continue;
        spend_use(hero, (const uint8_t *)skill);
        if (!*(const int32_t *)(skill + SKILL_MOVE_BACK) && !*(const int32_t *)(skill + SKILL_MOVE_FORWARD))
            continue;
        bool seen = false;
        for (int i = 0; i < g_nmoves; i++)
            seen |= g_moves[i].hero == hero;
        if (seen || g_nmoves >= MAX_HEROES)
            continue;
        g_moves[g_nmoves].hero = hero;
        g_moves[g_nmoves++].skill = skill;
    }
    // a skill flagged here whose riposte didn't happen (roll failed, stunned...) goes back to normal now
    int w = first;
    for (int i = first; i < g_nflagged; i++) {
        if (queued(battle, g_flagged[i]))
            g_flagged[w++] = g_flagged[i];
        else
            g_flagged[i][SKILL_IS_RIPOSTE] = 0;
    }
    g_nflagged = w;
}

static void h_post_effects(void *battle, const void *skill) {
    o_post_effects(battle, skill);
    int n = g_nmoves;
    g_nmoves = 0;
    for (int i = 0; i < n; i++) {
        void *hero = g_moves[i].hero;
        bool dead = (*(bool (*const *const *)(const void *))hero)[ACTOR_IS_DEAD / 8](hero);
        void *timeline = dead || !*g_display ? NULL : g_move_timeline(battle, hero, g_moves[i].skill);
        combat_logf("riposte: %p moves with %s (back %d, forward %d)%s", hero,
                    (const char *)g_moves[i].skill + SKILL_ID, *(const int32_t *)(g_moves[i].skill + SKILL_MOVE_BACK),
                    *(const int32_t *)(g_moves[i].skill + SKILL_MOVE_FORWARD),
                    timeline ? "" : ": no move (dead or no room)");
        if (timeline)
            g_queue_timeline(*g_display, timeline, false);
    }
}

static void on_turn_or_end(HookContext *ctx, void *ud) {
    (void)ctx;
    (void)ud;
    unflag_from(0);
}
// The chosen skills last until the next battle: a riposte's presentation (stance anim) can outlast battle_end.
static void on_battle_start(HookContext *ctx, void *ud) {
    (void)ctx;
    (void)ud;
    unflag_from(0);
    memset(g_chosen, 0, sizeof g_chosen);
    g_nmoves = 0;
}

void riposte_install(void) {
    if (!config_riposte()->ntypes)
        return;
    void *attempt = sym("_ZN6Battle14RiposteAttemptEPNS_6AttackE");
#ifndef _WIN32
    void *get = sym("_ZNK5Actor15GetRiposteSkillEv");
#endif
    *(void **)&g_valid = sym("_ZNK6Battle21GetIsSkillBattleValidEPK5ActorPK16ActorCombatSkill");
    *(void **)&g_valid_bool = sym("_ZNK6Battle16SkillValidResultcvbEv");
    *(void **)&g_target_valid = sym("_ZNK16ActorCombatSkill21GetIsSkillTargetValidEPK5ActorS2_");
    *(void **)&g_multi = sym("_ZNK16ActorCombatSkill16GetIsMultiTargetEv");
    *(void **)&g_move = sym("_ZNK16ActorCombatSkill18GetIsMoveTypeSkillEv");
    *(void **)&g_riposte_enabled = sym("_ZNK5Actor16IsRiposteEnabledEv");
    *(void **)&g_set_riposte_enabled = sym("_ZN5Actor17SetRiposteEnabledEb");
    *(void **)&g_can_be_riposted = sym("_ZNK16ActorCombatSkill16GetCanBeRipostedEv");
    *(void **)&g_stunned = sym("_ZNK5Actor12GetIsStunnedEv");
    *(void **)&g_uniform = sym("_ZN19prng_minstd_wrapped14random_uniformEv");
    g_display = (void **)sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    void *post = sym("_ZN6Battle20PostSkillSideEffectsEPK16ActorCombatSkill");
    *(void **)&g_move_timeline = sym("_ZN6Battle32CreatePerformerSkillMoveTimelineEP5ActorPK16ActorCombatSkill");
    *(void **)&g_add_battle_use = sym("_ZN5Actor37IncrementBattleLimitedCombatSkillUsesEj");
    if (!g_add_battle_use)
        logf_("riposte: IncrementBattleLimitedCombatSkillUses not found, ripostes don't spend uses");
    *(void **)&g_queue_timeline = sym("_ZN6RaidUI6Screen11RaidDisplay13QueueTimelineEP8Timelineb");
#ifdef _WIN32
    bool site = attempt != NULL; // the GetRiposteSkill copies: their own entries
#else
    g_site_ret = call_site_return(attempt, RIPOSTE_CALL, get);
    bool site = g_site_ret != NULL;
#endif
    if (!site || !g_valid || !g_valid_bool || !g_target_valid || !g_multi || !g_move || !g_can_be_riposted ||
        !g_riposte_enabled || !g_set_riposte_enabled || !g_stunned || !g_uniform || !g_display) {
        logf_("riposte: game functions not found (other game version?), ripostes stay vanilla");
        return;
    }
    static const uint8_t attempt_pro[] = PROLOGUE_SUB32(0x18, 0x01); // sub rsp,0x118
#ifdef _WIN32
    // the sites first (a hooked RiposteAttempt without them would leave ripostes vanilla anyway)
    if (!install_mid_hook_rw("RiposteAttempt riposte skill", sym("win:RiposteAttempt riposte skill"), site_attempt)) {
        logf_("riposte: RiposteAttempt's riposte skill site not hooked, ripostes stay vanilla");
        return;
    }
    if (!install_mid_hook_rw("Hero::CalculateDerivedStatsInternal riposte skill",
                             sym("win:CalculateDerivedStatsInternal riposte skill"), site_derived))
        logf_("riposte: derived stats site not hooked, ripostes use the class riposte skill's accuracy and damage");
    if (!install_mid_hook_rw("Hero::UpdateStanceAnimation riposte skill",
                             sym("win:UpdateStanceAnimation riposte skill"), site_stance))
        logf_("riposte: stance site not hooked, riposting heroes keep the class riposte stance");
#else
    // push rbp; mov rbp,rsp; mov rax,[rdi]; call [rax+0x38] (GetClass); mov rdi,rax; pop rbp (then jmp rel32)
    static const uint8_t get_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x07,
                                      0xFF, 0x50, 0x38, 0x48, 0x89, 0xC7, 0x5D};
    *(void **)&o_riposte_skill =
        install_hook("Actor::GetRiposteSkill", get, (void *)h_riposte_skill, get_pro, sizeof get_pro);
    if (!o_riposte_skill)
        return;
#endif
    *(void **)&o_attempt =
        install_hook("Battle::RiposteAttempt", attempt, (void *)h_attempt, attempt_pro, sizeof attempt_pro);
    if (!o_attempt) {
        logf_("riposte: RiposteAttempt not hooked, ripostes stay vanilla");
        return;
    }
    static const uint8_t post_pro[] = PROLOGUE_SUB32(0x98, 0x02); // sub rsp,0x298
    if (post && g_move_timeline && g_queue_timeline)
        *(void **)&o_post_effects =
            install_hook("Battle::PostSkillSideEffects", post, (void *)h_post_effects, post_pro, sizeof post_pro);
    if (!o_post_effects)
        logf_("riposte: PostSkillSideEffects not hooked, ripostes with moving skills don't move");
    if (!hook_on("turn_start", HOOK_AFTER, on_turn_or_end, NULL) ||
        !hook_on("battle_end", HOOK_AFTER, on_turn_or_end, NULL) ||
        !hook_on("battle_start", HOOK_AFTER, on_battle_start, NULL))
        logf_("riposte: turn and battle listeners not registered, riposte flags clear only after each attempt");
    logf_("riposte: heroes riposte with their own skills");
}
