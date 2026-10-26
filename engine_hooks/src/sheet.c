// Character sheet combat skills, paged (classes with an engine config): one page per skills' "sheet_page" (else
// their "guild_row"), in config order, then the other skills a row at a time; a page with more than a row of
// skills continues on the next; "hidden" skills and skills the
// hero doesn't own yet (the Guild shows those) left out, and empty pages with them.
// Prev / next tabs (the class's skill_pages icons) either side of the row page through, wrapping; disabled
// when there's one page. Vanilla classes are untouched.
//
// UI::Panel::CharacterDisplay::ShowCombatSkillGrid(this, vec2 pos, Hero*, HeroClass*) hands
// ShowSkillGrid (which draws slot i at layout .starting_pos + (i % .max_rows, i / .max_rows) * .spacing and
// reads skills only through these) seven lambdas: $_0 count(class), $_1 level(hero, i) (< 0: not owned),
// $_2 icon(class, i, level), $_3 selected(hero, i), $_4 selection valid(hero), $_5 toggle(hero, i) (also
// tweens the slot's SkillRenderData[i]), $_6 tooltip(hero, i, pos, f) (tooltip.c hooks it and calls
// sheet_skill_index). While a configured hero's grid draws, the count is the page's and every index is
// mapped from the page slot to the class skill. Levels count from the skill's "guild_first_rank": a
// placeholder level shows as not owned.
#include "common.h"
#include <string.h>

#define ICON_SIZE  72.0f
#define TAB_W      20.0f
#define TAB_GAP    4.0f
#define MAX_SKILLS 48

static void *(*g_campaign_assets)(void);
static int (*g_skill_level)(const void *hero, unsigned index);
static bool (*g_is_selected)(const void *hero, unsigned index);
static bool (*g_toggle)(void *hero, unsigned index);
static void (*g_push)(void);
static void (*g_pop)(void);
static void (*g_translate)(const Vec2 *v);
static void (*g_params_ctor)(void *self, Vec2 pos, const void *texture, uint64_t id, bool enabled, const void *tooltip);
static void (*g_texture_from_file)(void *out_handle, const char *path, unsigned flags);

// ---------------------------------------------------------------- pages (per hero guid, not saved)
static struct {
    unsigned guid;
    int page;
} g_pg[32];
static int *page_of(unsigned guid) {
    int free_i = -1;
    for (int i = 0; i < 32; i++) {
        if (g_pg[i].guid == guid)
            return &g_pg[i].page;
        if (!g_pg[i].guid && free_i < 0)
            free_i = i;
    }
    if (free_i < 0)
        free_i = (int)(guid % 32);
    g_pg[free_i].guid = guid;
    g_pg[free_i].page = 0;
    return &g_pg[free_i].page;
}

// the class's skills the hero owns (a real rank: placeholder levels below "guild_first_rank" don't count; the
// Guild shows the rest), in page order; start[p] = first of page p
static int build_pages(const ClassConfig *c, const void *hero, const uint8_t *cls, int per_row, int *order, int *start,
                       int *npages) {
    uint8_t *const *b = *(uint8_t *const *const *)(cls + CLASS_SKILLS), *const *e = *(
                                                                            uint8_t *const *const *)(cls +
                                                                                                     CLASS_SKILLS + 8);
    int n = (int)((e - b) / 3), row[MAX_SKILLS], ord[MAX_SKILLS], k = 0, max_row = 0;
    if (n > MAX_SKILLS)
        n = MAX_SKILLS;
    for (int i = 0; i < n; i++) {
        unsigned h = *(const unsigned *)(b[i * 3] + SKILL_ID_HASH);
        if (config_skill_hidden(c, h))
            continue;
        if (g_skill_level(hero, (unsigned)i) - (config_guild_first_rank(c, h) - 1) < 0)
            continue;                    // not owned
        config_guild_row(c, h, &ord[k]); // (its place in the config, for the order)
        row[k] = config_sheet_page(c, h);
        if (!row[k])
            ord[k] = 1000 + i;
        if (row[k] > max_row)
            max_row = row[k];
        order[k++] = i;
    }
    for (int i = 1; i < k; i++) // by (row, or after the rows), then order
        for (int j = i; j > 0; j--) {
            int ra = row[j - 1] ? row[j - 1] : max_row + 1, rz = row[j] ? row[j] : max_row + 1;
            if (ra < rz || (ra == rz && ord[j - 1] <= ord[j]))
                break;
            int t;
            t = row[j - 1];
            row[j - 1] = row[j];
            row[j] = t;
            t = ord[j - 1];
            ord[j - 1] = ord[j];
            ord[j] = t;
            t = order[j - 1];
            order[j - 1] = order[j];
            order[j] = t;
        }
    *npages = 0;
    int in_page = 0, prev_key = -1;
    for (int i = 0; i < k; i++) {
        int key = row[i] ? row[i] : max_row + 1;
        if (key != prev_key || in_page >= per_row) {
            start[(*npages)++] = i;
            in_page = 0;
            prev_key = key;
        }
        in_page++;
    }
    start[*npages] = k;
    return k;
}

// ---------------------------------------------------------------- while a configured hero's grid draws
static struct {
    bool on;
    const void *hero, *cls;
    const ClassConfig *c;
    int n, map[MAX_SKILLS], pages;
} g_ctx;

unsigned sheet_skill_index(unsigned slot) {
    return g_ctx.on && slot < (unsigned)g_ctx.n ? (unsigned)g_ctx.map[slot] : slot;
}

static int first_rank_of(unsigned index) {
    const uint8_t *cls = g_ctx.cls;
    uint8_t *const *b = *(uint8_t *const *const *)(cls + CLASS_SKILLS);
    return config_guild_first_rank(g_ctx.c, *(const unsigned *)(b[index * 3] + SKILL_ID_HASH));
}

typedef unsigned (*count_fn)(void *self, const void **cls);
static count_fn g_count_orig;
static unsigned count_hook(void *self, const void **cls) {
    return g_ctx.on && *cls == g_ctx.cls ? (unsigned)g_ctx.n : g_count_orig(self, cls);
}

// $_1 (replaced: a 15-byte tail call) and $_3 (the same)
static int level_hook(void *self, const void **hero, const unsigned *slot) {
    unsigned i = sheet_skill_index(*slot);
    int l = g_skill_level(*hero, i);
    if (g_ctx.on && l >= 0) {
        l -= first_rank_of(i) - 1; // placeholder levels don't count
        if (l < 0)
            l = -1; // only placeholders: not owned yet
    }
    return l;
}
static bool selected_hook(void *self, const void **hero, const unsigned *slot) {
    return g_is_selected(*hero, sheet_skill_index(*slot));
}

#ifdef _WIN32
typedef void *(*icon_fn)(void *self, void *out, const void **cls, const unsigned *slot,
                         const unsigned *level); // MSVC: this first
#else
typedef void *(*icon_fn)(void *out, void *self, const void **cls, const unsigned *slot, const unsigned *level);
#endif
static icon_fn g_icon_orig;
#ifdef _WIN32
static void *icon_hook(void *self, void *out, const void **cls, const unsigned *slot, const unsigned *level) {
#else
static void *icon_hook(void *out, void *self, const void **cls, const unsigned *slot, const unsigned *level) {
#endif
    unsigned i = sheet_skill_index(*slot), l = *level;
    if (g_ctx.on)
        l += (unsigned)(first_rank_of(i) - 1); // back to the skill's own level
#ifdef _WIN32
    return g_icon_orig(self, out, cls, &i, &l);
#else
    return g_icon_orig(out, self, cls, &i, &l);
#endif
}

typedef bool (*toggle_fn)(void *self, void **hero, const unsigned *slot);
static toggle_fn g_toggle_orig;
static bool toggle_hook(void *self, void **hero, const unsigned *slot) {
    unsigned i = sheet_skill_index(*slot);
    if (i == *slot)
        return g_toggle_orig(self, hero, slot);
    return g_toggle(*hero, i); // (the slot's tween is by slot; skipped)
}

// ---------------------------------------------------------------- the tabs
static struct {
    uint8_t h[16];
    bool loaded, missing;
} g_tex[4];

static void step_page(const uint8_t *data) {
    unsigned guid;
    int step, pages;
    memcpy(&guid, data, 4);
    memcpy(&step, data + 4, 4);
    memcpy(&pages, data + 8, 4);
    int *p = page_of(guid);
    if (pages > 1)
        *p = (*p + step + pages) % pages;
    dbgf_("sheet page: hero %u page %d/%d", guid, *p + 1, pages);
}

static void tab(const ClassConfig *c, unsigned guid, int pages, int next, float x, float y) {
    bool enabled = pages > 1;
    int i = next + (enabled ? 0 : 2);
    if (!enabled && !config_page_icon(c, i)[0])
        i = next;
    if (!g_tex[i].loaded) {
        const char *path = config_page_icon(c, i);
        g_tex[i].loaded = true;
        g_tex[i].missing = !path[0];
        if (path[0])
            g_texture_from_file(g_tex[i].h, path, 0);
    }
    if (g_tex[i].missing)
        return;
    uint8_t params[0x400], tex[16];
    memset(params, 0, sizeof params);
    memcpy(tex, g_tex[i].h, sizeof tex);
    g_params_ctor(params, (Vec2){x, y}, tex, next ? 0x73686e78ull : 0x73687076ull, enabled, NULL); // 'shnx' / 'shpv'
    uint8_t data[12];
    int step = next ? 1 : -1;
    memcpy(data, &guid, 4);
    memcpy(data + 4, &step, 4);
    memcpy(data + 8, &pages, 4);
    StdFunction click;
    ui_function(&click, step_page, data, sizeof data);
    ui_image_button(params, &click);
}

typedef void (*grid_fn)(void *self, Vec2 pos, void *hero, const void *cls);
static grid_fn g_grid_orig;

static void grid_hook(void *self, Vec2 pos, void *hero, const void *cls) {
    const ClassConfig *c = hero ? config_for_hero(hero) : NULL;
    uint8_t *assets = g_campaign_assets ? g_campaign_assets() : NULL;
    if (!c || !cls || !assets) {
        g_grid_orig(self, pos, hero, cls);
        return;
    }
    const uint8_t *layout = assets + CAMPAIGN_SKILL_GRID;
    int per_row = *(const int *)(layout + SKILLGRID_PER_ROW);
    if (per_row <= 0 || per_row > 16)
        per_row = 7;
    int order[MAX_SKILLS], start[MAX_SKILLS + 1], pages;
    build_pages(c, hero, cls, per_row, order, start, &pages);
    unsigned guid = hero_guid(hero);
    int *page = page_of(guid);
    if (*page >= pages)
        *page = 0;
    g_ctx.on = pages > 0;
    g_ctx.hero = hero;
    g_ctx.cls = cls;
    g_ctx.c = c;
    g_ctx.pages = pages;
    g_ctx.n = pages > 0 ? start[*page + 1] - start[*page] : 0;
    for (int s = 0; s < g_ctx.n; s++)
        g_ctx.map[s] = order[start[*page] + s];
    g_grid_orig(self, pos, hero, cls);
    g_ctx.on = false;
    if (!pages || !config_page_icon(c, 0)[0])
        return;
    const Vec2 st = *(const Vec2 *)(layout + SKILLGRID_START), sp = *(const Vec2 *)(layout + SKILLGRID_SPACING);
    g_push();
    g_translate(&pos);
    tab(c, guid, pages, 0, st.x - TAB_W - TAB_GAP, st.y);
    tab(c, guid, pages, 1, st.x + (per_row - 1) * sp.x + ICON_SIZE + TAB_GAP, st.y);
    g_pop();
}

void sheet_install(void) {
    bool any = false;
    for (int i = 0; config_class_at(i); i++)
        any = true;
    if (!any) {
        logf_("character sheet: no classes configured, off");
        return;
    }
    g_campaign_assets = (void *(*)(void))sym("_ZN2UI14CampaignAssets7CurrentEv");
    g_skill_level = (int (*)(const void *, unsigned))sym("_ZNK4Hero19GetCombatSkillLevelEj");
    g_is_selected = (bool (*)(const void *, unsigned))sym("_ZNK4Hero24GetIsCombatSkillSelectedEj");
    g_toggle = (bool (*)(void *, unsigned))sym("_ZN4Hero33AttemptCombatSkillSelectionToggleEj");
    g_push = (void (*)(void))sym("_Z14Transform_Pushv");
    g_pop = (void (*)(void))sym("_Z13Transform_Popv");
    g_translate = (void (*)(const Vec2 *))sym("_Z19Transform_TranslateRK4vec2");
    g_params_ctor = (void (*)(void *, Vec2, const void *, uint64_t, bool, const void *))sym(
        "_ZN21ImageButtonParametersC2E4vec214ResourceHandleI7TextureEybP20GuiTooltipParameters");
    g_texture_from_file = (void (*)(void *, const char *, unsigned))sym("_Z20new_Texture_FromFile8c_stringj");
    if (!g_campaign_assets || !g_skill_level || !g_is_selected || !g_toggle || !g_push || !g_pop || !g_translate ||
        !g_params_ctor || !g_texture_from_file) {
        logf_("character sheet: missing symbols, off");
        return;
    }
#define LAMBDA(n, sig)                                                                                                 \
    "_ZNSt3__110__function6__funcIZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClassE3$_" #n   \
    "NS_9allocatorISB_EE" sig
    // the lambdas first: the grid hook only pages when all of them are in
    static const uint8_t count_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x06,
                                        0x48, 0x8B, 0x88, 0xE0, 0x0B, 0x00, 0x00};
    g_count_orig = (count_fn)install_hook("CharacterDisplay skill count", sym(LAMBDA(0, "FjSA_EEclEOSA_")),
                                          (void *)count_hook, count_pro, sizeof count_pro);
    // $_1 / $_3: push rbp; mov rbp,rsp; mov rdi,[rsi]; mov esi,[rdx]; pop rbp; jmp <Hero fn> (15 bytes, replaced)
    static const uint8_t level_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x3E, 0x8B,
                                        0x32, 0x5D, 0xE9, 0x51, 0xE4, 0x9D, 0xFF};
    void *lv = install_hook("CharacterDisplay skill level", sym(LAMBDA(1, "FiPKS6_jEEclEOSF_Oj")), (void *)level_hook,
                            level_pro, sizeof level_pro);
    static const uint8_t sel_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x3E, 0x8B,
                                      0x32, 0x5D, 0xE9, 0xE1, 0x98, 0x9E, 0xFF};
    void *se = install_hook("CharacterDisplay skill selected", sym(LAMBDA(3, "FbS7_jEEclEOS7_Oj")),
                            (void *)selected_hook, sel_pro, sizeof sel_pro);
    static const uint8_t icon_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48,
                                       0x89, 0xFB, 0x48, 0x8B, 0x02, 0x8B, 0x09};
    g_icon_orig = (icon_fn)install_hook("CharacterDisplay skill icon",
                                        sym(LAMBDA(2, "F14ResourceHandleI7TextureESA_jjEEclEOSA_OjSK_")),
                                        (void *)icon_hook, icon_pro, sizeof icon_pro);
    static const uint8_t toggle_pro[] = PROLOGUE_SUB8(0x48); // sub rsp,0x48
    g_toggle_orig = (toggle_fn)install_hook("CharacterDisplay skill toggle", sym(LAMBDA(5, "FbS7_jEEclEOS7_Oj")),
                                            (void *)toggle_hook, toggle_pro, sizeof toggle_pro);
#undef LAMBDA
    if (!g_count_orig || !lv || !se || !g_icon_orig || !g_toggle_orig) {
        logf_("character sheet: lambda hooks incomplete, paging off (a hooked lambda still maps nothing)");
        return;
    }
    static const uint8_t grid_pro[] = PROLOGUE_SUB32(0x08, 0x03); // sub rsp,0x308
    g_grid_orig =
        (grid_fn)install_hook("CharacterDisplay::ShowCombatSkillGrid",
                              sym("_ZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClass"),
                              (void *)grid_hook, grid_pro, sizeof grid_pro);
}
