// Combat skill bar paging (config "skill_pages": {"size": 4, "icons": [prev, next]}).
// In battle a hero's valid skills are ordered and paged by their character sheet groups (see paging_window):
// Hero::GetModeSelectedCombatSkills (skills.c) keeps the page's window, so the bar, the 1-4 hotkeys and the
// skill that's used all agree.
// Fixed slots: the bar always has `size` skill slots, then Move (hotkey size + 1: key 5 at size 4), then Pass and
// the tabs. The bar (Panel_Banner::ShowBattle) draws slots 0 .. GetNumberOfSelectedCombatSkills() - 1 (the skill
// list + 1: Move is the slot after the list), slot i with hotkey '1' + i, and skips a slot whose
// GetSelectedCombatSkill(i) is NULL but still steps past it. So for a paged hero in battle the count is size + 1
// and the slots after the page's skills are NULL up to `size`; index `size` is Move (the game's getters return
// Move for any index past the list, so Hero::SelectCombatAbility / GetCombatSkillAtIndex need no change). The skill
// list itself isn't padded (act-outs and the AI read it). Out of battle (in a raid) the bar is the same slot loop,
// unpaged: the same fixed slots apply while the hero's skills fit in `size`. Empty slots get the vanilla empty-slot
// frame (panels/icons_ability/ability_none.png, a disabled button), drawn when the slot loop's own
// GetSelectedCombatSkill call (found by scanning ShowBattle for its first call) gets NULL: the slot's transform is
// current then, and an unscaled skill icon is drawn at its origin (Transform_Translate(float) is depth only).
// NULL guards: the 1-5 hotkeys (RaidDisplay::ProcessSkillSelectionKeys) pass the slot's skill to
// Battle::CanBeHeroSkillUsed (false for NULL: the game plays "button_invalid"), and the controller's
// DungeonController::ChooseAbility passes each slot to GetBestValidCombatTargetFocusId (0 for NULL); both read the
// skill without checking; out of battle DungeonController::UpdatePartyActionInput passes each slot to
// RaidDisplay::CanPerformSkillOutsideOfBattle (false for NULL). The other in-battle callers (the character sheet's UpdateHeroPips, focus lambda $_16,
// GetAbilityTargetType) check for NULL.
// Two tabs page through them: Panel_Banner::ShowBattle draws the Pass tab with GUI_ImageButton (button id
// 'pass'); our GUI_ImageButton hook then draws "next" just right of it and "previous" just left of the first
// skill, in the same transform. Next wraps from the last page to the first, previous the other way. The tabs
// are always there for a paging class in battle; with one page they're disabled (no hover or click) and drawn
// with the disabled icons (the game doesn't dim a disabled button itself).
// The tabs are drawn from ui.c's GUI_ImageButton hook (after the Pass tab). The u64-id ImageButtonParameters
// ctor (vec2, ResourceHandle<Texture>, id, bool enabled (false = drawn but no hover or click: ShowBattle plays
// "pit_invalid" when it's false), GuiTooltipParameters*) takes over the handle without a reference
// (callers pass a fresh copy), so ours are never destroyed (that would release our texture).
#include "common.h"
#include <stdlib.h>
#include <string.h>

#define PARAMS_SIZE     0x400         // >= sizeof(ImageButtonParameters) (~0x328)
#define PASS_ID         0x70617373ull // 'pass'
#define TAB_W           20            // the Pass tab's width (ability_pass.png is 20 x 72)
#define SLOT            76            // ability_layout .spacing: a skill slot and its gap
#define GAP             4
#define EMPTY_SLOT_ICON "panels/icons_ability/ability_none.png" // vanilla empty skill slot, 72 x 72

// ---------------------------------------------------------------- page state (per hero guid, not saved)
#define MAX_PAGED 32
static struct {
    unsigned guid;
    int page, pages, shown;
} g_pg[MAX_PAGED];

static int *find_pg(unsigned guid, bool create) {
    int free_i = -1;
    for (int i = 0; i < MAX_PAGED; i++) {
        if (g_pg[i].guid == guid && g_pg[i].pages)
            return &g_pg[i].page;
        if (!g_pg[i].pages && free_i < 0)
            free_i = i;
    }
    if (!create || free_i < 0)
        return NULL;
    g_pg[free_i].guid = guid;
    g_pg[free_i].page = 0;
    g_pg[free_i].pages = 1;
    return &g_pg[free_i].page;
}
#define PG_OF(p) ((typeof(g_pg[0]) *)((uint8_t *)(p) - __builtin_offsetof(typeof(g_pg[0]), page)))

static void **g_raid; // &Raid::s_Instance
static bool (*g_in_battle)(const void *raid);
static bool in_battle(void) {
    return g_raid && *g_raid && g_in_battle && g_in_battle(*g_raid);
}

// In battle the skills are ordered as the character sheet shows them: by "sheet_page" (else "guild_row";
// skills with neither last), then by their place in the config. Pages: each such group goes on the current
// page if it fits, else starts the next (a group bigger than a page continues over pages), so a spec's skills
// stay together and small groups (the three spec choices) share a page.
#define MAX_BAR 32
static void order_skills(const ClassConfig *c, void **skills, int n, int *key) {
    int ord[MAX_BAR];
    for (int i = 0; i < n; i++) {
        unsigned h = skill_id_hash(skills[i]);
        key[i] = config_sheet_page(c, h);
        if (!key[i])
            key[i] = 1000;
        config_guild_row(c, h, &ord[i]);
    }
    for (int i = 1; i < n; i++) // stable insertion sort by (key, order)
        for (int j = i; j > 0 && (key[j - 1] > key[j] || (key[j - 1] == key[j] && ord[j - 1] > ord[j])); j--) {
            void *t = skills[j - 1];
            skills[j - 1] = skills[j];
            skills[j] = t;
            int k = key[j - 1];
            key[j - 1] = key[j];
            key[j] = k;
            k = ord[j - 1];
            ord[j - 1] = ord[j];
            ord[j] = k;
        }
}

int paging_window(const ClassConfig *c, const void *hero, void **skills, int n) {
    int size = config_page_size(c);
    if (size <= 0 || !in_battle() || n > MAX_BAR)
        return n;
    int key[MAX_BAR], start[MAX_BAR + 1], pages = 0;
    order_skills(c, skills, n, key);
    for (int i = 0, in_page = 0; i < n;) { // pack the groups into pages
        int g = i;
        while (g < n && key[g] == key[i])
            g++; // group [i, g)
        if (in_page && in_page + (g - i) > size)
            in_page = 0; // doesn't fit: next page
        for (; i < g; i++) {
            if (in_page == size)
                in_page = 0; // a group longer than a page carries on
            if (!in_page)
                start[pages++] = i;
            in_page++;
        }
    }
    start[pages] = n;
    int *p = find_pg(hero_guid(hero), true); // a record even for one page: the tabs show, disabled
    if (!p)
        return n;
    if (pages <= 1) {
        PG_OF(p)->pages = 1;
        PG_OF(p)->shown = n;
        *p = 0;
        return n;
    }
    if (*p >= pages)
        *p = pages - 1;
    int count = start[*p + 1] - start[*p];
    memmove(skills, skills + start[*p], count * sizeof *skills);
    PG_OF(p)->pages = pages;
    PG_OF(p)->shown = count;
    return count;
}

void paging_reset(const void *hero) {
    int *p = find_pg(hero_guid(hero), false);
    if (p)
        *p = 0;
}

// ---------------------------------------------------------------- the tabs
static void step_page(const uint8_t *data) {
    unsigned guid;
    int step;
    memcpy(&guid, data, sizeof guid);
    memcpy(&step, data + 4, sizeof step);
    int *p = find_pg(guid, false);
    if (!p)
        return;
    int pages = PG_OF(p)->pages;
    if (pages > 1)
        *p = (*p + step + pages) % pages;
    dbgf_("skill page: hero %u page %d/%d", guid, *p + 1, pages);
}

static void (*g_params_ctor)(void *self, Vec2 pos, const void *texture, uint64_t id, bool b, const void *tooltip);
static void (*g_texture_from_file)(void *out_handle, const char *path, unsigned flags);
static void *(*g_selected_hero)(const void *party);
static struct {
    uint8_t h[16];
    bool loaded, missing;
} g_tex[5]; // 0-3 the tab icons, 4 the empty slot

// texture i, loaded from path on first use (false: no path)
static bool texture(int i, const char *path, uint8_t out[16]) {
    if (!g_tex[i].loaded) {
        g_tex[i].loaded = true;
        g_tex[i].missing = !path[0];
        if (path[0]) {
            g_texture_from_file(g_tex[i].h, path, 0);
            logf_("skill pages: loaded %s", path);
        }
    }
    if (g_tex[i].missing)
        return false;
    memcpy(out, g_tex[i].h, 16);
    return true;
}

// an empty skill slot's frame: a disabled button (drawn, no hover or click)
static void empty_slot(int slot, float x, float y) {
    uint8_t params[PARAMS_SIZE], tex[16];
    if (!texture(4, EMPTY_SLOT_ICON, tex))
        return;
    memset(params, 0, sizeof params);
    g_params_ctor(params, (Vec2){x, y}, tex, 0x63706530ull + slot, false, NULL); // 'cpe0' + slot
    StdFunction none;
    memset(&none, 0, sizeof none);
    ui_image_button(params, &none);
}

static void tab(const ClassConfig *c, unsigned guid, int next, bool enabled, float x, float y) {
    int i = next + (enabled ? 0 : 2); // icon: 0 prev, 1 next, 2/3 disabled
    if (!enabled && !config_page_icon(c, i)[0])
        i = next; // no disabled icon given: the normal one
    uint8_t params[PARAMS_SIZE];
    memset(params, 0, sizeof params);
    uint8_t tex[16];
    if (!texture(i, config_page_icon(c, i), tex))
        return;
    g_params_ctor(params, (Vec2){x, y}, tex, next ? 0x63706e78ull : 0x6370707aull, enabled,
                  NULL); // 'cpnx' / 'cppz'; false: no hover/click
    uint8_t data[8];
    int step = next ? 1 : -1;
    memcpy(data, &guid, 4);
    memcpy(data + 4, &step, 4);
    StdFunction click;
    ui_function(&click, step_page, data, sizeof data);
    ui_image_button(params, &click);
}

// after the Pass tab: the page tabs, in the same transform
static void after_button(const void *params) {
    if (*(const uint64_t *)((const uint8_t *)params + PARAMS_ID) != PASS_ID || !in_battle())
        return;
    const void *hero = g_selected_hero((uint8_t *)*g_raid + RAID_PARTY);
    const ClassConfig *c = config_for_hero(hero);
    if (!c || config_page_size(c) <= 0)
        return;
    int *p = find_pg(hero_guid(hero), false);
    if (!p)
        return;
    bool enabled = PG_OF(p)->pages > 1;
    const float *pos = (const float *)((const uint8_t *)params + PARAMS_POS);
    // right of Pass; left of the first slot: the `size` slots and Move sit between it and Pass
    int size = config_page_size(c);
    float slot0 = pos[0] - (size + 1) * SLOT;
    tab(c, hero_guid(hero), 1, enabled, pos[0] + TAB_W + GAP, pos[1]);
    tab(c, hero_guid(hero), 0, enabled, slot0 - TAB_W - GAP, pos[1]);
    static float last_x = -1;
    if (pos[0] != last_x) {
        dbgf_("skill pages: Pass tab at %.0f,%.0f, %d skills shown", pos[0], pos[1], PG_OF(p)->shown);
        last_x = pos[0];
    }
}

// ---------------------------------------------------------------- fixed slots
typedef unsigned (*count_fn)(const void *hero);
static count_fn g_count_orig;

// The bar's slot count (size) if the hero has fixed slots now, else 0; *shown = the skills on the bar. In battle:
// the page (record from paging_window). Out of battle in a raid: the whole (unpaged) list if it fits. count: the
// game's GetNumberOfSelectedCombatSkills (list + 1; 0 for a corpse), or -1 to ask it.
static int fixed_slots(const void *hero, int count, int *shown) {
    if (!g_raid || !*g_raid)
        return 0;
    const ClassConfig *c = config_for_hero(hero);
    int size = c ? config_page_size(c) : 0;
    if (size <= 0)
        return 0;
    if (in_battle()) {
        int *p = find_pg(hero_guid(hero), false);
        if (!p || PG_OF(p)->shown > size)
            return 0;
        *shown = PG_OF(p)->shown;
        return size;
    }
    if (count < 0)
        count = (int)g_count_orig(hero);
    if (count <= 0 || count - 1 > size)
        return 0;
    *shown = count - 1;
    return size;
}

typedef void *(*skill_at_fn)(const void *hero, unsigned i);
typedef bool (*can_use_fn)(const void *battle, const void *actor, const void *skill);
typedef void *(*focus_fn)(const void *controller, const void *actor, const void *skill);
typedef bool (*outside_fn)(const void *display, const void *skill);
static outside_fn g_outside_orig;
static const uint8_t *g_bar_slot_ret; // return address of the bar slot loop's GetSelectedCombatSkill call
static skill_at_fn g_skill_at_orig;
static can_use_fn g_can_use_orig;
static focus_fn g_focus_orig;

static unsigned count_hook(const void *hero) {
    unsigned n = g_count_orig(hero); // runs GetModeSelectedCombatSkills: the page record is current
    int shown, size = n ? fixed_slots(hero, (int)n, &shown) : 0; // 0: a corpse
    return size ? (unsigned)size + 1 : n;
}

static void *skill_at_hook(const void *hero, unsigned i) {
    void *r = g_skill_at_orig(hero, i);
    int shown, size = g_count_orig ? fixed_slots(hero, -1, &shown) : 0; // the count hook makes the slots
    if (!size || (int)i < shown || (int)i > size)
        return r;
    if ((int)i == size)
        return g_skill_at_orig(hero, (unsigned)shown); // Move
    if (__builtin_return_address(0) == g_bar_slot_ret)
        empty_slot((int)i, 0, 0); // the bar draws this slot now
    return NULL;                  // an empty slot
}

static bool can_use_hook(const void *battle, const void *actor, const void *skill) {
    return skill && g_can_use_orig(battle, actor, skill);
}

static void *focus_hook(const void *controller, const void *actor, const void *skill) {
    return skill ? g_focus_orig(controller, actor, skill) : NULL;
}

static bool outside_hook(const void *display, const void *skill) {
    return skill && g_outside_orig(display, skill);
}

// The return address of ShowBattle's first call to GetSelectedCombatSkill (the slot loop's NULL check), or NULL.
static const uint8_t *find_bar_slot_call(void) {
    const uint8_t *f = sym("_ZN12Panel_Banner10ShowBattleERN6RaidUI6Screen11RaidDisplayE");
    const uint8_t *target = sym("_ZNK4Hero22GetSelectedCombatSkillEj");
    if (!f || !target)
        return NULL;
    for (int i = 0; i < 0x3000; i++) {
        if (f[i] != 0xE8)
            continue;
        int32_t rel;
        memcpy(&rel, f + i + 1, 4);
        if (f + i + 5 + rel == target)
            return f + i + 5;
    }
    return NULL;
}

static void install_slots(void) {
    static const uint8_t count_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53,
                                        0x48, 0x83, 0xEC, 0x20, 0x49, 0x89, 0xFE}; // ... mov r14,rdi
    static const uint8_t at_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53,
                                     0x48, 0x83, 0xEC, 0x20, 0x41, 0x89, 0xF6}; // ... mov r14d,esi
    static const uint8_t can_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                      0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x40}; // sub rsp,0x40
    static const uint8_t focus_pro[] = PROLOGUE_SUB8(0x18);
    static const uint8_t outside_pro[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                          0x55, 0x41, 0x54, 0x53, 0x50, 0x49, 0x89, 0xF6}; // push rax; mov r14,rsi
    g_can_use_orig = (can_use_fn)install_hook("Battle::CanBeHeroSkillUsed",
                                              sym("_ZNK6Battle18CanBeHeroSkillUsedEPK5ActorPK16ActorCombatSkill"),
                                              (void *)can_use_hook, can_pro, sizeof can_pro);
    g_focus_orig = (focus_fn)install_hook(
        "DungeonController::GetBestValidCombatTargetFocusId",
        sym("_ZNK17DungeonController31GetBestValidCombatTargetFocusIdEPK5ActorPK16ActorCombatSkill"),
        (void *)focus_hook, focus_pro, sizeof focus_pro);
    g_outside_orig = (outside_fn)install_hook(
        "RaidDisplay::CanPerformSkillOutsideOfBattle",
        sym("_ZNK6RaidUI6Screen11RaidDisplay30CanPerformSkillOutsideOfBattleEPK15HeroCombatSkill"),
        (void *)outside_hook, outside_pro, sizeof outside_pro);
    if (!g_can_use_orig || !g_focus_orig || !g_outside_orig) {
        logf_("skill pages: NULL guards missing, fixed slots off");
        return;
    }
    g_bar_slot_ret = find_bar_slot_call();
    if (!g_bar_slot_ret)
        logf_("skill pages: bar slot loop call not found, no empty slot frames");
    g_skill_at_orig =
        (skill_at_fn)install_hook("Hero::GetSelectedCombatSkill", sym("_ZNK4Hero22GetSelectedCombatSkillEj"),
                                  (void *)skill_at_hook, at_pro, sizeof at_pro);
    if (!g_skill_at_orig)
        return; // the count alone would repeat Move in the empty slots
    g_count_orig = (count_fn)install_hook("Hero::GetNumberOfSelectedCombatSkills",
                                          sym("_ZNK4Hero31GetNumberOfSelectedCombatSkillsEv"), (void *)count_hook,
                                          count_pro, sizeof count_pro);
}

void paging_install(void) {
    bool any = false;
    for (int i = 0; config_class_at(i); i++)
        any |= config_page_size(config_class_at(i)) > 0;
    if (!any) {
        logf_("skill pages: none configured, off");
        return;
    }
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    g_in_battle = (bool (*)(const void *))sym("_ZNK4Raid10IsInBattleEv");
    g_selected_hero = (void *(*)(const void *))sym("_ZNK5Party15GetSelectedHeroEv");
    g_texture_from_file = (void (*)(void *, const char *, unsigned))sym("_Z20new_Texture_FromFile8c_stringj");
    g_params_ctor = (void (*)(void *, Vec2, const void *, uint64_t, bool, const void *))sym(
        "_ZN21ImageButtonParametersC2E4vec214ResourceHandleI7TextureEybP20GuiTooltipParameters");
    if (!g_raid || !g_in_battle || !g_selected_hero || !g_texture_from_file || !g_params_ctor) {
        logf_("skill pages: missing symbols, off");
        return;
    }
    ui_on_image_button(NULL, after_button);
    install_slots();
}
