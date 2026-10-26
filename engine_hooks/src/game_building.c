// Town building object/ABI adapters. Providers supply content and positions, never game memory layouts.
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
bool game_building_upgrades_open(void *panel) {
    static bool (*active)(const void *);
    if (!active)
        *(void **)&active = sym("_ZNK2UI5Panel4Base11GetIsActiveEv");
    void ***v = panel ? (void ***)((uint8_t *)panel + PANEL_TREES) : NULL;
    if (v && v[0] && v[1] >= v[0] && active)
        for (void **p = v[0]; p < v[1]; p++)
            if (*p && active(*p))
                return true;
    return false;
}
void game_building_move_hero_slot(void *panel, Vec2 delta) {
    static void *(*assets)(void);
    static void (*position)(void *, Vec2);
    if (!assets)
        *(void **)&assets = sym("_ZN6TownUI6Assets7CurrentEv");
    if (!position)
        *(void **)&position = sym("_ZN2UI5Panel4Base10SetBasePosE4vec2");
    void *slot = panel ? *(void **)((uint8_t *)panel + PANEL_HERO_SLOT) : NULL;
    uint8_t *a = assets ? assets() : NULL;
    if (slot && a && position) {
        Vec2 v = *(Vec2 *)(a + ASSETS_HERO_SLOT_POS);
        position(slot, (Vec2){v.x + delta.x, v.y + delta.y});
    }
}
void *game_building_closure_hero(void *closure) {
    return closure ? *(void **)((uint8_t *)closure + 8) : NULL;
}
bool ui_hero_action_text_site(const void *caller) {
    static const uint8_t *sites[4];
    static bool ready;
    if (!ready) {
        ready = true;
        void *fn = sym("_ZN6TownUI5Panel17HeroActionDisplay27PreChildrenShowBodyInternalEv");
        unsigned offsets[4] = {GL(0xd0e, 0x4fa), GL(0x104b, 0x5d8), GL(0x7d7, 0x2e9), GL(0x475, 0x651)};
        for (int i = 0; i < 4; i++)
            sites[i] = call_site_return(fn, offsets[i] - 5, NULL);
    }
    for (int i = 0; i < 4; i++)
        if (caller && caller == sites[i])
            return true;
    return false;
}
struct BuildingDescriptions {
    int count;
    char lines[16][256];
};
bool ui_building_description_add(BuildingDescriptions *out, const char *text) {
    if (!out || !text || out->count == 16)
        return false;
    snprintf(out->lines[out->count++], 256, "%s", text);
    return true;
}
static bool (*description_provider)(unsigned, int, BuildingDescriptions *);
#ifdef _WIN32
static void *(*next_description)(void *, void *, uint64_t);
static void *description(void *self, void *out, uint64_t requirement) {
#else
static void *(*next_description)(void *, void *, uint64_t);
static void *description(void *out, void *self, uint64_t requirement) {
#endif
    BuildingDescriptions result = {0};
    static void *(*allocate)(size_t);
    if (!allocate)
        *(void **)&allocate = sym("_Znwm");
    if (allocate && description_provider((unsigned)requirement, (int)(uint8_t)(requirement >> 32) - 'a', &result)) {
        void **v = out;
        v[0] = v[1] = v[2] = NULL;
        if (result.count) {
            size_t bytes = (size_t)result.count * 256;
            // The game owns the returned vector and frees it with its C++ allocator.
            v[0] = allocate(bytes);
            memcpy(v[0], result.lines, bytes);
            v[1] = v[2] = (uint8_t *)v[0] + bytes;
        }
        return out;
    }
#ifdef _WIN32
    return next_description(self, out, requirement);
#else
    return next_description(out, self, requirement);
#endif
}
bool ui_building_descriptions(bool (*provider)(unsigned, int, BuildingDescriptions *)) {
    if (!provider || description_provider)
        return false;
    if (!game_hook_wrap("BlacksmithDisplay::GetUpgradeDescriptionList", (void *)description,
                        (void **)&next_description))
        return false;
    description_provider = provider;
    return true;
}
static bool (*offset_provider)(void *, Vec2 *, Vec2 *);
#ifdef _WIN32
static void *(*next_offset)(void *, Vec2 *, unsigned);
static Vec2 original_offset(void *panel, unsigned i) {
    Vec2 out = {0};
    next_offset(panel, &out, i);
    return out;
}
#else
static Vec2 (*next_offset)(void *, unsigned);
static Vec2 original_offset(void *panel, unsigned i) {
    return next_offset(panel, i);
}
#endif
static Vec2 offset(void *panel, unsigned i) {
    static void *(*assets)(void);
    if (!assets)
        *(void **)&assets = sym("_ZN6TownUI6Assets7CurrentEv");
    uint8_t *a = assets ? assets() : NULL;
    if (!a)
        return original_offset(panel, i);
    Vec2 *v = (Vec2 *)(a + ASSETS_TREES_OFFSET), *s = (Vec2 *)(a + ASSETS_TREES_SPACING), oldv = *v, olds = *s,
         newv = *v, news = *s;
    if (v->x != 0 || s->x != 0 || v->y <= 0 || s->y <= 0 || v->y >= 1000 || s->y >= 1000 ||
        !offset_provider(panel, &newv, &news))
        return original_offset(panel, i);
    *v = newv;
    *s = news;
    Vec2 out = original_offset(panel, i);
    *v = oldv;
    *s = olds;
    return out;
}
#ifdef _WIN32
static void *offset_win(void *panel, Vec2 *out, unsigned i) {
    *out = offset(panel, i);
    return out;
}
#endif
bool ui_building_tree_offsets(bool (*provider)(void *, Vec2 *, Vec2 *)) {
    if (!provider || offset_provider)
        return false;
#ifdef _WIN32
    void *fn = (void *)offset_win;
#else
    void *fn = (void *)offset;
#endif
    if (!game_hook_wrap("BuildingDisplay::GetChildPanelOffset", fn, (void **)&next_offset))
        return false;
    offset_provider = provider;
    return true;
}
