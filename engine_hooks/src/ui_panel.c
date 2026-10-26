// Panels: clipped regions, a scrolling panel and a box of anchored panels. See common.h ("ui_panel.c") and
// docs/11-ui-library.md. Panels draw nothing of their own (no frame, no padding): a background is the content's.
//
// How the game scrolls (TownUI::Panel::RealmInventory, the stash): GUI_PushClip(left, right, top, bottom) in the
// current transform, Transform_Push, Transform_Translate(0, -scroll), the content, Transform_Pop, GUI_PopClip. A clip
// is recorded with each widget queued under it (GUI_AppendWidget, WidgetParameters+0x188) and applied when it's drawn,
// and the game's hover test (GUI_MouseOver) rejects points outside it (View_IsPointClipped), as do drop targets: what
// the content draws with the game's GUI is hidden and inert outside the panel. ui_mouse_in does the same test.
// Clips are intersected here (in world space, Transform_LocalToWorld) before they're pushed, so panels nest.
//
// Scrolling is by pixel and eased: the wheel, the bar's arrows and the arrow overlay move a target, the shown position
// follows it (<key>.speed per second, 14), and dragging the bar's pip moves both.
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void (*g_push_clip)(float left, float right, float top, float bottom); // GUI_PushClip
static void (*g_pop_clip)(void);                                              // GUI_PopClip
static void (*g_push_unclip)(void);                                           // GUI_PushUnclip (popped by GUI_PopClip)
static void (*g_push)(void);                                                  // Transform_Push
static void (*g_pop)(void);                                                   // Transform_Pop
static void (*g_translate)(const Vec2 *v);                                    // Transform_Translate(vec2 const&)
static void (*g_depth)(float z);                                              // Transform_Translate(float)
static Vec2 (*g_to_world)(const Vec2 *v);                                     // Transform_LocalToWorld(vec2 const&)

static char g_k[96];
static const char *k(const char *key, const char *name) {
    snprintf(g_k, sizeof g_k, "%s.%s", key, name);
    return g_k;
}

static bool ready(void) {
    return g_push_clip && g_pop_clip && g_push && g_pop && g_translate && g_to_world;
}

// ---------------------------------------------------------------- clips
#define MAX_CLIPS 16
static struct {
    float x0, y0, x1, y1;
} g_clips[MAX_CLIPS]; // world space, intersected with the ones below
static int g_nclips;

bool ui_clip_push(Vec2 pos, Vec2 size) {
    if (!ready() || g_nclips == MAX_CLIPS)
        return false;
    Vec2 o = g_to_world(&(Vec2){0, 0}), u = g_to_world(&(Vec2){1, 1});
    float sx = u.x - o.x, sy = u.y - o.y; // UI transforms translate and scale
    if (sx == 0 || sy == 0)
        return false;
    Vec2 a = g_to_world(&pos), b = g_to_world(&(Vec2){pos.x + size.x, pos.y + size.y});
    float x0 = fminf(a.x, b.x), x1 = fmaxf(a.x, b.x), y0 = fminf(a.y, b.y), y1 = fmaxf(a.y, b.y);
    if (g_nclips) {
        x0 = fmaxf(x0, g_clips[g_nclips - 1].x0);
        x1 = fminf(x1, g_clips[g_nclips - 1].x1);
        y0 = fmaxf(y0, g_clips[g_nclips - 1].y0);
        y1 = fminf(y1, g_clips[g_nclips - 1].y1);
        if (x1 < x0)
            x1 = x0;
        if (y1 < y0)
            y1 = y0;
    }
    g_clips[g_nclips++] = (typeof(g_clips[0])){x0, y0, x1, y1};
    float lx0 = (x0 - o.x) / sx, lx1 = (x1 - o.x) / sx, ly0 = (y0 - o.y) / sy, ly1 = (y1 - o.y) / sy; // back to local
    g_push_clip(fminf(lx0, lx1), fmaxf(lx0, lx1), fminf(ly0, ly1), fmaxf(ly0, ly1));
    return true;
}

void ui_clip_pop(void) {
    if (!g_nclips || !g_pop_clip)
        return;
    g_nclips--;
    g_pop_clip();
}

// ---------------------------------------------------------------- the scroll panel
UiScrollType ui_scroll_type_parse(const char *s, UiScrollType fallback) {
    static const struct {
        const char *name;
        UiScrollType t;
    } names[] = {{"off", UI_SCROLL_OFF},           {"hidden", UI_SCROLL_HIDDEN},
                 {"arrow", UI_SCROLL_ARROW},       {"auto_arrow", UI_SCROLL_AUTO_ARROW},
                 {"scroller", UI_SCROLL_SCROLLER}, {"auto_scroller", UI_SCROLL_AUTO_SCROLLER},
                 {"on", UI_SCROLL_SCROLLER},       {"auto", UI_SCROLL_AUTO_SCROLLER}};
    for (size_t i = 0; s && i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(s, names[i].name))
            return names[i].t;
    return fallback;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static bool bar_wanted(const UiScrollPanel *p) {
    return p->type == UI_SCROLL_SCROLLER || (p->type == UI_SCROLL_AUTO_SCROLLER && p->content > p->size.y + 0.5f);
}

float ui_scrollpanel_begin(UiScrollPanel *p, UiLayout *l, const char *key, UiScrollType type, Vec2 pos, Vec2 size) {
    p->l = l;
    snprintf(p->key, sizeof p->key, "%s", key ? key : "scroll");
    p->type = type;
    p->pos = pos;
    p->size = size;
    p->bar = bar_wanted(p); // from last frame's content height
    p->width = size.x - (p->bar ? ui_layout_num(l, k(p->key, "bar.inset"), 41) : 0);
    if (p->width < 0)
        p->width = 0;
    p->open = ready() && ui_clip_push(pos, (Vec2){p->width, size.y});
    if (!p->open)
        return p->width;
    g_push();
    float shown = type == UI_SCROLL_OFF ? 0 : roundf(p->shown);
    g_translate(&(Vec2){pos.x, pos.y - shown});
    return p->width;
}

// an arrow over the panel's bottom (down: more below) or top (up: more above): ------v------- in three pieces (the ends
// stretched, the V as it is); a click on it pages that way
static void arrow(UiScrollPanel *p, bool up) {
    uint8_t l[16], c[16], r[16];
    const char *dir = up ? "arrow.up" : "arrow.down";
    char key[64];
#define ART(piece, fallback)                                                                                           \
    (snprintf(key, sizeof key, "%s.art.%s", dir, piece), ui_layout_str(p->l, k(p->key, key), fallback))
    if (!ui_texture(ART("l", up ? "shared/engine_panel/engine_scroll_arrow_up_l.png"
                                : "shared/engine_panel/engine_scroll_arrow_l.png"),
                    l) ||
        !ui_texture(ART("c", up ? "shared/engine_panel/engine_scroll_arrow_up_c.png"
                                : "shared/engine_panel/engine_scroll_arrow_c.png"),
                    c) ||
        !ui_texture(ART("r", up ? "shared/engine_panel/engine_scroll_arrow_up_r.png"
                                : "shared/engine_panel/engine_scroll_arrow_r.png"),
                    r))
        return;
#undef ART
    Vec2 v = ui_layout_vec2(p->l, k(p->key, "arrow.v_size"), (Vec2){30, 20});
    float inset = ui_layout_num(p->l, k(p->key, "arrow.inset"), 12),
          edge = ui_layout_num(p->l, k(p->key, "arrow.edge"), 2);
    float y = up ? p->pos.y + edge : p->pos.y + p->size.y - v.y - edge;
    float x0 = p->pos.x + inset, x1 = p->pos.x + p->width - inset, cx = p->pos.x + p->width / 2;
    if (x1 - x0 < v.x)
        return;
    if (g_depth) {
        g_push();
        g_depth(ui_layout_num(p->l, k(p->key, "arrow.depth"), 2));
    }
    uint8_t shade[16]; // a dark band from the edge, so the arrow reads over the text
    float sh = ui_layout_num(p->l, k(p->key, "arrow.shade_height"), 40);
    if (sh > 0 && ui_texture(up ? "shared/engine_panel/engine_scroll_shade_up.png"
                                : "shared/engine_panel/engine_scroll_shade_down.png",
                             shade))
        ui_image_sized((Vec2){p->pos.x, up ? p->pos.y : p->pos.y + p->size.y - sh}, shade, (Vec2){p->width, sh});
    ui_image_sized((Vec2){x0, y}, l, (Vec2){cx - v.x / 2 - x0, v.y});
    ui_image_sized((Vec2){cx - v.x / 2, y}, c, v);
    ui_image_sized((Vec2){cx + v.x / 2, y}, r, (Vec2){x1 - cx - v.x / 2, v.y});
    if (g_depth)
        g_pop();
    float page = p->size.y * ui_layout_num(p->l, k(p->key, "arrow.page"), 0.8f);
    if (ui_mouse_in((Vec2){x0, y}, (Vec2){x1 - x0, v.y}) && ui_click(UI_MOUSE_LEFT, 8))
        p->scroll.pos += up ? -page : page; // clamped by the caller
}

bool ui_scroll_rows(float shown, float view_h, float row_h, float top_pad, int margin, int n, int *first, int *last) {
    if (n <= 0 || row_h <= 0)
        return false;
    float top = roundf(shown) - top_pad; // as ui_scrollpanel_begin translates
    int a = (int)floorf(top / row_h) - margin, b = (int)ceilf((top + view_h) / row_h) + margin;
    if (a < 0)
        a = 0;
    if (b > n - 1)
        b = n - 1;
    if (a > b)
        return false;
    *first = a;
    *last = b;
    return true;
}

void ui_scrollpanel_end(UiScrollPanel *p, float content_height) {
    if (p->open) {
        g_pop();
        ui_clip_pop();
        p->open = false;
    }
    p->content = content_height;
    float max = fmaxf(0, content_height - p->size.y);
    double t = now(), dt = p->t > 0 ? t - p->t : 0;
    p->t = t;
    if (dt > 0.25)
        dt = 0.25;
    if (p->type == UI_SCROLL_OFF) {
        p->scroll.pos = p->shown = 0;
        return;
    }
    float step = p->step > 0 ? p->step : ui_layout_num(p->l, k(p->key, "step"), 48);
    if (p->bar && bar_wanted(p)) { // the bar takes the wheel over the panel and itself
        float bw = ui_layout_num(p->l, k(p->key, "bar.inset"), 41);
        Vec2 off = ui_layout_vec2(p->l, k(p->key, "bar.offset"), (Vec2){0, 0});
        char bar[64];
        snprintf(bar, sizeof bar, "%s.bar", p->key);
        unsigned id = 0x5d000000u | ((unsigned)(uintptr_t)p & 0x00fffffeu); // unique per panel (its state)
        float depth = ui_layout_num(p->l, k(p->key, "bar.depth"), 0);
        bool unclip = g_push_unclip && ui_layout_num(p->l, k(p->key, "bar.unclip"), 0);
        if (unclip)
            g_push_unclip(); // outside a clip the panel sits in (a game panel's)
        if (depth && g_depth) {
            g_push();
            g_depth(depth);
        }
        ui_scrollbar(p->l, bar, &p->scroll, max, step, (Vec2){p->pos.x + p->size.x - bw + off.x, p->pos.y + off.y},
                     p->size.y, p->pos, p->size, id);
        if (depth && g_depth)
            g_pop();
        if (unclip)
            g_pop_clip();
    } else if (max > 0 && ui_mouse_in(p->pos, p->size))
        p->scroll.pos -= ui_mouse_wheel_take() * step;
    if (p->scroll.pos < 0)
        p->scroll.pos = 0;
    if (p->scroll.pos > max)
        p->scroll.pos = max;
    if (p->type == UI_SCROLL_ARROW || p->type == UI_SCROLL_AUTO_ARROW) { // by what's shown: more below, more above
        if (p->shown < max - 0.5f)
            arrow(p, false);
        if (p->shown > 0.5f)
            arrow(p, true);
        if (p->scroll.pos < 0)
            p->scroll.pos = 0;
        if (p->scroll.pos > max)
            p->scroll.pos = max;
    }
    // ease the shown position toward the target (dragging the pip: at once)
    float speed = ui_layout_num(p->l, k(p->key, "speed"), 14);
    if (p->scroll.dragging || speed <= 0)
        p->shown = p->scroll.pos;
    else {
        float f = (float)fmin(1.0, speed * dt);
        p->shown += (p->scroll.pos - p->shown) * f;
        if (fabsf(p->scroll.pos - p->shown) < 0.5f)
            p->shown = p->scroll.pos;
    }
    if (p->shown > max)
        p->shown = max;
    if (p->shown < 0)
        p->shown = 0;
}

void ui_scrollpanel_reset(UiScrollPanel *p) {
    p->scroll.pos = p->shown = 0;
    p->scroll.dragging = false;
}

// ---------------------------------------------------------------- the panel box
void ui_panelbox_begin(UiPanelBox *b, UiLayout *l, const char *key, Vec2 pos, Vec2 size) {
    b->l = l;
    snprintf(b->key, sizeof b->key, "%s", key ? key : "box");
    float top = fmaxf(0, ui_layout_num(l, k(b->key, "top"), 0)),
          bottom = fmaxf(0, ui_layout_num(l, k(b->key, "bottom"), 0));
    float left = fmaxf(0, ui_layout_num(l, k(b->key, "left"), 0)),
          right = fmaxf(0, ui_layout_num(l, k(b->key, "right"), 0));
    if (top + bottom > size.y)
        bottom = fmaxf(0, size.y - top);
    if (left + right > size.x)
        right = fmaxf(0, size.x - left);
    float my = pos.y + top, mh = size.y - top - bottom; // the middle band, between top and bottom
    b->part[UI_BOX_TOP] = (UiRect){{pos.x, pos.y}, {size.x, top}};
    b->part[UI_BOX_BOTTOM] = (UiRect){{pos.x, pos.y + size.y - bottom}, {size.x, bottom}};
    b->part[UI_BOX_LEFT] = (UiRect){{pos.x, my}, {left, mh}};
    b->part[UI_BOX_RIGHT] = (UiRect){{pos.x + size.x - right, my}, {right, mh}};
    b->part[UI_BOX_CENTRE] = (UiRect){{pos.x + left, my}, {size.x - left - right, mh}};
    b->open = -1;
}

bool ui_panelbox_part(UiPanelBox *b, UiBoxPart part, Vec2 *size) {
    if (part < UI_BOX_TOP || part > UI_BOX_RIGHT || b->open >= 0)
        return false;
    UiRect r = b->part[part];
    if (r.size.x <= 0 || r.size.y <= 0 || !ready() || !ui_clip_push(r.pos, r.size))
        return false;
    g_push();
    g_translate(&r.pos);
    b->open = part;
    if (size)
        *size = r.size;
    return true;
}

void ui_panelbox_part_end(UiPanelBox *b) {
    if (b->open < 0)
        return;
    g_pop();
    ui_clip_pop();
    b->open = -1;
}

float ui_panelbox_centre(UiPanelBox *b, float *height) {
    UiRect r = b->part[UI_BOX_CENTRE];
    if (height)
        *height = r.size.y;
    UiScrollType t = ui_scroll_type_parse(ui_layout_str(b->l, k(b->key, "scroll"), "auto"), UI_SCROLL_AUTO_SCROLLER);
    char key[64];
    snprintf(key, sizeof key, "%s.centre", b->key);
    return ui_scrollpanel_begin(&b->centre, b->l, key, t, r.pos, r.size);
}

void ui_panelbox_centre_end(UiPanelBox *b, float content_height) {
    ui_scrollpanel_end(&b->centre, content_height);
}

// ---------------------------------------------------------------- install
void ui_panel_install(void) {
    g_push_clip = (void (*)(float, float, float, float))sym("_Z12GUI_PushClipffff");
    g_pop_clip = (void (*)(void))sym("_Z11GUI_PopClipv");
    g_push_unclip = (void (*)(void))sym("_Z14GUI_PushUnclipv");
    g_push = (void (*)(void))sym("_Z14Transform_Pushv");
    g_pop = (void (*)(void))sym("_Z13Transform_Popv");
    g_translate = (void (*)(const Vec2 *))sym("_Z19Transform_TranslateRK4vec2");
    g_depth = (void (*)(float))sym("_Z19Transform_Translatef");
    g_to_world = (Vec2 (*)(const Vec2 *))sym("_Z22Transform_LocalToWorldRK4vec2");
    if (!ready())
        logf_("ui_panel: clip or transform functions missing, panels off");
}
