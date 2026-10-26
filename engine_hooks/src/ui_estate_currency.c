// Client-provided placements of currency entries in the estate summary.
#include "common.h"
#include <string.h>
static CurrencyPlacement providers[8];
static unsigned nproviders;
static struct {
    uint8_t *cls;
    uint8_t heirloom;
    float x;
} drawing[8];
static unsigned ndrawing;
static bool moved;
static Vec2 saved;
static void (*next_summary)(void *);
static void packed(float *x, float *y) {
#ifdef _WIN32
    Regs *r = game_site_regs();
    if (r) {
        unsigned a, b;
        memcpy(&a, x, 4);
        memcpy(&b, y, 4);
        r->rbx = (uint64_t)b << 32 | a;
    }
#endif
}
static void entry(const Regs *regs) {
    if (!ndrawing)
        return;
    const Regs *r = regs;
#ifdef _WIN32
    float *x = game_site_xmm(7), *y = game_site_xmm(6);
    uint8_t *cls = (uint8_t *)r->rsi;
#else
    float *x = (float *)(r->rbp - 0xb00), *y = x + 1;
    uint8_t *cls = (uint8_t *)r->rax;
#endif
    if (!x || !y)
        return;
    if (moved) {
        *x = saved.x;
        *y = saved.y;
        moved = false;
        packed(x, y);
    }
    for (unsigned i = 0; i < ndrawing; i++)
        if (cls == drawing[i].cls && cls[CURRENCY_IN_SUMMARY]) {
            saved = (Vec2){*x, *y};
            *x = drawing[i].x;
            moved = true;
            packed(x, y);
            break;
        }
}
static void summary(void *panel) {
    static void **library;
    static unsigned (*count)(void *);
    static uint8_t *(*at)(void *, unsigned);
    if (!library)
        library = sym("_ZN6Estate11SystemClass10s_InstanceE");
    if (!count)
        *(void **)&count = sym("_ZNK6Estate11SystemClass26GetNumberOfCurrencyClassesEv");
    if (!at)
        *(void **)&at = sym("_ZNK6Estate11SystemClass25GetCurrencyClassFromIndexEj");
    ndrawing = 0;
    moved = false;
    if (library && *library && count && at)
        for (unsigned p = 0; p < nproviders; p++) {
            unsigned hash = 0;
            float x = 0;
            if (!providers[p](&hash, &x))
                continue;
            for (unsigned i = 0; i < count(*library); i++) {
                uint8_t *cls = at(*library, i);
                if (!cls || *(unsigned *)(cls + CURRENCY_HASH) != hash)
                    continue;
                drawing[ndrawing].cls = cls;
                drawing[ndrawing].heirloom = cls[CURRENCY_HEIRLOOM];
                drawing[ndrawing++].x = x;
                cls[CURRENCY_HEIRLOOM] = 1;
                break;
            }
        }
    next_summary(panel);
    for (unsigned i = 0; i < ndrawing; i++)
        drawing[i].cls[CURRENCY_HEIRLOOM] = drawing[i].heirloom;
    ndrawing = 0;
}
bool ui_estate_currency_place(CurrencyPlacement fn) {
    if (!fn || nproviders == 8)
        return false;
    if (!next_summary) {
        if (!game_hook_site("EstateSummary currency entry", entry))
            return false;
        if (!game_hook_wrap("EstateSummary::PreChildrenShowInternal", (void *)summary, (void **)&next_summary))
            return false;
    }
    providers[nproviders++] = fn;
    return true;
}
