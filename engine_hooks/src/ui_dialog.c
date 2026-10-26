// ui_dialog: a dialog frame drawn at any size from pieces, and where to put it (see common.h, "ui_dialog.c").
//
// The frame is a nine-slice whose top edge is split by an ornament: corners at their own size, the edges and the middle
// stretched, the top edge in two (tw, te: each may differ, to meet the ornament without a seam), the ornament centred on
// the top at its own size; splatter pieces outside the corners go first, each at its own size at an offset from its
// corner, so a bigger frame spreads them apart and a smaller one overlaps them. All pieces are drawn in the GUI's order
// (ui_image_sized), so the panel's later controls and tooltips lie over the frame.
//
// The screen's centre in the current transform: plinthGetCanvasSize(float&, float&) gives the canvas (the render
// target, in pixels) and Transform_CanvasToLocal(vec3 const&) maps a canvas point back through the projection and the
// current transform (it returns a vec3: x, y in xmm0). macOS only; elsewhere the centre is the 1920 x 1080 GUI's.
#include "common.h"
#include <stdio.h>
#include <string.h>

static const char *key(const char *style, const char *name, char *buf, size_t cap) {
    snprintf(buf, cap, "%s.%s", style, name);
    return buf;
}

static void piece(const char *art, const char *name, Vec2 pos, Vec2 size) {
    char path[256];
    uint8_t tex[16];
    snprintf(path, sizeof path, "%s_%s.png", art, name);
    if ((size.x > 0 && size.y > 0) || (size.x == 0 && size.y == 0)) // a stretched piece squeezed to nothing: skipped
        if (ui_texture(path, tex))
            ui_image_sized(pos, tex, size);
}

void ui_dialog_frame(UiLayout *l, const char *style, Vec2 pos, Vec2 size) {
    char k[128];
    const char *art = ui_layout_str(l, key(style, "art", k, sizeof k), "");
    if (!art[0])
        return;
    float bl = ui_layout_num(l, key(style, "border.left", k, sizeof k), 20),
          bt = ui_layout_num(l, key(style, "border.top", k, sizeof k), 20);
    float br = ui_layout_num(l, key(style, "border.right", k, sizeof k), 20),
          bb = ui_layout_num(l, key(style, "border.bottom", k, sizeof k), 20);
    float x0 = pos.x, y0 = pos.y, x1 = pos.x + size.x, y1 = pos.y + size.y;
    float mw = size.x - bl - br, mh = size.y - bt - bb;
    // the splatter, behind everything, from its corners
    static const char *const corners[4] = {"tl", "tr", "bl", "br"};
    for (int i = 0; i < 4; i++) {
        char name[32];
        snprintf(name, sizeof name, "splatter.%s", corners[i]);
        Vec2 off = ui_layout_vec2(l, key(style, name, k, sizeof k), (Vec2){0, 0});
        snprintf(name, sizeof name, "splat_%s", corners[i]);
        Vec2 at = {(i & 1 ? x1 : x0) + off.x, (i & 2 ? y1 : y0) + off.y};
        if (ui_layout_num(l, key(style, "splatter.show", k, sizeof k), 1))
            piece(art, name, at, (Vec2){0, 0});
    }
    // the nine-slice: the middle row and the bottom, then the top with its ornament over it
    piece(art, "l", (Vec2){x0, y0 + bt}, (Vec2){bl, mh});
    piece(art, "c", (Vec2){x0 + bl, y0 + bt}, (Vec2){mw, mh});
    piece(art, "r", (Vec2){x1 - br, y0 + bt}, (Vec2){br, mh});
    piece(art, "bl", (Vec2){x0, y1 - bb}, (Vec2){0, 0});
    piece(art, "b", (Vec2){x0 + bl, y1 - bb}, (Vec2){mw, bb});
    piece(art, "br", (Vec2){x1 - br, y1 - bb}, (Vec2){0, 0});
    Vec2 orn = ui_layout_vec2(l, key(style, "ornament", k, sizeof k), (Vec2){0, 0}); // its size; 0: none
    float ox = x0 + (size.x - orn.x) / 2;
    if (orn.x > 0) { // the band either side of it, then the ornament
        piece(art, "tw", (Vec2){x0 + bl, y0}, (Vec2){ox - x0 - bl, bt});
        piece(art, "te", (Vec2){ox + orn.x, y0}, (Vec2){x1 - br - ox - orn.x, bt});
    } else
        piece(art, "tw", (Vec2){x0 + bl, y0}, (Vec2){mw, bt});
    piece(art, "tl", (Vec2){x0, y0}, (Vec2){0, 0});
    piece(art, "tr", (Vec2){x1 - br, y0}, (Vec2){0, 0});
    if (orn.x > 0)
        piece(art, "ornament", (Vec2){ox, y0}, orn);
}

Vec2 ui_screen_centre(void) {
    static bool resolved;
    static void (*canvas)(float *, float *);
    static Vec2 (*to_local)(const float *);
    if (!resolved) {
        resolved = true;
        canvas = (void (*)(float *, float *))sym("_Z19plinthGetCanvasSizeRfS_");
        to_local = (Vec2 (*)(const float *))sym("_Z23Transform_CanvasToLocalRK4vec3");
        if (!canvas || !to_local)
            logf_("dialog: canvas symbols missing, the screen's centre is the GUI's (960, 540)");
    }
    if (!canvas || !to_local)
        return (Vec2){960, 540};
    float w = 0, h = 0;
    canvas(&w, &h);
    float c[3] = {w / 2, h / 2, 0};
    return to_local(c);
}

void ui_screen_bounds(Vec2 *top_left, Vec2 *bottom_right) {
    static bool resolved;
    static void (*canvas)(float *, float *);
    static Vec2 (*to_local)(const float *);
    if (!resolved) {
        resolved = true;
        canvas = (void (*)(float *, float *))sym("_Z19plinthGetCanvasSizeRfS_");
        to_local = (Vec2 (*)(const float *))sym("_Z23Transform_CanvasToLocalRK4vec3");
    }
    if (!canvas || !to_local) {
        if (top_left)
            *top_left = (Vec2){0, 0};
        if (bottom_right)
            *bottom_right = (Vec2){1920, 1080};
        return;
    }
    float w = 0, h = 0;
    canvas(&w, &h);
    float p[4][3] = {{0, 0, 0}, {w, 0, 0}, {0, h, 0}, {w, h, 0}};
    Vec2 a = to_local(p[0]);
    float min_x = a.x, max_x = a.x, min_y = a.y, max_y = a.y;
    for (int i = 1; i < 4; i++) {
        Vec2 q = to_local(p[i]);
        if (q.x < min_x)
            min_x = q.x;
        if (q.x > max_x)
            max_x = q.x;
        if (q.y < min_y)
            min_y = q.y;
        if (q.y > max_y)
            max_y = q.y;
    }
    if (top_left)
        *top_left = (Vec2){min_x, min_y};
    if (bottom_right)
        *bottom_right = (Vec2){max_x, max_y};
}

UiPlace ui_place_parse(const char *s) {
    if (s && !strcmp(s, "centre"))
        return UI_PLACE_CENTRE;
    if (s && !strcmp(s, "top_left"))
        return UI_PLACE_TOP_LEFT;
    return UI_PLACE_SCREEN;
}

Vec2 ui_dialog_place(UiPlace place, Vec2 point, Vec2 size) {
    if (place == UI_PLACE_TOP_LEFT)
        return point;
    Vec2 c = place == UI_PLACE_CENTRE ? point : ui_screen_centre();
    return (Vec2){c.x - size.x / 2, c.y - size.y / 2};
}
