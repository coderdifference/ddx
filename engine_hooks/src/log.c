// Log to ddx.log in the game's data folder (next to app.log). A native client that names a file (log_file, in its
// engine_client_init) logs to <name>.log and <name>_combat.log there (fresh each run): logf_ and combat_logf find the
// caller's binary from their return address. The library's own lines, and other callers, go to ddx.log /
// ddx_combat.log.
#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static FILE *g_log;
static FILE *g_combat_log;
bool g_debug;

void log_open(void) {
    char dir[4096], path[4200];
    if (game_data_dir(dir, sizeof dir)) {
        snprintf(path, sizeof path, "%s/ddx.log", dir);
        g_log = fopen(path, "a");
    }
#ifndef _WIN32
    if (!g_log)
        g_log = fopen("/tmp/ddx.log", "a");
#endif
}

void combat_log_open(void) {
    char dir[4096], path[4200];
    if (game_data_dir(dir, sizeof dir)) {
        snprintf(path, sizeof path, "%s/ddx_combat.log", dir);
        g_combat_log = fopen(path, "a");
    }
#ifndef _WIN32
    if (!g_combat_log)
        g_combat_log = fopen("/tmp/ddx_combat.log", "a");
#endif
    if (g_combat_log) {
        time_t t = time(NULL);
        char ts[32];
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
        fprintf(g_combat_log, "\n[%s] combat event probe started\n", ts);
        fflush(g_combat_log);
    }
}

// ---------------------------------------------------------------- clients' own files (log_file)
#define MAX_LOG_FILES 16
static struct {
    const void *base; // the registering binary's load address
    char name[64];
    FILE *f[2]; // [combat]: opened at its first line
    bool tried[2];
} g_files[MAX_LOG_FILES];
static int g_nfiles;

// The load address of the binary holding addr.
static const void *image_of(const void *addr) {
#ifdef _WIN32
    HMODULE h = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)addr, &h))
        return NULL;
    return h;
#else
    Dl_info info;
    return dladdr(addr, &info) ? info.dli_fbase : NULL;
#endif
}

static void register_file(const void *caller, const char *name) {
    const void *base = image_of(caller);
    bool ok = name && name[0] && strlen(name) < sizeof g_files[0].name;
    for (const char *c = name; ok && *c; c++)
        ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-';
    if (!ok || !base) {
        logf_("log_file \"%s\": a name of letters, digits, _ and - (no .log), from a client", name ? name : "");
        return;
    }
    for (int i = 0; i < g_nfiles; i++) {
        if (g_files[i].base == base) {
            logf_("log_file \"%s\": this client already logs to %s.log", name, g_files[i].name);
            return;
        }
        if (!strcmp(g_files[i].name, name)) {
            logf_("log_file \"%s\": another client logs there already", name);
            return;
        }
    }
    if (g_nfiles == MAX_LOG_FILES) {
        logf_("log_file \"%s\": more than %d clients' files", name, MAX_LOG_FILES);
        return;
    }
    g_files[g_nfiles].base = base;
    snprintf(g_files[g_nfiles].name, sizeof g_files[0].name, "%s", name);
    logf_("log_file: %s logs to %s.log and %s_combat.log",
          mods_current() >= 0 ? mods_folder(mods_current()) : "a client", name, name);
    g_nfiles++;
}
void log_file(const char *name) {
    register_file(__builtin_return_address(0), name);
}

// The file for a caller: its client's (log_file), else the library's.
static FILE *sink(const void *caller, bool combat) {
    FILE *lib = combat ? g_combat_log : g_log;
    if (!g_nfiles)
        return lib;
    const void *base = image_of(caller);
    for (int i = 0; i < g_nfiles; i++) {
        if (g_files[i].base != base)
            continue;
        if (!g_files[i].tried[combat]) {
            g_files[i].tried[combat] = true;
            char dir[4096], path[4200];
            if (game_data_dir(dir, sizeof dir)) {
                snprintf(path, sizeof path, "%s/%s%s.log", dir, g_files[i].name, combat ? "_combat" : "");
                g_files[i].f[combat] = fopen(path, "w");
            }
        }
        return g_files[i].f[combat] ? g_files[i].f[combat] : lib;
    }
    return lib;
}

static void vline(FILE *f, const char *fmt, va_list ap) {
    if (!f)
        return;
    time_t t = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "[%s] ", ts);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
}

void combat_log_event(const char *name, const void *event, size_t bytes) {
    if (!g_combat_log || !event)
        return;
    time_t t = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(g_combat_log, "[%s] %s event=%p bytes=", ts, name, event);
    const unsigned char *p = event;
    for (size_t i = 0; i < bytes; i++)
        fprintf(g_combat_log, "%s%02x", i ? " " : "", p[i]);
    fputc('\n', g_combat_log);
    fflush(g_combat_log);
}

void combat_logf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vline(sink(__builtin_return_address(0), true), fmt, ap);
    va_end(ap);
}

void combat_log_actor_event(const char *name, const void *actor, const char *kind, unsigned guid) {
    if (!g_combat_log)
        return;
    time_t t = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(g_combat_log, "[%s] %s actor=%s guid=%u ptr=%p\n", ts, name, kind, guid, actor);
    fflush(g_combat_log);
}

void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vline(sink(__builtin_return_address(0), false), fmt, ap);
    va_end(ap);
}

// Every hook point, both phases, into ddx_combat.log (a "*" listener: it logs the points that are installed, and doesn't
// make any point wanted). before lines show the context as the listeners left it (a cancel included).
static void actor_tag(const void *actor, char *out, size_t n) {
    if (!actor)
        snprintf(out, n, "-");
    else if (is_hero(actor))
        snprintf(out, n, "hero:%u", hero_guid(actor));
    else if (is_monster(actor))
        snprintf(out, n, "monster:%u", monster_guid(actor));
    else
        snprintf(out, n, "%p", actor);
}

static void log_point(HookContext *c, void *userdata) {
    (void)userdata;
    char actor[32], attacker[32], target[32];
    actor_tag(c->actor, actor, sizeof actor);
    actor_tag(c->attacker, attacker, sizeof attacker);
    actor_tag(c->target, target, sizeof target);
    char prop[160] = "";
    if (c->prop)
        snprintf(prop, sizeof prop, " prop=%s \"%s\"", prop_id(c->prop), prop_title(c->prop));
    char item[160] = ""; // Inventory::Item: type and id strings, amount
    if (c->item) {
        const char *it = (const char *)c->item;
        snprintf(item, sizeof item, " item=%.64s/%.64s x%d", it + ITEM_TYPE_STRING, it + ITEM_ID,
                 *(const int *)(it + ITEM_AMOUNT));
    }
    char refuse[24] = "";
    if (c->refuse)
        snprintf(refuse, sizeof refuse, " refuse=0x%x", c->refuse);
    combat_logf("point %s %s actor=%s attacker=%s target=%s skill=%08x amount=%.3f result=%.3f kind=%d source=%s "
                "type=%s hit=%u crit=%u synthetic=%u%s%s%s%s%s%s",
                c->point, hook_phase_name(c->phase), actor, attacker, target, c->skill ? skill_id_hash(c->skill) : 0,
                c->amount, c->result, c->kind, c->source ? c->source : "-", c->type ? c->type : "-", c->hit, c->crit,
                c->synthetic, prop, item, c->skip ? " skipped=" : "", c->skip ? c->skip : "", refuse,
                c->phase == HOOK_BEFORE && c->cancel ? " cancel" : "");
}

void combat_log_install(void) {
    hook_on("*", HOOK_BEFORE, log_point, NULL);
    hook_on("*", HOOK_AFTER, log_point, NULL);
}
