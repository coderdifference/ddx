#include "../src/modmenu_internal.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int owner = -1, released, events;
static bool applied = true, save_ok = true;
int mods_current(void) {
    return owner;
}
void mods_set_current(int value) {
    owner = value;
}
bool mods_active(int mod) {
    (void)mod;
    return applied;
}
bool mods_package_applied(void) {
    return applied;
}
bool mm_platform_ready(void) {
    return true;
}
bool mm_platform_save(void) {
    return save_ok;
}
void mm_platform_install(void) {}
void mm_blur(void) {}
void mm_reset_draw(void) {}
unsigned ui_frame(void) {
    return 1;
}
void logf_(const char *fmt, ...) {
    (void)fmt;
}
static void release(char *text) {
    released++;
    free(text);
}
static int committed;
static char *provider(const char *text, size_t length) {
    assert(strlen(text) == length);
    JVal *j = json_parse(text, NULL, 0);
    assert(j);
    if (!strcmp(mm_str(j, "event"), "save")) {
        const char *t = mm_str(j, "campaign");
        const char *data = "{\"example\":{\"x\":42}}";
        committed = modmenu_commit(t, strlen(t), data, strlen(data));
    }
    json_free(j);
    events++;
    return strdup("{\"version\":1,\"open\":true}");
}
int main(void) {
    JVal *u = json_parse("{\"s\":\"caf\\u00e9 \\ud83d\\ude80\"}", NULL, 0);
    assert(u && !strcmp(mm_str(u, "s"), "caf\xc3\xa9 \xf0\x9f\x9a\x80"));
    json_free(u);
    assert(!json_parse("\"\\ud800\"", NULL, 0));
    assert(!json_parse("\"\\u0000\"", NULL, 0));
    assert(!json_parse("\"\\q\"", NULL, 0));
    const char *schema = "{\"id\":\"example\",\"title\":\"Example\",\"version\":1,\"fields\":[]}";
    assert(modmenu_form_register(schema, NULL, NULL) == 1);
    assert(modmenu_form_register(schema, NULL, NULL) == 0);
    modmenu_client_finished(-1, true);
    assert(modmenu_register("ddx.framework", provider, release) == 1);
    assert(modmenu_register("duplicate", provider, release) == 0);
    assert(!mm_available());
    modmenu_loaded();
    assert(mm_available());
    assert(!mm_is_open());
    mm_set_open(true);
    assert(mm_is_open());
    mm_event("save", NULL);
    assert(committed == 1);
    assert(!strcmp(modmenu_values("example"), "{\"x\":42}"));
    save_ok = false;
    mm_event("save", NULL);
    assert(committed == 0);
    assert(!strcmp(modmenu_values("example"), "{\"x\":42}"));
    assert(modmenu_commit("stale", 5, "{}", 2) == 0);
    mm_set_open(false);
    assert(!mm_is_open());
    modmenu_reset();
    assert(!mm_available());
    assert(!modmenu_values("example"));
    assert(events == released);
    printf("PASS: modmenu native registry, Unicode, commit, rollback, lifecycle (%d replies released)\n", released);
}
