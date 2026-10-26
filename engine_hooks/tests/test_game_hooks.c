// Offline test for shared game hooks (game_hooks.c), built for x86_64 and run (Rosetta on Apple silicon): the
// catalogue's symbols resolve to local functions and install_hook_rip is faked (it returns the "game" function as the
// trampoline and keeps the detour), so calling the detour runs the real stubs and the wrapper chain.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../src/common.h"
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

typedef long (*Fn)(long, long);
__attribute__((noinline)) static long game(long a, long b) {
    return a * 100 + b;
} // the "game's" function
static uint8_t g_code[0x400]; // the symbol's bytes (sites: code + offset)
static void *g_detour;
static const void *g_target;
static int g_installs;

void *sym(const char *mangled) {
    (void)mangled;
    return g_code;
}
void *install_hook_rip(const char *name, void *target, void *detour, const uint8_t *expected, size_t len,
                       const HookRip *rip, size_t nrip) {
    (void)name;
    (void)expected;
    (void)len;
    (void)rip;
    (void)nrip;
    g_installs++;
    g_target = target;
    g_detour = detour;
    return (void *)game;
}

static Fn g_next1, g_next2;
static long wrap1(long a, long b) {
    return g_next1(a, b) + 1;
} // innermost: game + 1
static long wrap2(long a, long b) {
    return g_next2(a + 1, b) * 2;
} // outermost: changes an argument too

// owned wrappers on another function: each appends its digit to the result (library 7, mod 1, mod 2)
static Fn g_nl, g_na, g_nb;
static long wrap_l(long a, long b) {
    return g_nl(a, b) * 10 + 7;
}
static long wrap_a(long a, long b) {
    return g_na(a, b) * 10 + 1;
}
static long wrap_b(long a, long b) {
    return g_nb(a, b) * 10 + 2;
}

static long g_seen_rdi, g_seen_rsi;
static int g_order;
static void observer1(const Regs *r) {
    g_seen_rdi = (long)r->rdi;
    g_seen_rsi = (long)r->rsi;
    g_order = g_order * 10 + 1;
}
static void observer2(const Regs *r) {
    (void)r;
    g_order = g_order * 10 + 2;
}
static void observer3(const Regs *r) {
    (void)r;
    g_order = g_order * 10 + 3;
}

int main(void) {
    // unknown ids and the wrong kind are refused
    void *next = (void *)1;
    CHECK(!game_hook_wrap("No::Such", (void *)wrap1, &next) && !next);
    CHECK(!game_hook_wrap("EstateSummary currency entry", (void *)wrap1, &next));
    CHECK(!game_hook_site("Actor::UpdateTrinketBuffs", observer1));
    CHECK(g_installs == 0);

    // one wrapper: the hook goes in, next is the game
    CHECK(game_hook_wrap("Actor::UpdateTrinketBuffs", (void *)wrap1, (void **)&g_next1));
    CHECK(g_installs == 1 && g_target == g_code && g_next1 == game);
    Fn hooked = (Fn)g_detour; // what the game's patched entry jumps to
    CHECK(hooked(3, 4) == 305);
    // a second wrapper chains in front of the first, without a second hook
    CHECK(game_hook_wrap("Actor::UpdateTrinketBuffs", (void *)wrap2, (void **)&g_next2));
    CHECK(g_installs == 1 && g_next2 == wrap1);
    CHECK(hooked(3, 4) == 2 * (game(4, 4) + 1)); // wrap2 -> wrap1 -> the game

    // a site: observers see the registers, in order, then the game's code runs on with them unchanged
    CHECK(game_hook_site("EstateSummary currency entry", observer1));
    CHECK(g_installs == 2 && g_target == g_code + 0x31b);
    CHECK(game_hook_site("EstateSummary currency entry", observer2));
    CHECK(g_installs == 2);
    Fn site = (Fn)g_detour;
    CHECK(site(7, 9) == 709);
    CHECK(g_seen_rdi == 7 && g_seen_rsi == 9 && g_order == 12);

    campaign(-1, 0, -1, -1); // a mod's observer runs only while it's applied
    g_mod_current = 1;
    CHECK(game_hook_site("EstateSummary currency entry", observer3));
    g_mod_current = -1;
    g_order = 0;
    CHECK(site(7, 9) == 709 && g_order == 123);
    campaign(-1, -1, -1, -1);
    g_order = 0;
    CHECK(site(7, 9) == 709 && g_order == 12);

    // owners: the chain is the library's wrappers innermost, then the campaign's mods lowest in its list first (the
    // top mod's outermost); a mod not applied is left out, and the chain is relinked at each campaign
    g_mod_current = 1;
    CHECK(game_hook_wrap("Actor::GetAdditionalResistance", (void *)wrap_a, (void **)&g_na));
    Fn chain = (Fn)g_detour;
    CHECK(chain(0, 5) == 5); // no campaign: mod 1's isn't linked
    g_mod_current = -1;
    CHECK(game_hook_wrap("Actor::GetAdditionalResistance", (void *)wrap_l, (void **)&g_nl));
    g_mod_current = 2;
    CHECK(game_hook_wrap("Actor::GetAdditionalResistance", (void *)wrap_b, (void **)&g_nb));
    g_mod_current = -1;
    CHECK(chain(0, 5) == 57);
    campaign(-1, 0, 1, -1); // mod 1 at the top: 1(2(L(game)))
    CHECK(chain(0, 5) == 5721);
    campaign(-1, 1, 0, -1); // mod 2 at the top
    CHECK(chain(0, 5) == 5712);
    campaign(-1, -1, 0, -1);
    CHECK(chain(0, 5) == 572);
    campaign(-1, -1, -1, -1);
    CHECK(chain(0, 5) == 57);
    CHECK(hooked(3, 4) == 2 * (game(4, 4) + 1)); // the library's chain above is untouched

    if (!g_failures)
        puts("game hooks: all passed");
    return g_failures != 0;
}
