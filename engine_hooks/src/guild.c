// The Guild's combat skills as one icon each (every class): the icon shows the skill's rank, the next rank's
// cost and tooltip, and a click buys the next rank; the icons form a grid (rows from the skills' "guild_row").
//
// Vanilla: selecting a hero (GuildDisplay OnActionEnterStartInternal's $_1) makes an UpgradeTreeDisplay per
// class skill (tree "<class>.<skill>", in GuildDisplay+0x250), and GuildDisplay::OnActionUpdateInternal puts
// tree n at guild_layout .skill_pos + n * .skill_spacing every frame. A tree draws its icon (an image button,
// id = tree hash; with .icon_cost_visible the icon stands for the first rank: cost and lock until bought, a
// click buys it) and its divider (texture at +0x120, at .divider_offset), then its child panels: one
// UpgradeRequirementDisplay per further rank (the shields), each a button that buys its rank.
//
// Here, for trees with the Guild's layout (TownUI::Assets+0x1ef8):
//  - GetNumberOfChildPanels says 0: the rank shields are gone.
//  - after OnActionUpdateInternal, trees are moved into the grid; only a row's first tree keeps its divider.
//  - the icon's button (GUI_ImageButton, while its tree draws): once the first rank is bought, a click runs the
//    shields' own purchase handler (UpgradeTreeDisplay::OnEnterStart's $_0: AttemptPurchase, then the upgrade
//    sounds; the Guild's purchase callbacks refresh the hero) for the next rank; its tooltip becomes that rank's
//    (the Guild's requirement tooltip function: the skill at the next level) plus its prerequisites, as the
//    shields show them; the rank number is drawn on the icon. Costs aren't drawn on the page (CostDisplay::
//    CurrencyCost is skipped while a Guild tree draws, for unlearned skills too): the next rank's cost is the
//    tooltip's last line (red when it can't be afforded).
//  - the Guild's skill tooltip builder (OnActionEnterStartInternal's $_1's lambda(Text&, bool next): the skill
//    at the hero's level, and with `next` the next level too, from the combat tooltip line helpers) goes through
//    tooltip.c's base rewrite (title template, markup; not the live values).
#include "common.h"
#include <stdio.h>
#include <string.h>

// Windows (MSVC): the offsets marked * differ; the tooltip functions are MSVC std::functions (_Func_base* at 0x38 of
// 0x40, _Do_call in vtable slot 2); TownUI::Assets::Current is inlined (a stand-in in syms_win.c); the Guild
// layouts' fields sit at the macOS offsets from it.
#define MAX_LINES 32

// layout of the grid (game pixels, relative to guild_layout .skill_pos)
#define COLUMNS         4     // skills per row without "guild_row"
#define SPACING_X       80.0f // as the character sheet's skill grid
#define SPACING_Y       100.0f
#define RANK_X          58.0f // the rank number's centre, on the icon (and its badge's)
#define RANK_Y          58.0f
#define RANK_TEXT_DY    14.5f // GUI_Text centres x but draws down from y: the digit's centre is ~14.5 below
#define RANK_FONT       "town_character_skill_level" // the character sheet's skill level number
#define RANK_COLOUR     "town_character_skill_level"
#define RANK_BADGE      "panels/engine_guild_rank.png" // behind the number, centred on it (the mod ships it)
#define RANK_BADGE_SIZE 26.0f
#define BUY_OVERLAY     "panels/engine_guild_buy.png" // the lock overlay's coin bag version: can be bought

typedef struct {
    uint32_t tree;
    uint8_t code;
    uint8_t pad[3];
    uint32_t guid;
} PurchaseId; // 12 bytes
typedef struct {
    char *begin, *end, *cap;
} CharVector;
typedef struct {
    uint8_t *begin, *end, *cap;
} ByteVector;
typedef struct {
    uint64_t a;
    uint32_t b;
    uint8_t c;
} LocStr;

static void *(*g_assets)(void);
static void *(*g_roster)(void);
static void *(*g_get_hero)(void *roster, unsigned guid);
static void (*g_set_base_pos)(void *panel, Vec2 pos);
static void (*g_all_codes)(CharVector *out, const unsigned *tree);
static bool (*g_is_purchased)(const PurchaseId *id);
static bool (*g_is_locked)(const PurchaseId *id);
static void (*g_cost)(ByteVector *out, const PurchaseId *id);
static void (*g_currency_dtor)(void *currency);
static const uint8_t *(*g_req_class)(const PurchaseId *id);
static const uint8_t *(*g_tree_class)(unsigned hash);
static bool (*g_tree_has_tag)(const void *tree, unsigned tag);
static void (*g_purchase_click)(void *fn_self, const char *code); // OnEnterStart's $_0 operator()
static void (*g_text_clear)(void *text);
static void (*g_add_line)(void *text, const void *line);
static void (*g_set_string)(void *line, const char *s);
static void (*g_colour_entry)(void *out, unsigned id);
static uint64_t (*g_entry_hvec4)(const void *entry);
static LocStr (*g_localize)(const char *id);
static const char *(*g_loc_string)(const LocStr *);
static int (*g_loc_format)(const LocStr *fmt, char *out, unsigned size, ...);
static void (*g_fls64_assign)(void *fls, const char *s);
static void (*g_delete)(void *);
static bool (*g_can_afford)(const PurchaseId *id);
static int (*g_skill_level)(const void *hero, unsigned index);
static void (*g_texture_from_file)(void *out_handle, const char *path, unsigned flags);
static void (*g_image_params)(void *self, Vec2 pos, const void *texture);
static void (*g_image)(const void *params);
static void (*g_colour)(unsigned colour_hash);
static void *(*g_campaign_assets)(void);
static void (*g_colour_rgba)(uint32_t rgba);

// TownUI::Assets: guild_layout .skill_pos (vec2) and guild_upgrade_tree_layout, NULL if unknown
static const Vec2 *guild_skill_pos(void) {
    uint8_t *assets = g_assets ? g_assets() : NULL;
    return assets ? (const Vec2 *)(assets + ASSETS_GUILD_SKILL_POS) : NULL;
}
static const void *guild_tree_layout(void) {
    uint8_t *assets = g_assets ? g_assets() : NULL;
    return assets ? assets + ASSETS_GUILD_TREE_LAYOUT : NULL;
}

static bool is_guild_tree(const uint8_t *tree) {
    const void *layout = guild_tree_layout();
    return layout && *(const void *const *)(tree + TREE_LAYOUT) == layout;
}

// ---------------------------------------------------------------- the grid (per tree, rebuilt every update)
#define MAX_TREES 32
static struct {
    const void *tree;
    int col, row;
    bool divider;
} g_cells[MAX_TREES];
static int g_ncells;

static int cell_of(const void *tree) {
    for (int i = 0; i < g_ncells; i++)
        if (g_cells[i].tree == tree)
            return i;
    return -1;
}

// the skill id of a tree: "<class id>.<skill id>" of one of the hero's class skills
static const char *tree_skill(const uint8_t *cls, unsigned tree_hash) {
    uint8_t *const *b = *(uint8_t *const *const *)(cls + CLASS_SKILLS), *const *e = *(
                                                                            uint8_t *const *const *)(cls +
                                                                                                     CLASS_SKILLS + 8);
    for (uint8_t *const *p = b; p < e; p += 3) {
        char id[160];
        snprintf(id, sizeof id, "%s.%s", (const char *)(cls + CLASS_ID), (const char *)(*p + SKILL_ID));
        if (name_hash(id) == tree_hash)
            return (const char *)(*p + SKILL_ID);
    }
    return NULL;
}

typedef void (*update_fn)(void *guild, float dt);
static update_fn g_update_orig;

static void update_hook(void *guild, float dt) {
    g_update_orig(guild, dt);
    const Vec2 *pos = guild_skill_pos();
    uint8_t **b = *(uint8_t ***)((uint8_t *)guild + GUILD_TREES),
            **e = *(uint8_t ***)((uint8_t *)guild + GUILD_TREES + 8);
    int n = (int)(e - b);
    if (!pos || n <= 0 || n > MAX_TREES) {
        g_ncells = 0;
        return;
    }
    const void *hero = g_get_hero(g_roster(), *(unsigned *)(b[0] + TREE_GUID));
    const uint8_t *cls = hero ? *(uint8_t *const *)((const uint8_t *)hero + HERO_CLASS) : NULL;
    const ClassConfig *c = hero ? config_for_hero(hero) : NULL;
    int row[MAX_TREES], order[MAX_TREES], idx[MAX_TREES], max_row = 0;
    for (int i = 0; i < n; i++) {
        const char *skill = cls ? tree_skill(cls, *(unsigned *)(b[i] + TREE_HASH)) : NULL;
        row[i] = c && skill ? config_guild_row(c, name_hash(skill), &order[i]) : 0;
        if (!row[i])
            order[i] = 1000 + i; // unrowed: after, in the game's order
        if (row[i] > max_row)
            max_row = row[i];
        idx[i] = i;
    }
    for (int i = 1; i < n; i++) // sort by (row or after the rows, order)
        for (int j = i; j > 0; j--) {
            int a = idx[j - 1], z = idx[j];
            int ra = row[a] ? row[a] : max_row + 1, rz = row[z] ? row[z] : max_row + 1;
            if (ra < rz || (ra == rz && order[a] <= order[z]))
                break;
            idx[j - 1] = z;
            idx[j] = a;
        }
    // rows as shown (config rows without skills take no space), then centred rows offset against the widest
    int cell_col[MAX_TREES], cell_row[MAX_TREES], row_cols[MAX_TREES] = {0}, row_key[MAX_TREES], widest = 0;
    int col = 0, out_row = -1, prev_key = -1;
    for (int k = 0; k < n; k++) {
        int i = idx[k];
        int key = row[i] ? row[i] : max_row + 1;
        if (key != prev_key || (!row[i] && col >= COLUMNS)) {
            out_row++;
            col = 0;
            prev_key = key;
            row_key[out_row] = row[i];
        }
        cell_col[k] = col;
        cell_row[k] = out_row;
        row_cols[out_row] = ++col;
        if (col > widest)
            widest = col;
    }
    g_ncells = 0;
    const Vec2 base = *pos;
    for (int k = 0; k < n; k++) {
        int i = idx[k], r = cell_row[k];
        bool centred = c && row_key[r] && config_guild_row_centred(c, row_key[r]);
        float dx = centred ? (widest - row_cols[r]) * SPACING_X / 2 : 0;
        g_set_base_pos(b[i], (Vec2){base.x + dx + cell_col[k] * SPACING_X, base.y + r * SPACING_Y});
        // one divider per row, from its first skill; none under a centred (header) row
        g_cells[g_ncells++] = (typeof(g_cells[0])){b[i], cell_col[k], r, cell_col[k] == 0 && !centred};
    }
}

// ---------------------------------------------------------------- no rank shields
typedef unsigned (*count_fn)(const void *tree);
static count_fn g_count_orig;
static unsigned count_hook(const void *tree) {
    return is_guild_tree(tree) ? 0 : g_count_orig(tree);
}

// ---------------------------------------------------------------- the icon
static const uint8_t *g_drawing; // the Guild tree being drawn

typedef struct {
    int rank, n;
    char next;
    bool locked;
    PurchaseId next_id;
} Ranks;

// the tree's class config and skill (for "guild_first_rank" and the title template), if its hero's class has one
static const ClassConfig *tree_config(const uint8_t *tree, unsigned *skill_hash) {
    const void *hero = g_get_hero(g_roster(), *(const unsigned *)(tree + TREE_GUID));
    const ClassConfig *c = hero ? config_for_hero(hero) : NULL;
    const uint8_t *cls = hero ? *(uint8_t *const *)((const uint8_t *)hero + HERO_CLASS) : NULL;
    const char *skill = c && cls ? tree_skill(cls, *(const unsigned *)(tree + TREE_HASH)) : NULL;
    if (!skill)
        return NULL;
    *skill_hash = name_hash(skill);
    return c;
}

// the skill's first real rank ("guild_first_rank"; ranks below it are placeholders every hero owns)
static int first_rank(const uint8_t *tree) {
    unsigned sh;
    const ClassConfig *c = tree_config(tree, &sh);
    return c ? config_guild_first_rank(c, sh) : 1;
}

// only placeholder ranks owned: disabled while the first real rank is locked, else shown as not learned
// (as vanilla's unlearned skills: dimmed, the lock overlay, a click learns it)
static bool shows_disabled(const uint8_t *tree, const Ranks *r) {
    return r->rank > 0 && r->rank < first_rank(tree) && r->next && r->locked;
}
static bool shows_unbought(const uint8_t *tree, const Ranks *r) {
    return r->rank > 0 && r->rank < first_rank(tree) && r->next && !r->locked;
}

// no rank badge for skills whose title has no rank (config "tooltip_title" without [[rank]]), or disabled ones
static bool shows_rank(const uint8_t *tree, const Ranks *r) {
    unsigned sh;
    const ClassConfig *c = tree_config(tree, &sh);
    if (!c)
        return true;
    const char *title = config_tooltip_title(c, sh);
    return !(title && !strstr(title, "[[rank]]")) && r->rank >= first_rank(tree);
}

static Ranks ranks_of(const uint8_t *tree) {
    Ranks r = {0};
    unsigned hash = *(const unsigned *)(tree + TREE_HASH), guid = *(const unsigned *)(tree + TREE_GUID);
    CharVector codes = {0};
    g_all_codes(&codes, &hash);
    for (char *p = codes.begin; p < codes.end; p++) {
        PurchaseId id = {hash, (uint8_t)*p, {0}, guid};
        r.n++;
        if (g_is_purchased(&id)) {
            if (!r.next)
                r.rank++;
            continue;
        }
        if (!r.next) {
            r.next = *p;
            r.next_id = id;
            r.locked = g_is_locked(&id);
        }
    }
    if (codes.begin)
        g_delete(codes.begin);
    return r;
}

static uint64_t colour_of(const char *id) {
    uint8_t entry[256];
    g_colour_entry(entry, name_hash(id));
    return g_entry_hvec4(entry);
}

static void add_line(void *text, const char *s, const char *font, const char *colour) {
    if (*(unsigned *)((uint8_t *)text + TEXT_COUNT) >= MAX_LINES)
        return;
    uint8_t line[0x28];
    memset(line, 0, sizeof line);
    *(void **)line = (void *)ui_font(font); // ui_font: mod fonts are registered on first use
    *(uint64_t *)(line + 0x08) = colour_of(colour);
    *(uint16_t *)(line + 0x10) = 1;
    line[0x20] = 1; // on its own line
    g_set_string(line, s);
    g_add_line(text, line);
}

// the next rank's prerequisites, as UpgradeRequirementDisplay's tooltip lists them
static void add_prerequisites(void *text, const Ranks *r, const void *hero) {
    const uint8_t *req = g_req_class(&r->next_id);
    if (!req)
        return;
    const uint32_t *pb = *(uint32_t *const *)(req + REQ_PREREQS), *pe = *(uint32_t *const *)(req + REQ_PREREQS + 8);
    unsigned resolve = *(const unsigned *)(req + REQ_RESOLVE);
    LocStr title = g_localize("upgrade_prerequisite_tooltip_title");
    add_line(text, g_loc_string(&title), "tooltip_header", "upgrade_tree_tooltip_title");
    LocStr fmt = g_localize("upgrade_prerequisite_requirement_tooltip_body_format");
    int shown = 0;
    for (const uint32_t *p = pb; p < pe; p += 2)
        shown += p[0] != r->next_id.tree; // the skill's own earlier rank goes without saying
    if (!shown && !resolve)
        return;
    for (const uint32_t *p = pb; p < pe; p += 2) {
        if (p[0] == r->next_id.tree)
            continue;
        const uint8_t *tc = g_tree_class(p[0]);
        if (!tc)
            continue;
        char key[96], buf[160];
        snprintf(key, sizeof key, "upgrade_tree_name_%s", (const char *)tc);
        LocStr name = g_localize(key);
#ifdef _WIN32
        const uint8_t *head = *(uint8_t *const *)(tc + TREECLASS_FIRST); // MSVC map: its head's left = first
        uint8_t code = ((const uint8_t *)p)[4], first = *(*(uint8_t *const *)head + 0x2c);
#else
        uint8_t code = ((const uint8_t *)p)[4], first = *(*(uint8_t *const *)(tc + TREECLASS_FIRST) + 0x2c);
#endif
        static unsigned first_not_upgrade;
        if (!first_not_upgrade)
            first_not_upgrade = name_hash("first_level_not_upgrade");
        int level = code - first + (g_tree_has_tag(tc, first_not_upgrade) ? 1 : 0) + 1;
        buf[0] = 0;
        g_loc_format(&fmt, buf, sizeof buf, g_loc_string(&name), level);
        PurchaseId pid = {p[0], code, {0}, tc[TREECLASS_INSTANCED] == 1 ? r->next_id.guid : 0};
        add_line(text, buf, "tooltip",
                 g_is_purchased(&pid) ? "upgrade_tree_tooltip_body" : "upgrade_tree_prerequisite_not_purchased");
    }
    if (resolve && hero) {
        LocStr rf = g_localize("upgrade_prerequisite_resolve_level_tooltip_body_format");
        char buf[160] = "";
        g_loc_format(&rf, buf, sizeof buf, resolve);
        float (*level)(const void *) = (*(float (*const *const *)(const void *))hero)[HERO_RESOLVE_SLOT];
        add_line(text, buf, "tooltip",
                 level(hero) >= (float)resolve ? "upgrade_tree_tooltip_body"
                                               : "upgrade_tree_prerequisite_not_purchased");
    }
}

// "Cost: 250 Gold" (string engine_guild_cost, "%s" = the amounts), red when the hero's estate can't afford it
static void add_cost(void *text, const PurchaseId *id) {
    ByteVector costs = {0};
    g_cost(&costs, id);
    char amounts[160] = "";
    for (uint8_t *p = costs.begin; p && p < costs.end; p += 0x48) { // Currency: amount 0, type id 4
        const char *type = (const char *)(p + 4);
        char key[96];
        snprintf(key, sizeof key, "str_inventory_title_%s", type);
        LocStr ls = g_localize(key);
        const char *name = g_loc_string(&ls);
        if (!name || !name[0] || !strcmp(name, key)) {
            snprintf(key, sizeof key, "str_inventory_title_heirloom%s", type);
            ls = g_localize(key);
            name = g_loc_string(&ls);
            if (!name || !name[0] || !strcmp(name, key))
                name = type;
        }
        size_t l = strlen(amounts);
        snprintf(amounts + l, sizeof amounts - l, "%s%d %s", l ? ", " : "", *(const int *)p, name);
    }
    for (uint8_t *p = costs.begin; p && p < costs.end; p += 0x48)
        g_currency_dtor(p);
    if (costs.begin)
        g_delete(costs.begin);
    if (!amounts[0])
        return;
    LocStr fmt = g_localize("engine_guild_cost");
    const char *f = g_loc_string(&fmt);
    char buf[200];
    if (f && strstr(f, "%s")) {
        buf[0] = 0;
        g_loc_format(&fmt, buf, sizeof buf, amounts);
    } else
        snprintf(buf, sizeof buf, "Cost: %s", amounts);
    add_line(text, buf, "tooltip",
             g_can_afford(id) ? "upgrade_tree_tooltip_body" : "upgrade_tree_prerequisite_not_purchased");
}

static void buy_next(const uint8_t *data) {
    struct {
        void *vtbl;
        const void *tree;
        unsigned start;
        unsigned pad;
    } fn = {NULL, *(const void *const *)data, 1, 0};
    char code = (char)data[8];
    const uint8_t *tree = fn.tree;
    PurchaseId id = {*(const unsigned *)(tree + TREE_HASH), (uint8_t)code, {0}, *(const unsigned *)(tree + TREE_GUID)};
    if (g_is_locked(&id))
        return; // AttemptPurchase checks gold and prerequisite ranks,
                // not resolve: the shields refuse locked ranks first
    dbgf_("guild: buying rank code %c", code);
    g_purchase_click(&fn, &code);
}

static bool button_before(void *params, StdFunction *click) {
    const uint8_t *tree = g_drawing;
    if (!tree || *(uint64_t *)((uint8_t *)params + BTN_ID) != *(const unsigned *)(tree + TREE_HASH))
        return false;
    const uint8_t *layout = *(const uint8_t *const *)(tree + TREE_LAYOUT);
    Ranks r = ranks_of(tree);
    if (!*(const int *)(layout + LAYOUT_COST_VISIBLE) || !r.next)
        return false;
    uint8_t *tip = *(uint8_t **)((uint8_t *)params + BTN_TOOLTIP);
    if (r.rank == 0) { // not learned: the game's icon, tooltip + cost
        if (tip)
            add_cost(tip + TIP_TEXT, &r.next_id);
        return false;
    }
    const void *fa = *(void *const *)(tree + TREE_REQ_FN_A), *fb = *(void *const *)(tree + TREE_REQ_FN_B);
    const void *f = fa ? fa : fb;
    if (tip && f) { // the next rank's tooltip
        void *text = tip + TIP_TEXT;
        g_text_clear(text);
        void (*call)(const void *, const PurchaseId *, void *) =
            (*(void (*const *const *)(const void *, const PurchaseId *, void *))f)[FN_CALL_SLOT];
        call(f, &r.next_id, text);
        add_prerequisites(text, &r, g_get_hero(g_roster(), r.next_id.guid));
        add_cost(text, &r.next_id);
    }
    if (shows_disabled(tree, &r))
        g_colour(name_hash("ability_invalid")); // as the combat bar's unusable skills
    else if (shows_unbought(tree, &r))
        g_colour(name_hash("upgrade_tree_icon_not_purchased")); // as unlearned
    if (r.locked) {                                             // as the shields sound when locked
        g_fls64_assign((uint8_t *)params + BTN_HOVER_SOUND, "/ui/shared/button_mouse_over_2");
        g_fls64_assign((uint8_t *)params + BTN_CLICK_SOUND, "/ui/town/button_click_locked");
    }
    uint8_t data[16] = {0};
    memcpy(data, &tree, sizeof tree);
    data[8] = (uint8_t)r.next;
    ui_function(click, buy_next, data, sizeof data);
    return true;
}

// the coin bag texture (a ResourceHandle), loaded once; NULL if it can't be
static const void *buy_overlay(void) {
    static uint8_t h[16];
    static int state; // 0 not loaded, 1 loaded, -1 unavailable
    if (!state) {
        state = g_texture_from_file ? 1 : -1;
        if (state > 0)
            g_texture_from_file(h, BUY_OVERLAY, 0);
    }
    return state > 0 ? h : NULL;
}

static void button_after(const void *params) {
    const uint8_t *tree = g_drawing;
    if (!tree || *(const uint64_t *)((const uint8_t *)params + BTN_ID) != *(const unsigned *)(tree + TREE_HASH))
        return;
    const uint8_t *layout = *(const uint8_t *const *)(tree + TREE_LAYOUT);
    Ranks r = ranks_of(tree);
    if (!*(const int *)(layout + LAYOUT_COST_VISIBLE) || r.rank == 0)
        return; // not learned: vanilla draws it
    g_colour_rgba(0xffffffffu);
    bool unbought = shows_unbought(tree, &r), disabled = shows_disabled(tree, &r);
    const void *overlay = unbought                        ? buy_overlay()
                          : disabled && g_campaign_assets ? (uint8_t *)g_campaign_assets() + CAMPAIGN_ICON_LOCK
                                                          : NULL;
    if (overlay && g_image_params && g_image) { // the coin bag (can be bought) or the lock (can't yet)
        uint8_t ip[0x400];
        memset(ip, 0, sizeof ip);
        g_image_params(ip, *(const Vec2 *)(layout + LAYOUT_LOCK), overlay);
        g_image(ip);
    }
    if (!shows_rank(tree, &r))
        return;
    const Vec2 icon = *(const Vec2 *)(layout + LAYOUT_ICON);
    char num[8];
    snprintf(num, sizeof num, "%d", r.rank - (first_rank(tree) - 1)); // placeholders don't count
    static uint8_t badge[16];
    static int badge_state; // 0 not loaded, 1 loaded, -1 unavailable
    if (!badge_state) {
        badge_state = g_texture_from_file && g_image_params && g_image ? 1 : -1;
        if (badge_state > 0)
            g_texture_from_file(badge, RANK_BADGE, 0);
    }
    if (badge_state > 0) {
        uint8_t ip[0x400];
        memset(ip, 0, sizeof ip);
        g_image_params(ip, (Vec2){icon.x + RANK_X - RANK_BADGE_SIZE / 2, icon.y + RANK_Y - RANK_BADGE_SIZE / 2}, badge);
        g_image(ip); // (not destroyed: it would release our texture)
    }
    ui_text((Vec2){icon.x + RANK_X, icon.y + RANK_Y - RANK_TEXT_DY}, num, RANK_FONT, RANK_COLOUR);
}

// no costs on the page while a Guild tree draws (they're in the tooltip)
typedef void (*cost_fn)(Vec2 pos, ByteVector *costs, bool a, bool b, bool c);
static cost_fn g_cost_orig;
static void cost_hook(Vec2 pos, ByteVector *costs, bool a, bool b, bool c) {
    if (!g_drawing) {
        g_cost_orig(pos, costs, a, b, c);
        return;
    }
#ifdef _WIN32
    if (costs && costs->begin) {
        g_delete(costs->begin);
        costs->begin = costs->end = costs->cap = NULL;
    } // the callee
#endif // destroys a by-value vector (Currency's destructor is empty)
}

// the Guild's skill tooltip: base rewrite (closure: hero 0, class 8, skill index 0x10)
typedef void (*tip_fn)(const void *closure, void *text, bool next);
static tip_fn g_tip_orig;
static void tip_hook(const void *closure, void *text, bool next) {
    g_tip_orig(closure, text, next);
    const void *hero = *(const void *const *)closure;
    const uint8_t *cls = *(const uint8_t *const *)((const uint8_t *)closure + 8);
    unsigned index = *(const unsigned *)((const uint8_t *)closure + 0x10);
    if (!hero || !cls)
        return; // every class: titles; configured ones: base lines
    uint8_t *const *levels =
        (uint8_t *const *)(*(uint8_t *const *)(cls + CLASS_SKILLS) + index * 0x18); // vector<HeroCombatSkill>
    int n = (int)((levels[1] - levels[0]) / SKILL_LEVEL_SIZE), l = g_skill_level(hero, index);
    if (l < 0)
        l = 0;
    if (l >= n)
        return;
    const void *skills[2] = {levels[0] + l * SKILL_LEVEL_SIZE,
                             next && l + 1 < n ? levels[0] + (l + 1) * SKILL_LEVEL_SIZE : NULL};
    tooltip_rewrite_base((uint8_t *)text - TIP_TEXT, cls, hero, skills, skills[1] ? 2 : 1);
}

typedef void (*show_fn)(void *tree);
static show_fn g_show_orig;

static void show_hook(void *tree) {
    if (!is_guild_tree(tree)) {
        g_show_orig(tree);
        return;
    }
    int i = cell_of(tree);
    uint8_t saved[16], *divider = (uint8_t *)tree + TREE_DIVIDER;
    bool hide = i >= 0 && !g_cells[i].divider; // one divider per row (none under centred rows)
    if (hide) {
        memcpy(saved, divider, sizeof saved);
        static const uint64_t none[2] = {0xFFFFFFFF00000000ull, 0};
        memcpy(divider, none, sizeof none);
    }
    // an unlearned skill that can be learned now: vanilla draws its lock overlay (CampaignAssets' lock texture);
    // for the call that texture is the coin bag
    uint8_t *lock = g_campaign_assets ? (uint8_t *)g_campaign_assets() + CAMPAIGN_ICON_LOCK : NULL, saved_lock[16];
    Ranks r = ranks_of(tree);
    const void *bag = lock && r.rank == 0 && r.next && !r.locked ? buy_overlay() : NULL;
    if (bag) {
        memcpy(saved_lock, lock, sizeof saved_lock);
        memcpy(lock, bag, sizeof saved_lock);
    }
    const uint8_t *prev = g_drawing;
    g_drawing = tree;
    g_show_orig(tree);
    g_drawing = prev;
    if (bag)
        memcpy(lock, saved_lock, sizeof saved_lock);
    if (hide)
        memcpy(divider, saved, sizeof saved);
}

void guild_install(void) {
    g_assets = (void *(*)(void))sym("_ZN6TownUI6Assets7CurrentEv");
    g_roster = (void *(*)(void))sym("_ZN8Campaign13CurrentRosterEv");
    g_get_hero = (void *(*)(void *, unsigned))sym("_ZN6Roster6System7GetHeroEj");
    g_set_base_pos = (void (*)(void *, Vec2))sym("_ZN2UI5Panel4Base10SetBasePosE4vec2");
    g_all_codes = (void (*)(CharVector *, const unsigned *))sym("_ZN7Upgrade22GetAllRequirementCodesERKj");
    g_is_purchased = (bool (*)(const PurchaseId *))sym("_ZN7Upgrade14GetIsPurchasedERKNS_18PurchaseIdentifierE");
    g_is_locked = (bool (*)(const PurchaseId *))sym("_ZN7Upgrade11GetIsLockedERKNS_18PurchaseIdentifierE");
    g_cost = (void (*)(ByteVector *, const PurchaseId *))sym(
        "_ZN7Upgrade27GetCurrencyCostWithDiscountERKNS_18PurchaseIdentifierE");
    g_currency_dtor = (void (*)(void *))sym("_ZN6Estate8CurrencyD1Ev");
    g_req_class =
        (const uint8_t *(*)(const PurchaseId *))sym("_ZN7Upgrade19GetRequirementClassERKNS_18PurchaseIdentifierE");
    g_tree_class = (const uint8_t *(*)(unsigned))sym("_ZN7Upgrade12GetTreeClassEj");
    g_tree_has_tag = (bool (*)(const void *, unsigned))sym("_ZN7Upgrade18GetDoesTreeHaveTagEPKNS_9TreeClassEj");
    g_purchase_click = (void (*)(void *, const char *))sym(
        "_ZNSt3__110__function6__funcIZN6TownUI5Panel18UpgradeTreeDisplay12OnEnterStartEvE3$_0NS_9allocatorIS5_"
        "EEFvcEEclEOc");
    g_text_clear = (void (*)(void *))sym("_ZN20GuiTooltipParameters4Text5ClearEv");
    g_add_line = (void (*)(void *, const void *))sym("_ZN20GuiTooltipParameters4Text7AddLineERKNS0_4LineE");
    g_set_string = (void (*)(void *, const char *))sym("_ZN20GuiTooltipParameters4Text4Line9SetStringEPKc");
    g_colour_entry = (void (*)(void *, unsigned))sym("_ZN13ColourLibrary8GetEntryEj");
    g_entry_hvec4 = (uint64_t (*)(const void *))sym("_ZNK13ColourLibrary5Entrycv5hvec4Ev");
    g_localize = (LocStr (*)(const char *))sym("_ZN12Localization20LocalizeStringFromIdEPKc");
    g_loc_string = (const char *(*)(const LocStr *))sym("_ZNK12Localization15LocalizedString9GetStringEv");
    g_loc_format =
        (int (*)(const LocStr *, char *, unsigned, ...))sym("_ZNK12Localization15LocalizedString6FormatEPcjz");
    g_fls64_assign = (void (*)(void *, const char *))sym("_ZN17FixedLengthStringILi64EEaSEPKc");
    g_delete = (void (*)(void *))sym("_ZdlPv");
    g_can_afford = (bool (*)(const PurchaseId *))sym("_ZN7Upgrade20GetCanAffordPurchaseERKNS_18PurchaseIdentifierE");
    g_skill_level = (int (*)(const void *, unsigned))sym("_ZNK4Hero19GetCombatSkillLevelEj");
    g_texture_from_file = (void (*)(void *, const char *, unsigned))sym("_Z20new_Texture_FromFile8c_stringj");
    g_image_params =
        (void (*)(void *, Vec2, const void *))sym("_ZN15ImageParametersC2E4vec214ResourceHandleI7TextureE");
    g_image = (void (*)(const void *))sym("_Z9GUI_ImageRK15ImageParameters");
    g_colour = (void (*)(unsigned))sym("_Z10GUI_Colourj");
    g_campaign_assets = (void *(*)(void))sym("_ZN2UI14CampaignAssets7CurrentEv");
    g_colour_rgba = (void (*)(uint32_t))sym("_Z10GUI_Colour4rgba");
    if (!g_can_afford || !g_skill_level || !g_colour || !g_colour_rgba || !guild_skill_pos() || !guild_tree_layout() ||
        !g_roster || !g_get_hero || !g_set_base_pos || !g_all_codes || !g_is_purchased || !g_is_locked || !g_cost ||
        !g_currency_dtor || !g_req_class || !g_tree_class || !g_tree_has_tag || !g_purchase_click || !g_text_clear ||
        !g_add_line || !g_set_string || !g_colour_entry || !g_entry_hvec4 || !g_localize || !g_loc_string ||
        !g_loc_format || !g_fls64_assign || !g_delete) {
        logf_("guild: missing symbols, off");
        return;
    }
    static const uint8_t push6_rax[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41,
                                        0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50}; // ... push rbx; push rax
    g_update_orig = (update_fn)install_hook("GuildDisplay::OnActionUpdateInternal",
                                            sym("_ZN6TownUI5Panel12GuildDisplay22OnActionUpdateInternalEf"),
                                            (void *)update_hook, push6_rax, sizeof push6_rax);
    static const uint8_t count_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x87, 0xA8, 0x00, 0x00,
                                             0x00, 0x48, 0x2B, 0x87, 0xA0, 0x00, 0x00, 0x00}; // mov/sub rax,[rdi+..]
    g_count_orig = (count_fn)install_hook("UpgradeTreeDisplay::GetNumberOfChildPanels",
                                          sym("_ZNK6TownUI5Panel18UpgradeTreeDisplay22GetNumberOfChildPanelsEv"),
                                          (void *)count_hook, count_prologue, sizeof count_prologue);
    g_show_orig = (show_fn)install_hook("UpgradeTreeDisplay::PreChildrenShowInternal",
                                        sym("_ZN6TownUI5Panel18UpgradeTreeDisplay23PreChildrenShowInternalEv"),
                                        (void *)show_hook, push6_rax, sizeof push6_rax);
    if (g_show_orig)
        ui_on_image_button(button_before, button_after);
    static const uint8_t cost_prologue[] = PROLOGUE_SUB32(0xC8, 0x00); // sub rsp,0xc8
    g_cost_orig = (cost_fn)install_hook(
        "CostDisplay::CurrencyCost",
        sym("_ZN11CostDisplay12CurrencyCostE4vec2NSt3__16vectorIN6Estate8CurrencyENS1_9allocatorIS4_EEEEbbb"),
        (void *)cost_hook, cost_prologue, sizeof cost_prologue);
    static const uint8_t tip_prologue[] = PROLOGUE_SUB32(0x88, 0x00); // sub rsp,0x88
    g_tip_orig = (tip_fn)install_hook("GuildDisplay skill tooltip",
                                      sym("_ZZZN6TownUI5Panel12GuildDisplay26OnActionEnterStartInternalEvENK3$_"
                                          "1clEjENKUlRN20GuiTooltipParameters4TextEbE_clES5_b"),
                                      (void *)tip_hook, tip_prologue, sizeof tip_prologue);
}
