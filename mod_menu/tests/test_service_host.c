// Real NativeAOT service boundary smoke test; no game or live saves are used.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#define EXPORT __attribute__((visibility("default")))
#endif
typedef char *(*Dispatch)(const char *, size_t);
typedef void (*Release)(char *);
static Dispatch dispatch;
static Release release;
static int commits, version = 0;
EXPORT int modmenu_host_version(void) {
    return version;
}
EXPORT int modmenu_register(const char *id, Dispatch d, Release r) {
    assert(!strcmp(id, "ddx.modmenu"));
    dispatch = d;
    release = r;
    return 1;
}
EXPORT const char *modmenu_client_event(const char *id, const char *json) {
    (void)id;
    (void)json;
    return NULL;
}
EXPORT int modmenu_commit(const char *token, size_t tn, const char *json, size_t n) {
    assert(tn == 1 && *token == 'A');
    assert(strstr(json, "42"));
    assert(n == strlen(json));
    commits++;
    return 1;
}
static char *send(const char *json) {
    char *reply = dispatch(json, strlen(json));
    assert(reply);
    return reply;
}
int main(int argc, char **argv) {
    assert(argc == 2);
#ifdef _WIN32
    HMODULE module = LoadLibraryA(argv[1]);
    assert(module);
    int (*init)(void *) = (void *)GetProcAddress(module, "engine_client_init");
#else
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    int (*init)(void *) = dlsym(module, "engine_client_init");
#endif
    assert(init);
    struct {
        int api;
        const char *dir;
    } info = {1, "/tmp/engine"};
    assert(init(&info) == 2);
    version = 1;
    assert(init(&info) == 0);
    assert(dispatch && release);
    char *r =
        send("{\"event\":\"load\",\"campaign\":\"A\",\"schemas\":[{\"version\":1,\"id\":\"example\",\"title\":"
             "\"Example\",\"fields\":[{\"id\":\"limit\",\"label\":\"Limit\",\"type\":\"integer\",\"default\":20}]}]}");
    release(r);
    r = send("{\"event\":\"open\",\"campaign\":\"A\",\"paused\":true}");
    assert(strstr(r, "\"schema\":null"));
    release(r);
    r = send("{\"event\":\"select\",\"campaign\":\"A\",\"mod\":\"example\"}");
    assert(strstr(r, "\"limit\":20"));
    release(r);
    r = send("{\"event\":\"edit\",\"campaign\":\"A\",\"field\":\"limit\",\"value\":42}");
    assert(strstr(r, "\"error\":null"));
    release(r);
    r = send("{\"event\":\"save\",\"campaign\":\"A\"}");
    assert(commits == 1 && strstr(r, "\"error\":null"));
    release(r);
    r = send("{\"event\":\"unload\"}");
    assert(strstr(r, "\"open\":false"));
    release(r);
    puts("PASS: actual NativeAOT registration, capability rejection, dispatch, view, commit and release");
}
