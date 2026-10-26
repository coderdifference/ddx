// Curio, obstacle, door and trap hook points (hooks.c).
// Windows: PropInteraction::GenerateResult is inlined into PropInteractionTable::Evaluate, so curio_result hooks
// CreatePropInteractionResult (only called there) and fires for a made result only; ShowTrap and
// Hero::GetTrapDodgeChance are inlined into Overlay_Event::ShowScreen, which is hooked in ShowTrap's place, and the
// chance is read and changed through the library's wrapper on Actor::GetCurrentResistance (the trap resistance asked
// while the trap resolves): the chance is rebuilt as the game does (resistance, the disarm bonus, less the quest
// difficulty's base); a client's own wrapper on that resistance applies but isn't in the chance reported.
// Design: design/docs/13-expedition-hooks.md; offsets: docs/engine-design.md "Curio, trap, loot and scouting points".
//   curio_result  PropInteraction::GenerateResult(Prop*, Hero*) const: the game picks an interaction's outcome (a curio
//                 investigated or given an item, an obstacle, a door, a prisoner) and makes its result object
//   curio_apply   each *InteractionResult::Apply() const: the outcome happens (one hook per result class; Nothing's
//                 Apply is an empty 6-byte function, too short to patch, so "nothing" has only curio_result)
//                 prop_applying() names the result whose Apply runs; quirk_notices.c needs only the purge's hook
//   trap          Overlay_Event::ShowTrap resolves a trap: Hero::GetTrapDodgeChance (before: the chance, may be
//                 changed), then TriggerDisarmedTrap or TriggerTrap(..., avoided) (after: result 1 disarmed or
//                 avoided, 0 sprung)
//   scout         (with expedition_hooks.c's) a scouting curio (a map): CurioInteractionScoutingResult::Apply reveals
//                 areas itself (Map::NagivateMap from the current area, its lambdas setting each area's knowledge),
//                 not through ScoutArea, so its curio_apply hook also fires scout: area the current one, source
//                 "curio", kind the depth (the result data's digit at +4, times 2; 0: the whole map). Before: cancel
//                 skips it.
// The result object: vtable, PropInteractionResultData* at +0x08 (the result type, an int, first), Hero* at +0x10, and
// for the obstacle, door and prisoner results the Prop* at +0x18. The type indexes k_result_ids (the curio CSV's
// result types and the game's own ids where it has them; 5, teleport, has no result class).
#include "common.h"
#include <stdio.h>
#include <string.h>

#define NRESULTS 16
static const char *const k_result_ids[NRESULTS] = {
    "nothing",      "loot",        "quirk",         "effect",          "scouting",         "teleport",
    "purge",        "disease",     "quest_item",    "summon",          "obstacle_default", "clear_obstacle",
    "default_door", "unlock_door", "prisoner_talk", "prisoner_display"};

static void **g_raid; // &Raid::s_Instance
static void *raid(void) {
    return g_raid ? *g_raid : NULL;
}
static void *(*g_raid_area)(const void *raid); // Raid::GetArea() const

static int result_type(const void *result) {
    const int *data = result ? *(const int *const *)((const uint8_t *)result + RESULT_DATA) : NULL;
    return data && *data >= 0 && *data < NRESULTS ? *data : -1;
}

// ---------------------------------------------------------------- curio_result
// The props of the last few results made, for the result classes that don't keep theirs (curio_apply's prop).
#define RECENT 8
static struct {
    const void *result;
    void *prop;
} g_recent[RECENT];
static int g_next_recent;

static void note_result(void *result, void *prop, void *hero) {
    g_recent[g_next_recent] = (typeof(g_recent[0])){result, prop};
    g_next_recent = (g_next_recent + 1) % RECENT;
    int type = result_type(result);
    HookContext c = {
        .raid = raid(), .actor = hero, .prop = prop, .kind = type, .source = type >= 0 ? k_result_ids[type] : NULL};
    hook_emit("curio_result", HOOK_AFTER, &c);
}

#ifdef _WIN32
static void *(*g_create_original)(const void *data, void *prop, void *hero);
static void *create_hook(const void *data, void *prop, void *hero) { // CreatePropInteractionResult
    void *result = g_create_original(data, prop, hero);
    if (result)
        note_result(result, prop, hero); // Evaluate tries the first result again when one makes none
    return result;
}
#else
static void *(*g_generate_original)(const void *interaction, void *prop, void *hero);
static void *generate_hook(const void *interaction, void *prop, void *hero) {
    void *result = g_generate_original(interaction, prop, hero);
    note_result(result, prop, hero);
    return result;
}
#endif

// ---------------------------------------------------------------- curio_apply
// Results whose Apply can be skipped (before: cancel) without leaving the game waiting on it: the curio outcomes that
// only change heroes, the party or its loot. Obstacles, doors, prisoners, quest items and summons always apply.
static bool cancellable(int type) {
    return type == 1 || type == 2 || type == 3 || type == 4 || type == 6 || type == 7;
}

static void (*g_apply_original[NRESULTS])(const void *result);
static int g_applying = -1; // the result type whose original Apply runs (prop_applying)

int prop_applying(void) {
    return g_applying;
}

static void apply(int hooked, const void *result) {
    int type = result_type(result);
    void *prop = NULL;
    if (type >= 10)
        prop = *(void *const *)((const uint8_t *)result + RESULT_PROP);
    else
        for (int i = 0; i < RECENT; i++)
            if (g_recent[i].result == result)
                prop = g_recent[i].prop;
    HookContext c = {.raid = raid(),
                     .actor = *(void *const *)((const uint8_t *)result + RESULT_HERO),
                     .prop = prop,
                     .kind = type,
                     .source = type >= 0 ? k_result_ids[type] : NULL};
    if (!hook_emit("curio_apply", HOOK_BEFORE, &c) && cancellable(type))
        return;
    HookContext scout = {.raid = c.raid, .actor = c.actor, .prop = prop, .kind = -1, .source = "curio"};
    if (type == 4) {
        const uint8_t *data = *(const uint8_t *const *)((const uint8_t *)result + RESULT_DATA);
        if (data)
            scout.kind = 2 * (data[SCOUTING_DEPTH] - '0');
        scout.area = c.raid && g_raid_area ? g_raid_area(c.raid) : NULL;
        if (!hook_emit("scout", HOOK_BEFORE, &scout))
            return;
    }
    int outer = g_applying;
    g_applying = type;
    g_apply_original[hooked](result); // the class this hook is on (the type should match)
    g_applying = outer;
    if (type == 4)
        hook_emit("scout", HOOK_AFTER, &scout);
    hook_emit("curio_apply", HOOK_AFTER, &c);
}

#define APPLY_HOOK(n)                                                                                                  \
    static void apply_##n(const void *result) {                                                                        \
        apply(n, result);                                                                                              \
    }
APPLY_HOOK(1)
APPLY_HOOK(2)
APPLY_HOOK(3)
APPLY_HOOK(4)
APPLY_HOOK(6)
APPLY_HOOK(7)
APPLY_HOOK(8)
APPLY_HOOK(9)
APPLY_HOOK(10)
APPLY_HOOK(11)
APPLY_HOOK(12)
APPLY_HOOK(13)
APPLY_HOOK(14)
APPLY_HOOK(15)

// The Apply functions: their first whole instructions (from the disassembly) and relative operands.
static const struct {
    int type;
    void (*hook)(const void *);
    const char *name, *mangled;
    uint8_t len, bytes[20];
    HookRip rip; // disp 0: none
} k_apply[] = {
    {1,
     apply_1,
     "CurioInteractionLootResult::Apply",
     "_ZNK26CurioInteractionLootResult5ApplyEv",
     17,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x68},
     {0, 0}},
    {2,
     apply_2,
     "CurioInteractionQuirkResult::Apply",
     "_ZNK27CurioInteractionQuirkResult5ApplyEv",
     16,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x81, 0xec, 0x38, 0x01, 0x00, 0x00},
     {0, 0}},
    // push rbp; mov rbp,rsp; push rbx; push rax; lea rax,[rip+disp32] (Raid::s_Instance); mov rbx,[rax]
    {3,
     apply_3,
     "CurioInteractionEffectResult::Apply",
     "_ZNK28CurioInteractionEffectResult5ApplyEv",
     16,
     {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x8d, 0x05, 0x0b, 0xfd, 0xb5, 0x02, 0x48, 0x8b, 0x18},
     {9, 13}},
    {4,
     apply_4,
     "CurioInteractionScoutingResult::Apply",
     "_ZNK30CurioInteractionScoutingResult5ApplyEv",
     20,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
      0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xe8, 0x00, 0x00, 0x00},
     {0, 0}},
    // cmp byte [rdi+0x18],0; je rel8; push rbp; mov rbp,rsp; sub rsp,0x40
    {6,
     apply_6,
     "CurioInteractionPurgeResult::Apply",
     "_ZNK27CurioInteractionPurgeResult5ApplyEv",
     14,
     {0x80, 0x7f, 0x18, 0x00, 0x74, 0x44, 0x55, 0x48, 0x89, 0xe5, 0x48, 0x83, 0xec, 0x40},
     {5, 6}},
    {7,
     apply_7,
     "CurioInteractionDiseaseResult::Apply",
     "_ZNK29CurioInteractionDiseaseResult5ApplyEv",
     20,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
      0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x38, 0x01, 0x00, 0x00},
     {0, 0}},
    {8,
     apply_8,
     "CurioInteractionQuestItemResult::Apply",
     "_ZNK31CurioInteractionQuestItemResult5ApplyEv",
     20,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
      0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x08, 0x06, 0x00, 0x00},
     {0, 0}},
    {9,
     apply_9,
     "CurioInteractionSummonResult::Apply",
     "_ZNK28CurioInteractionSummonResult5ApplyEv",
     17,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x58},
     {0, 0}},
    {10,
     apply_10,
     "ObstacleDefaultInteractionResult::Apply",
     "_ZNK32ObstacleDefaultInteractionResult5ApplyEv",
     20,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
      0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x68, 0x01, 0x00, 0x00},
     {0, 0}},
    {11,
     apply_11,
     "ClearObstacleInteractionResult::Apply",
     "_ZNK30ClearObstacleInteractionResult5ApplyEv",
     14,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x81, 0xec, 0x70, 0x02, 0x00, 0x00},
     {0, 0}},
    // push rbp; mov rbp,rsp; push rbx; push rax; mov rdi,[rdi+0x18]; test rdi,rdi; je rel8
    {12,
     apply_12,
     "DefaultDoorInteractionResult::Apply",
     "_ZNK28DefaultDoorInteractionResult5ApplyEv",
     15,
     {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x8b, 0x7f, 0x18, 0x48, 0x85, 0xff, 0x74, 0x35},
     {14, 15}},
    {13,
     apply_13,
     "UnlockDoorInteractionResult::Apply",
     "_ZNK27UnlockDoorInteractionResult5ApplyEv",
     14,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x81, 0xec, 0xc0, 0x00, 0x00, 0x00},
     {0, 0}},
    {14,
     apply_14,
     "PrisonerTalkInteractionResult::Apply",
     "_ZNK29PrisonerTalkInteractionResult5ApplyEv",
     16,
     {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x81, 0xec, 0xc8, 0x00, 0x00, 0x00},
     {0, 0}},
    {15,
     apply_15,
     "PrisonerDisplayInteractionResult::Apply",
     "_ZNK32PrisonerDisplayInteractionResult5ApplyEv",
     15,
     {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x8b, 0x7f, 0x18, 0x48, 0x85, 0xff, 0x74, 0x3d},
     {14, 15}},
};

// ---------------------------------------------------------------- trap
// ShowTrap runs every frame while the trap overlay is up and resolves the trap once: g_trap is that resolution.
static struct {
    bool active, rolled, done;
    const void *overlay;
    HookContext c;
} g_trap;

static float trap_chance(const void *hero, bool disarming, float chance) { // the roll's chance: trap before
    g_trap.rolled = true;
    g_trap.c = (HookContext){
        .raid = raid(), .actor = (void *)hero, .amount = chance, .kind = -1, .source = disarming ? "disarm" : "walk"};
    hook_emit("trap", HOOK_BEFORE, &g_trap.c);
    return g_trap.c.amount;
}

#ifdef _WIN32
#define TRAP_RESISTANCE 7 // ResistanceTypes: trap
static float (*g_resistance_next)(const void *actor, int type);
static const uint8_t *g_rules; // _Rules

static float trap_base(void) { // RuleBook::GetDifficultyTrapBase(the quest's difficulty)
    const uint8_t *r = raid();
    const uint8_t *quest = r ? *(const uint8_t *const *)(r + RAID_QUEST) : NULL;
    if (!quest || !g_rules)
        return 0.0f;
    const float *b = *(const float *const *)(g_rules + RULES_TRAP_BASE);
    const float *e = *(const float *const *)(g_rules + RULES_TRAP_BASE + 8);
    unsigned d = *(const unsigned *)(quest + QUEST_DIFFICULTY);
    return b && d < (size_t)(e - b) ? b[d] : 0.0f;
}

static float resistance_hook(const void *actor, int type) { // Actor::GetCurrentResistance (catalogue wrapper)
    float value = g_resistance_next(actor, type);
    if (type != TRAP_RESISTANCE || !g_trap.active || g_trap.rolled || !g_trap.overlay)
        return value;
    bool disarming = *((const uint8_t *)g_trap.overlay + OVERLAY_TRAP_DISARMING) != 0;
    float chance =
        value - trap_base() + (disarming && g_rules ? *(const float *)(g_rules + RULES_TRAP_DISARM_BONUS) : 0.0f);
    return value + (trap_chance(actor, disarming, chance) - chance);
}
#else
static float (*g_dodge_original)(const void *hero, bool disarming, bool b);
static float dodge_hook(const void *hero, bool disarming, bool b) {
    float chance = g_dodge_original(hero, disarming, b);
    if (!g_trap.active || g_trap.rolled)
        return chance; // tooltips and the resistance display
    return trap_chance(hero, disarming, chance);
}
#endif

static void (*g_trigger_original)(void *overlay, void *display, int hero, bool avoided);
static void trigger_hook(void *overlay, void *display, int hero, bool avoided) {
    if (g_trap.active) {
        g_trap.done = true;
        g_trap.c.result = avoided;
        g_trap.c.type = avoided ? "avoided" : "sprung";
    }
    g_trigger_original(overlay, display, hero, avoided);
}

static void (*g_disarmed_original)(void *overlay, void *display, int hero);
static void disarmed_hook(void *overlay, void *display, int hero) {
    if (g_trap.active) {
        g_trap.done = true;
        g_trap.c.result = 1;
        g_trap.c.type = "disarmed";
    }
    g_disarmed_original(overlay, display, hero);
}

static void (*g_show_trap_original)(void *overlay, void *display);
static void show_trap_hook(void *overlay, void *display) {
    g_trap.active = true;
    g_trap.overlay = overlay;
    g_trap.rolled = g_trap.done = false;
    g_show_trap_original(overlay, display);
    g_trap.active = false;
    if (g_trap.rolled && g_trap.done)
        hook_emit("trap", HOOK_AFTER, &g_trap.c);
}

const char *prop_id(const void *prop) {
    return prop ? (const char *)prop + PROP_NAME : NULL;
}

const char *prop_title(const void *prop) {
    const char *id = prop_id(prop);
    if (!id || !*id)
        return id;
    const uint8_t *type = *(const uint8_t *const *)((const uint8_t *)prop + PROP_TYPE);
    const char *ui = type ? (const char *)type + PROP_TYPE_UI : id;
    char key[160];
    snprintf(key, sizeof key, "str_curio_title_%s", *ui ? ui : id);
    if (loc_exists(key))
        return ui_loc(key);
    snprintf(key, sizeof key, "str_obstacle_%s_title", id);
    if (loc_exists(key))
        return ui_loc(key);
    // no name in the game's strings (hallway curios such as crate and sack, doors): the id, readable ("Cove Door")
    static char names[4][64];
    static int next;
    char *out = names[next++ % 4];
    snprintf(out, sizeof names[0], "%s", id);
    for (char *c = out; *c; c++) {
        if (*c == '_')
            *c = ' ';
        if ((c == out || c[-1] == ' ') && *c >= 'a' && *c <= 'z')
            *c -= 32;
    }
    return out;
}

void prop_hooks_install(void) {
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    if (hook_wanted("curio_result") || hook_wanted("curio_apply")) { // curio_apply uses the props noted here
#ifdef _WIN32
        *(void **)&g_create_original =
            install_hook("CreatePropInteractionResult (curio_result)",
                         sym("_Z27CreatePropInteractionResultRK25PropInteractionResultDataP4PropP4Hero"),
                         (void *)create_hook, NULL, 0);
#else
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                    0x54, 0x53, 0x48, 0x89, 0xd3, 0x49, 0x89, 0xf6}; // ... mov rbx,rdx; mov r14,rsi
        *(void **)&g_generate_original =
            install_hook("PropInteraction::GenerateResult (curio_result)",
                         sym("_ZNK15PropInteraction14GenerateResultEP4PropP4Hero"), (void *)generate_hook, p, sizeof p);
#endif
    }
    g_raid_area = (void *(*)(const void *))sym("_ZNK4Raid7GetAreaEv");
    bool all = hook_wanted("curio_apply") || hook_wanted("scout"); // scout: a scouting curio
    if (all || quirk_notices_on())                                 // quirk_notices.c: only the purge's
        for (size_t i = 0; i < sizeof k_apply / sizeof *k_apply; i++) {
            if (!all && k_apply[i].type != 6)
                continue;
            char name[96];
            snprintf(name, sizeof name, "%s (curio_apply)", k_apply[i].name);
            *(void **)&g_apply_original[k_apply[i].type] =
                install_hook_rip(name, sym(k_apply[i].mangled), (void *)k_apply[i].hook, k_apply[i].bytes,
                                 k_apply[i].len, &k_apply[i].rip, k_apply[i].rip.disp ? 1 : 0);
        }
    if (hook_wanted("trap")) {
        static const uint8_t trigger[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                          0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x38, 0x03, 0x00, 0x00};
        static const uint8_t disarmed[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
                                           0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x28, 0x02, 0x00, 0x00};
        static const uint8_t show[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
                                       0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x10};
#ifdef _WIN32
        g_rules = sym("Rules");
        bool chance = g_rules && game_hook_wrap("Actor::GetCurrentResistance", (void *)resistance_hook,
                                                (void **)&g_resistance_next);
        const char *show_name = "Overlay_Event::ShowScreen (trap; ShowTrap inlined)",
                   *show_sym = "win:Overlay_Event::ShowScreen";
#else
        static const uint8_t dodge[] = {
            0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48,
            0x83, 0xec, 0x10, 0x89, 0xd3, 0x41, 0x89, 0xf6}; // ... sub rsp,0x10; mov ebx,edx; mov r14d,esi
        *(void **)&g_dodge_original =
            install_hook("Hero::GetTrapDodgeChance (trap)", sym("_ZNK4Hero18GetTrapDodgeChanceEbb"), (void *)dodge_hook,
                         dodge, sizeof dodge);
        bool chance = g_dodge_original != NULL;
        const char *show_name = "Overlay_Event::ShowTrap (trap)",
                   *show_sym = "_ZN13Overlay_Event8ShowTrapERN6RaidUI6Screen11RaidDisplayE";
#endif
        *(void **)&g_trigger_original =
            install_hook("Overlay_Event::TriggerTrap (trap)",
                         sym("_ZN13Overlay_Event11TriggerTrapERN6RaidUI6Screen11RaidDisplayEib"), (void *)trigger_hook,
                         trigger, sizeof trigger);
        *(void **)&g_disarmed_original =
            install_hook("Overlay_Event::TriggerDisarmedTrap (trap)",
                         sym("_ZN13Overlay_Event19TriggerDisarmedTrapERN6RaidUI6Screen11RaidDisplayEi"),
                         (void *)disarmed_hook, disarmed, sizeof disarmed);
        if (chance && g_trigger_original && g_disarmed_original)
            *(void **)&g_show_trap_original =
                install_hook(show_name, sym(show_sym), (void *)show_trap_hook, show, sizeof show);
    }
}
