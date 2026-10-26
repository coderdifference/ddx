// SDL text-input commits, shared by both platform renderers. No game object layouts.
#include "modmenu_internal.h"
#include <string.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif
static void (*start)(void), (*stop)(void);
static bool active, installed;
static unsigned char lock;
static char pending_text[65536];
static size_t used;
static void acquire(void) {
    while (__atomic_test_and_set(&lock, __ATOMIC_ACQUIRE)) {
    }
}
static void unlock(void) {
    __atomic_clear(&lock, __ATOMIC_RELEASE);
}
static int watch(void *unused, const void *event) {
    (void)unused;
    const unsigned char *e = event;
    uint32_t type;
    memcpy(&type, e, 4);
    if (type != 0x303 || !__atomic_load_n(&active, __ATOMIC_ACQUIRE))
        return 1; // SDL_TEXTINPUT
    size_t n = strnlen((const char *)e + 12, 32);
    acquire();
    if (n < 32 && used + n < sizeof pending_text) {
        memcpy(pending_text + used, e + 12, n);
        used += n;
        pending_text[used] = 0;
    }
    unlock();
    return 1;
}
static void *sdl(const char *name) {
#ifdef _WIN32
    return sym(name);
#else
    return dlsym(RTLD_DEFAULT, name);
#endif
}
bool mm_text_ready(void) {
    return sdl("SDL_AddEventWatch") && sdl("SDL_StartTextInput") && sdl("SDL_StopTextInput");
}
void mm_text_begin(void) {
    if (!installed) {
        void (*add)(int (*)(void *, const void *), void *) = sdl("SDL_AddEventWatch");
        start = sdl("SDL_StartTextInput");
        stop = sdl("SDL_StopTextInput");
        if (!add || !start || !stop)
            return;
        add(watch, NULL);
        installed = true;
    }
    acquire();
    used = 0;
    pending_text[0] = 0;
    unlock();
    __atomic_store_n(&active, true, __ATOMIC_RELEASE);
    start();
}
void mm_text_end(void) {
    __atomic_store_n(&active, false, __ATOMIC_RELEASE);
    if (stop)
        stop();
    acquire();
    used = 0;
    pending_text[0] = 0;
    unlock();
}
unsigned mm_text_take(char *out, unsigned cap) {
    if (!out || cap < 2)
        return 0;
    acquire();
    size_t n = used < cap - 1 ? used : cap - 1;
    while (n < used && n && ((unsigned char)pending_text[n] & 0xc0) == 0x80)
        n--;
    memcpy(out, pending_text, n);
    out[n] = 0;
    memmove(pending_text, pending_text + n, used - n);
    used -= n;
    pending_text[used] = 0;
    unlock();
    return (unsigned)n;
}
