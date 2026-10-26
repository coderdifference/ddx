// Where the game keeps its data (mods/, app.log) and where Steam keeps Workshop mods.
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <mach-o/dyld.h>
#include <stdlib.h>
#endif

#ifdef _WIN32
// The install folder: <Steam>/steamapps/common/DarkestDungeon, two up from _windows/win64/Darkest.exe.
static bool install_dir(char *out, size_t n) {
    char exe[MAX_PATH];
    DWORD l = GetModuleFileNameA(NULL, exe, sizeof exe);
    if (!l || l >= sizeof exe)
        return false;
    for (int up = 0; up < 3; up++) { // strip Darkest.exe, win64, _windows
        char *slash = strrchr(exe, '\\');
        if (!slash)
            return false;
        *slash = 0;
    }
    return snprintf(out, n, "%s", exe) < (int)n;
}

bool game_data_dir(char *out, size_t n) {
    return install_dir(out, n);
}

bool workshop_dir(char *out, size_t n) {
    char dir[MAX_PATH];
    if (!install_dir(dir, sizeof dir))
        return false;
    return snprintf(out, n, "%s\\..\\..\\workshop\\content\\262060", dir) < (int)n; // from steamapps/common/<game>
}
#else
bool game_data_dir(char *out, size_t n) {
    char exe[4096];
    uint32_t l = sizeof exe;
    if (_NSGetExecutablePath(exe, &l) != 0)
        return false;
    char *slash = strrchr(exe, '/'); // .../Contents/MacOS/Darkest
    if (!slash)
        return false;
    *slash = 0;
    return snprintf(out, n, "%s/../Resources/data", exe) < (int)n;
}

bool workshop_dir(char *out, size_t n) {
    const char *home = getenv("HOME");
    if (!home)
        return false;
    return snprintf(out, n, "%s/Library/Application Support/Steam/steamapps/workshop/content/262060", home) < (int)n;
}
#endif

// The library package: the mod folder whose engine/ holds LIBRARY_MARKER (the library's own layouts, art and strings,
// and its binary). It is not a client: mod_engine_dirs leaves it out and library_engine_dir finds it.
#define LIBRARY_MARKER "library.json"
static bool is_library_engine_dir(const char *eng) {
    char p[4500];
    snprintf(p, sizeof p, "%s/%s", eng, LIBRARY_MARKER);
    FILE *f = fopen(p, "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

// Calls fn(engine_dir) for each <mods>/<mod>/engine folder (the game's mods folder first, then the Workshop's: the order
// configs load in); library: the package's folders only, else every other mod's. Stops when fn returns false.
static int each_engine_dir(bool library, bool (*fn)(const char *engine_dir, void *ud), void *ud) {
    char roots[2][4200];
    int nroots = 0, n = 0;
    char dir[4096];
    if (game_data_dir(dir, sizeof dir))
        snprintf(roots[nroots++], sizeof roots[0], "%s/mods", dir);
    if (workshop_dir(dir, sizeof dir))
        snprintf(roots[nroots++], sizeof roots[0], "%s", dir);
    for (int r = 0; r < nroots; r++) {
        DIR *d = opendir(roots[r]);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.')
                continue;
            char eng[4400];
            snprintf(eng, sizeof eng, "%s/%s/engine", roots[r], e->d_name);
            DIR *d2 = opendir(eng);
            if (!d2)
                continue;
            closedir(d2);
            if (is_library_engine_dir(eng) != library)
                continue;
            n++;
            if (!fn(eng, ud)) {
                closedir(d);
                return n;
            }
        }
        closedir(d);
    }
    return n;
}

int mod_engine_dirs(bool (*fn)(const char *engine_dir, void *ud), void *ud) {
    return each_engine_dir(false, fn, ud);
}

typedef struct {
    char *out;
    size_t n;
} DirOut;
static bool take_first(const char *engine_dir, void *ud) {
    DirOut *o = ud;
    snprintf(o->out, o->n, "%s", engine_dir);
    return false;
}

bool library_engine_dir(char *out, size_t n) {
    static char found[4400];
    static int state; // 0 not looked yet, 1 found, -1 none
    if (!state) {
        DirOut o = {found, sizeof found};
        state = each_engine_dir(true, take_first, &o) ? 1 : -1;
        if (state > 0)
            logf_("library package: %s", found);
        else
            logf_("library package: not found (no mod folder has engine/%s); its defaults are missing", LIBRARY_MARKER);
    }
    if (state < 0)
        return false;
    return snprintf(out, n, "%s", found) < (int)n;
}

// ui_layout_mod: the active mods' engine/ui/<file>, highest priority first (mods_by_priority), then the library
// package's, chained so a name missing from one is looked up in the next (a mod overrides the library's defaults per
// name, and a mod higher in the campaign's list overrides one below it). Each file name's files are found once
// (layouts are read every frame) and chained again after each campaign load; the files reload when they change.
#define MAX_LAYOUT_MODS 16
typedef struct {
    char file[128];
    int n;
    struct {
        int mod;
        UiLayout *l;
    } m[MAX_LAYOUT_MODS];
    UiLayout *pkg, *head;
    unsigned gen; // g_layout_gen when chained
} LayoutFiles;
static unsigned g_layout_gen = 1;
static void layouts_on_campaign(void) {
    g_layout_gen++;
}

static UiLayout *open_layout(const char *engine_dir, const char *file) {
    char path[4500];
    snprintf(path, sizeof path, "%s/ui/%s", engine_dir, file);
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    fclose(fp);
    return ui_layout_load(path);
}
static bool find_layout(const char *engine_dir, void *ud) {
    LayoutFiles *f = ud;
    UiLayout *l = open_layout(engine_dir, f->file);
    if (l && f->n < MAX_LAYOUT_MODS) {
        f->m[f->n].mod = mods_find(engine_dir);
        f->m[f->n++].l = l;
    }
    return true;
}

static void append(LayoutFiles *f, UiLayout **tail, UiLayout *l) {
    if (!f->head)
        f->head = l;
    else if (!ui_layout_chain(*tail, l))
        return;
    *tail = l;
}

static void chain(LayoutFiles *f) {
    for (int i = 0; i < f->n; i++)
        ui_layout_chain(f->m[i].l, NULL);
    if (f->pkg)
        ui_layout_chain(f->pkg, NULL);
    UiLayout *tail = NULL;
    f->head = NULL;
    int order[64], n = mods_by_priority(order, 64);
    for (int k = 0; k < n; k++)
        for (int i = 0; i < f->n; i++)
            if (f->m[i].mod == order[k])
                append(f, &tail, f->m[i].l);
    for (int i = 0; i < f->n; i++)
        if (f->m[i].mod < 0)
            append(f, &tail, f->m[i].l); // outside any mod
    if (f->pkg)
        append(f, &tail, f->pkg);
    f->gen = g_layout_gen;
}

UiLayout *ui_layout_mod(const char *file) {
    enum { MAX_FOUND = 64 };
    static LayoutFiles found[MAX_FOUND];
    static int nfound;
    if (!file)
        return NULL;
    LayoutFiles *f = NULL;
    for (int i = 0; i < nfound && !f; i++)
        if (!strcmp(found[i].file, file))
            f = &found[i];
    if (!f) {
        if (nfound == MAX_FOUND) {
            logf_("layout: more than %d layout names, %s not loaded", MAX_FOUND, file);
            return NULL;
        }
        if (!nfound)
            mods_on_campaign(layouts_on_campaign);
        f = &found[nfound++];
        memset(f, 0, sizeof *f);
        snprintf(f->file, sizeof f->file, "%s", file);
        mod_engine_dirs(find_layout, f);
        char lib[4400];
        if (library_engine_dir(lib, sizeof lib))
            f->pkg = open_layout(lib, file);
        if (!f->n && !f->pkg)
            logf_("layout: no mod or library package has engine/ui/%s", file);
    }
    if (f->gen != g_layout_gen)
        chain(f);
    return f->head;
}
