// Native game-GUI renderer for the managed service's declarative form view.
#include "modmenu_internal.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static void (*push)(void), (*pop)(void), (*translate)(const Vec2 *), (*scale)(float), (*depth)(float);
static bool (*key)(unsigned, bool, bool);
static void (*raw_begin)(void), (*raw_end)(void);
static unsigned (*text_key)(char *, unsigned);
static UiScrollPanel left, right;
static char selected[129], focus[257], edit[65537], problem[256];
static char *pending;
static bool saving;
static unsigned typed_frame = ~0u;
static void resolve(void) {
    if (push)
        return;
    push = sym("_Z14Transform_Pushv");
    pop = sym("_Z13Transform_Popv");
    translate = sym("_Z19Transform_TranslateRK4vec2");
    scale = sym("_Z15Transform_Scalef");
    depth = sym("_Z19Transform_Translatef");
    key = sym("_Z18Input_IsKeyPressedjbb");
    raw_begin = mm_text_begin;
    raw_end = mm_text_end;
    text_key = mm_text_take;
}
void mm_blur(void) {
    if (focus[0] && raw_end)
        raw_end();
    focus[0] = 0;
}
void mm_reset_draw(void) {
    mm_blur();
    free(pending);
    pending = NULL;
    saving = false;
    selected[0] = 0;
    problem[0] = 0;
    ui_scrollpanel_reset(&left);
    ui_scrollpanel_reset(&right);
}
// Confirm-dialog typography, hover art and vertical padding; open-sided divider treatment.
static bool action_button(UiLayout *layout, Vec2 pos, Vec2 size, const char *label, bool enabled) {
    bool hover = enabled && ui_mouse_in(pos, size);
    uint8_t texture[16];
    ui_colour(enabled ? 0xffffffffu : 0x88777777u);
    const char *top_art =
        ui_layout_str(layout, "modmenu.button.top", "campaign/town/buildings/upgrade/tree_divider_medium.png");
    const char *bottom_art = ui_layout_str(layout, "modmenu.button.bottom", "activity_log/separator.png");
    if (ui_texture(top_art, texture)) {
        ui_image_sized((Vec2){pos.x, pos.y + 5}, texture, (Vec2){size.x, 2});
        // Compose the centered diamond from the same divider art, without side borders.
        for (int row = 0; row < 12; row++) {
            float width = 12 - 2 * fabsf((float)row - 5.5f);
            ui_image_sized((Vec2){pos.x + (size.x - width) / 2, pos.y + row}, texture, (Vec2){width, 1});
        }
    }
    if (ui_texture(bottom_art, texture))
        ui_image_sized((Vec2){pos.x, pos.y + size.y - 3}, texture, (Vec2){size.x, 3});
    ui_colour(0xffffffffu);
    if (hover && ui_texture(ui_layout_str(layout, "confirm.button.hover",
                                          "shared/confirm_dialog/confirm_dialog.answer_text_selected_overlay.png"),
                            texture))
        ui_image_sized((Vec2){pos.x, pos.y + 10}, texture, (Vec2){size.x, size.y - 20});
    ui_text_anchored((Vec2){pos.x + size.x / 2, pos.y + size.y / 2}, label,
                     ui_layout_str(layout, "confirm.answer_font", "confirm_dialog_answer"),
                     ui_layout_str(layout, "confirm.answer_colour", "confirm_dialog_answer"), UI_ANCHOR_CENTRE,
                     size.x - 2 * ui_layout_num(layout, "confirm.button.pad", 24));
    return hover && ui_click(UI_MOUSE_LEFT, 8);
}
static bool button(Vec2 p, float width, const char *label, bool enabled) {
    bool hover = enabled && ui_mouse_in(p, (Vec2){width, 32});
    ui_text_anchored(p, label, "confirm_dialog_answer", hover ? "white" : "confirm_dialog_answer", 0, width);
    return hover && ui_click(UI_MOUSE_LEFT, 8);
}
static void queue(const char *event, const char *id, const char *value) {
    if (pending)
        return;
    problem[0] = 0;
    MmBuffer b = {0};
    if (!strcmp(event, "select")) {
        mm_put(&b, "\"mod\":");
        mm_quote(&b, id);
    } else {
        mm_put(&b, "\"field\":");
        mm_quote(&b, id);
        mm_put(&b, ",\"value\":");
        mm_put(&b, value);
    }
    if (!b.failed)
        pending = b.text;
    else
        free(b.text);
}
static const JVal *find_field(const JVal *view, const char *id) {
    const JVal *fields = json_get(json_get(view, "schema"), "fields");
    for (int i = 0; fields && i < fields->n; i++)
        if (!strcmp(mm_str(fields->items + i, "id"), id))
            return fields->items + i;
    return NULL;
}
static bool flush_edit(void) {
    if (!focus[0])
        return true;
    const JVal *field = find_field(mm_view(), focus);
    if (!field) {
        mm_blur();
        return false;
    }
    MmBuffer b = {0};
    if (!strcmp(mm_str(field, "type"), "text"))
        mm_quote(&b, edit);
    else {
        char *end;
        double value = strtod(edit, &end);
        if (!*edit || *end || !isfinite(value)) {
            snprintf(problem, sizeof problem, "Enter a valid number.");
            return false;
        }
        char number[40];
        snprintf(number, sizeof number, "%.17g", value);
        mm_put(&b, number);
    }
    queue("edit", focus, b.text);
    free(b.text);
    return pending != NULL;
}
static void type(void) {
    if (!focus[0] || !key || ui_frame() == typed_frame)
        return;
    typed_frame = ui_frame();
    if (key(8, false, true)) {
        size_t n = strlen(edit);
        if (n) {
            do {
                n--;
            } while (n && ((unsigned char)edit[n] & 0xc0) == 0x80);
            edit[n] = 0;
        }
    }
    char text[64] = {0};
    if (text_key)
        text_key(text, sizeof text);
    if (text[0] && strlen(edit) + strlen(text) < sizeof edit)
        strcat(edit, text);
    if (key(13, false, false))
        flush_edit();
}
static void field_control(UiLayout *layout, const JVal *field, const JVal *value, float *y, float width) {
    const char *id = mm_str(field, "id"), *kind = mm_str(field, "type");
    float label_h = fmaxf(30, ui_text_height("confirm_dialog_answer", mm_str(field, "label"), width));
    ui_text_anchored((Vec2){0, *y}, mm_str(field, "label"), "confirm_dialog_answer", "confirm_dialog_question", 0,
                     width);
    *y += label_h + 4;
    const JVal *choices = json_get(field, "choices");
    if (!strcmp(kind, "single") || !strcmp(kind, "multi") || !strcmp(kind, "enum")) {
        for (int i = 0; choices && i < choices->n; i++) {
            const JVal *choice = choices->items + i;
            const char *cid = mm_str(choice, "id");
            bool checked = false;
            if (value && value->t == J_STR)
                checked = !strcmp(value->str, cid);
            if (value && value->t == J_ARR)
                for (int k = 0; k < value->n; k++)
                    if (value->items[k].t == J_STR && !strcmp(value->items[k].str, cid))
                        checked = true;
            char label[768];
            snprintf(label, sizeof label, "%s %s",
                     !strcmp(kind, "enum") ? (checked ? "(o)" : "( )") : (checked ? "[x]" : "[ ]"),
                     mm_str(choice, "label"));
            if (button((Vec2){8, *y}, width - 8, label, true)) {
                mm_blur();
                MmBuffer b = {0};
                if (!strcmp(kind, "multi")) {
                    mm_put(&b, "[");
                    int n = 0;
                    if (value && value->t == J_ARR)
                        for (int k = 0; k < value->n; k++)
                            if (value->items[k].t == J_STR && strcmp(value->items[k].str, cid)) {
                                if (n++)
                                    mm_put(&b, ",");
                                mm_quote(&b, value->items[k].str);
                            }
                    if (!checked) {
                        if (n++)
                            mm_put(&b, ",");
                        mm_quote(&b, cid);
                    }
                    mm_put(&b, "]");
                } else
                    mm_quote(&b, cid);
                queue("edit", id, b.text);
                free(b.text);
            }
            *y += 34;
        }
    } else {
        char *serialized = value && value->t != J_STR ? mm_stringify(value) : NULL;
        const char *text = !strcmp(focus, id)           ? edit
                           : value && value->t == J_STR ? value->str
                           : serialized                 ? serialized
                                                        : "";
        char display[66000];
        snprintf(display, sizeof display, "%s%s", text, !strcmp(focus, id) ? " |" : "");
        float height = ui_layout_num(layout, "modmenu.input_height", 46);
        Vec2 input_pos = {0, *y}, input_size = {width, height};
        bool hover = ui_mouse_in(input_pos, input_size);
        ui_dialog_frame(layout, "tooltip", input_pos, input_size);
        bool clipped = ui_clip_push((Vec2){12, *y + 6}, (Vec2){width - 24, height - 12});
        ui_text_anchored((Vec2){14, *y + height / 2}, display, "confirm_dialog_answer", "confirm_dialog_answer",
                         UI_ANCHOR_LEFT, 0);
        if (clipped)
            ui_clip_pop();
        if (hover && ui_click(UI_MOUSE_LEFT, 8)) {
            if (strcmp(focus, id)) {
                mm_blur();
                snprintf(focus, sizeof focus, "%s", id);
                snprintf(edit, sizeof edit, "%s", text);
                if (raw_begin)
                    raw_begin();
                problem[0] = 0;
            }
        }
        free(serialized);
        *y += height + 10;
    }
    *y += 20;
}
void mm_draw(void) {
    resolve();
    if (!push || !pop || !translate || !scale || !key)
        return;
    const JVal *view = mm_view();
    if (!view)
        return;
    if (strcmp(selected, mm_str(view, "selected"))) {
        snprintf(selected, sizeof selected, "%s", mm_str(view, "selected"));
        ui_scrollpanel_reset(&right);
        mm_blur();
        problem[0] = 0;
    }
    if (key(27, false, false)) {
        mm_blur();
        mm_set_open(false);
        return;
    }
    type();
    Vec2 lo, hi;
    ui_screen_bounds(&lo, &hi);
    float preference = 1;
    const char *settings = modmenu_values("ddx.framework");
    JVal *options = settings ? json_parse(settings, NULL, 0) : NULL;
    const JVal *size = json_get(options, "menu_scale");
    if (size && size->t == J_NUM && size->num >= .75 && size->num <= 1.25)
        preference = (float)size->num;
    json_free(options);
    UiLayout *layout = ui_layout_mod("dialog.layout.json");
    Vec2 dialog = ui_layout_vec2(layout, "modmenu.size", (Vec2){1280, 800});
    dialog.x = fmaxf(800, dialog.x);
    dialog.y = fmaxf(600, dialog.y);
    float fit = fminf(preference, fminf((hi.x - lo.x - 48) / dialog.x, (hi.y - lo.y - 48) / dialog.y));
    Vec2 c = ui_screen_centre();
    push();
    translate(&c);
    scale(fit);
    translate(&(Vec2){-dialog.x / 2, -dialog.y / 2});
    if (depth)
        depth(10000);
    float margin = ui_layout_num(layout, "modmenu.margin", 28);
    float padding = ui_layout_num(layout, "modmenu.panel_padding", 20);
    float top = ui_layout_num(layout, "modmenu.panel_top", 156);
    float bottom = fminf(ui_layout_num(layout, "modmenu.panel_bottom", 704), dialog.y - 96);
    float list_width = ui_layout_num(layout, "modmenu.list_width", 312);
    float form_x = margin + list_width + 20;
    float form_width = dialog.x - margin - form_x;
    float content_h = bottom - top - 2 * padding;
    ui_dialog_frame(layout, "frame", (Vec2){0, 0}, dialog);
    ui_dialog_frame(layout, "tooltip", (Vec2){margin, top}, (Vec2){list_width, bottom - top});
    ui_dialog_frame(layout, "tooltip", (Vec2){form_x, top}, (Vec2){form_width, bottom - top});
    ui_text((Vec2){dialog.x / 2, ui_layout_num(layout, "modmenu.title_y", 110)}, "Mod Options", "journal_popup_title",
            "confirm_dialog_question");
    const JVal *mods = json_get(view, "mods");
    float list_content_w = list_width - 2 * padding;
    ui_scrollpanel_begin(&left, layout, "modmenu.list", UI_SCROLL_HIDDEN, (Vec2){margin + padding, top + padding},
                         (Vec2){list_content_w, content_h});
    float y = 0;
    for (int i = 0; mods && i < mods->n; i++) {
        const JVal *mod = mods->items + i;
        const char *label = mm_str(mod, "title");
        float row_h = fmaxf(48, ui_text_height("confirm_dialog_answer", label, list_content_w - 24) + 20);
        bool chosen = !strcmp(selected, mm_str(mod, "id"));
        bool hover = ui_mouse_in((Vec2){0, y}, (Vec2){list_content_w, row_h});
        if (chosen || hover)
            ui_dialog_frame(layout, "tooltip", (Vec2){0, y}, (Vec2){list_content_w, row_h});
        ui_text_anchored((Vec2){12, y + 10}, label, "confirm_dialog_answer", "confirm_dialog_answer",
                         UI_ANCHOR_TOP_LEFT, list_content_w - 24);
        if (hover && ui_click(UI_MOUSE_LEFT, 8)) {
            mm_blur();
            queue("select", mm_str(mod, "id"), NULL);
        }
        y += row_h + 10;
    }
    ui_scrollpanel_end(&left, y);
    const JVal *fields = json_get(json_get(view, "schema"), "fields"), *values = json_get(view, "values");
    float form_content_w = form_width - 2 * padding;
    ui_scrollpanel_begin(&right, layout, "modmenu.form", UI_SCROLL_HIDDEN, (Vec2){form_x + padding, top + padding},
                         (Vec2){form_content_w, content_h});
    y = 0;
    for (int i = 0; fields && i < fields->n; i++) {
        const JVal *field = fields->items + i;
        field_control(layout, field, json_get(values, mm_str(field, "id")), &y, form_content_w);
    }
    ui_scrollpanel_end(&right, y);
    const char *error = problem[0] ? problem : *mm_last_error() ? mm_last_error() : mm_str(view, "error");
    Vec2 button_size = ui_layout_vec2(layout, "modmenu.button_size", (Vec2){180, 48});
    float footer_y = fminf(ui_layout_num(layout, "modmenu.footer_y", 728), dialog.y - button_size.y - 24);
    ui_text_anchored((Vec2){margin + button_size.x + 24, footer_y + 8}, error, "confirm_dialog_answer",
                     "confirm_dialog_question", UI_ANCHOR_TOP_LEFT, dialog.x - 2 * margin - 2 * button_size.x - 48);
    bool closing = action_button(layout, (Vec2){margin, footer_y}, button_size, "Back", true);
    if (action_button(layout, (Vec2){dialog.x - margin - button_size.x, footer_y}, button_size, "Save",
                      *selected != 0)) {
        saving = true;
        if (!flush_edit() && focus[0])
            saving = false;
    }
    pop();
    if (closing) {
        mm_blur();
        mm_set_open(false);
        return;
    }
    if (pending) {
        char *message = pending;
        pending = NULL;
        mm_event(strstr(message, "\"mod\":") == message ? "select" : "edit", message);
        free(message);
        if (!*mm_last_error() && !*mm_str(mm_view(), "error")) {
            problem[0] = 0;
            mm_blur();
        } else
            saving = false;
    }
    if (saving) {
        saving = false;
        mm_event("save", NULL);
        if (!*mm_last_error() && !*mm_str(mm_view(), "error"))
            snprintf(problem, sizeof problem, "Saved to this campaign.");
    }
}
