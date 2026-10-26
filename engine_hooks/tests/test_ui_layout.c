// Offline test for ui_layout.c: names through nested objects, vec2s and numbers, fallbacks, a missing file.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/common.h"

static int g_logged;
void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    g_logged++;
}
unsigned name_hash(const char *s) {
    unsigned h = 0;
    while (*s)
        h = h * 53 + (unsigned char)*s++;
    return h;
}

static int fails;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                         \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

int main(void) {
    char path[] = "/tmp/ui_layout_test_XXXXXX";
    int fd = mkstemp(path);
    FILE *f = fdopen(fd, "w");
    fputs("{\"anvil\": {\"pos\": [83, 456.5], \"size\": 64}, \"fonts\": {\"text\": \"town_activity_description\"},"
          " \"bad\": [1, 2, 3]}",
          f);
    fclose(f);
    UiLayout *l = ui_layout_load(path);
    CHECK(l && ui_layout_load(path) == l); // loaded once
    Vec2 p = ui_layout_vec2(l, "anvil.pos", (Vec2){0, 0});
    CHECK(p.x == 83 && p.y == 456.5f);
    CHECK(ui_layout_num(l, "anvil.size", 0) == 64);
    CHECK(!strcmp(ui_layout_str(l, "fonts.text", ""), "town_activity_description"));
    CHECK(ui_layout_num(l, "anvil.missing", 7) == 7); // fallbacks
    Vec2 b = ui_layout_vec2(l, "bad", (Vec2){1, 1});
    CHECK(b.x == 1 && b.y == 1);                    // not a pair
    CHECK(ui_layout_num(l, "anvil.pos.x", 5) == 5); // through a non-object
    // each missing name is logged once, however many there are and however often they're read
    int before = g_logged;
    char name[32];
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < 40; i++) {
            snprintf(name, sizeof name, "many.k%d", i);
            ui_layout_num(l, name, 0);
        }
    CHECK(g_logged - before == 40);
    // a chain: a mod's file over the library's defaults, per name
    char lib_path[] = "/tmp/ui_layout_lib_XXXXXX";
    f = fdopen(mkstemp(lib_path), "w");
    fputs("{\"anvil\": {\"pos\": [1, 2], \"art\": \"panels/a\"}, \"log\": {\"font\": \"tooltip\", \"step\": 38}}", f);
    fclose(f);
    UiLayout *lib = ui_layout_load(lib_path);
    CHECK(ui_layout_chain(l, lib));
    CHECK(!ui_layout_chain(lib, l)); // would loop
    p = ui_layout_vec2(l, "anvil.pos", (Vec2){0, 0});
    CHECK(p.x == 83);                                              // the mod's value wins
    CHECK(!strcmp(ui_layout_str(l, "anvil.art", ""), "panels/a")); // a name it lacks: the library's
    CHECK(ui_layout_num(l, "log.step", 0) == 38);
    CHECK(ui_layout_has(l, "log.font") && !ui_layout_has(l, "log.nope"));
    before = g_logged;
    ui_layout_num(l, "log.nope", 0);
    CHECK(g_logged - before == 1); // missing from both: logged once
    remove(lib_path);
    remove(path);
    UiLayout *m = ui_layout_load("/nonexistent/x.layout.json"); // kept, defaults until it appears
    CHECK(m && ui_layout_num(m, "a", 3) == 3);
    if (!fails)
        printf("all passed\n");
    return fails != 0;
}
