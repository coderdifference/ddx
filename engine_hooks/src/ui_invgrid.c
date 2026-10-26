// Inventory grids: the one owner of the game's grid drawing, for observers (ui_invgrid_on) and scrolling
// (ui_invgrid_scroll). See common.h ("ui_invgrid.c") and docs/11-ui-library.md.
//
// Inventory::Display::InventoryButtonGrid(InventorySystemInterface*, Actor const*, GridLayout const&, vec2,
// InventoryButtonGridParameters const&, InventoryButtonGridDisplayInfo*) draws every mouse-driven inventory grid (the
// estate stash, the Provisioner's store and the party's supplies, the party's inventory in a dungeon; with a controller
// the Provisioner calls the Inventory::System overload instead, which is not scrolled). Cell i is the interface's slot
// interface i (a std::vector<InventorySlotInterface*> at +0x10 / +0x18 / +0x20; GetSlotCount() is its size), drawn
// at GridLayout start (+0x04) + (i % columns (+0x00), i / columns) * step (+0x0c). It draws up to the row after the
// storage's last item, at most GetSlotCount() cells, each with InventoryButtonInGrid(actor, item, i, ..., the slot
// interface as DragContext, +0x10 as DropContext, display, params).
//
// Scrolling: a registered grid (by its storage's config type) is drawn whole inside a scroll panel (ui_panel.c, the
// stash's own way: clipped and shifted), its viewport visible_rows rows of the grid's step tall (or a height the client
// gives; one that returns 0 leaves the grid unscrolled that frame, the game's), with the normal scroll bar right of
// it; the wheel moves a row. Cells outside the viewport are clipped and
// inert (the game's hover and drop tests honour the clip), so the cell indices, clicks and drags are the grid's own.
// Layout per client key, read every frame: .scroll (auto_scroller), .visible_rows (2), and the scroll panel's settings
// (.bar.offset, .bar.depth: the dungeon panel draws over its grid at +7, .bar.unclip, ...).
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_OBSERVERS 8
#define MAX_SCROLLED  8
#define MAX_STATES    8

typedef void *(*GridFn)(void *iface, const void *actor, const void *layout, Vec2 pos, const void *params, void *info);
static GridFn o_grid;
static void *(*g_iface_system)(const void *iface);

static struct {
    UiInvGridBegin begin;
    UiInvGridEnd end;
} g_obs[MAX_OBSERVERS];
static int g_nobs;
static ModsList g_obs_mods; // their mods (only an applied mod's run; the top mod's last)

// owner: the registering mod (mods_current); a setting applies only while the campaign applies its mod, the highest
// in the campaign's list winning where two scroll the same config type (the library's own: lowest)
static struct {
    unsigned type;
    UiLayout *layout;
    char key[48];
    UiInvGridHeight height;
    int owner;
} g_scrolled[MAX_SCROLLED];
static int g_nscrolled;

typedef struct {
    const void *iface;
    UiScrollPanel panel;
} State;
static State g_states[MAX_STATES];

void ui_invgrid_on(UiInvGridBegin begin, UiInvGridEnd end) {
    if (g_nobs < MAX_OBSERVERS && mods_list_add(&g_obs_mods) >= 0) {
        g_obs[g_nobs].begin = begin;
        g_obs[g_nobs].end = end;
        g_nobs++;
    } else
        logf_("ui_invgrid: too many observers");
}

void ui_invgrid_scroll(const char *config_type, UiLayout *layout, const char *key) {
    if (g_nscrolled == MAX_SCROLLED || !config_type || !layout || !key) {
        logf_("ui_invgrid: scroll %s refused", config_type ? config_type : "?");
        return;
    }
    g_scrolled[g_nscrolled].type = name_hash(config_type);
    g_scrolled[g_nscrolled].layout = layout;
    snprintf(g_scrolled[g_nscrolled].key, sizeof g_scrolled[g_nscrolled].key, "%s", key);
    g_scrolled[g_nscrolled].height = NULL;
    g_scrolled[g_nscrolled].owner = mods_current();
    g_nscrolled++;
}

void ui_invgrid_scroll_height(const char *config_type, UiInvGridHeight height) {
    unsigned type = config_type ? name_hash(config_type) : 0;
    for (int i = 0; i < g_nscrolled; i++)
        if (g_scrolled[i].type == type)
            g_scrolled[i].height = height;
}

bool ui_invgrid_ready(void) {
    return o_grid != NULL;
}

static State *state(const void *iface, bool create) {
    State *free_s = NULL;
    for (int i = 0; i < MAX_STATES; i++) {
        if (g_states[i].iface == iface)
            return &g_states[i];
        if (!g_states[i].iface && !free_s)
            free_s = &g_states[i];
    }
    if (!create)
        return NULL;
    if (!free_s) {
        memset(g_states, 0, sizeof g_states);
        free_s = &g_states[0];
    } // full: start over
    memset(free_s, 0, sizeof *free_s);
    free_s->iface = iface;
    return free_s;
}

void ui_invgrid_reset(const void *iface) {
    State *s = state(iface, false);
    if (s)
        ui_scrollpanel_reset(&s->panel);
}

static char g_k[96];
static const char *k(const char *key, const char *name) {
    snprintf(g_k, sizeof g_k, "%s.%s", key, name);
    return g_k;
}

// The grid's background, tiled: <key>.tile.art (one cell's picture) under every cell in view, drawn as the panel's
// content (it scrolls with the cells; pictures take no clicks), tile.size (the cell's step) at tile.offset from the
// cell's top left. For grids whose game background is a fixed picture of a few rows. tile.start ([x, y], optional): only
// grids laid out from that GridLayout start_pos (the grid the art was cut for: one config type can have several grids,
// as "raid" in the Provisioner and in a dungeon).
static void tiles(UiLayout *l, const char *key, unsigned cols, unsigned rows, Vec2 start, Vec2 step, float shown,
                  float view_h) {
    uint8_t tex[16];
    const char *art = ui_layout_str(l, k(key, "tile.art"), "");
    if (!art[0] || step.y <= 0)
        return;
    Vec2 only = ui_layout_vec2(l, k(key, "tile.start"), start);
    if (fabsf(only.x - start.x) > 0.5f || fabsf(only.y - start.y) > 0.5f || !ui_texture(art, tex))
        return;
    Vec2 size = ui_layout_vec2(l, k(key, "tile.size"), step),
         off = ui_layout_vec2(l, k(key, "tile.offset"), (Vec2){0, 0});
    unsigned first = shown > 0 ? (unsigned)(shown / step.y) : 0, last = (unsigned)((shown + view_h) / step.y) + 1;
    for (unsigned r = first; r < rows && r <= last; r++)
        for (unsigned c = 0; c < cols; c++)
            ui_image_sized((Vec2){c * step.x + off.x, r * step.y + off.y}, tex, size);
}

// ---------------------------------------------------------------- hidden cells
// For a cell that's clipped (GUI_GetIsClipped: scrolled out of view) and holds an item, InventoryButtonGrid draws
// nothing but adds a GUI_HotSpot with the clip lifted (GUI_PushUnclip; for the controller's focus, presumably). Lifted,
// it takes the mouse wherever the hidden cell lies: a scrolled stash's top row took the tabs above it. With the
// mouse that hot spot is never wanted (its cell is out of view), so it's skipped unless a controller is in use.
#ifdef _WIN32
#define GRID_HIDDEN_HOTSPOT 0xc94 // the call's offset in InventoryButtonGrid (0x7df1b4)
#else
#define GRID_HIDDEN_HOTSPOT 0xaca // the call's offset in InventoryButtonGrid (macOS)
#endif
static const uint8_t *g_hidden_site;
static bool (*g_using_controller)(void);

static bool hidden_hotspot(const uint8_t *params) {
    (void)params;
    return ui_hotspot_caller() != g_hidden_site || (g_using_controller && g_using_controller());
}

// ---------------------------------------------------------------- hooks
static void *h_grid(void *iface, const void *actor, const void *layout, Vec2 pos, const void *params, void *info) {
    MODS_EACH(g_obs_mods, false, i) if (g_obs[i].begin) g_obs[i].begin(iface);

    // scrolled? (its storage's config type was registered)
    int sc = -1;
    void *system = iface && g_iface_system ? g_iface_system(iface) : NULL;
    unsigned type = system ? *(const unsigned *)((const uint8_t *)system + SYSTEM_CONFIG) : 0;
    for (int i = 0; i < g_nscrolled && system; i++) {
        int o = g_scrolled[i].owner;
        if (g_scrolled[i].type != type || !mods_active(o))
            continue;
        if (sc < 0 || (o >= 0 && (g_scrolled[sc].owner < 0 || mods_pos(o) < mods_pos(g_scrolled[sc].owner))))
            sc = i;
    }
    void *const *const *v = iface ? (void *const *const *)((uint8_t *)iface + IFACE_SLOTS) : NULL;
    unsigned cols = layout ? *(const unsigned *)((const uint8_t *)layout + GRID_COLUMNS) : 0;
    unsigned n = v && v[0] ? (unsigned)(v[1] - v[0]) : 0;
    State *s = sc >= 0 && cols ? state(iface, true) : NULL;
    void *hovered;
    Vec2 start = layout ? *(const Vec2 *)((const uint8_t *)layout + GRID_START) : (Vec2){0, 0};
    Vec2 step = layout ? *(const Vec2 *)((const uint8_t *)layout + GRID_STEP) : (Vec2){0, 0};
    Vec2 grid_pos = {pos.x + start.x, pos.y + start.y};
    float view_h = 0; // the viewport; a height callback's 0: not scrolled this frame
    if (s && ui_layout_num(g_scrolled[sc].layout, k(g_scrolled[sc].key, "off"), 0) == 0)
        view_h = g_scrolled[sc].height
                     ? g_scrolled[sc].height(iface, grid_pos)
                     : ui_layout_num(g_scrolled[sc].layout, k(g_scrolled[sc].key, "visible_rows"), 2) * step.y;
    if (view_h > 0) {
        UiLayout *l = g_scrolled[sc].layout;
        const char *key = g_scrolled[sc].key;
        float rows = (float)((n + cols - 1) / cols), grid_w = (float)cols * step.x;
        float bar = ui_layout_num(l, k(key, "bar.inset"), 41);
        UiScrollType t =
            ui_scroll_type_parse(ui_layout_str(l, k(key, "scroll"), "auto_scroller"), UI_SCROLL_AUTO_SCROLLER);
        if (!s->panel.l)
            logf_("ui_invgrid: %s grid %p scrolls: %u cells, %u columns, %.0f px of %.0f shown", key, iface, n, cols,
                  view_h, rows * step.y);
        s->panel.step = step.y; // a wheel notch or an arrow: a row
        ui_scrollpanel_begin(&s->panel, l, key, t, grid_pos, (Vec2){grid_w + bar, view_h});
        tiles(l, key, cols, (unsigned)rows, start, step, s->panel.shown, view_h);
        hovered = o_grid(iface, actor, layout, (Vec2){-start.x, -start.y}, params,
                         info); // its cells from the panel's top left
        ui_scrollpanel_end(&s->panel, rows * step.y);
    } else
        hovered = o_grid(iface, actor, layout, pos, params, info);
    MODS_EACH(g_obs_mods, false, i) if (g_obs[i].end) g_obs[i].end(iface, hovered);
    return hovered;
}

void ui_invgrid_install(void) {
    g_iface_system = (void *(*)(const void *))sym("_ZNK24InventorySystemInterface18GetInventorySystemEv");
    if (!g_iface_system) {
        logf_("ui_invgrid: symbols missing, off");
        return;
    }
    static const uint8_t grid[] = PROLOGUE_SUB32(0xb8, 0x09);
    void *grid_fn =
        sym("_ZN9Inventory7Display19InventoryButtonGridEP24InventorySystemInterfacePK5ActorRKNS0_"
            "10GridLayoutE4vec2RKNS0_29InventoryButtonGridParametersEPNS0_30InventoryButtonGridDisplayInfoE");
    g_hidden_site = call_site_return(grid_fn, GRID_HIDDEN_HOTSPOT,
                                     sym("_Z11GUI_HotSpotRK17HotSpotParametersNSt3__18functionIFvvEEES5_S5_S5_"));
    g_using_controller = (bool (*)(void))sym("_Z23Input_IsUsingControllerv");
    if (g_hidden_site)
        ui_on_hotspot(hidden_hotspot, NULL);
    else
        logf_("ui_invgrid: the hidden cells' hot spot call wasn't found; scrolled-out cells keep taking the mouse");
    *(void **)&o_grid =
        install_hook("Inventory::Display::InventoryButtonGrid", grid_fn, (void *)h_grid, grid, sizeof grid);
}
