// Offline tests for item content (item_tips.c): building content and placing its layers, without the game.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define ITEM_TIPS_TEST
#include "../src/item_tips.c"
#include "mods_stub.h"

bool g_debug;
void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}
unsigned name_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}
bool is_hero(const void *a) {
    return a != NULL;
}
const void *items_active_hero(void) {
    return NULL;
}
int equip_slots_items(const void *hero, const uint8_t **out, int max) {
    (void)hero;
    (void)out;
    (void)max;
    return 0;
}
static ItemTypeDef g_def;
const ItemTypeDef *item_type_find(unsigned h) {
    return h == name_hash("gear") ? &g_def : NULL;
}
int item_types_count(void) {
    return 1;
}
const ItemTypeDef *item_type_at(int i) {
    return i ? NULL : &g_def;
}
bool ui_texture_size(const uint8_t tex[16], Vec2 *s) {
    (void)tex;
    *s = (Vec2){20, 10};
    return true;
}
Vec2 ui_anchor_point(UiAnchor a, Vec2 s) {
    return (Vec2){(a % 3) * s.x / 2, (a / 3) * s.y / 2};
}
// text: 10 units a character wide, 20 a line tall
float ui_text_lines_width(const UiTextLine *l, int n) {
    float w = 0;
    for (int i = 0; i < n; i++)
        if (strlen(l[i].text) * 10.f > w)
            w = strlen(l[i].text) * 10.f;
    return w;
}
float ui_text_lines_height(const UiTextLine *l, int n, float w) {
    (void)l;
    (void)w;
    return 20.f * n;
}
static struct {
    Vec2 pos;
    float w;
    int n, col;
    const char *first;
} g_text[8];
static int g_ntext;
float ui_text_lines_now(const UiTextLine *l, int n, Vec2 p, float w, int col) {
    g_text[g_ntext++] = (typeof(g_text[0])){p, w, n, col, n ? l[0].text : NULL};
    return 20.f * n;
}
static Vec2 g_badge_centre, g_badge_size;
static char g_badge_text[16];
void ui_badge_now(const uint8_t tex[16], Vec2 s, Vec2 c, const char *t, const char *f, uint32_t rgba, Vec2 o) {
    (void)tex;
    (void)f;
    (void)rgba;
    (void)o;
    g_badge_centre = c;
    g_badge_size = s;
    snprintf(g_badge_text, sizeof g_badge_text, "%s", t ? t : "");
}
bool ui_draw_callback(void (*fn)(const uint8_t *data), const void *data, size_t n) {
    (void)n;
    fn(data);
    return true;
}

static int g_failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                                         \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)
#define NEAR(a, b) ((a) - (b) < 0.01f && (b) - (a) < 0.01f)

static bool describe(const ItemContentQuery *q, ItemContent *c, void *ud) {
    (void)ud;
    item_content_line(c, NULL, 0, "Name");
    char body[] = "body line";
    int b = item_content_line(c, NULL, 0, body);
    strcpy(body, "changed"); // the content keeps its own copy
    item_content_line(c, NULL, 0, "two");
    ItemContentLayer *h = item_content_add(c, ITEM_LAYER_TEXT, ITEM_REGION_HEAD);
    h->count = 1;
    h->column = 1;
    h->at = h->anchor = UI_ANCHOR_TOP;
    ItemContentLayer *t = item_content_add(c, ITEM_LAYER_TEXT, ITEM_REGION_BODY);
    t->first = b;
    t->count = -1;
    t->column = 1;
    t->at = t->anchor = UI_ANCHOR_TOP;
    ItemContentLayer *d = item_content_add(c, ITEM_LAYER_IMAGE, ITEM_REGION_HEAD);
    d->at = UI_ANCHOR_TOP_RIGHT;
    d->anchor = UI_ANCHOR_CENTRE;
    d->size = (Vec2){64, 64};
    d->offset = (Vec2){7, -3};
    strcpy(d->label, "3");
    c->head = (Vec2){item_content_lines_width(c, 0, 1), item_content_lines_height(c, 0, 1, 0)};
    c->body = (Vec2){q->width ? q->width : item_content_lines_width(c, b, -1), item_content_lines_height(c, b, -1, 0)};
    return true;
}

int main(void) {
    g_def = (ItemTypeDef){.type = "gear", .describe = describe};
    uint8_t item[0x600] = {0};
    *(unsigned *)(item + ITEM_TYPE_HASH) = name_hash("gear");
    CHECK(!item_describe(NULL, NULL, 100));
    const ItemContent *c = item_describe(item, NULL, 0);
    CHECK(c && c->nlines == 3 && !strcmp(item_content_string(c, 1), "body line"));
    CHECK(NEAR(c->head.x, 40) && NEAR(c->head.y, 20) && NEAR(c->body.x, 90) && NEAR(c->body.y, 40));
    Vec2 all = item_content_size(c, ITEM_REGIONS_ALL), body = item_content_size(c, ITEM_REGIONS_BODY);
    CHECK(NEAR(all.x, 90) && NEAR(all.y, 60) && NEAR(body.y, 40));
    // both regions, 90 wide: the name centred on top, the body under it, the disc's centre on the corner moved by (7, -3)
    float h = item_content_draw(c, ITEM_REGIONS_ALL, (Vec2){100, 200}, 0);
    CHECK(NEAR(h, 60));
    CHECK(g_ntext == 2 && !strcmp(g_text[0].first, "Name") && NEAR(g_text[0].pos.x, 100) && NEAR(g_text[0].w, 90));
    CHECK(NEAR(g_text[1].pos.y, 220) && g_text[1].n == 2 && g_text[1].col == 1 &&
          !strcmp(g_text[1].first, "body line"));
    CHECK(NEAR(g_badge_centre.x, 197) && NEAR(g_badge_centre.y, 197) && !strcmp(g_badge_text, "3"));
    // the body alone (a panel with its own header): from the top, the head's layers left out
    g_ntext = 0;
    g_badge_centre = (Vec2){0, 0};
    item_content_draw(c, ITEM_REGIONS_BODY, (Vec2){0, 0}, 300);
    CHECK(g_ntext == 1 && NEAR(g_text[0].pos.y, 0) && NEAR(g_text[0].w, 300) && NEAR(g_badge_centre.x, 0));
    // full content: lines and layers refused, not overrun
    ItemContent big;
    content_clear(&big);
    int n = 0;
    while (item_content_line(&big, NULL, 0, "x") >= 0)
        n++;
    CHECK(n == ITEM_CONTENT_LINES);
    for (n = 0; item_content_add(&big, ITEM_LAYER_TEXT, ITEM_REGION_BODY); n++) {
    }
    CHECK(n == ITEM_CONTENT_LAYERS);
    content_clear(&big);
    static char huge[ITEM_CONTENT_TEXT + 1];
    memset(huge, 'a', sizeof huge - 1);
    CHECK(item_content_line(&big, NULL, 0, huge) == -1 && item_content_line(&big, NULL, 0, "fits") == 0);
    // equipped: by identity
    ItemContentQuery q = {.nequipped = 1, .equipped = {item}};
    uint8_t copy[0x600];
    memcpy(copy, item, sizeof copy);
    CHECK(item_is_equipped(&q, item) && !item_is_equipped(&q, copy) && !item_is_equipped(NULL, item));
    printf("item tips: %s\n", g_failures ? "FAILED" : "all passed");
    return g_failures != 0;
}
