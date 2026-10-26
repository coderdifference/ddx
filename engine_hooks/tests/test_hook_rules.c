// Offline test for hook_rules.c: rules parsed from JSON, run on contexts with stand-ins for the game.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/common.h"

bool g_debug;
void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static int HERO_A, HERO_B, MONSTER;
bool is_hero(const void *a) {
    return a == &HERO_A || a == &HERO_B;
}
unsigned name_hash(const char *s) {
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h;
}
unsigned hero_class_hash(const void *h) {
    return name_hash(h == &HERO_A ? "crusader" : "vestal");
}
const char *hook_phase_name(HookPhase p) {
    return p == HOOK_BEFORE ? "before" : "after";
}

// the registry: remember each rule's listener
static struct {
    const char *point;
    HookPhase phase;
    HookCallback cb;
    void *ud;
} g_on[32];
static int g_non;
int mods_current(void) {
    return -1;
}
void mods_set_current(int m) {}
bool mods_active(int m) {
    return true;
}

bool hook_on(const char *point, HookPhase phase, HookCallback cb, void *ud) {
    if (!strcmp(point, "nope"))
        return false;
    g_on[g_non++] = (typeof(g_on[0])){point, phase, cb, ud};
    return true;
}
static void emit(const char *point, HookContext *c) {
    c->point = point;
    for (int i = 0; i < g_non; i++)
        if (!strcmp(g_on[i].point, point) && g_on[i].phase == c->phase)
            g_on[i].cb(c, g_on[i].ud);
}

// the combat context: one name, "torch"
bool combat_context_build(CombatContext *ctx, const void *self) {
    memset(ctx, 0, sizeof *ctx);
    ctx->torch = 40;
    return true;
}
double combat_context_lookup(const char *name, bool *ok, void *ctx) {
    if (!strcmp(name, "torch"))
        return ((CombatContext *)ctx)->torch;
    *ok = false;
    return 0;
}
double combat_context_call(const char *name, const ExprArg *a, int n, bool *ok, void *ctx) {
    *ok = false;
    return 0;
}
int act_out_targets(const char *name, void *actor, void *rng, void **out, int max) {
    if (strcmp(name, "party"))
        return 0;
    out[0] = &HERO_A;
    out[1] = &HERO_B;
    return 2;
}

// actions: a log of what ran
static char g_ran[1024];
static void ran(const char *fmt, ...) {
    char b[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    strcat(g_ran, b);
    strcat(g_ran, ";");
}
static const char *who(const void *a) {
    return a == &HERO_A ? "A" : a == &HERO_B ? "B" : a == &MONSTER ? "M" : "?";
}
static float g_random = 0.5f;
float game_random(void) {
    return g_random;
}
void *game_rng(void) {
    return NULL;
}
bool game_damage(void *a, float x, const char *t, const char *s, void *by) {
    ran("damage %s %g %s", who(a), x, t ? t : "-");
    return true;
}
bool game_heal(void *a, float x, const void *by, const char *s) {
    ran("heal %s %g", who(a), x);
    return true;
}
bool game_stress(void *a, float x, const void *by, const char *s) {
    ran("stress %s %g", who(a), x);
    HookContext c = {.phase = HOOK_AFTER, .actor = a, .amount = x};
    emit("stress_damage", &c); // as the game would: a point while the action runs
    return true;
}
bool game_torch(float x) {
    ran("torch %g", x);
    return true;
}
bool game_apply_effect(void *p, void *t, const char *e) {
    ran("effect %s %s", who(t), e);
    return true;
}
bool game_add_quirk(void *h, const char *id) {
    ran("quirk %s %s", who(h), id);
    return true;
}
bool game_remove_quirk(void *h, const char *id) {
    ran("unquirk %s %s", who(h), id);
    return true;
}
bool game_give_item(const char *t, const char *id, int n) {
    ran("item %s %s %d", t, id, n);
    return true;
}
bool game_queue_loot(const char *t, const char *code) {
    ran("loot %s %s", t ? t : "-", code);
    return true;
}
int actor_remove_buffs(void *a, const char *const *p, int n) {
    ran("unbuff %s %s", who(a), p[0]);
    return n;
}
const ClassConfig *config_for_hero(const void *h) {
    return (const ClassConfig *)h;
}
bool config_apply_action(const ClassConfig *c, const void *h, const char *e) {
    ran("action %s %s", who(h), e);
    return true;
}

#include "../src/hook_rules.c"

static int fails;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                         \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)
#define RAN(s)                                                                                                         \
    do {                                                                                                               \
        CHECK(!strcmp(g_ran, s));                                                                                      \
        if (strcmp(g_ran, s))                                                                                          \
            printf("  ran: %s\n", g_ran);                                                                              \
        g_ran[0] = 0;                                                                                                  \
    } while (0)

int main(void) {
    const char *json =
        "[\n"
        "{\"point\": \"hp_damage\", \"phase\": \"before\", \"match\": {\"type\": \"bleed\"}, \"scale\": 0.5},\n"
        "{\"point\": \"hp_damage\", \"phase\": \"before\", \"match\": {\"source\": \"trap*\"}, \"amount\": \"amount + "
        "torch\"},\n"
        "{\"point\": \"quirk_add\", \"phase\": \"before\", \"class\": \"crusader\", \"match\": {\"type\": "
        "\"fear_of_*\"}, \"cancel\": true},\n"
        "{\"point\": \"loot_queue\", \"phase\": \"before\", \"set_type\": \"big_chest\"},\n"
        "{\"point\": \"kill\", \"when\": \"torch < 50\", \"do\": [{\"stress\": -5}, {\"effect\": \"fx\", \"on\": "
        "\"target\"},\n"
        "    {\"heal\": \"2 * 3\", \"on\": \"party\"}, {\"torch\": 10}, {\"item\": \"gold\", \"type\": \"gold\", "
        "\"amount\": 50},\n"
        "    {\"loot\": \"chest\", \"type\": \"curio\"}, {\"quirk\": \"q1\"}, {\"remove_quirk\": \"q2\"},\n"
        "    {\"remove_buffs\": [\"b*\"]}, {\"action\": \"act1\"}, {\"damage\": 3, \"type\": \"bleed\", \"on\": "
        "\"target\"}]},\n"
        "{\"point\": \"stress_damage\", \"do\": [{\"torch\": -1}]},\n"
        "{\"point\": \"stress_damage\", \"nested\": true, \"do\": [{\"log\": \"nested\"}]},\n"
        "{\"point\": \"crit\", \"chance\": \"25%\", \"do\": [{\"torch\": 1}]},\n"
        "{\"point\": \"crit\"},\n"
        "{\"point\": \"nope\", \"do\": [{\"torch\": 1}]},\n"
        "{\"point\": \"miss\", \"do\": [{\"fly\": 1}]}\n"
        "]";
    char err[128];
    JVal *root = json_parse(json, err, sizeof err);
    if (!root) {
        printf("FAIL json: %s\n", err);
        return 1;
    }
    hook_rules_parse(root, "test.json");
    json_free(root);
    CHECK(g_nrules == 9); // "crit" with nothing and "miss" with no valid action skipped
    hook_rules_register();
    CHECK(g_non == 8); // "nope" isn't a point

    HookContext c = {.phase = HOOK_BEFORE, .actor = &HERO_A, .amount = 10, .type = "bleed", .source = "monster"};
    emit("hp_damage", &c);
    CHECK(c.amount == 5);
    c = (HookContext){.phase = HOOK_BEFORE, .actor = &HERO_A, .amount = 10, .type = "trap", .source = "trap"};
    emit("hp_damage", &c);
    CHECK(c.amount == 50); // 10 + torch 40

    c = (HookContext){.phase = HOOK_BEFORE, .actor = &HERO_A, .type = "fear_of_birds"};
    emit("quirk_add", &c);
    CHECK(c.cancel);
    c = (HookContext){.phase = HOOK_BEFORE, .actor = &HERO_B, .type = "fear_of_birds"};
    emit("quirk_add", &c);
    CHECK(!c.cancel); // a vestal: the class filter

    c = (HookContext){.phase = HOOK_BEFORE, .type = "chest"};
    emit("loot_queue", &c);
    CHECK(c.type && !strcmp(c.type, "big_chest"));

    g_ran[0] = 0;
    c = (HookContext){.phase = HOOK_AFTER, .actor = &HERO_A, .target = &MONSTER, .amount = 7};
    emit("kill", &c);
    // the stress action fires stress_damage: only the nested rule runs (the log), not the torch -1 rule
    RAN("stress A -5;effect M fx;heal A 6;heal B 6;torch 10;item gold gold 50;loot curio chest;quirk A q1;"
        "unquirk A q2;unbuff A b*;action A act1;damage M 3 bleed;");

    c = (HookContext){.phase = HOOK_AFTER, .actor = &HERO_A};
    emit("stress_damage", &c);
    RAN("torch -1;"); // outside a rule's actions it runs

    c = (HookContext){.phase = HOOK_AFTER, .actor = &HERO_A, .synthetic = true};
    emit("stress_damage", &c);
    RAN(""); // synthetic: only nested rules (the log)

    g_random = 0.5f;
    c = (HookContext){.phase = HOOK_AFTER, .actor = &HERO_A};
    emit("crit", &c);
    RAN("");
    g_random = 0.1f;
    emit("crit", &c);
    RAN("torch 1;");

    printf(fails ? "hook rules: %d failure(s)\n" : "hook rules: all passed\n", fails);
    return fails != 0;
}
