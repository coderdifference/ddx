// Offline tests for a registry's campaign order (mods_list.c), on tests/mods_stub.h's mods 0..3.
#include <stdio.h>
#include <string.h>
#include "../src/common.h"
#include "mods_stub.h"

static int g_failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                                         \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static ModsList g_list;
static int g_entry[8]; // each entry's owner, as the registry's own array would hold

static void add(int owner) {
    g_mod_current = owner;
    int i = mods_list_add(&g_list);
    g_entry[i] = owner;
    g_mod_current = -1;
}

// the active entries' owners in dispatch order, as a string ("L01": the library's, then mod 0's, then mod 1's)
static const char *order(bool top_first) {
    static char s[16];
    int n = 0;
    MODS_EACH(g_list, top_first, i) s[n++] = g_entry[i] < 0 ? 'L' : (char)('0' + g_entry[i]);
    s[n] = 0;
    return s;
}

int main(void) {
    add(1);
    add(-1);
    add(0);
    add(1);
    CHECK(!strcmp(order(false), "L")); // no campaign: the library's only
    CHECK(mods_list_first(&g_list) == 1);
    campaign(1, 0, -1, -1);               // mod 1 on top
    CHECK(!strcmp(order(false), "L011")); // lowest first, the top mod's last
    CHECK(!strcmp(order(true), "110L"));  // the top mod's first, the library's last
    CHECK(mods_list_first(&g_list) == 0); // entry 0: mod 1's first
    CHECK(mods_list_active(&g_list, 2) && mods_list_active(&g_list, 1));
    campaign(0, -1, -1, -1); // mod 1 not applied: cached order rebuilt
    CHECK(!strcmp(order(true), "0L"));
    CHECK(!mods_list_active(&g_list, 0) && !mods_list_active(&g_list, 3));
    add(0); // a registration rebuilds it too
    CHECK(!strcmp(order(true), "00L"));
    CHECK(mods_order(g_list.owner, g_list.n, false, (int[8]){0}) == 3);
    for (int i = 0; i < MODS_LIST_MAX; i++)
        mods_list_add(&g_list);
    CHECK(mods_list_add(&g_list) == -1 && g_list.n == MODS_LIST_MAX); // full
    printf(g_failures ? "%d failure(s)\n" : "mods list: all passed\n", g_failures);
    return g_failures != 0;
}
