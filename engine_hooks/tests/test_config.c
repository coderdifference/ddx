// Offline test for json.c + config.c with stand-ins for the game (build: engine_hooks/tests/run.sh).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../src/config.c"

bool g_debug;
void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}
unsigned name_hash(const char *s) {
    unsigned h = 0;
    while (*s)
        h = h * 53 + (unsigned char)*s++;
    return h;
}
static const char HERO = 0;
bool is_hero(const void *a) {
    return a == &HERO;
}
unsigned hero_class_hash(const void *h) {
    return name_hash("example_knight");
}
unsigned hero_guid(const void *h) {
    return 1;
}
static int g_vals[MAX_FIELDS];
int herodata_get(unsigned g, int f) {
    return f >= 0 ? g_vals[f] : 0;
}
void herodata_set(unsigned g, int f, int v) {
    if (f >= 0)
        g_vals[f] = v;
}
bool game_data_dir(char *out, size_t n) {
    return false;
}
bool workshop_dir(char *out, size_t n) {
    return false;
}
void hook_rules_parse(const JVal *list, const char *path) {}
static int g_test_mod = -1, g_test_cur = -1, g_test_pos[4] = {-1, -1, -1, -1}; // the mods the files belong to
int mods_find(const char *p) {
    return g_test_mod;
}
void mods_set_current(int m) {
    g_test_cur = m;
}
int mods_current(void) {
    return g_test_cur;
}
int mods_pos(int m) {
    return m >= 0 && m < 4 ? g_test_pos[m] : -1;
}
bool mods_active(int m) {
    return m < 0 || mods_pos(m) >= 0;
}
int mods_by_priority(int *out, int max) {
    int n = 0;
    for (int p = 0; p < 4; p++)
        for (int m = 0; m < 4 && n < max; m++)
            if (g_test_pos[m] == p)
                out[n++] = m;
    return n;
}
void mods_on_campaign(void (*fn)(void)) {}
static char g_announced[128];
void announce_mod(const char *n, const char *f, const char *c) {
    snprintf(g_announced, sizeof g_announced, "%s|%s|%s", n, f ? f : "", c ? c : "");
}
static char g_removed[256];
int actor_remove_buffs(void *a, const char *const *p, int n) {
    g_removed[0] = 0;
    for (int i = 0; i < n; i++) {
        strcat(g_removed, p[i]);
        strcat(g_removed, ";");
    }
    return n;
}

static double test_fn(const char *name, const ExprArg *a, int n, bool *ok, void *ctx) {
    if (!strcmp(name, "has") && n == 1 && a[0].is_str)
        return !strcmp(a[0].str, "abc");
    if (!strcmp(name, "twice") && n == 1 && !a[0].is_str)
        return a[0].num * 2;
    *ok = false;
    return 0;
}

static int fails;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                         \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

int main(int argc, char **argv) {
    field_register("test");
    load_file(argv[1]);
    CHECK(g_nclasses == 1);
    const ClassConfig *c = config_for_hero(&HERO);
    CHECK(c != NULL);
    int test = field_find("test"), hp = field_find("hp");
    CHECK(test >= 0 && hp >= 0 && field_find("spec") >= 0);
    // the example config (example_class.engine.json): pickers only without a spec; spenders by spec and hp
    int spec0 = field_find("spec");
    g_vals[spec0] = 0;
    g_vals[hp] = 0;
    CHECK(config_skill_allowed(c, &HERO, name_hash("pick_light")));
    CHECK(!config_skill_allowed(c, &HERO, name_hash("light_bolt")));
    CHECK(config_apply_action(c, &HERO, "ex_pick_wrath"));
    CHECK(g_vals[spec0] == 3 && g_vals[hp] == 0);
    CHECK(!config_skill_allowed(c, &HERO, name_hash("pick_light")));
    CHECK(config_skill_allowed(c, &HERO, name_hash("wrath_strike")));
    CHECK(!config_skill_allowed(c, &HERO, name_hash("verdict"))); // 0 hp
    config_apply_action(c, &HERO, "ex_power_gain");
    config_apply_action(c, &HERO, "ex_power_gain");
    CHECK(g_vals[hp] == 2);
    CHECK(config_skill_allowed(c, &HERO, name_hash("verdict")));
    double tvd;
    CHECK(config_skill_dmg(c, &HERO, name_hash("verdict"), &tvd) && tvd > 0.3999 && tvd < 0.4001);
    double lod;
    CHECK(config_effect_heal(c, &HERO, "ex_mend_heal", &lod) && lod == 8.0); // 2 + 3 * 2
    CHECK(config_apply_action(c, &HERO, "ex_respec"));
    CHECK(g_vals[spec0] == 0 && g_vals[hp] == 0);
    CHECK(!strcmp(g_removed, "EX_LIGHT_SPEC_*;EX_GUARD_SPEC_*;EX_WRATH_SPEC_*;"));
    CHECK(config_skill_allowed(c, &HERO, name_hash("pick_guard")));
    // a richer inline config: enums, lists, min/max, set+add with clamp
    const char *j =
        "{\"class\":\"x\",\"enums\":{\"spec\":[\"none\",\"holy\",\"prot\",\"ret\"]},\"limits\":{\"hp\":[0,3]},"
        "\"skills\":{\"tv\":{\"spec\":[\"prot\",\"ret\"],\"hp_min\":1,\"hp_max\":2}},"
        "\"actions\":{\"gain\":{\"add\":{\"hp\":1}},\"pick_ret\":{\"remove_buffs\":[\"MY_*_SPEC_*\",\"X\"],\"set\":{"
        "\"spec\":\"ret\",\"hp\":0}}},"
        "\"animation_variants\":[\"{spec}_{hp}\",\"{spec}\"],\"effect_variants\":{\"lod\":\"lod_{hp}\"}}";
    char err[128];
    JVal *root = json_parse(j, err, sizeof err);
    CHECK(root != NULL);
    parse_class(root, "inline");
    json_free(root);
    const ClassConfig *x = &g_classes[1];
    JVal *g = json_parse("{\"announce\":{\"name\":\"str_my_mod\",\"colour\":\"notable\"}}", err, sizeof err);
    CHECK(g != NULL);
    parse_globals(g, "inline"); // the mod in the announcement's list
    json_free(g);
    CHECK(!strcmp(g_announced, "str_my_mod||notable"));
    int spec = field_find("spec");
    unsigned tv = name_hash("tv");
    CHECK(config_apply_action(x, &HERO, "pick_ret"));
    CHECK(g_vals[spec] == 3 && g_vals[hp] == 0);
    CHECK(!strcmp(g_removed, "MY_*_SPEC_*;X;"));
    CHECK(!config_skill_allowed(x, &HERO, tv)); // hp 0 < min 1
    config_apply_action(x, &HERO, "gain");
    CHECK(g_vals[hp] == 1);
    CHECK(config_skill_allowed(x, &HERO, tv));
    config_apply_action(x, &HERO, "gain");
    config_apply_action(x, &HERO, "gain");
    config_apply_action(x, &HERO, "gain");
    CHECK(g_vals[hp] == 3);                     // clamped
    CHECK(!config_skill_allowed(x, &HERO, tv)); // hp 3 > max 2
    g_vals[hp] = 2;
    g_vals[spec] = 1;
    CHECK(!config_skill_allowed(x, &HERO, tv)); // holy not in list
    char v[64];
    g_vals[spec] = 3;
    g_vals[hp] = 2;
    CHECK(config_animation_variant(x, &HERO, 0, v, sizeof v) && !strcmp(v, "ret_2"));
    CHECK(config_animation_variant(x, &HERO, 1, v, sizeof v) && !strcmp(v, "ret"));
    CHECK(!config_animation_variant(x, &HERO, 2, v, sizeof v));
    g_vals[spec] = 9;
    CHECK(config_animation_variant(x, &HERO, 1, v, sizeof v) && !strcmp(v, "9")); // no enum name -> number
    g_vals[hp] = 3;
    CHECK(config_effect_variant(x, &HERO, "lod", v, sizeof v) && !strcmp(v, "lod_3"));
    CHECK(!config_effect_variant(x, &HERO, "other", v, sizeof v));
    CHECK(json_parse("{\"a\":}", err, sizeof err) == NULL && err[0]);
    // formulas
    double d;
    CHECK(expr_eval("-100% + 70% * 3", NULL, NULL, &d) && d > 1.0999 && d < 1.1001);
    CHECK(expr_eval("(2 + 3 * 2) / 4", NULL, NULL, &d) && d == 2.0);
    CHECK(!expr_eval("2 +", NULL, NULL, &d));
    // conditions and calls
    CHECK(expr_eval("1 < 2 && 3 >= 3", NULL, NULL, &d) && d == 1);
    CHECK(expr_eval("1 > 2 || !(2 == 2)", NULL, NULL, &d) && d == 0);
    CHECK(expr_eval("2 + 3 * 2 == 8", NULL, NULL, &d) && d == 1);
    CHECK(expr_eval("2 != 3", NULL, NULL, &d) && d == 1);
    CHECK(expr_eval_fn("has(\"abc\") && twice(2 + 1) == 6", NULL, test_fn, NULL, &d) && d == 1);
    CHECK(expr_eval_fn("has(\"x\")", NULL, test_fn, NULL, &d) && d == 0);
    CHECK(!expr_eval_fn("nope(1)", NULL, test_fn, NULL, &d));
    CHECK(!expr_eval("has(\"abc\")", NULL, NULL, &d)); // no calls without fn
    CHECK(!expr_eval_fn("has(\"abc\"", NULL, test_fn, NULL, &d));
    const char *jd =
        "{\"class\":\"y\",\"fields\":[\"hp\"],\"skills\":{\"tv\":{\"hp_min\":1,\"dmg\":\"-100% + 70% * hp\"}}}";
    root = json_parse(jd, err, sizeof err);
    parse_class(root, "inline2");
    json_free(root);
    const ClassConfig *y = &g_classes[2];
    g_vals[hp] = 2;
    CHECK(config_skill_dmg(y, &HERO, name_hash("tv"), &d) && d > 0.3999 && d < 0.4001);
    CHECK(!config_skill_dmg(y, &HERO, name_hash("other"), &d));
    CHECK(config_skill_allowed(y, &HERO, name_hash("tv"))); // "dmg" is not a condition
    // tooltips: [[expr|fmt]] tokens, enum names, skill and effect formulas, no hero
    const char *jt = "{\"class\":\"z\",\"fields\":[\"hp\",\"spec\"],\"enums\":{\"spec\":[\"none\",\"holy\"]},"
                     "\"skills\":{\"hw\":{\"heal\":\"4 + 5 * hp\",\"dmg\":\"-90% + 40% * hp\",\"tooltip\":[\"Heals "
                     "[[heal]]\",{\"text\":\"Next "
                     "[[heal@hp+1]]\",\"hp_max\":2,\"font\":\"tooltip_header\"}],\"tooltip_replace\":true}},"
                     "\"limits\":{\"hp\":[0,3]},\"effects\":{\"lod\":{\"heal\":\"2 + 3 * hp\"}}}";
    root = json_parse(jt, err, sizeof err);
    parse_class(root, "inline3");
    json_free(root);
    const ClassConfig *z = &g_classes[3];
    char out[256];
    g_vals[hp] = 2;
    g_vals[spec] = 1;
    CHECK(!config_tooltip_fill(z, &HERO, name_hash("hw"), NULL, 0, NULL, "no tokens", out, sizeof out));
    CHECK(config_tooltip_fill(z, &HERO, name_hash("hw"), NULL, 0, NULL, "Heal [[heal]] at [[hp]] ([[spec]])", out,
                              sizeof out) &&
          !strcmp(out, "Heal 14 at 2 (holy)"));
    CHECK(config_tooltip_fill(z, &HERO, name_hash("hw"), NULL, 0, NULL, "DMG [[dmg|+%]], LoD [[heal_lod]], [[hp*2|+]]",
                              out, sizeof out) &&
          !strcmp(out, "DMG -10%, LoD 8, +4"));
    CHECK(config_tooltip_fill(z, NULL, name_hash("hw"), NULL, 0, NULL, "[[heal]] [[spec]] [[nope]]", out, sizeof out) &&
          !strcmp(out, "4 none [[?nope]]"));
    TipVar vs[] = {{"acc", 0.85}, {"heal", 3}, {"crit", 0.04}};
    CHECK(config_tooltip_fill(z, &HERO, name_hash("hw"), vs, 3, NULL, "[[acc*100]] [[heal]] [[crit|+%]]", out,
                              sizeof out) &&
          !strcmp(out, "85 14 +4%")); // heal formula wins over the var
    CHECK(config_tooltip_fill(z, &HERO, name_hash("other"), vs, 3, NULL, "[[heal]]", out, sizeof out) &&
          !strcmp(out, "3"));
    // a line's "with" applies to tokens without their own @; limits as <field>_max / _min
    CHECK(config_tooltip_fill(z, &HERO, name_hash("hw"), NULL, 0, "hp+1",
                              "[[heal]] [[hp]]/[[hp_max]] [[dmg|+%]] [[heal@hp=0]]", out, sizeof out) &&
          !strcmp(out, "19 3/3 +30% 4"));
    CHECK(config_tooltip_fill(z, &HERO, name_hash("hw"), NULL, 0, NULL, "[[hp_min]]-[[hp_max]]", out, sizeof out) &&
          !strcmp(out, "0-3"));
    // hit range: ceil((extra + wpn * (1 + dmg + rank)) * mult); hw's dmg formula -90% + 40% * hp
    TipVar dv[] = {{"wpn_min", 6},        {"wpn_max", 12},       {"dmg_extra_min", 1}, {"dmg_extra_max", 1},
                   {"dmg_mult_min", 1.2}, {"dmg_mult_max", 1.2}, {"dmg_rank", 0}};
    g_vals[hp] = 2; // dmg = -0.1
    CHECK(
        config_tooltip_fill(z, &HERO, name_hash("hw"), dv, 7, NULL,
                            "[[hit_min]]-[[hit_max]] next [[hit_min@hp+1]]-[[hit_max@hp+1]]", out, sizeof out) &&
        !strcmp(out, "8-15 next 11-20")); // (1+5.4)*1.2=7.68, (1+10.8)*1.2=14.16; (1+7.8)*1.2=10.56, (1+15.6)*1.2=19.92
    TipOut tl[8];
    bool repl;
    int nt = config_skill_tooltip(z, &HERO, name_hash("hw"), tl, 8, &repl);
    CHECK(nt == 2 && !strcmp(tl[0].text, "Heals [[heal]]") && !strcmp(tl[1].text, "Next [[heal@hp+1]]"));
    CHECK(repl && tl[0].font == NULL && tl[1].font && !strcmp(tl[1].font, "tooltip_header"));
    g_vals[hp] = 3;
    nt = config_skill_tooltip(z, &HERO, name_hash("hw"), tl, 8, &repl);
    CHECK(nt == 1); // "hp_max": 2 line hidden at 3
    g_vals[hp] = 2;
    CHECK(config_tooltip_fill(z, &HERO, name_hash("hw"), NULL, 0, NULL,
                              "[[heal@hp+1]] [[hp@hp+1]] [[dmg@hp+1|+%]] [[heal_lod@hp=0]] [[hp@hp+5]]", out,
                              sizeof out) &&
          !strcmp(out, "19 3 +30% 2 3")); // limits clamp hp+5 to 3
    // items: only the configuring class, while the conditions hold; hidden skills
    const char *ji = "{\"class\":\"example_knight\",\"fields\":[\"spec\"],\"enums\":{\"spec\":[\"none\",\"holy\"]},"
                     "\"items\":{\"respec\":{\"spec\":\"holy\"}},\"skills\":{\"gone\":{\"hidden\":true}}}";
    root = json_parse(ji, err, sizeof err);
    parse_class(root, "inline4");
    json_free(root);
    g_vals[field_find("spec")] = 1;
    CHECK(config_item_allowed(&HERO, name_hash("respec"), false) ==
          -1); // a second example_knight config isn't in use: the first wins
    CHECK(config_item_allowed(&HERO, name_hash("bandage"), false) == -1);
    CHECK(!config_skill_allowed(&g_classes[4], &HERO, name_hash("gone")));
    // cooldowns: shared group, own-turn and actor-turn counting, reset
    const char *jc =
        "{\"class\":\"cdtest\",\"cooldowns\":{\"builder\":{\"turns\":1},\"slow\":{\"turns\":2,\"unit\":\"actor\"}},"
        "\"cooldown_tooltip\":[\"one\",\"many\"],"
        "\"skills\":{\"cs\":{\"cooldown\":\"builder\"},\"hotr\":{\"cooldown\":[\"builder\"]},\"x\":{\"cooldown\":"
        "\"slow\"}}}";
    root = json_parse(jc, err, sizeof err);
    parse_class(root, "inline5");
    json_free(root);
    const ClassConfig *cc = &g_classes[5];
    CHECK(cc->ncds == 2 && field_find("cd_builder") >= 0 && field_find("cd_slow") >= 0);
    config_cooldown_start(cc, &HERO, name_hash("cs"), true);
    CHECK(config_cooldown_left(cc, &HERO, name_hash("hotr")) == 2); // shared; the use turn still to end
    config_cooldown_tick(cc, 1, false);                             // use turn ends
    CHECK(config_cooldown_left(cc, &HERO, name_hash("cs")) == 1);   // next own turn: blocked
    CHECK(!strcmp(config_cooldown_tooltip(cc, 1), "one") && !strcmp(config_cooldown_tooltip(cc, 3), "many") &&
          !config_cooldown_tooltip(cc, 0));
    config_cooldown_tick(cc, 1, true); // actor ticks don't touch own cooldowns
    CHECK(config_cooldown_left(cc, &HERO, name_hash("cs")) == 1);
    config_cooldown_tick(cc, 1, false);
    CHECK(config_cooldown_left(cc, &HERO, name_hash("cs")) == 0); // the turn after: ready
    config_cooldown_start(cc, &HERO, name_hash("cs"), false);     // off turn (a riposte): no use turn to end
    CHECK(config_cooldown_left(cc, &HERO, name_hash("cs")) == 1); // next own turn: blocked
    config_cooldown_tick(cc, 1, false);
    CHECK(config_cooldown_left(cc, &HERO, name_hash("cs")) == 0);
    config_cooldown_start(cc, &HERO, name_hash("x"), false); // actor cooldowns: the same off turn
    CHECK(config_cooldown_left(cc, &HERO, name_hash("x")) == 3);
    config_cooldown_reset(cc, 1);
    config_cooldown_start(cc, &HERO, name_hash("x"), true);
    CHECK(config_cooldown_left(cc, &HERO, name_hash("x")) == 3);
    config_cooldown_tick(cc, 1, true);
    config_cooldown_tick(cc, 1, true);
    CHECK(config_cooldown_left(cc, &HERO, name_hash("x")) == 1); // 2 actor turns later: still 1
    config_cooldown_reset(cc, 1);
    CHECK(config_cooldown_left(cc, &HERO, name_hash("x")) == 0);
    CHECK(config_cooldown_left(cc, &HERO, name_hash("nocd")) == 0);
    CHECK(config_cooldown_turns(cc, name_hash("hotr")) == 1 && config_cooldown_turns(cc, name_hash("x")) == 2 &&
          config_cooldown_turns(cc, name_hash("nocd")) == 0);
    // guild rows: row and the skill's place in the section
    const char *jg = "{\"class\":\"gtest\",\"skills\":{\"a\":{\"guild_row\":2,\"guild_first_rank\":2},\"b\":{},\"c\":{"
                     "\"guild_row\":1}}}";
    root = json_parse(jg, err, sizeof err);
    parse_class(root, "inline_g");
    json_free(root);
    const ClassConfig *gc = config_class_at(g_nclasses - 1);
    int go = -1;
    CHECK(config_guild_row(gc, name_hash("a"), &go) == 2 && go == 0);
    CHECK(config_guild_row(gc, name_hash("b"), &go) == 0 && go == 1);
    CHECK(config_guild_row(gc, name_hash("c"), &go) == 1 && go == 2);
    CHECK(config_guild_row(gc, name_hash("zz"), &go) == 0 && go == 3);
    CHECK(config_guild_first_rank(gc, name_hash("a")) == 2 && config_guild_first_rank(gc, name_hash("b")) == 1 &&
          config_guild_first_rank(gc, name_hash("zz")) == 1);
    // base tooltips: no "with" / "base": false lines, conditions at the override, spender range from hp_min to the limit
    const char *jb =
        "{\"class\":\"btest\",\"fields\":[\"hp\"],\"limits\":{\"hp\":[0,3]},\"skills\":{\"spend\":{\"hp_min\":1,"
        "\"tooltip\":[\"s\",{\"text\":\"n\",\"hp_max\":2,\"base\":false},{\"text\":\"w\",\"with\":\"hp+1\"},{\"text\":"
        "\"full\",\"hp_min\":3}]},"
        "\"build\":{\"tooltip\":[\"g\"]}}}";
    root = json_parse(jb, err, sizeof err);
    parse_class(root, "inline_b");
    json_free(root);
    const ClassConfig *bc = config_class_at(g_nclasses - 1);
    int bf, blo, bhi;
    CHECK(config_skill_base_range(bc, name_hash("spend"), &bf, &blo, &bhi) && bf == field_find("hp") && blo == 1 &&
          bhi == 3);
    CHECK(!config_skill_base_range(bc, name_hash("build"), &bf, &blo, &bhi));
    TipOut bo[8];
    bool brep;
    CHECK(config_skill_tooltip_base(bc, name_hash("spend"), bf, 2, bo, 8, &brep) == 1 && !strcmp(bo[0].text, "s"));
    CHECK(config_skill_tooltip_base(bc, name_hash("spend"), bf, 3, bo, 8, &brep) == 2 && !strcmp(bo[1].text, "full"));
    // sheet pages (else the guild row) and centred Guild rows
    const char *jp =
        "{\"class\":\"ptest\",\"guild_centred_rows\":[1,3],\"skills\":{\"s\":{\"guild_row\":1,\"sheet_page\":1},"
        "\"a\":{\"guild_row\":2,\"sheet_page\":1},\"r\":{\"guild_row\":4}}}";
    root = json_parse(jp, err, sizeof err);
    parse_class(root, "inline_p");
    json_free(root);
    const ClassConfig *pc = config_class_at(g_nclasses - 1);
    CHECK(config_sheet_page(pc, name_hash("a")) == 1 && config_sheet_page(pc, name_hash("r")) == 4 &&
          config_sheet_page(pc, name_hash("x")) == 0);
    CHECK(config_guild_row_centred(pc, 1) && config_guild_row_centred(pc, 3) && !config_guild_row_centred(pc, 2));
    // a file with only global sections: no class, no "missing class" complaint; act_outs.allow_back_to_back
    CHECK(!config_act_outs()->allow_back_to_back && !config_act_outs()->use_max);
    char gpath[] = "/tmp/test_config_globals_XXXXXX";
    int fd = mkstemp(gpath);
    FILE *gf = fd >= 0 ? fdopen(fd, "w") : NULL;
    CHECK(gf != NULL);
    if (gf) {
        fputs("{\"act_outs\": {\"allow_back_to_back\": true, \"use_max\": true, \"per_source\": true, "
              "\"max_per_turn\": 2, \"rules\": [{\"source\": \"fearful\", \"act_out\": "
              "\"ignore_command\", \"when\": \"torch < 25\", \"weight\": 3}, {\"weight\": \"1 + round / 10\"}, "
              "{\"source\": \"x\"}], \"prerequisites\": {\"disease_vampire_crave\": {\"match\": \"any\", "
              "\"requires\": [{\"source\": \"abusive\", \"within\": \"battle\", \"nothing_counts\": true}, "
              "\"fearful\"]}}, "
              "\"types\": {\"heal_lowest_ally\": {\"like\": \"heal_self\", \"outcome\": \"heal\", \"target\": "
              "\"party_lowest_hp\"}, "
              "\"comfort\": {\"result\": 0, \"bark_chance\": 0.5, \"outcome\": \"effect\"}}}}",
              gf);
        fclose(gf);
        int before = g_nclasses;
        load_file(gpath);
        CHECK(g_nclasses == before);
        CHECK(config_act_outs()->allow_back_to_back);
        CHECK(config_act_outs()->use_max && config_act_outs()->per_source && config_act_outs()->max_per_turn == 2);
        const ActOutConfig *ac = config_act_outs(); // the third rule has no weight: skipped
        CHECK(ac->nrules == 2);
        CHECK(!strcmp(ac->rules[0].source, "fearful") && !strcmp(ac->rules[0].act_out, "ignore_command"));
        CHECK(!strcmp(ac->rules[0].when, "torch < 25") && !strcmp(ac->rules[0].weight, "3"));
        CHECK(!strcmp(ac->rules[1].source, "*") && !strcmp(ac->rules[1].act_out, "*") && !ac->rules[1].when[0]);
        CHECK(!strcmp(ac->rules[1].weight, "1 + round / 10"));
        CHECK(ac->nprereqs == 1 && !strcmp(ac->prereqs[0].source, "disease_vampire_crave") && ac->prereqs[0].any);
        CHECK(ac->prereqs[0].n == 2 && !strcmp(ac->prereqs[0].req[0].source, "abusive"));
        CHECK(ac->prereqs[0].req[0].within == ACT_OUT_WITHIN_BATTLE && ac->prereqs[0].req[0].nothing_counts);
        CHECK(!strcmp(ac->prereqs[0].req[1].source, "fearful") && ac->prereqs[0].req[1].within == ACT_OUT_WITHIN_TURN);
        CHECK(!ac->prereqs[0].req[1].nothing_counts);
        CHECK(ac->ntypes == 2 && !strcmp(ac->types[0].id, "heal_lowest_ally") &&
              !strcmp(ac->types[0].like, "heal_self"));
        CHECK(!strcmp(ac->types[0].outcome, "heal") && !strcmp(ac->types[0].target, "party_lowest_hp"));
        CHECK(ac->types[0].result == -1 && ac->types[0].bark_chance < 0 && ac->types[0].burndown < 0);
        CHECK(!strcmp(ac->types[1].target, "self") && ac->types[1].result == 0 && ac->types[1].bark_chance == 0.5f);
        remove(gpath);
    }

    // two mods configure one class and the settings: the mod higher in the campaign's list wins; files outside any
    // mod (the ones above) always apply
    static const char *const prio[2] = {"{\"class\": \"prio_class\", \"riposte\": {\"skills\": [\"melee\"]}, "
                                        "\"act_outs\": {\"chances\": {\"fearful\": {\"a\": 1}}}}",
                                        "{\"class\": \"prio_class\", \"riposte\": {\"skills\": [\"ranged\", "
                                        "\"melee\"]}, \"act_outs\": {\"chances\": {\"fearful\": {\"b\": 2}}}}"};
    for (int m = 0; m < 2; m++) {
        char p[] = "/tmp/test_config_prio_XXXXXX";
        int pfd = mkstemp(p);
        FILE *pf = pfd >= 0 ? fdopen(pfd, "w") : NULL;
        CHECK(pf != NULL);
        if (!pf)
            continue;
        fputs(prio[m], pf);
        fclose(pf);
        g_test_mod = m;
        load_file(p);
        remove(p);
    }
    g_test_mod = -1;
    unsigned ph = name_hash("prio_class");
    g_test_pos[0] = 0;
    g_test_pos[1] = 1;
    on_campaign();
    CHECK(config_for_class_hash(ph) && config_for_class_hash(ph)->mod == 0);
    CHECK(config_riposte()->ntypes == 1);
    CHECK(config_act_outs()->ntraits == 1 && !strcmp(config_act_outs()->traits[0].w[0].type, "a"));
    CHECK(config_act_outs()->ntypes == 2); // from the file outside any mod
    g_test_pos[0] = 1;
    g_test_pos[1] = 0;
    on_campaign();
    CHECK(config_for_class_hash(ph) && config_for_class_hash(ph)->mod == 1);
    CHECK(config_riposte()->ntypes == 2);
    CHECK(config_act_outs()->ntraits == 1 && !strcmp(config_act_outs()->traits[0].w[0].type, "b"));
    g_test_pos[0] = -1;
    on_campaign();
    CHECK(config_for_class_hash(ph) && config_for_class_hash(ph)->mod == 1);
    g_test_pos[1] = -1; // no campaign
    on_campaign();
    CHECK(!config_for_class_hash(ph) && !config_riposte()->ntypes && !config_act_outs()->ntraits);
    CHECK(config_act_outs()->ntypes == 2 && config_for_hero(&HERO));

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
