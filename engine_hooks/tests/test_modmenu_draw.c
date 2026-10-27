// Exercise the actual native field renderer with a bounded fake widget-text pool.
#include "../src/modmenu_draw.c"
#include <assert.h>
static size_t text_bytes;
static int texts, inputs;
static float help_width;
bool ui_texture(const char *path, uint8_t out[16]) {
    return false;
}
void ui_colour(uint32_t rgba) {}
void ui_image_sized(Vec2 pos, const uint8_t tex[16], Vec2 size) {}
static bool click_input;
static JVal *test_view;
const JVal *mm_view(void) {
    return test_view;
}
float ui_text_height(const char *font, const char *text, float width) {
    return strstr(text, "wrapped") ? 90 : 30;
}
void ui_text_anchored(Vec2 pos, const char *text, const char *font, const char *colour, int anchor, float width) {
    if (!strcmp(font, "banner_sub_header")) {
        help_width = width;
        assert(!strcmp(colour, "engine_modmenu_comment"));
    }
    text_bytes += strlen(text) + 1;
    texts++;
    assert(text_bytes < 4096); // a small budget proves allocation depends on the viewport, not form size
}
bool ui_mouse_in(Vec2 pos, Vec2 size) {
    return click_input;
}
bool ui_click(int button, float slop) {
    bool clicked = click_input;
    click_input = false;
    return clicked;
}
const char *ui_layout_str(UiLayout *layout, const char *name, const char *fallback) {
    return fallback;
}
float ui_layout_num(UiLayout *layout, const char *name, float fallback) {
    return fallback;
}
void ui_dialog_frame(UiLayout *layout, const char *style, Vec2 pos, Vec2 size) {
    inputs++;
}
bool ui_clip_push(Vec2 pos, Vec2 size) {
    return true;
}
void ui_clip_pop(void) {}
static float draw(const JVal *field, const JVal *value, int count, float shown) {
    right = (UiScrollPanel){.open = true, .type = UI_SCROLL_HIDDEN, .shown = shown, .size = {800, 480}};
    text_bytes = 0;
    texts = inputs = 0;
    float y = 0;
    for (int i = 0; i < count; i++)
        field_control(NULL, field, value, &y, 800, false);
    return y;
}
int main(int argc, char **argv) {
    char number[64];
    display_number(number, sizeof number, .85, false);
    assert(!strcmp(number, "0.85"));
    display_number(number, sizeof number, 10, false);
    assert(!strcmp(number, "10"));
    display_number(number, sizeof number, 1000000, true);
    assert(!strcmp(number, "1000000"));
    display_number(number, sizeof number, .12345678901234567, false);
    assert(strtod(number, NULL) == .12345678901234567);
    JVal *field = json_parse("{\"id\":\"cost\",\"type\":\"integer\",\"label\":\"A wrapped label describing a long "
                             "Blacksmith service cost field\"}",
                             NULL, 0);
    JVal *value = json_parse("123", NULL, 0);
    for (int count = 525; count <= 2048; count += 1523) {
        float total = draw(field, value, count, 0);
        assert(total == count * 170 && inputs > 0 && inputs < 5 && texts < 9);
        assert(draw(field, value, count, total / 2) == total && inputs > 0 && inputs < 5 && texts < 9);
        assert(draw(field, value, count, total - 480) == total && inputs > 0 && inputs < 5 && texts < 9);
    }
    // Table rows keep label and value on one line and retain the offscreen culling budget.
    right = (UiScrollPanel){.open = true, .type = UI_SCROLL_HIDDEN, .size = {800, 480}};
    text_bytes = 0;
    texts = inputs = 0;
    float compact_y = 0;
    for (int i = 0; i < 525; i++)
        field_control(NULL, field, value, &compact_y, 800, true);
    assert(compact_y == 525 * 102 && inputs > 0 && inputs < 7 && texts < 14);
    // A control remains visible after its label has scrolled out; preserve full height.
    assert(draw(field, value, 1, 100) == 170 && inputs == 1 && texts == 1);
    right.open = false;
    float y = 0;
    texts = 0;
    field_control(NULL, field, value, &y, 800, false);
    assert(texts == 0 && y == 170);
    // Clicking another value commits the active input first; invalid input keeps focus.
    test_view = json_parse("{\"schema\":{\"fields\":[{\"id\":\"old\",\"type\":\"integer\"}]}}", NULL, 0);
    strcpy(focus, "old");
    strcpy(edit, "222");
    click_input = true;
    draw(field, value, 1, 0);
    assert(pending && strstr(pending, "old") && strstr(pending, "222") && !strcmp(next_focus, "cost"));
    free(pending);
    pending = NULL;
    next_focus[0] = 0;
    strcpy(edit, "invalid");
    click_input = true;
    draw(field, value, 1, 0);
    assert(!pending && !next_focus[0] && !strcmp(focus, "old"));
    mm_blur();
    problem[0] = 0;
    json_free(test_view);
    test_view = NULL;
    json_free(field);
    json_free(value);
    // Help wraps at full form width and remains visible after its input scrolls out.
    field = json_parse(
        "{\"id\":\"help\",\"type\":\"integer\",\"label\":\"Chance\",\"description\":\"wrapped property help\"}", NULL,
        0);
    value = json_parse("1", NULL, 0);
    right = (UiScrollPanel){.open = true, .type = UI_SCROLL_HIDDEN, .size = {800, 480}};
    y = 0;
    texts = inputs = 0;
    text_bytes = 0;
    help_width = 0;
    field_control(NULL, field, value, &y, 800, true);
    assert(y == 154 && help_width == 800 && texts == 3 && inputs == 1);
    right.shown = 60;
    y = 0;
    texts = inputs = 0;
    text_bytes = 0;
    field_control(NULL, field, value, &y, 800, true);
    assert(y == 154 && texts == 1 && inputs == 0);
    json_free(field);
    json_free(value);
    // Cull choices individually even when a single enumeration spans many screens.
    MmBuffer b = {0};
    mm_put(&b, "{\"id\":\"choices\",\"type\":\"enum\",\"label\":\"Choice\",\"choices\":[");
    for (int i = 0; i < 256; i++)
        mm_put(&b, i ? ",{\"id\":\"x\",\"label\":\"Choice label\"}" : "{\"id\":\"x\",\"label\":\"Choice label\"}");
    mm_put(&b, "]}");
    field = json_parse(b.text, NULL, 0);
    free(b.text);
    float total = draw(field, NULL, 1, 0);
    assert(total == 34 + 256 * 34 + 20 && texts > 0 && texts < 17);
    assert(draw(field, NULL, 1, total / 2) == total && texts > 0 && texts < 17);
    json_free(field);
    // Also exercise every real client field (mixed label heights, integer/float values).
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        assert(f);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        char *data = calloc((size_t)size + 1, 1);
        assert(fread(data, 1, (size_t)size, f) == (size_t)size);
        fclose(f);
        JVal *form = json_parse(data, NULL, 0);
        free(data);
        assert(form);
        const JVal *fields = json_get(form, "fields");
        assert(fields && fields->n == 525);
        for (int pass = 0; pass < 3; pass++) {
            right = (UiScrollPanel){.open = true, .type = UI_SCROLL_HIDDEN, .shown = pass * 25000, .size = {800, 480}};
            text_bytes = 0;
            texts = inputs = 0;
            y = 0;
            for (int i = 0; i < fields->n; i++)
                field_control(NULL, fields->items + i, json_get(fields->items + i, "default"), &y, 800, false);
            assert(texts > 0 && texts < 20 && y > 50000);
        }
        json_free(form);
    }
    puts("PASS: large native forms submit only visible widget strings; scrolling preserves content height");
}
