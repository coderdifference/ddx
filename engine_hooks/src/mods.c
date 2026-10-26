// The mods built on the library and which of them the current campaign applies, in its order (see common.h, "mods.c").
//
// A mod is a folder with an engine/ folder (paths.c mod_engine_dirs: the game's mods folder, then the Workshop's; the
// library package left out). The game names a campaign's mods by UGCId: a local mod's id is its project.xml Title
// (ugc.c), a Workshop item's (unverified) its folder's name; a mod matches either.
//
// The campaign's list: Darkest::SetupUserGeneratedContent (called by ProcessProfileInitialization / ProcessProfile at
// each campaign load, to mount its mods) reads it through ProfileGameFile::GetUGCIdList() (returns `this`) on
// Darkest+0x748: a std::vector<UGCId> at +0x208 / +0x210, 0xc0 bytes each, the id a char[128] at 0. We read it after
// the original runs. Darkest::CleanupUserGeneratedContent (CleanupCampaign) ends the campaign: no mod is active.
// macOS only (Windows: every mod active, as before).
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODS  64
#define UGC_ID    128
#define PATH_MAX_ 4400
typedef struct {
    char engine[PATH_MAX_]; // the client-owned engine/ directory
    char root[PATH_MAX_];   // <mods>/<mod>
    char real[PATH_MAX_];   // the same resolved (no "..", no links): how the loader names its binaries
    char title[UGC_ID], folder[UGC_ID];
    int pos; // place in the campaign's list, -1 not applied
} Mod;
static Mod g_mods[MAX_MODS];
static int g_nmods;
static Mod g_package;
static bool g_have_package;
static bool g_campaign;           // a campaign's list is known
static unsigned g_generation = 1; // bumped as each campaign's list is read and as it closes
static int g_current = -1;        // the mod registering now
#define MAX_LISTENERS 16
static void (*g_listeners[MAX_LISTENERS])(void);
static int g_nlisteners;

static void read_title(Mod *m) {
    char xml[PATH_MAX_ + 16];
    snprintf(xml, sizeof xml, "%s/project.xml", m->root);
    FILE *f = fopen(xml, "rb");
    if (!f)
        return;
    char buf[8192];
    size_t got = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[got] = 0;
    char *t = strstr(buf, "<Title>"), *te = t ? strstr(t, "</Title>") : NULL;
    if (t && te)
        snprintf(m->title, sizeof m->title, "%.*s", (int)(te - t - 7), t + 7);
}

// engine_dir (<mods>/<mod>/engine) -> the mod's root, folder and title
static void fill(Mod *m, const char *engine_dir) {
    memset(m, 0, sizeof *m);
    snprintf(m->engine, sizeof m->engine, "%s", engine_dir);
    snprintf(m->root, sizeof m->root, "%s", engine_dir);
    char *e = strrchr(m->root, '/');
    if (e)
        *e = 0;
    const char *base = strrchr(m->root, '/');
    snprintf(m->folder, sizeof m->folder, "%s", base ? base + 1 : m->root);
    char r[PATH_MAX_];
#ifdef _WIN32
    if (_fullpath(r, m->root, sizeof r))
        snprintf(m->real, sizeof m->real, "%s", r); // backslashes, as the loader names DLLs
#else
    if (realpath(m->root, r))
        snprintf(m->real, sizeof m->real, "%s", r);
#endif
    read_title(m);
    m->pos = -1;
}

static bool add(const char *engine_dir, void *ud) {
    (void)ud;
    if (g_nmods >= MAX_MODS) {
        logf_("mods: more than %d mods built on the library, %s left out", MAX_MODS, engine_dir);
        return false;
    }
    fill(&g_mods[g_nmods], engine_dir);
    logf_("mods: %d = %s (\"%s\")", g_nmods, g_mods[g_nmods].folder, g_mods[g_nmods].title);
    g_nmods++;
    return true;
}

int mods_count(void) {
    return g_nmods;
}

static bool under(const char *path, const char *root) {
    size_t n = strlen(root);
    return n && !strncmp(path, root, n) && (path[n] == '/' || path[n] == 0);
}

int mods_find(const char *path) {
    if (!path)
        return -1;
    for (int i = 0; i < g_nmods; i++)
        if (under(path, g_mods[i].root) || under(path, g_mods[i].real))
            return i;
    return -1;
}

const char *mods_title(int mod) {
    return mod >= 0 && mod < g_nmods ? g_mods[mod].title : "";
}
const char *mods_folder(int mod) {
    return mod >= 0 && mod < g_nmods ? g_mods[mod].folder : "";
}
const char *mods_engine_dir(int mod) {
    return mod >= 0 && mod < g_nmods ? g_mods[mod].engine : "";
}
int mods_current(void) {
    return g_current;
}
void mods_set_current(int mod) {
    g_current = mod;
}
bool mods_campaign(void) {
    return g_campaign;
}
unsigned mods_generation(void) {
    return __atomic_load_n(&g_generation, __ATOMIC_ACQUIRE);
}

int mods_pos(int mod) {
    return g_campaign && mod >= 0 && mod < g_nmods ? g_mods[mod].pos : -1;
}

bool mods_active(int mod) {
    if (mod < 0)
        return true; // the library's own
    return mods_pos(mod) >= 0;
}

int mods_by_priority(int *out, int max) {
    int n = 0;
    for (int i = 0; i < g_nmods && n < max; i++) {
        if (mods_pos(i) < 0)
            continue;
        int k = n++;
        for (; k > 0 && mods_pos(out[k - 1]) > mods_pos(i); k--)
            out[k] = out[k - 1];
        out[k] = i;
    }
    return n;
}

bool mods_package_applied(void) {
    return g_have_package && g_campaign && g_package.pos >= 0;
}

void mods_on_campaign(void (*fn)(void)) {
    if (fn && g_nlisteners < MAX_LISTENERS)
        g_listeners[g_nlisteners++] = fn;
}

static int list_pos(const uint8_t *const *v, const Mod *m) {
    int i = 0;
    for (const uint8_t *u = v[0]; u && u + UGC_ID_SIZE <= v[1]; u += UGC_ID_SIZE, i++) {
        const char *id = (const char *)u;
        if ((m->title[0] && !strncmp(id, m->title, UGC_ID)) || (m->folder[0] && !strncmp(id, m->folder, UGC_ID)))
            return i;
    }
    return -1;
}

static void notify(void) {
    __atomic_add_fetch(&g_generation, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < g_nlisteners; i++)
        g_listeners[i]();
}

static void (*o_setup)(void *darkest);
static void h_setup(void *darkest) {
    o_setup(darkest);
    const uint8_t *const *v = (const uint8_t *const *)((const uint8_t *)darkest + DARKEST_UGC_IDS);
    char active[1024] = "";
    for (int i = 0; i < g_nmods; i++) {
        g_mods[i].pos = list_pos(v, &g_mods[i]);
        if (g_mods[i].pos >= 0) {
            size_t n = strlen(active);
            snprintf(active + n, sizeof active - n, "%s%s@%d", n ? ", " : "", g_mods[i].folder, g_mods[i].pos);
        }
    }
    g_package.pos = g_have_package ? list_pos(v, &g_package) : -1;
    g_campaign = true;
    char pkg[32];
    snprintf(pkg, sizeof pkg, "applied @%d", g_package.pos);
    logf_("mods: campaign loaded: %s; library package %s (@n: place in the campaign's list, 0 = highest priority)",
          active[0] ? active : "no mod built on the library", g_package.pos >= 0 ? pkg : "NOT applied");
    notify();
}

static void (*o_cleanup)(void *darkest);
static void h_cleanup(void *darkest) {
    o_cleanup(darkest);
    g_campaign = false;
    for (int i = 0; i < g_nmods; i++)
        g_mods[i].pos = -1;
    g_package.pos = -1;
    dbgf_("mods: campaign closed, no mod active");
    notify();
}

void mods_install(void) {
    mod_engine_dirs(add, NULL);
    char pkg[4096];
    if (library_engine_dir(pkg, sizeof pkg)) {
        fill(&g_package, pkg);
        g_have_package = true;
    }
    static const uint8_t setup_prologue[] = PROLOGUE_SUB8(0x68), cleanup_prologue[] = PROLOGUE_SUB8(0x58);
    *(void **)&o_setup =
        install_hook("Darkest::SetupUserGeneratedContent (mods)", sym("_ZN7Darkest25SetupUserGeneratedContentEv"),
                     (void *)h_setup, setup_prologue, sizeof setup_prologue);
    *(void **)&o_cleanup =
        install_hook("Darkest::CleanupUserGeneratedContent (mods)", sym("_ZN7Darkest27CleanupUserGeneratedContentEv"),
                     (void *)h_cleanup, cleanup_prologue, sizeof cleanup_prologue);
    if (!o_setup)
        logf_("mods: the campaign's mods can't be read; mods built on the library stay off in campaigns");
}
