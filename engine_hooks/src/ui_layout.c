// Layout files: positions and sizes by name, read from a JSON file and reloaded when it changes (see common.h,
// "ui_layout.c"). A name is a dotted path through nested objects ("anvil.pos"); a vec2 is a two-number array.
// A layout may fall back to another (ui_layout_chain): a name it lacks is looked up there (a mod's file over the
// library package's defaults, ui_layout_mod).
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_LAYOUTS 64

struct UiLayout {
    char path[1024];
    JVal *root;
    time_t mtime;
    time_t checked;
    unsigned *missing; // names already logged as missing (hashes; grows as needed)
    int nmissing, cap_missing;
    UiLayout *next; // where a name this file lacks is looked up (ui_layout_chain)
};
static UiLayout g_layouts[MAX_LAYOUTS];
static int g_nlayouts;

static bool load(UiLayout *l) {
    struct stat st;
    if (stat(l->path, &st) != 0) {
        if (l->mtime != -1)
            logf_("layout: %s missing (the defaults are used)", l->path);
        l->mtime = -1;
        return false;
    }
    FILE *f = fopen(l->path, "rb");
    if (!f)
        return false;
    char *text = malloc((size_t)st.st_size + 1);
    size_t n = text ? fread(text, 1, (size_t)st.st_size, f) : 0;
    fclose(f);
    if (!text)
        return false;
    text[n] = 0;
    char err[160] = {0};
    JVal *root = json_parse(text, err, sizeof err);
    free(text);
    l->mtime = st.st_mtime;
    if (!root) {
        logf_("layout: %s: %s (keeping the last good one)", l->path, err);
        return false;
    }
    if (l->root)
        json_free(l->root);
    l->root = root;
    l->nmissing = 0;
    logf_("layout: loaded %s", l->path);
    return true;
}

// reload when the file changed (checked at most once a second)
static void refresh(UiLayout *l) {
    time_t now = time(NULL);
    if (now == l->checked)
        return;
    l->checked = now;
    struct stat st;
    if (stat(l->path, &st) == 0 && st.st_mtime != l->mtime)
        load(l);
}

UiLayout *ui_layout_load(const char *path) {
    if (!path)
        return NULL;
    for (int i = 0; i < g_nlayouts; i++)
        if (!strcmp(g_layouts[i].path, path))
            return &g_layouts[i];
    if (g_nlayouts == MAX_LAYOUTS) {
        logf_("layout: too many files, %s not loaded", path);
        return NULL;
    }
    UiLayout *l = &g_layouts[g_nlayouts];
    memset(l, 0, sizeof *l);
    snprintf(l->path, sizeof l->path, "%s", path);
    load(l); // kept even if unreadable: used once it appears, logged once
    l->checked = time(NULL);
    g_nlayouts++;
    return l;
}

static const JVal *find_in(UiLayout *l, const char *name) {
    refresh(l);
    const JVal *v = l->root;
    char part[64];
    const char *s = name;
    while (v && *s) {
        size_t n = strcspn(s, ".");
        if (n >= sizeof part)
            return NULL;
        memcpy(part, s, n);
        part[n] = 0;
        v = v->t == J_OBJ ? json_get(v, part) : NULL;
        s += n + (s[n] == '.');
    }
    return v;
}

static const JVal *find(UiLayout *l, const char *name) {
    if (!name)
        return NULL;
    for (int hops = 0; l && hops < MAX_LAYOUTS; l = l->next, hops++) {
        const JVal *v = find_in(l, name);
        if (v)
            return v;
    }
    return NULL;
}

bool ui_layout_chain(UiLayout *l, UiLayout *next) {
    if (!l)
        return false;
    for (UiLayout *n = next; n; n = n->next)
        if (n == l)
            return false; // would loop
    l->next = next;
    return true;
}

static void missing(UiLayout *l, const char *name) {
    while (l && !l->root)
        l = l->next; // no file: logged once when it went missing; the first that has one
    if (!l)
        return;
    unsigned h = name_hash(name);
    for (int i = 0; i < l->nmissing; i++)
        if (l->missing[i] == h)
            return;
    if (l->nmissing == l->cap_missing) { // a name not remembered would be logged again every frame
        int cap = l->cap_missing ? l->cap_missing * 2 : 32;
        unsigned *grown = realloc(l->missing, (size_t)cap * sizeof *grown);
        if (!grown)
            return; // out of memory: stop logging rather than repeat
        l->missing = grown;
        l->cap_missing = cap;
    }
    l->missing[l->nmissing++] = h;
    logf_("layout: %s has no %s (using the default)", l->path, name);
}

float ui_layout_num(UiLayout *l, const char *name, float fallback) {
    const JVal *v = find(l, name);
    if (v && v->t == J_NUM)
        return (float)v->num;
    missing(l, name);
    return fallback;
}

Vec2 ui_layout_vec2(UiLayout *l, const char *name, Vec2 fallback) {
    const JVal *v = find(l, name);
    if (v && v->t == J_ARR && v->n == 2 && v->items[0].t == J_NUM && v->items[1].t == J_NUM)
        return (Vec2){(float)v->items[0].num, (float)v->items[1].num};
    missing(l, name);
    return fallback;
}

const char *ui_layout_str(UiLayout *l, const char *name, const char *fallback) {
    const JVal *v = find(l, name);
    if (v && v->t == J_STR)
        return v->str;
    missing(l, name);
    return fallback;
}

int ui_layout_keys(UiLayout *l, const char *name, const char **keys, int max) {
    const JVal *v = find(l, name);
    if (!v || v->t != J_OBJ) {
        missing(l, name);
        return 0;
    }
    int n = 0;
    for (int i = 0; i < v->n && n < max; i++)
        keys[n++] = v->keys[i];
    return n;
}

bool ui_layout_has(UiLayout *l, const char *name) {
    return find(l, name) != NULL;
}
