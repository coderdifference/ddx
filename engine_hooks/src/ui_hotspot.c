// The one owner of GUI_HotSpot (an invisible clickable rect: a tab, a slot's drop area), for clients that skip a hot
// spot or add to it (ui_on_hotspot). See common.h ("ui_hotspot.c").
//
// GUI_HotSpot(HotSpotParameters const&, std::function<void()> x4): the id (u64) at params+0x08, the position (vec2) at
// +0x18 (the same base as ImageButtonParameters). By-value std::function arguments are the caller's temporaries
// (Itanium ABI: the caller destroys them), so skipping the call leaks nothing; callers don't use its result (the
// dungeon panels' tab hot spots, Panel_Inventory::Show / Panel_Map::Show, destroy the functions right after).
// Windows (MSVC): GUI_HotSpot(result, params, f1..f4), its 5-byte input state through the hidden pointer, and the
// callee destroys the four functions (each _Func_base* at +0x38, _Delete_this(not in its own buffer) in vtable slot 4),
// so a skipped call clears the result and destroys them in its place.
#include "common.h"
#include <string.h>

#define MAX_CLIENTS 8

static struct {
    UiHotspotBefore before;
    UiHotspotAfter after;
} g_clients[MAX_CLIENTS];
static int g_n;
static ModsList g_mods; // the clients' mods (only an applied mod's run)
#ifndef _WIN32
static uint64_t (*o_hotspot)(uint8_t *params, void *f1, void *f2, void *f3, void *f4);
#endif

void ui_on_hotspot(UiHotspotBefore before, UiHotspotAfter after) {
    if (g_n < MAX_CLIENTS && mods_list_add(&g_mods) >= 0) {
        g_clients[g_n].before = before;
        g_clients[g_n].after = after;
        g_n++;
    } else
        logf_("ui_hotspot: too many clients");
}

static const void *g_caller;
const void *ui_hotspot_caller(void) {
    return g_caller;
}

#ifdef _WIN32
static uint8_t *(*o_hotspot_win)(uint8_t *result, uint8_t *params, StdFunction *f1, StdFunction *f2, StdFunction *f3,
                                 StdFunction *f4);

static uint8_t *h_hotspot(uint8_t *result, uint8_t *params, StdFunction *f1, StdFunction *f2, StdFunction *f3,
                          StdFunction *f4) {
    g_caller = __builtin_return_address(0);
    MODS_EACH(g_mods, false, i)
    if (g_clients[i].before && params && !g_clients[i].before(params)) {
        memset(result, 0, 5);
        ui_function_destroy(f1);
        ui_function_destroy(f2);
        ui_function_destroy(f3);
        ui_function_destroy(f4);
        return result;
    }
    uint8_t *r = o_hotspot_win(result, params, f1, f2, f3, f4);
    MODS_EACH(g_mods, false, i) if (g_clients[i].after && params) g_clients[i].after(params);
    return r;
}
#else
static uint64_t h_hotspot(uint8_t *params, void *f1, void *f2, void *f3, void *f4) {
    g_caller = __builtin_return_address(0);
    MODS_EACH(g_mods, false, i)
    if (g_clients[i].before && params && !g_clients[i].before(params))
        return 0;
    uint64_t r = o_hotspot(params, f1, f2, f3, f4);
    MODS_EACH(g_mods, false, i) if (g_clients[i].after && params) g_clients[i].after(params);
    return r;
}
#endif

#ifdef _WIN32
bool ui_hotspot_ready(void) {
    return o_hotspot_win != NULL;
}
#else
bool ui_hotspot_ready(void) {
    return o_hotspot != NULL;
}
#endif

void ui_hotspot_install(void) {
    static const uint8_t prologue[] = PROLOGUE_SUB32(0x18, 0x05); // sub rsp,0x518
#ifdef _WIN32
    *(void **)&o_hotspot_win = install_hook("GUI_HotSpot",
#else
    *(void **)&o_hotspot = install_hook("GUI_HotSpot",
#endif
                                            sym("_Z11GUI_HotSpotRK17HotSpotParametersNSt3__18functionIFvvEEES5_S5_S5_"),
                                            (void *)h_hotspot, prologue, sizeof prologue);
}
