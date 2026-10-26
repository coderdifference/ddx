// Offline tests for the hook point registry (hooks.c), with a test-only point that has both phases.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define HOOKS_TEST_POINTS
#include "../src/hooks.c"
#include "mods_stub.h"

bool g_debug;
void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static int g_failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                                         \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static char g_order[64];
static void note(HookContext *c, void *ud) {
    strncat(g_order, ud, sizeof g_order - strlen(g_order) - 1);
    (void)c;
}
static void halve(HookContext *c, void *ud) {
    (void)ud;
    c->amount *= 0.5f;
}
static void cancel_big(HookContext *c, void *ud) {
    (void)ud;
    if (c->amount > 10)
        c->cancel = true;
}
static float g_seen;
static void see(HookContext *c, void *ud) {
    (void)ud;
    g_seen = c->amount;
}

int main(void) {
    // the catalogue
    CHECK(hook_point_exists("hit") && hook_point_exists("raid_start") && !hook_point_exists("nope"));
    int hit = -1;
    for (int i = 0; i < hook_point_count(); i++)
        if (!strcmp(hook_point_name(i), "hit"))
            hit = i;
    CHECK(hit >= 0 && hook_point_has_phase(hit, HOOK_AFTER) && !hook_point_has_phase(hit, HOOK_BEFORE));
    CHECK(hook_point_about(hit) && !hook_point_name(-1) && !hook_point_name(hook_point_count()));

    // registering: unknown points, missing phases and bad arguments are refused
    CHECK(!hook_on("nope", HOOK_AFTER, note, "x"));
    CHECK(!hook_on("hit", HOOK_BEFORE, note, "x"));
    CHECK(!hook_on("hit", HOOK_AFTER, NULL, NULL));
    CHECK(!hook_on(NULL, HOOK_AFTER, note, "x"));

    // wanted: only points with their own listeners ("*" doesn't count), or everything in debug
    CHECK(!hook_wanted("hit") && !hook_wanted("test_value"));
    CHECK(hook_on("*", HOOK_AFTER, note, "*"));
    CHECK(!hook_wanted("hit"));
    CHECK(hook_on("hit", HOOK_AFTER, note, "a") && hook_on("hit", HOOK_AFTER, note, "b"));
    CHECK(hook_wanted("hit") && !hook_wanted("miss"));
    static const char *const both[] = {"miss", "hit"}, *const none[] = {"miss", "kill"};
    CHECK(hook_any_wanted(both, 2) && !hook_any_wanted(none, 2));
    g_debug = true;
    CHECK(hook_wanted("miss"));
    g_debug = false;

    // dispatch: the point's listeners in order, then "*"; point and phase are set
    HookContext c = {.amount = 3};
    CHECK(hook_emit("hit", HOOK_AFTER, &c));
    CHECK(!strcmp(g_order, "ab*") && !strcmp(c.point, "hit") && c.phase == HOOK_AFTER);
    g_order[0] = 0;
    CHECK(hook_emit("miss", HOOK_AFTER, &c) && !strcmp(g_order, "*"));
    g_order[0] = 0;
    CHECK(hook_emit("nope", HOOK_AFTER, &c) && !g_order[0]); // undeclared: nothing runs
    CHECK(hook_emit("hit", HOOK_BEFORE, &c) && !g_order[0]); // a phase the point lacks: nothing runs

    // before: listeners change the context in order; "*" sees the result; cancel stops the game
    CHECK(hook_on("test_value", HOOK_BEFORE, halve, NULL) && hook_on("test_value", HOOK_BEFORE, cancel_big, NULL));
    CHECK(hook_on("*", HOOK_BEFORE, see, NULL));
    HookContext v = {.amount = 8, .cancel = true}; // a stale cancel is cleared
    CHECK(hook_emit("test_value", HOOK_BEFORE, &v) && v.amount == 4 && g_seen == 4);
    v = (HookContext){.amount = 40};
    CHECK(!hook_emit("test_value", HOOK_BEFORE, &v) && v.amount == 20 && v.cancel);
    v.cancel = true;
    CHECK(hook_emit("test_value", HOOK_AFTER, &v)); // after never cancels

    // hook_on_all: every point is wanted, and it runs with the "*" listeners
    CHECK(!hook_wanted("kill"));
    g_order[0] = 0;
    CHECK(hook_on_all(HOOK_AFTER, note, "!"));
    CHECK(hook_wanted("kill") && hook_wanted("raid_start"));
    CHECK(hook_emit("hit", HOOK_AFTER, &c) && !strcmp(g_order, "ab*!"));

    // owners: a mod's listeners run only while the campaign applies it; the library's first, then the mods from the
    // lowest in the campaign's list to the highest (the top mod's before changes stand)
    g_mod_current = 1;
    CHECK(hook_on("kill", HOOK_AFTER, note, "1"));
    g_mod_current = 2;
    CHECK(hook_on("kill", HOOK_AFTER, note, "2"));
    g_mod_current = -1;
    CHECK(hook_on("kill", HOOK_AFTER, note, "L"));
    g_order[0] = 0;
    CHECK(hook_emit("kill", HOOK_AFTER, &c) && !strcmp(g_order, "L*!")); // no campaign: the library's only
    campaign(-1, 0, 1, -1);                                              // mod 1 at the top
    g_order[0] = 0;
    CHECK(hook_emit("kill", HOOK_AFTER, &c) && !strcmp(g_order, "L21*!"));
    campaign(-1, 1, 0, -1); // mod 2 at the top
    g_order[0] = 0;
    CHECK(hook_emit("kill", HOOK_AFTER, &c) && !strcmp(g_order, "L12*!"));
    campaign(-1, -1, 0, -1); // mod 1 not applied
    g_order[0] = 0;
    CHECK(hook_emit("kill", HOOK_AFTER, &c) && !strcmp(g_order, "L2*!"));
    campaign(-1, -1, -1, -1);
    g_order[0] = 0;
    CHECK(hook_emit("kill", HOOK_AFTER, &c) && !strcmp(g_order, "L*!"));

    // the listener table has a limit
    int added = 0;
    while (hook_on("miss", HOOK_AFTER, note, "") && added < 1000)
        added++;
    CHECK(added < 1000 && !hook_on("kill", HOOK_AFTER, note, ""));

    if (g_failures)
        printf("%d failure(s)\n", g_failures);
    else
        printf("all passed\n");
    return g_failures != 0;
}
