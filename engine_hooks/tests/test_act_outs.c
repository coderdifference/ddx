// Offline test for act_outs.c's roll (macOS build): fake act-out data laid out as the game's, stand-ins for the rest.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/act_outs.c"
#include "mods_stub.h"

bool g_debug;
void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}
void *sym(const char *m) {
    return NULL;
}
void *install_hook(const char *n, void *t, void *d, const uint8_t *e, size_t l) {
    return NULL;
}
void *install_hook_rip(const char *n, void *t, void *d, const uint8_t *e, size_t l, const HookRip *r, size_t nr) {
    return NULL;
}
bool hook_emit(const char *p, HookPhase ph, HookContext *c) {
    return true;
}
bool hook_any_wanted(const char *const *p, int n) {
    return false;
}
bool hook_on(const char *p, HookPhase ph, HookCallback cb, void *ud) {
    return true;
}
bool is_hero(const void *a) {
    return true;
}
#define CLASS_A 111u
#define CLASS_B 222u
unsigned hero_class_hash(const void *h) {
    return CLASS_A;
}
unsigned hero_guid(const void *h) {
    return 1;
}
void combat_logf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}
static ActOutConfig g_cfg;
// the combat context: a stand-in holding only the torch (the rules' expressions read it through the bindings)
static float g_torch = 100;
// heroes for the target picks (act_out_targets): copied in when set
static CtxActor g_heroes[CTX_MAX_HEROES];
static int g_nheroes;
bool combat_context_build(CombatContext *c, const void *self) {
    memset(c, 0, sizeof *c);
    c->torch = g_torch;
    c->nheroes = g_nheroes;
    memcpy(c->heroes, g_heroes, sizeof g_heroes);
    return true;
}
float actor_stat(const void *actor, const char *stat) {
    for (int i = 0; i < g_nheroes; i++)
        if (g_heroes[i].actor == actor && !strcmp(stat, "max_hp"))
            return g_heroes[i].max_hp;
    return NAN;
}
// std::function stand-in: the display's queue keeps the calls, played in order by play_queue()
static struct {
    void (*call)(const uint8_t *);
    uint8_t data[16];
} g_queue[16];
static int g_nqueue;
void ui_function(StdFunction *out, void (*call)(const uint8_t *data), const void *data, size_t n) {
    memset(out, 0, sizeof *out);
    memcpy(out->buf, &call, sizeof call);
    memcpy(out->buf + 8, data, n < 16 ? n : 16);
}
double combat_context_lookup(const char *name, bool *ok, void *ctx) {
    if (!strcmp(name, "torch"))
        return ((const CombatContext *)ctx)->torch;
    *ok = false;
    return 0;
}
double combat_context_call(const char *name, const ExprArg *a, int n, bool *ok, void *ctx) {
    *ok = false;
    return 0;
}
const ActOutConfig *config_act_outs(void) {
    return &g_cfg;
}

static const char *const k_ids[ACT_OUT_VANILLA_TYPES] = {
    "nothing",        "bark_stress",         "change_pos",        "ignore_command",
    "random_command", "retreat_from_combat", "attack_friendly",   "attack_self",
    "mark_self",      "stress_heal_self",    "stress_heal_party", "buff_random_party_member",
    "buff_party",     "heal_self",           "consume_item"};
static const char *fake_id(int t) {
    return k_ids[t];
}

static float dark_off(const CombatContext *ctx, const char *source, const char *act_out, void *ud) {
    return ctx->torch < 50 ? 0 : 1;
}

// independent rolls: a stand-in for the game's Combat_StartTurn (its roll through h_roll; the act-out recorded as the
// party's last, as the game does when it performs one; the type's turn result), a scripted rng, permissive checks
static uint8_t g_raid_buf[0x5000];
static void *g_raid_ptr = g_raid_buf;
static int g_ran[16], g_nran;
static int fake_start_turn(void *unused, void *rng, void *actor) {
    const uint8_t *d = h_roll(actor, rng);
    if (!d)
        return 0;
    int type = entry_type(d);
    g_ran[g_nran++] = type;
    *(int *)(g_raid_buf + RAID_PARTY + PARTY_LAST_ACT_OUT) = type;
    return type == 3 || type == 5 ? 2 : 1; // ignore_command, retreat: the turn is lost; else 1
}
static bool ok_valid(const void *b, bool h, int t) {
    return true;
}
static bool fake_not_valid(const void *b, bool h, int t) {
    return false;
}
static bool ok_disabled(const void *b, int t) {
    return false;
}
static unsigned ok_count(const void *r, int t) {
    return 0;
}
static float g_rnd[16];
static int g_rnd_i;
static float seq_uniform(void *rng) {
    return g_rnd[g_rnd_i++];
}

// A turn as the game drives it: bs_before_turn_Bark calls Combat_StartTurn, then steps on unless act_outs_hold()
// keeps it there for another call.
static int g_calls;
static int run_turn(void *rng, void *actor) {
    g_calls = 1;
    int r = h_start_turn(NULL, rng, actor);
    while (act_outs_hold()) {
        r = h_start_turn(NULL, rng, actor);
        g_calls++;
    }
    return r;
}

// the game's side of run_new
static void fake_queue_lambda(void *display, StdFunction *f) {
    memcpy(&g_queue[g_nqueue].call, f->buf, sizeof g_queue[0].call);
    memcpy(g_queue[g_nqueue++].data, f->buf + 8, 16);
}
static void play_queue(void) {
    for (int i = 0; i < g_nqueue; i++)
        g_queue[i].call(g_queue[i].data);
    g_nqueue = 0;
}
static int g_last_set = -1, g_counted = -1, g_halos;
static char g_bark[128];
static void *g_healed;
static float g_heal_amount, g_burn;
static void fake_set_last(void *b, bool h, int type) {
    g_last_set = type;
}
static void fake_count(void *raid, int type) {
    g_counted = type;
}
static int fake_result(int type) {
    return type == 13 ? 1 : 0;
}
static float fake_bark_chance(int type) {
    return type == 13 ? 0.5f : 0;
}
static float fake_burndown(int type) {
    return type == 13 ? 0.25f : 0;
}
static void fake_add_burndown(void *b, float s, bool x) {
    g_burn += s;
}
static const char *fake_sound(const void *src) {
    return NULL;
}
static int fake_halo(const void *src) {
    return 3;
}
static void fake_queue_halo(void *d, void *actor, int halo, bool x) {
    g_halos++;
}
static void fake_wait(void *d, float s) {}
static void fake_bark(void *d, void *actor, const char *id, bool a, bool b, SourceId s) {
    snprintf(g_bark, sizeof g_bark, "%s", id);
}
static void fake_heal(void *actor, void *rng, float amount, const void *healer, int source, bool x) {
    g_healed = actor;
    g_heal_amount = amount;
}
static int fake_type_from_id(const char *id) {
    return 15;
}
static const char *fake_id_from_type(int t) {
    return k_ids[t];
}

static int fails;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                         \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)
static bool near(float a, float b) {
    return fabsf(a - b) < 1e-4f;
}

// An ActOut::Data: a vector of CombatStartTurnData (0xb0 bytes each), in a buffer of n entries.
static unsigned g_class_a[] = {CLASS_A}, g_class_b[] = {CLASS_B};
static void set_entry(uint8_t *e, int type, float chance, const unsigned *classes) {
    *(int *)(e + ACT_OUT_TYPE) = type;
    *(float *)(e + ACT_OUT_CHANCE) = chance;
    *(const unsigned **)(e + ACT_OUT_CLASSES) = classes;
    *(const unsigned **)(e + ACT_OUT_CLASSES + 8) = classes ? classes + 1 : NULL;
}
static float weight_of(const Table *t, const uint8_t *e) {
    for (int i = 0; i < t->n; i++)
        if (t->e[i] == e)
            return t->w[i];
    return 0;
}

static uint8_t *g_trait_storage;
static const uint8_t *fake_trait_type(const void *lib, unsigned hash) {
    return hash == 77 ? g_trait_storage : NULL;
}
static float g_uniform;
static float fake_uniform(void *rng) {
    return g_uniform;
}

int main(void) {
    g_act_out_id = fake_id;
    // fearful-like: nothing 26, bark_stress 4, change_pos 0, ignore_command 4, attack_friendly 1 for class A and B
    uint8_t *e = calloc(6, ACT_OUT_SIZE);
    uint8_t *nothing = e, *bark = e + ACT_OUT_SIZE, *change = e + 2 * ACT_OUT_SIZE, *ignore = e + 3 * ACT_OUT_SIZE,
            *af_a = e + 4 * ACT_OUT_SIZE, *af_b = e + 5 * ACT_OUT_SIZE;
    set_entry(nothing, 0, 26, NULL);
    set_entry(bark, 1, 4, NULL);
    set_entry(change, 2, 0, NULL);
    set_entry(ignore, 3, 4, NULL);
    set_entry(af_a, 6, 1, g_class_a);
    set_entry(af_b, 6, 1, g_class_b);
    Vec data = {e, e + 6 * ACT_OUT_SIZE};

    // no config: the game's chances, class-filtered, chance 0 left out
    Table t = {.n = 0};
    add_data(&t, (const uint8_t *)&data, CLASS_A, NULL);
    CHECK(t.n == 4 && near(t.total, 35));
    CHECK(near(weight_of(&t, nothing), 26) && near(weight_of(&t, af_a), 1) && weight_of(&t, af_b) == 0);
    CHECK(weight_of(&t, change) == 0);

    // configured: weights by type exactly as written (percentages), a type at 0 in the data switched on
    ActOutChances c = {.trait = "fearful", .n = 4};
    c.w[0] = (ActOutWeight){"nothing", 50};
    c.w[1] = (ActOutWeight){"change_pos", 30};
    c.w[2] = (ActOutWeight){"attack_friendly", 20};
    c.w[3] = (ActOutWeight){"no_such_act_out", 99};
    t = (Table){.n = 0};
    add_data(&t, (const uint8_t *)&data, CLASS_A, &c);
    CHECK(t.n == 3 && near(t.total, 100));
    CHECK(near(weight_of(&t, nothing), 50) && near(weight_of(&t, change), 30) && near(weight_of(&t, af_a), 20));
    CHECK(weight_of(&t, bark) == 0 && weight_of(&t, ignore) == 0 && weight_of(&t, af_b) == 0);

    // a type with no entry the hero may use weighs nothing
    ActOutChances c2 = {.trait = "x", .n = 2};
    c2.w[0] = (ActOutWeight){"nothing", 1};
    c2.w[1] = (ActOutWeight){"heal_self", 1};
    t = (Table){.n = 0};
    add_data(&t, (const uint8_t *)&data, CLASS_A, &c2);
    CHECK(t.n == 1 && near(weight_of(&t, nothing), 1));

    // the whole roll on a fake hero: the trait's entries (configured) and a quirk's (its own chances), picked as the
    // game does: the first entry whose running sum is above uniform * total
    static uint8_t trait[TRAIT_ACT_OUTS + sizeof(Vec)];
    memcpy(trait + TRAIT_ACT_OUTS, &data, sizeof data);
    static uint8_t qdata_e[ACT_OUT_SIZE], quirk[QUIRKCLASS_ACT_OUTS + sizeof(Vec)];
    set_entry(qdata_e, 1, 5, NULL); // a quirk's bark_stress, chance 5
    Vec qv = {qdata_e, qdata_e + ACT_OUT_SIZE};
    memcpy(quirk + QUIRKCLASS_ACT_OUTS, &qv, sizeof qv);
    static uint8_t qentry[QUIRK_SIZE];
    const uint8_t *qp = quirk;
    memcpy(qentry, &qp, sizeof qp);
    static uint8_t actor[0x1400];
    *(unsigned *)(actor + ACTOR_TRAIT) = 77;
    Vec quirks = {qentry, qentry + QUIRK_SIZE};
    memcpy(actor + HERO_QUIRKS, &quirks, sizeof quirks);
    g_trait_type = fake_trait_type;
    g_trait_storage = trait;
    static void *lib;
    g_trait_lib = &lib;
    void *vtable[4] = {0, 0, 0, (void *)fake_uniform};
    void *rng = vtable;
    c.trait_hash = 77;
    g_cfg.ntraits = 1;
    g_cfg.traits[0] = c; // nothing 50, change_pos 30, attack_friendly 20; quirk 5
    g_uniform = 0.0f;
    CHECK(roll(actor, &rng) == nothing);
    g_uniform = 0.47f;
    CHECK(roll(actor, &rng) == nothing); // 49.35 of 105
    g_uniform = 0.48f;
    CHECK(roll(actor, &rng) == change); // 50.4
    g_uniform = 0.9f;
    CHECK(roll(actor, &rng) == af_a); // 94.5
    g_uniform = 0.96f;
    CHECK(roll(actor, &rng) == qdata_e); // 100.8: the quirk (its game weight, 5)
    g_uniform = 0.9999f;
    CHECK(roll(actor, &rng) == qdata_e);
    actor[HERO_NO_QUIRK_ACT_OUTS] = 1; // quirks left out
    g_uniform = 0.9f;
    CHECK(roll(actor, &rng) == af_a);       // 90 of 100
    *(unsigned *)(actor + ACTOR_TRAIT) = 0; // no trait, no quirks: none, the rng unused
    CHECK(roll(actor, &rng) == NULL);

    // rules: a C rule for the trait's change_pos (off below 50 torch), a JSON rule doubling the quirk's
    // bark_stress when the torch is under 25; sources are the trait's and quirk's id strings
    memcpy(trait, "fearful", 8);
    memcpy(quirk, "disease_vampire_crave", 22);
    actor[HERO_NO_QUIRK_ACT_OUTS] = 0;
    *(unsigned *)(actor + ACTOR_TRAIT) = 77;
    CHECK(act_out_rule_register(
        &(ActOutRule){.name = "dark change", .source = "fear*", .act_out = "change_pos", .check = dark_off}));
    ActOutJsonRule jr = {.source = "disease_*", .act_out = "bark_stress", .when = "torch < 25", .weight = "2"};
    CHECK(act_out_rule_register(&(ActOutRule){
        .name = "json", .source = jr.source, .act_out = jr.act_out, .check = json_rule_check, .ud = &jr}));
    Table r = {.n = 0};
    r.cur = (const char *)trait;
    add_data(&r, (const uint8_t *)&data, CLASS_A, &c);
    r.cur = (const char *)quirk;
    add_data(&r, (const uint8_t *)&qv, CLASS_A, NULL);
    g_torch = 100;
    Table r1 = r;
    apply_rules(&r1, actor); // bright: nothing changes
    CHECK(r1.n == 4 && near(r1.total, 105));
    g_torch = 40;
    Table r2 = r;
    apply_rules(&r2, actor); // change_pos off; the quirk unchanged
    CHECK(r2.n == 3 && near(r2.total, 75) && weight_of(&r2, change) == 0);
    g_torch = 10;
    Table r3 = r;
    apply_rules(&r3, actor); // and the quirk's bark_stress doubled
    CHECK(r3.n == 3 && near(weight_of(&r3, qdata_e), 10) && near(r3.total, 80));
    g_torch = 10; // the whole roll sees the rules: 0.9 * 80 = 72 -> quirk
    g_uniform = 0.9f;
    CHECK(roll(actor, &rng) == qdata_e);

    // a quirk configured by its id's hash (+0x40), as written: bark_stress 100 replaces its game weight 5
    *(unsigned *)(quirk + QUIRKCLASS_HASH) = 99;
    ActOutChances qc = {.trait = "disease_vampire_crave", .trait_hash = 99, .n = 1};
    qc.w[0] = (ActOutWeight){"bark_stress", 100};
    g_cfg.ntraits = 2;
    g_cfg.traits[1] = qc;
    g_nrules = 0;
    g_uniform = 0.5f;
    CHECK(roll(actor, &rng) == qdata_e); // 100 of 200: trait 0-100, quirk 100-200
    g_uniform = 0.49f;
    CHECK(roll(actor, &rng) != qdata_e);

    // use_max: per act-out, the source giving it the most; the pool is the sum of those. Trait: nothing 50,
    // change_pos 30, attack_friendly 20; quirk: bark_stress 100 -> the pool is all four (no shared type), total 200.
    g_cfg.use_max = true;
    Table m = {.n = 0};
    m.cur = (const char *)trait;
    add_data(&m, (const uint8_t *)&data, CLASS_A, &g_cfg.traits[0]);
    m.cur = (const char *)quirk;
    add_data(&m, (const uint8_t *)&qv, CLASS_A, &g_cfg.traits[1]);
    keep_max(&m);
    CHECK(m.n == 4 && near(m.total, 200));
    // a shared type: the quirk's nothing (60) beats the trait's (50); the trait's bark_stress (0) isn't there
    uint8_t q2e[2 * ACT_OUT_SIZE];
    memset(q2e, 0, sizeof q2e);
    set_entry(q2e, 0, 60, NULL);
    set_entry(q2e + ACT_OUT_SIZE, 6, 10, NULL); // nothing 60, attack_friendly 10
    Vec q2 = {q2e, q2e + 2 * ACT_OUT_SIZE};
    m = (Table){.n = 0};
    m.cur = (const char *)trait;
    add_data(&m, (const uint8_t *)&data, CLASS_A, &g_cfg.traits[0]);
    m.cur = (const char *)quirk;
    add_data(&m, (const uint8_t *)&q2, CLASS_A, NULL);
    keep_max(&m); // nothing 60 (quirk), change_pos 30, attack_friendly 20 (trait)
    CHECK(m.n == 3 && near(m.total, 110));
    CHECK(near(weight_of(&m, q2e), 60) && weight_of(&m, nothing) == 0 && near(weight_of(&m, af_a), 20));
    CHECK(weight_of(&m, q2e + ACT_OUT_SIZE) == 0 && near(weight_of(&m, change), 30));
    // a tie goes to the first source (the trait)
    set_entry(q2e, 0, 50, NULL);
    m = (Table){.n = 0};
    m.cur = (const char *)trait;
    add_data(&m, (const uint8_t *)&data, CLASS_A, &g_cfg.traits[0]);
    m.cur = (const char *)quirk;
    add_data(&m, (const uint8_t *)&q2, CLASS_A, NULL);
    keep_max(&m);
    CHECK(near(weight_of(&m, nothing), 50) && weight_of(&m, q2e) == 0);
    g_cfg.use_max = false;

    // per source: the trait and each quirk roll once on their own table (nothing included); what they give is
    // queued and run in that order. Trait: bark_stress 50, nothing 50. Quirk 1: retreat 25, ignore_command 25,
    // nothing 50. Quirk 2: attack_friendly 100 (class A).
    uint8_t te[2 * ACT_OUT_SIZE], q1e[3 * ACT_OUT_SIZE], q2e2[ACT_OUT_SIZE];
    memset(te, 0, sizeof te);
    memset(q1e, 0, sizeof q1e);
    memset(q2e2, 0, sizeof q2e2);
    set_entry(te, 1, 50, NULL);
    set_entry(te + ACT_OUT_SIZE, 0, 50, NULL);
    set_entry(q1e, 5, 25, NULL);
    set_entry(q1e + ACT_OUT_SIZE, 3, 25, NULL);
    set_entry(q1e + 2 * ACT_OUT_SIZE, 0, 50, NULL);
    set_entry(q2e2, 6, 100, g_class_a);
    Vec tv = {te, te + 2 * ACT_OUT_SIZE}, q1v = {q1e, q1e + 3 * ACT_OUT_SIZE}, q2v = {q2e2, q2e2 + ACT_OUT_SIZE};
    memcpy(trait + TRAIT_ACT_OUTS, &tv, sizeof tv);
    static uint8_t quirk1[QUIRKCLASS_ACT_OUTS + sizeof(Vec)], quirk2[QUIRKCLASS_ACT_OUTS + sizeof(Vec)];
    memcpy(quirk1, "q1", 3);
    memcpy(quirk2, "q2", 3);
    memcpy(quirk1 + QUIRKCLASS_ACT_OUTS, &q1v, sizeof q1v);
    memcpy(quirk2 + QUIRKCLASS_ACT_OUTS, &q2v, sizeof q2v);
    static uint8_t qentries[2 * QUIRK_SIZE];
    const uint8_t *qp1 = quirk1, *qp2 = quirk2;
    memcpy(qentries, &qp1, sizeof qp1);
    memcpy(qentries + QUIRK_SIZE, &qp2, sizeof qp2);
    Vec qs = {qentries, qentries + 2 * QUIRK_SIZE};
    memcpy(actor + HERO_QUIRKS, &qs, sizeof qs);
    actor[HERO_NO_QUIRK_ACT_OUTS] = 0;
    g_cfg.ntraits = 0;
    g_nrules = 0;
    g_cfg.per_source = true;
    g_raid = &g_raid_ptr;
    o_start_turn = fake_start_turn;
    o_valid = ok_valid;
    o_disabled = ok_disabled;
    o_count = ok_count;
    void *svt[4] = {0, 0, 0, (void *)seq_uniform};
    void *srng = svt;
    // bark, ignore_command, attack_friendly: all three run, then the pass ends the turn
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.4f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 2);
    CHECK(g_nran == 3 && g_ran[0] == 1 && g_ran[1] == 3 && g_ran[2] == 6 && g_calls == 3); // one call per act-out
    // bark, retreat: the retreat stops it there (quirk 2's attack_friendly doesn't run)
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.1f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 2);
    CHECK(g_nran == 2 && g_ran[0] == 1 && g_ran[1] == 5);
    // bark, nothing, attack_friendly: the hero then acts (1)
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.9f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 1 && g_nran == 2 && g_rnd_i == 3);
    // max_per_turn 1: only the first (the trait's bark)
    g_cfg.max_per_turn = 1;
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.4f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 1 && g_nran == 1 && g_ran[0] == 1);
    g_cfg.max_per_turn = 0;
    // quirks left out, the trait gives nothing: a normal turn, the game not called, one draw
    actor[HERO_NO_QUIRK_ACT_OUTS] = 1;
    g_rnd[0] = 0.9f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 0 && g_nran == 0 && g_rnd_i == 1 && g_calls == 1);
    // the game steps on without calling again (the hero can't act any more): the hold lets go, the rest is dropped
    actor[HERO_NO_QUIRK_ACT_OUTS] = 0;
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.4f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(h_start_turn(NULL, &srng, actor) == 1 && g_nran == 1); // the trait's bark ran; sources left
    CHECK(act_outs_hold());                                      // held for the next call
    CHECK(!act_outs_hold());                                     // stepped again without a call: let go
    CHECK(h_start_turn(NULL, &srng, actor) == 1 && g_nran == 2); // a later call starts a new turn (rolls afresh)
    while (act_outs_hold())
        h_start_turn(NULL, &srng, actor);
// prerequisites (sources "q1": retreat 25, ignore_command 25, nothing 50; "q2": attack_friendly 100; trait
// "fearful": bark_stress 50, nothing 50)
#define PRE(src, ANY)                                                                                                  \
    do {                                                                                                               \
        g_cfg.nprereqs = 1;                                                                                            \
        memset(&g_cfg.prereqs[0], 0, sizeof g_cfg.prereqs[0]);                                                         \
        snprintf(g_cfg.prereqs[0].source, 64, "%s", src);                                                              \
        g_cfg.prereqs[0].any = ANY;                                                                                    \
    } while (0)
#define REQ(src, WITHIN, NC)                                                                                           \
    do {                                                                                                               \
        ActOutPrereq *q_ = &g_cfg.prereqs[0].req[g_cfg.prereqs[0].n++];                                                \
        snprintf(q_->source, 64, "%s", src);                                                                           \
        q_->within = WITHIN;                                                                                           \
        q_->nothing_counts = NC;                                                                                       \
    } while (0)
    g_nrec = 0;
    // q2 needs q1 this turn: q1 gives nothing -> q2 doesn't roll
    PRE("q2", false);
    REQ("q1", ACT_OUT_WITHIN_TURN, false);
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.9f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 1 && g_nran == 1 && g_ran[0] == 1 && g_rnd_i == 2);
    // the same, nothing_counts: q1's nothing counts, q2 runs
    g_cfg.prereqs[0].req[0].nothing_counts = true;
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.9f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 1 && g_nran == 2 && g_ran[1] == 6);
    // until: q1 needs q2 (later in order): q1 waits, q2 runs, then q1 rolls (ignore_command): bark, af, ignore
    PRE("q1", false);
    REQ("q2", ACT_OUT_WITHIN_TURN, false);
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.5f;
    g_rnd[2] = 0.4f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 2 && g_nran == 3 && g_ran[0] == 1 && g_ran[1] == 6 && g_ran[2] == 3 &&
          g_calls == 3);
    // all: q2 needs the trait and q1 this turn; the trait fires, q1 gives nothing -> no q2
    PRE("q2", false);
    REQ("fearful", ACT_OUT_WITHIN_TURN, false);
    REQ("q1", ACT_OUT_WITHIN_TURN, false);
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.9f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 1 && g_nran == 1);
    // any: either is enough -> q2 runs
    g_cfg.prereqs[0].any = true;
    g_rnd[0] = 0.1f;
    g_rnd[1] = 0.9f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 1 && g_nran == 2 && g_ran[1] == 6);
    // battle: q2 needs q1 within the battle. Turn 1: q1 fires (ignore_command), q2 runs. Turn 2: q1 nothing, q2 still
    // runs (q1 fired this battle). A new battle: q1 nothing, q2 doesn't roll.
    PRE("q2", false);
    REQ("q1", ACT_OUT_WITHIN_BATTLE, false);
    g_rnd[0] = 0.9f;
    g_rnd[1] = 0.4f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 2 && g_nran == 2);
    g_rnd[0] = 0.9f;
    g_rnd[1] = 0.9f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 1 && g_nran == 1 && g_ran[0] == 6);
    HookContext bs = {.actor = actor};
    on_battle_start(&bs, NULL);
    g_rnd[0] = 0.9f;
    g_rnd[1] = 0.9f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 0 && g_nran == 0 && g_rnd_i == 2);
    // expedition: still met after the new battle, until raid_start clears the hero's records
    g_cfg.prereqs[0].req[0].within = ACT_OUT_WITHIN_EXPEDITION;
    g_rnd[0] = 0.9f;
    g_rnd[1] = 0.9f;
    g_rnd[2] = 0.5f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 1 && g_nran == 1);
    on_raid_start(&bs, NULL);
    g_rnd[0] = 0.9f;
    g_rnd[1] = 0.9f;
    g_rnd_i = 0;
    g_nran = 0;
    CHECK(run_turn(&srng, actor) == 0 && g_nran == 0);
    g_cfg.nprereqs = 0;
    g_cfg.per_source = false;

    // new types: numbers from 0x100 (registered again: the same), ids both ways, defaults from "like"
    CHECK(act_out_type_register(&(ActOutTypeDef){
              .id = "heal_lowest_ally", .like = "heal_self", .result = -1, .bark_chance = -1, .burndown = -1}) ==
          0x100);
    g_cfg.types[0] = (ActOutJsonType){.id = "heal_lowest_ally", .outcome = "heal", .target = "party_lowest_hp"};
    g_cfg.ntypes = 1; // the campaign's JSON types (json_outcome looks it up)
    ActOutJsonType *jt_ = &g_cfg.types[0];
    int ty = act_out_type_register(&(ActOutTypeDef){.id = "heal_lowest_ally",
                                                    .like = "heal_self",
                                                    .result = -1,
                                                    .bark_chance = -1,
                                                    .burndown = -1,
                                                    .outcome = json_outcome});
    CHECK(ty == 0x100 && g_ntypes == 1 && type_of("heal_lowest_ally") == 0x100 && type_of("heal_self") == 13);
    CHECK(act_out_type_register(&(ActOutTypeDef){.id = ""}) == -1);
    o_type_from_id = fake_type_from_id;
    o_id_from_type = fake_id_from_type;
    g_result_of = o_result_of = fake_result;
    g_bark_chance_of = o_bark_chance_of = fake_bark_chance;
    g_burndown_of = fake_burndown;
    CHECK(h_type_from_id("heal_lowest_ally") == 0x100 && h_type_from_id("whatever") == 15);
    CHECK(!strcmp(h_id_from_type(0x100), "heal_lowest_ally") && !strcmp(h_id_from_type(15), "unknown"));
    CHECK(!strcmp(h_id_from_type(13), "heal_self"));
    CHECK(h_result_of(0x100) == 1 && near(h_bark_chance_of(0x100), 0.5f) && h_result_of(13) == 1);

    // run_new: the trait's entry rolled; checks pass; the setup; the outcome queued, played after: the party hero
    // with the least HP healed by number_value of its max HP
    static uint8_t hlo[ACT_OUT_SIZE];
    set_entry(hlo, ty, 100, NULL);
    memcpy(hlo + ACT_OUT_SOURCE_ID, "graceful", 9);
    *(float *)(hlo + ACT_OUT_NUMBER) = 0.25f;
    Vec hv = {hlo, hlo + ACT_OUT_SIZE};
    memcpy(trait + TRAIT_ACT_OUTS, &hv, sizeof hv);
    memcpy(trait, "graceful", 9);
    actor[HERO_NO_QUIRK_ACT_OUTS] = 1;
    static uint8_t h1[8], h2[8], h3[8];
    g_heroes[0] = (CtxActor){.actor = actor, .hp = 30, .max_hp = 30, .hp_pct = 100, .alive = true};
    g_heroes[1] = (CtxActor){.actor = h1, .hp = 12, .max_hp = 40, .hp_pct = 30, .alive = true};
    g_heroes[2] = (CtxActor){.actor = h2, .hp = 9, .max_hp = 10, .hp_pct = 90, .alive = true};
    g_heroes[3] = (CtxActor){.actor = h3, .hp = 1, .max_hp = 30, .alive = false, .corpse = true};
    g_nheroes = 4;
    static void *display_obj;
    static uint8_t display_t[0x1000];
    g_display = &display_obj;
    display_obj = &display_obj;
    g_display_t = display_t;
    g_queue_lambda = fake_queue_lambda;
    g_set_last = fake_set_last;
    g_count_act_out = fake_count;
    g_add_burndown = fake_add_burndown;
    g_sound_of = fake_sound;
    g_halo_of = fake_halo;
    g_queue_halo = fake_queue_halo;
    g_queue_wait = fake_wait;
    g_queue_bark = fake_bark;
    g_heal = fake_heal;
    o_start_turn = NULL; // the game's Combat_StartTurn must not be called
    g_rnd[0] = 0.5f;
    g_rnd[1] = 0.1f;
    g_rnd_i = 0; // the roll, then the bark (0.1 < 0.5)
    CHECK(h_start_turn(NULL, &srng, actor) == 1);
    CHECK(g_last_set == 0x100 && g_counted == 0x100 && near(g_burn, 0.25f) &&
          !strcmp(g_bark, "str_heal_lowest_ally_graceful"));
    CHECK(g_nqueue == 2 && g_healed == NULL); // the halo, the outcome: nothing applied yet
    play_queue();
    CHECK(g_halos == 1 && g_healed == h2 && near(g_heal_amount, 2.5f)); // the least HP (9), not the lowest %
    jt_->target[0] = 0;
    strcat(jt_->target, "party_lowest_hp_pct");
    g_rnd[0] = 0.5f;
    g_rnd[1] = 0.9f;
    g_rnd_i = 0;
    g_bark[0] = 0; // no bark this time
    CHECK(h_start_turn(NULL, &srng, actor) == 1 && !g_bark[0]);
    play_queue();
    CHECK(g_healed == h1 && near(g_heal_amount, 10)); // 30%
    // refused (a repeat): no setup, "nothing" as the last act-out
    o_valid = fake_not_valid;
    g_counted = -1;
    g_rnd[0] = 0.5f;
    g_rnd_i = 0;
    CHECK(h_start_turn(NULL, &srng, actor) == 0 && g_last_set == 0 && g_counted == -1 && g_nqueue == 0);
    o_valid = ok_valid;
    // targets: self, ally_random (never self), party (the living)
    void *to[4];
    CHECK(act_out_targets("self", actor, &srng, to, 4) == 1 && to[0] == actor);
    g_rnd[0] = 0.0f;
    g_rnd_i = 0;
    CHECK(act_out_targets("ally_random", actor, &srng, to, 4) == 1 && to[0] == h1);
    CHECK(act_out_targets("party", actor, &srng, to, 4) == 3);
    CHECK(act_out_targets("nobody", actor, &srng, to, 4) == 0);

    free(e);
    printf(fails ? "act_outs: %d failure(s)\n" : "act_outs: all passed\n", fails);
    return fails != 0;
}
