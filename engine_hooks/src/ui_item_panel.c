// Reusable item-description panel: layout-driven regions, scrolling, and an optional resolve badge.
#include "common.h"
#include <math.h>
#include <stdio.h>

static void key(char *out, size_t cap, const char *base, const char *name) {
    snprintf(out, cap, "%s.%s", base, name);
}

void ui_level_badge(UiLayout *layout, const char *base, Vec2 centre, Vec2 size, int level) {
    UiLayout *tips = ui_layout_mod("tooltips.layout.json");
    char path[256], number[16], k[128];
    snprintf(path, sizeof path,
             ui_layout_str(tips, "resolve_badge.icon",
                           "shared/resolve_level_bar/resolve_level_bar_number_background_lvl%d.png"),
             level < 0   ? 0
             : level > 6 ? 6
                         : level);
    snprintf(number, sizeof number, "%d", level);
    uint8_t texture[16];
    if (ui_texture(path, texture))
        ui_image_sized((Vec2){centre.x - size.x / 2, centre.y - size.y / 2}, texture, size);
    key(k, sizeof k, base, "font");
    const char *font = ui_layout_str(layout, k, "resolve_number");
    key(k, sizeof k, base, "text_offset");
    Vec2 offset = ui_layout_vec2(layout, k, ui_layout_vec2(tips, "resolve_badge.text_offset", (Vec2){0, 0}));
    key(k, sizeof k, base, "colour");
    const char *colour = ui_layout_str(layout, k, "resolve_number");
    ui_text_anchored((Vec2){centre.x + offset.x, centre.y - ui_text_height(font, number, 0) / 2 + offset.y}, number,
                     font, colour, 1, 0);
}

void ui_item_panel_reset(UiPanelBox *state) {
    if (state)
        ui_scrollpanel_reset(&state->centre);
}

UiRect ui_item_panel(UiPanelBox *state, UiLayout *layout, const char *base, const uint8_t *item, const void *hero,
                     int resolve, Vec2 pos, Vec2 size) {
    UiRect bounds = {pos, size};
    if (!state || !item || !base)
        return bounds;
    char k[128], box[128], badge[128];
    key(k, sizeof k, base, "inset");
    Vec2 inset = ui_layout_vec2(layout, k, (Vec2){16, 12});
    key(k, sizeof k, base, "badge.size");
    Vec2 bs = ui_layout_vec2(
        layout, k, ui_layout_vec2(ui_layout_mod("tooltips.layout.json"), "resolve_badge.size", (Vec2){64, 64}));
    key(k, sizeof k, base, "badge.pad");
    Vec2 pad = ui_layout_vec2(layout, k, (Vec2){4, 4});
    key(box, sizeof box, base, "box");
    key(k, sizeof k, box, "top");
    float top = ui_layout_num(layout, k, 0), inner_width = size.x - 2 * inset.x;
    const ItemContent *content = item_describe(item, hero, inner_width);
    if (!content)
        return bounds;
    unsigned regions = top > 0 ? ITEM_REGIONS_BODY : ITEM_REGIONS_ALL;
    if (size.y <= 0)
        bounds.size.y = 2 * inset.y + top + item_content_size(content, regions).y;
    key(k, sizeof k, base, "background");
    const char *bg = ui_layout_str(layout, k, "tooltip");
    key(k, sizeof k, bg, "edge");
    float edge = *bg ? ui_layout_num(ui_layout_mod("dialog.layout.json"), k, 0) : 0;
    if (resolve >= 0) {
        if (bounds.size.y < edge + pad.y + bs.y)
            bounds.size.y = edge + pad.y + bs.y;
        if (bounds.size.x < edge + pad.x + bs.x)
            bounds.size.x = edge + pad.x + bs.x;
    }
    if (*bg)
        ui_dialog_frame(ui_layout_mod("dialog.layout.json"), bg, bounds.pos, bounds.size);
    ui_panelbox_begin(state, layout, box, (Vec2){pos.x + inset.x, pos.y + inset.y},
                      (Vec2){bounds.size.x - 2 * inset.x, bounds.size.y - 2 * inset.y});
    Vec2 title_size;
    if (top > 0 && ui_panelbox_part(state, UI_BOX_TOP, &title_size)) {
        item_content_draw(content, ITEM_REGIONS_HEAD, (Vec2){0, roundf((title_size.y - content->head.y) / 2)},
                          title_size.x);
        ui_panelbox_part_end(state);
    }
    float width = ui_panelbox_centre(state, NULL);
    if (width != inner_width)
        content = item_describe(item, hero, width);
    float height = content ? item_content_draw(content, regions, (Vec2){0, 0}, width) : 0;
    ui_panelbox_centre_end(state, height);
    if (resolve >= 0) {
        key(badge, sizeof badge, base, "badge");
        ui_level_badge(layout, badge,
                       (Vec2){pos.x + bounds.size.x - edge - pad.x - bs.x / 2, pos.y + edge + pad.y + bs.y / 2}, bs,
                       resolve);
    }
    return bounds;
}
