// Combat event probe. Hook the dispatch specialization itself: Clone() is available on each event but is
// not called by the dispatch path in this game build.
#include "common.h"
#include <stdio.h>
#include <string.h>

static unsigned g_round;

static void actor_label(const void *actor, char *out, size_t n) {
    if (!actor) {
        snprintf(out, n, "(null)");
        return;
    }
    const char *name = (const char *)actor + ACTOR_NAME;
    size_t i = 0;
    while (i + 1 < n && i < 63 && name[i] >= 0x20 && name[i] != 0x7f) {
        out[i] = name[i];
        i++;
    }
    out[i] = 0;
    if (!i)
        snprintf(out, n, "(unnamed)");
}

void actor_skill_name(const void *actor, const void *skill, char *out, size_t n) {
    if (!n)
        return;
    out[0] = 0;
    if (skill && actor) {
        const void *cls = NULL;
        if (is_hero(actor))
            cls = *(const void *const *)((const uint8_t *)actor + HERO_CLASS);
        else if (is_monster(actor)) {
            const void *(*get_class)(const void *) =
                (const void *(*)(const void *))sym("_ZNK7Monster15GetMonsterClassEv");
            if (get_class)
                cls = get_class(actor);
        }
        void (*get_name)(char *, const void *, const void *, bool) =
            (void (*)(char *, const void *, const void *, bool))sym(
                "_ZN12ActorDisplay3App18GetCombatSkillNameEPK10ActorClassPK16ActorCombatSkillb");
        char name[256] = ""; // GetCombatSkillName writes a FixedLengthString's text
        if (cls && get_name)
            get_name(name, cls, skill, false);
        snprintf(out, n, "%s", name);
    }
}

static void skill_label(const void *actor, const void *skill, char *out, size_t n) {
    actor_skill_name(actor, skill, out, n);
    if (!out[0])
        snprintf(out, n, "hash:%08x", skill ? skill_id_hash(skill) : 0);
}

static void log_attack_details(const void *event) {
    const uint8_t *ev = event;
    const uint8_t *attack = *(const uint8_t *const *)(ev + 0x10);
    if (!attack) {
        combat_logf("BattleAttackEvent detail attack=(null)");
        return;
    }

    const void *attacker = *(const void *const *)(attack + 0x20);
    const void *skill = *(const void *const *)(attack + 0x28);
    char attacker_name[64];
    actor_label(attacker, attacker_name, sizeof attacker_name);
    unsigned guid = 0;
    const char *kind = "actor";
    if (is_hero(attacker)) {
        guid = hero_guid(attacker);
        kind = "hero";
    } else if (is_monster(attacker)) {
        guid = monster_guid(attacker);
        kind = "monster";
    }

    unsigned skill_hash = skill ? skill_id_hash(skill) : 0;
    char skill_name[128];
    skill_label(attacker, skill, skill_name, sizeof skill_name);

    const uint8_t *begin = *(const uint8_t *const *)(attack + 0x08);
    const uint8_t *end = *(const uint8_t *const *)(attack + 0x10);
    size_t count = begin && end >= begin ? (size_t)(end - begin) / 0x20 : 0;
    combat_logf("BattleAttackEvent detail attacker=%s name=\"%s\" guid=%u ptr=%p skill=\"%s\" skill_hash=%08x "
                "targets=%u hit_any=%u does_damage=%u damage_type=%d",
                kind, attacker_name, guid, attacker, skill_name, skill_hash, (unsigned)count, attack[0x4c],
                attack[0x4d], *(const int *)(attack + 0x50));
    for (size_t i = 0; i < count; i++) {
        const uint8_t *td = begin + i * 0x20;
        const void *target = *(const void *const *)td;
        char target_name[64];
        actor_label(target, target_name, sizeof target_name);
        const char *target_kind = is_hero(target) ? "hero" : is_monster(target) ? "monster" : "actor";
        unsigned target_guid = is_hero(target) ? hero_guid(target) : is_monster(target) ? monster_guid(target) : 0;
        unsigned hit = td[0x10], crit = td[0x11];
        float damage = *(const float *)(td + 0x08);
        combat_logf("  target[%u]=%s name=\"%s\" guid=%u ptr=%p hit=%u crit=%u damage=%.3f value_0c=%.3f value_14=%.3f "
                    "corpse=%u",
                    (unsigned)i, target_kind, target_name, target_guid, target, hit, crit, damage,
                    *(const float *)(td + 0x0c), *(const float *)(td + 0x14), td[0x18]);
    }
}

void combat_log_skill_use(const void *actor, const void *skill, const void *targets, bool flag) {
    char actor_name[64], skill_name[128];
    actor_label(actor, actor_name, sizeof actor_name);
    const char *kind = is_hero(actor) ? "hero" : is_monster(actor) ? "monster" : "actor";
    unsigned guid = is_hero(actor) ? hero_guid(actor) : is_monster(actor) ? monster_guid(actor) : 0;
    unsigned hash = skill ? skill_id_hash(skill) : 0;
    skill_label(actor, skill, skill_name, sizeof skill_name);

    const uint8_t *vec = targets;
    const void *begin = vec ? *(const void *const *)vec : NULL;
    const void *end = vec ? *(const void *const *)(vec + 8) : NULL;
    size_t count = begin && end >= begin ? (size_t)((const uint8_t *)end - (const uint8_t *)begin) / sizeof(void *) : 0;
    combat_logf("Battle::Actor_DoSkill actor=%s name=\"%s\" guid=%u ptr=%p skill=\"%s\" skill_hash=%08x "
                "target_count=%u flag=%u",
                kind, actor_name, guid, actor, skill_name, hash, (unsigned)count, flag);
    for (size_t i = 0; i < count; i++) {
        const void *target = ((const void *const *)begin)[i];
        char name[64];
        actor_label(target, name, sizeof name);
        const char *target_kind = is_hero(target) ? "hero" : is_monster(target) ? "monster" : "actor";
        unsigned target_guid = is_hero(target) ? hero_guid(target) : is_monster(target) ? monster_guid(target) : 0;
        combat_logf("  skill_target[%u]=%s name=\"%s\" guid=%u ptr=%p", (unsigned)i, target_kind, name, target_guid,
                    target);
    }
}

typedef void (*dispatch_fn)(void *list, void *event);
typedef struct {
    const char *name;
    const char *dispatch_symbol;
    const char *clone_symbol;
    dispatch_fn original;
    unsigned bytes;
} Probe;

static Probe g_probes[] = {
    {"BattleAttackEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI17BattleAttackEventEEvPT_",
     "_ZNK17BattleAttackEvent5CloneEv", NULL, 0},
    {"MonsterDeathEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI17MonsterDeathEventEEvPT_",
     "_ZNK17MonsterDeathEvent5CloneEv", NULL, 0},
    {"HeroDeathEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI14HeroDeathEventEEvPT_",
     "_ZNK14HeroDeathEvent5CloneEv", NULL, 0},
    {"DeathDamageEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI16DeathDamageEventEEvPT_",
     "_ZNK16DeathDamageEvent5CloneEv", NULL, 0},
    {"HeroReachedDeathsDoorEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI26HeroReachedDeathsDoorEventE"
     "EvPT_",
     "_ZNK26HeroReachedDeathsDoorEvent5CloneEv", NULL, 0},
    {"HeroSurvivedDeathsDoorAttackEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI33HeroSurvivedDeathsDoorAttac"
     "kEventEEvPT_",
     "_ZNK33HeroSurvivedDeathsDoorAttackEvent5CloneEv", NULL, 0},
    {"ActorDazedEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI15ActorDazedEventEEvPT_",
     "_ZNK15ActorDazedEvent5CloneEv", NULL, 0},
    {"TorchModifiedEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI18TorchModifiedEventEEvPT_",
     "_ZNK18TorchModifiedEvent5CloneEv", NULL, 0},
    {"HeartAttackEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI16HeartAttackEventEEvPT_",
     "_ZNK16HeartAttackEvent5CloneEv", NULL, 0},
    {"BattleCreatedEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI18BattleCreatedEventEEvPT_",
     "_ZNK18BattleCreatedEvent5CloneEv", NULL, 0},
    {"BattleDestroyedEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI20BattleDestroyedEventEEvPT_",
     "_ZNK20BattleDestroyedEvent5CloneEv", NULL, 0},
    {"MonsterBattleStageEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI23MonsterBattleStageEventEEvP"
     "T_",
     "_ZNK23MonsterBattleStageEvent5CloneEv", NULL, 0},
    {"HeroActOutStartEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI20HeroActOutStartEventEEvPT_",
     "_ZNK20HeroActOutStartEvent5CloneEv", NULL, 0},
    {"HeroActOutFinishEvent",
     "_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE16ListenerListData8DispatchI21HeroActOutFinishEventEEvPT"
     "_",
     "_ZNK21HeroActOutFinishEvent5CloneEv", NULL, 0},
};

static void dispatch(Probe *p, void *list, void *event) {
    combat_log_event(p->name, event, p->bytes);
    if (!strcmp(p->name, "BattleCreatedEvent"))
        g_round = 0;
    if (!strcmp(p->name, "BattleAttackEvent"))
        log_attack_details(event);
    if (!strcmp(p->name, "BattleDestroyedEvent") || !strcmp(p->name, "HeroActOutStartEvent") ||
        !strcmp(p->name, "HeroActOutFinishEvent")) {
        combat_logf("%s detail timestamp=%llu", p->name,
                    (unsigned long long)*(const uint64_t *)((const uint8_t *)event + 0x08));
    }
    if (!strcmp(p->name, "MonsterBattleStageEvent")) {
        const uint8_t *e = event;
        unsigned type_id = *(const unsigned *)(e + 0x10);
        unsigned (*stage_type)(unsigned) = (unsigned (*)(unsigned))sym("_ZN5Stage17GetTypeFromTypeIdEj");
        combat_logf("MonsterBattleStageEvent detail timestamp=%llu type_id=%u stage_type=%u",
                    (unsigned long long)*(const uint64_t *)(e + 0x08), type_id,
                    stage_type ? stage_type(type_id) : 0xffffffffu);
    }
    p->original(list, event);
}

#define PROBE_FN(n)                                                                                                    \
    static void dispatch_##n(void *l, void *e) {                                                                       \
        dispatch(&g_probes[n], l, e);                                                                                  \
    }
PROBE_FN(0)
PROBE_FN(1)
PROBE_FN(2)
PROBE_FN(3)
PROBE_FN(4)
PROBE_FN(5)
PROBE_FN(6)
PROBE_FN(7)
PROBE_FN(8)
PROBE_FN(9)
PROBE_FN(10)
PROBE_FN(11)
PROBE_FN(12)
PROBE_FN(13)
static dispatch_fn g_detours[] = {dispatch_0,  dispatch_1,  dispatch_2,  dispatch_3, dispatch_4,
                                  dispatch_5,  dispatch_6,  dispatch_7,  dispatch_8, dispatch_9,
                                  dispatch_10, dispatch_11, dispatch_12, dispatch_13};

static void (*g_add_monster_original)(void *battle, const void *monster_class, bool b1, bool b2, unsigned value,
                                      bool b3, bool b4, bool b5);
static void add_monster_hook(void *battle, const void *monster_class, bool b1, bool b2, unsigned value, bool b3,
                             bool b4, bool b5) {
    unsigned class_hash = monster_class ? *(const unsigned *)((const uint8_t *)monster_class + 0xd0) : 0;
    char class_id[64] = "";
    if (monster_class) {
        const char *id =
            (const char *)monster_class + 0x48; // ActorClass id string; GetCombatSkillName formats from here.
        size_t i = 0;
        while (i + 1 < sizeof class_id && i < 63 && id[i] >= 0x20 && id[i] != 0x7f) {
            class_id[i] = id[i];
            i++;
        }
        class_id[i] = 0;
    }
    combat_logf("Battle::AddMonsterToBattle class_id=\"%s\" class_hash=%08x class=%p args=%u,%u,%u,%u,%u value=%u",
                class_id, class_hash, monster_class, b1, b2, b3, b4, b5, value);
    g_add_monster_original(battle, monster_class, b1, b2, value, b3, b4, b5);
}

static void (*g_start_round_original)(void *battle);
static void start_round_hook(void *battle) {
    combat_log_actor_event("round_start", battle, "battle", 0);
    HookContext c = {.battle = battle, .kind = (int)++g_round};
    hook_emit("round_start", HOOK_AFTER, &c);
    g_start_round_original(battle);
}

static void (*g_set_ranks_original)(void *actor, unsigned rank);
static void set_ranks_hook(void *actor, unsigned rank) {
    if (!actor) {
        g_set_ranks_original(actor, rank);
        return;
    }
    unsigned old_rank = *(const unsigned *)((const uint8_t *)actor + ACTOR_RANK);
    const char *kind = is_hero(actor) ? "hero" : is_monster(actor) ? "monster" : "actor";
    unsigned guid = is_hero(actor) ? hero_guid(actor) : is_monster(actor) ? monster_guid(actor) : 0;
    char name[64];
    actor_label(actor, name, sizeof name);
    g_set_ranks_original(actor, rank);
    if (old_rank != rank)
        combat_logf("Actor::SetRanks actor=%s name=\"%s\" guid=%u ptr=%p rank=%u->%u", kind, name, guid, actor,
                    old_rank, *(const unsigned *)((const uint8_t *)actor + ACTOR_RANK));
}

static void (*g_actor_move_skill_original)(void *battle, void *actor, int rank);
static void actor_move_skill_hook(void *battle, void *actor, int rank) {
    unsigned old_rank = actor ? *(const unsigned *)((const uint8_t *)actor + ACTOR_RANK) : 0;
    const void *skill = skill_resolution_performer() == actor ? skill_resolution_skill() : NULL;
    char actor_name[64], skill_name[128];
    actor_label(actor, actor_name, sizeof actor_name);
    skill_label(actor, skill, skill_name, sizeof skill_name);
    const char *kind = is_hero(actor) ? "hero" : is_monster(actor) ? "monster" : "actor";
    unsigned guid = is_hero(actor) ? hero_guid(actor) : is_monster(actor) ? monster_guid(actor) : 0;
    combat_logf("Battle::Actor_MoveSkill actor=%s name=\"%s\" guid=%u skill=\"%s\" skill_hash=%08x rank_before=%u "
                "requested_rank=%d",
                kind, actor_name, guid, skill_name, skill ? skill_id_hash(skill) : 0, old_rank, rank);
    g_actor_move_skill_original(battle, actor, rank);
    unsigned new_rank = actor ? *(const unsigned *)((const uint8_t *)actor + ACTOR_RANK) : old_rank;
    if (new_rank != old_rank)
        combat_logf("Battle::Actor_MoveSkill result actor=%s name=\"%s\" guid=%u rank=%u->%u", kind, actor_name, guid,
                    old_rank, new_rank);
}

void combat_events_install(void) {
#ifdef _WIN32
    // Dispatch<T> is inlined into each sender on Windows: no function per event type to observe.
    logf_("combat events: event dispatch probes are macOS only");
#else
    // These Dispatcher specializations share this 17-byte prologue in the installed macOS binary.
    // The copied instructions have no relative operands, so the ordinary inline-hook trampoline is safe.
    static const uint8_t prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                       0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x18};
    for (unsigned i = 0; i < sizeof g_probes / sizeof g_probes[0]; i++) {
        Probe *p = &g_probes[i];
        uint8_t *clone = sym(p->clone_symbol);
        if (clone && clone[0] == 0x55 && clone[7] == 0xbf && clone[12] == 0xe8) {
            p->bytes = (unsigned)clone[8] | ((unsigned)clone[9] << 8) | ((unsigned)clone[10] << 16) |
                       ((unsigned)clone[11] << 24);
        }
        if (!p->bytes) {
            logf_("combat events: %s payload size unavailable", p->name);
            continue;
        }
        p->original = (dispatch_fn)install_hook(p->name, sym(p->dispatch_symbol), (void *)g_detours[i], prologue,
                                                sizeof prologue);
        if (p->original)
            logf_("combat events: observing dispatcher for %s (%u bytes)", p->name, p->bytes);
    }
#endif
    static const uint8_t round_prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41,
                                             0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
    g_start_round_original =
        (void (*)(void *))install_hook("Battle::StartRound (combat log)", sym("_ZN6Battle10StartRoundEv"),
                                       (void *)start_round_hook, round_prologue, sizeof round_prologue);
    static const uint8_t ranks_prologue[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50,
                                             0x89, 0xf3, 0x44, 0x8b, 0xb7, 0x18, 0x10, 0x00, 0x00};
    g_set_ranks_original =
        (void (*)(void *, unsigned))install_hook("Actor::SetRanks (combat log)", sym("_ZN5Actor8SetRanksEj"),
                                                 (void *)set_ranks_hook, ranks_prologue, sizeof ranks_prologue);
    static const uint8_t move_skill_prologue[] = PROLOGUE_SUB32(0xd8, 0x00);
    g_actor_move_skill_original = (void (*)(void *, void *, int))install_hook(
        "Battle::Actor_MoveSkill (combat log)", sym("_ZN6Battle15Actor_MoveSkillEP5Actori"),
        (void *)actor_move_skill_hook, move_skill_prologue, sizeof move_skill_prologue);
    static const uint8_t monster_prologue[] = PROLOGUE_SUB32(0x88, 0x00);
    g_add_monster_original = (void (*)(void *, const void *, bool, bool, unsigned, bool, bool, bool))install_hook(
        "Battle::AddMonsterToBattle (combat log)", sym("_ZN6Battle18AddMonsterToBattleEP12MonsterClassbbjbbb"),
        (void *)add_monster_hook, monster_prologue, sizeof monster_prologue);
}
