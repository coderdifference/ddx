// A vertical scroll bar in the game's art (shared/widgets/, as the stash's TownUI::Panel::
// s_RealmInventoryScrollBarDescription): arrows as ui_buttons, the track and its pip by mouse (ui_input.c), and the
// wheel over the scrolled area or the bar. See common.h ("ui_scroll.c"). Used by ui_invgrid.c and a client mod
// Blacksmith panel.
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static char g_k[96];
static const char *k(const char *key, const char *name) {
    snprintf(g_k, sizeof g_k, "%s.%s", key, name);
    return g_k;
}

static void on_arrow(const uint8_t *data) { // {UiScroll *, float step (negative: up)}
    UiScroll *s;
    float step;
    memcpy(&s, data, sizeof s);
    memcpy(&step, data + sizeof s, sizeof step);
    s->pos += step; // clamped at the next draw
}

static bool tex(UiLayout *l, const char *key, const char *name, const char *fallback, uint8_t out[16]) {
    return ui_texture(ui_layout_str(l, k(key, name), fallback), out);
}

void ui_scrollbar(UiLayout *l, const char *key, UiScroll *s, float max, float step, Vec2 at, float h, Vec2 area_pos,
                  Vec2 area_size, unsigned id) {
    if (max < 0)
        max = 0;
    if (s->pos < 0)
        s->pos = 0;
    if (s->pos > max)
        s->pos = max;
    uint8_t up[16], down[16], top[16], mid[16], bottom[16], pip[16];
    if (!tex(l, key, "art.up", "shared/widgets/scrollbar_uparrow.png", up) ||
        !tex(l, key, "art.down", "shared/widgets/scrollbar_downarrow.png", down) ||
        !tex(l, key, "art.top", "shared/widgets/scrollbartop.png", top) ||
        !tex(l, key, "art.mid", "shared/widgets/scrollbarmid.png", mid) ||
        !tex(l, key, "art.bottom", "shared/widgets/scrollbarbottom.png", bottom) ||
        !tex(l, key, "art.pip", "shared/widgets/scrollpip.png", pip))
        return;
    Vec2 arrow = ui_layout_vec2(l, k(key, "arrow_size"), (Vec2){41, 25});
    float track_w = ui_layout_num(l, k(key, "track_width"), 21), cap = ui_layout_num(l, k(key, "cap_height"), 12);
    float pip_size = ui_layout_num(l, k(key, "pip_size"), 24);
    float x = at.x, y = at.y;
    float cx = x + arrow.x / 2;                   // the bar's centre line
    float ty = y + arrow.y, th = h - 2 * arrow.y; // the track
    if (th < 2 * cap + pip_size)
        return;

    uint8_t data[sizeof s + sizeof step];
    float up_step = -step;
    memcpy(data, &s, sizeof s);
    memcpy(data + sizeof s, &up_step, sizeof up_step);
    ui_button((Vec2){x, y}, up, id, s->pos > 0, on_arrow, data, sizeof data);
    memcpy(data + sizeof s, &step, sizeof step);
    ui_button((Vec2){x, y + h - arrow.y}, down, id | 1, s->pos < max, on_arrow, data, sizeof data);
    float tx = cx - track_w / 2;
    ui_image_sized((Vec2){tx, ty}, top, (Vec2){track_w, cap});
    ui_image_sized((Vec2){tx, ty + cap}, mid, (Vec2){track_w, th - 2 * cap});
    ui_image_sized((Vec2){tx, ty + th - cap}, bottom, (Vec2){track_w, cap});

    // the pip: its centre runs from the track's top + half a pip to its bottom - half a pip
    float run = th - pip_size;
    const UiMouseButton *m = ui_mouse(UI_MOUSE_LEFT);
    Vec2 track_pos = {tx - (pip_size - track_w) / 2, ty}, track_size = {fmaxf(track_w, pip_size), th};
    if (m->pressed && ui_mouse_in(track_pos, track_size))
        s->dragging = true;
    if (!m->down)
        s->dragging = false;
    if (s->dragging && run > 0) {
        float f = (ui_mouse_local().y - ty - pip_size / 2) / run;
        s->pos = fminf(fmaxf(f, 0), 1) * max;
    }
    // the wheel, over the area or the bar: a notch (1) is a step
    if (ui_mouse_in(area_pos, area_size) || ui_mouse_in((Vec2){x, y}, (Vec2){arrow.x, h}))
        s->pos -= ui_mouse_wheel_take() * step;
    if (s->pos < 0)
        s->pos = 0;
    if (s->pos > max)
        s->pos = max;
    float f = max > 0 ? s->pos / max : 0;
    ui_image_sized((Vec2){cx - pip_size / 2, ty + f * run}, pip, (Vec2){pip_size, pip_size});
}
