// The game's string lookups (Localization::LocalizeStringFromId and ...WithVariationIndex, by id and by hash): this
// module owns their hooks.
//   - Clients may redirect an id to another before the lookup (loc_on_redirect; a client mod stash blanks a title).
//   - A key the current language's string table doesn't have (Localization::DoesLocalizedStringExist, the lookup's own
//     LocalizationLanguage::FindString) is written to ddx_localization.log in the game's data folder (next to
//     ddx.log), once per language and key per session: "<language> <key>", or "<language> #<hash>" for the
//     lookups by hash (the id's string_hash; the id itself isn't passed). The game then shows "[<key>]" in colour.
//     Code that only probes for an optional string asks loc_exists first, so that probe isn't logged.
// The language is Localization::GetCurrentLanguage (Localization::Data, whose first field is the language id).
// Windows: the existence checks and the language are stand-ins (syms_win.c); the lookup by id calls the one by hash
// (checked once, by id), and some callers have the lookup by hash inlined (their missing strings aren't logged).
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint64_t a, b;
} LocStr16; // Localization::LocalizedString, returned in rax:rdx

static LocStr16 (*o_by_id)(const char *id);
static LocStr16 (*o_by_hash)(unsigned hash);
static LocStr16 (*o_var_by_id)(const char *id, unsigned variation);
static LocStr16 (*o_var_by_hash)(unsigned hash, unsigned variation);
static bool (*g_exists_id)(const char *id);
static bool (*g_exists_hash)(unsigned hash);
static const char *(*g_language)(void);

#define MAX_REDIRECTS 4
static LocRedirectHook g_redirects[MAX_REDIRECTS];
static int g_nredirects;
static ModsList g_redirects_mods; // their mods: the top mod's asked first

void loc_on_redirect(LocRedirectHook fn) {
    if (g_nredirects < MAX_REDIRECTS && mods_list_add(&g_redirects_mods) >= 0)
        g_redirects[g_nredirects++] = fn;
}

static const char *redirect(const char *id) {
    if (id)
        MODS_EACH(g_redirects_mods, true, i) {
            const char *to = g_redirects[i](id);
            if (to)
                return to;
        }
    return id;
}

// ---------------------------------------------------------------- missing keys
static FILE *g_log;
static bool g_log_failed;
#define SEEN_CAP 4096 // open addressing on (language, key) hashes; full: log anyway
static unsigned g_seen[SEEN_CAP];

static void language(char out[32]) {
    const char *l = g_language ? g_language() : NULL;
    size_t i = 0;
    while (l && i < 31 && l[i] > 0x20 && l[i] < 0x7f) {
        out[i] = l[i];
        i++;
    }
    out[i] = 0;
    if (!out[0])
        snprintf(out, 32, "?");
}

static bool first_time(unsigned key) {
    key = key ? key : 1;
    for (unsigned i = 0, h = key % SEEN_CAP; i < SEEN_CAP; i++, h = (h + 1) % SEEN_CAP) {
        if (g_seen[h] == key)
            return false;
        if (!g_seen[h]) {
            g_seen[h] = key;
            return true;
        }
    }
    return true;
}

static void log_missing(const char *id, unsigned hash) {
    char lang[32];
    language(lang);
    if (!first_time(name_hash(lang) * 31u ^ (id ? name_hash(id) : hash)))
        return;
    if (!g_log && !g_log_failed) {
        char dir[4096], path[4200];
        if (game_data_dir(dir, sizeof dir)) {
            snprintf(path, sizeof path, "%s/ddx_localization.log", dir);
            g_log = fopen(path, "a");
        }
        g_log_failed = !g_log;
        if (g_log_failed)
            logf_("localization: can't open ddx_localization.log, missing strings not logged");
        else {
            time_t t = time(NULL);
            char ts[32];
            strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
            fprintf(g_log, "\n[%s] session started: strings missing from the current language's table\n", ts);
        }
    }
    if (!g_log)
        return;
    time_t t = time(NULL);
    char ts[16];
    strftime(ts, sizeof ts, "%H:%M:%S", localtime(&t));
    if (id)
        fprintf(g_log, "[%s] %s %s\n", ts, lang, id);
    else
        fprintf(g_log, "[%s] %s #%08x (looked up by hash)\n", ts, lang, hash);
    fflush(g_log);
}

static void check_id(const char *id) {
    if (id && id[0] && g_exists_id && !g_exists_id(id))
        log_missing(id, 0);
}

static void check_hash(unsigned hash) {
    if (hash && g_exists_hash && !g_exists_hash(hash))
        log_missing(NULL, hash);
}

bool loc_exists(const char *id) {
    if (!id || !id[0])
        return false;
    if (!g_exists_id)
        return strcmp(ui_loc(id), id) != 0; // (no check: ask the localizer, which logs a miss)
    return g_exists_id(id);
}

// ---------------------------------------------------------------- capture
static char *g_capture;
static size_t g_capture_cap;

void loc_capture_begin(char *out, size_t cap) {
    g_capture = cap ? out : NULL;
    g_capture_cap = cap;
    if (g_capture)
        g_capture[0] = 0;
}

void loc_capture_end(void) {
    g_capture = NULL;
}

static void capture(LocStr16 s) {
    if (g_capture)
        snprintf(g_capture, g_capture_cap, "%s", s.a ? (const char *)(uintptr_t)s.a : "");
}

// ---------------------------------------------------------------- hooks
static int g_by_id_depth; // inside the lookup by id (Windows: it calls the one by hash)

static LocStr16 h_by_id(const char *id) {
    id = redirect(id);
    check_id(id);
    g_by_id_depth++;
    LocStr16 s = o_by_id(id);
    g_by_id_depth--;
    capture(s);
    return s;
}

static LocStr16 h_by_hash(unsigned hash) {
    if (!g_by_id_depth)
        check_hash(hash);
    return o_by_hash(hash);
}

static LocStr16 h_var_by_id(const char *id, unsigned variation) {
    id = redirect(id);
    check_id(id);
    LocStr16 s = o_var_by_id(id, variation);
    capture(s);
    return s;
}

static LocStr16 h_var_by_hash(unsigned hash, unsigned variation) {
    check_hash(hash);
    return o_var_by_hash(hash, variation);
}

void localization_install(void) {
    g_exists_id = (bool (*)(const char *))sym("_ZN12Localization24DoesLocalizedStringExistEPKc");
    g_exists_hash = (bool (*)(unsigned))sym("_ZN12Localization24DoesLocalizedStringExistEj");
    g_language = (const char *(*)(void))sym("_ZN12Localization18GetCurrentLanguageEv");
    // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; sub rsp,0x220 (both lookups by id)
    static const uint8_t by_id[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                    0x54, 0x53, 0x48, 0x81, 0xEC, 0x20, 0x02, 0x00, 0x00};
    void *t = sym("_ZN12Localization20LocalizeStringFromIdEPKc");
    if (t)
        *(void **)&o_by_id =
            install_hook("Localization::LocalizeStringFromId(char const*)", t, (void *)h_by_id, by_id, sizeof by_id);
    t = sym("_ZN12Localization38LocalizeStringFromIdWithVariationIndexEPKcj");
    if (t)
        *(void **)&o_var_by_id = install_hook("Localization::LocalizeStringFromIdWithVariationIndex(char const*)", t,
                                              (void *)h_var_by_id, by_id, sizeof by_id);
    // push rbp; mov rbp,rsp; sub rsp,0x20; mov esi,edi; movzx ecx,byte [rip+disp32]
    static const uint8_t by_hash[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x20, 0x89,
                                      0xFE, 0x0F, 0xB6, 0x0D, 0,    0,    0,    0};
    static const HookRip by_hash_rip[] = {{13, 17}};
    t = sym("_ZN12Localization20LocalizeStringFromIdEj");
    if (t) {
        uint8_t want[sizeof by_hash];
        memcpy(want, by_hash, sizeof want);
        memcpy(want + 13, (const uint8_t *)t + 13, 4); // the displacement is checked by the relocation, not here
        *(void **)&o_by_hash = install_hook_rip("Localization::LocalizeStringFromId(unsigned)", t, (void *)h_by_hash,
                                                want, sizeof want, by_hash_rip, 1);
    }
    // push rbp; mov rbp,rsp; sub rsp,0x20; mov edx,esi; mov esi,edi; movzx ecx,byte [rip+disp32]
    static const uint8_t var_by_hash[] = {0x55, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x20, 0x89, 0xF2,
                                          0x89, 0xFE, 0x0F, 0xB6, 0x0D, 0,    0,    0,    0};
    static const HookRip var_by_hash_rip[] = {{15, 19}};
    t = sym("_ZN12Localization38LocalizeStringFromIdWithVariationIndexEjj");
    if (t) {
        uint8_t want[sizeof var_by_hash];
        memcpy(want, var_by_hash, sizeof want);
        memcpy(want + 15, (const uint8_t *)t + 15, 4);
        *(void **)&o_var_by_hash = install_hook_rip("Localization::LocalizeStringFromIdWithVariationIndex(unsigned)", t,
                                                    (void *)h_var_by_hash, want, sizeof want, var_by_hash_rip, 1);
    }
    logf_("localization: lookups hooked (id %s, hash %s, variation id %s, variation hash %s); missing strings -> "
          "ddx_localization.log%s",
          o_by_id ? "on" : "off", o_by_hash ? "on" : "off", o_var_by_id ? "on" : "off", o_var_by_hash ? "on" : "off",
          g_exists_id && g_exists_hash && g_language ? "" : " (checks unavailable)");
}
