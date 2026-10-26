// Native clients (see common.h, "clients.c"): every <mod>/engine/*.dylib (Windows *.dll) is loaded and its
// engine_client_init called. A client is built against the library's headers and leaves the library's symbols
// unresolved (macOS: -undefined dynamic_lookup, found in the injected library at load time; Windows: linked against
// ddx.dll, which is already loaded by the launcher).
#include "common.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define CLIENT_EXT ".dll"
#else
#include <dlfcn.h>
#define CLIENT_EXT ".dylib"
#endif

static int g_loaded, g_failed;

static void load_client(const char *engine_dir, const char *path) {
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) {
        logf_("client %s: can't load (error %lu)", path, (unsigned long)GetLastError());
        g_failed++;
        return;
    }
    EngineClientInitFn init = (EngineClientInitFn)(void *)GetProcAddress(h, ENGINE_CLIENT_INIT);
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        logf_("client %s: can't load: %s", path, dlerror());
        g_failed++;
        return;
    }
    EngineClientInitFn init = (EngineClientInitFn)dlsym(h, ENGINE_CLIENT_INIT);
#endif
    if (!init) {
        logf_("client %s: no %s, not a client", path, ENGINE_CLIENT_INIT);
        g_failed++;
        return;
    }
    EngineClientInfo info = {.api = ENGINE_CLIENT_API, .engine_dir = engine_dir};
    mods_set_current(mods_find(engine_dir)); // its registrations are its mod's
    int err = init(&info);
    mods_set_current(-1);
    if (err) {
        logf_("client %s: init failed (%d)", path, err);
        g_failed++;
        return;
    }
    logf_("client %s: loaded", path);
    g_loaded++;
}

static bool scan(const char *engine_dir, void *ud) {
    (void)ud;
    DIR *d = opendir(engine_dir);
    if (!d)
        return true;
    struct dirent *e;
    size_t el = strlen(CLIENT_EXT);
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.' || l <= el || strcmp(e->d_name + l - el, CLIENT_EXT))
            continue;
        char path[4600];
        snprintf(path, sizeof path, "%s/%s", engine_dir, e->d_name);
        load_client(engine_dir, path);
    }
    closedir(d);
    return true;
}

void clients_load_all(void) {
    mod_engine_dirs(scan, NULL);
    logf_("clients: %d loaded, %d failed", g_loaded, g_failed);
}
