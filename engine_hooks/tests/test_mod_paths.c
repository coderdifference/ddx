// Exercise the actual discovery/accessor code: callers compare engine directories and load extension data beneath them.
#include "../src/mods.c"
#include <assert.h>
void logf_(const char *fmt, ...) {}
bool g_debug;
void *sym(const char *name) {
    return NULL;
}
void *install_hook(const char *name, void *target, void *detour, const uint8_t *expected, size_t len) {
    return NULL;
}
int mod_engine_dirs(bool (*fn)(const char *, void *), void *ud) {
    return 0;
}
bool library_engine_dir(char *out, size_t n) {
    return false;
}
int main(void) {
    add("/tmp/ddx-mod-paths/First/engine", NULL);
    add("/tmp/ddx-mod-paths/Second/engine", NULL);
    assert(mods_count() == 2);
    assert(!strcmp(mods_folder(0), "First"));
    assert(!strcmp(mods_engine_dir(0), "/tmp/ddx-mod-paths/First/engine"));
    assert(!strcmp(mods_engine_dir(1), "/tmp/ddx-mod-paths/Second/engine"));
    assert(mods_find(mods_engine_dir(0)) == 0);
    assert(mods_find(mods_engine_dir(1)) == 1);
    assert(
        !strcmp(mods_engine_dir(0), "/tmp/ddx-mod-paths/First/engine")); // second lookup must not invalidate the first
    assert(!*mods_engine_dir(-1) && !*mods_engine_dir(2));
    puts("Mod engine directory regression passed");
    return 0;
}
