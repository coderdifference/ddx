// Mod fonts: a mod's fonts/*.fonts.darkest (the vanilla fonts/fonts.darkest syntax, `font:` and `font_ref:` lines)
// adds fonts beside the game's. A `font_ref:` may name one of the mod's fonts or one of the game's (e.g.
// "dwarven_axe_medium"), so a mod can give its own name to a vanilla font. The game reads only its own fonts/fonts.darkest, once at start-up
// (FontLibrary::Init -> new_FontLibrary_FromFile -> set_FontLibrary_FromFile, which Clear()s the table first, so it
// can't load a second file), before any mod is mounted.
//
// FontLibrary::s_Data (_Data*): +0x01 the current language (FixedLengthString<256>); +0x108 std::vector<Font*> the
// fonts as listed; +0x120 std::vector<Font*> the current language's (the list with language overrides swapped in,
// index for index; SetCurrentLanguage rebuilds it); +0x140 the font_ref map (id hash at node +0x1c, index into both
// lists at +0x20). Font (0x118, operator new): id hash +0, id FixedLengthString<256> +4, TextureFont* +0x108,
// textures loaded +0x110 (SetCurrentLanguage loads and unloads them; a new font starts unloaded).
// GetFontFromId(hash) = current list[ref map[hash]]->TextureFont.
//
// Registration copies set_FontLibrary_FromFile's `font:` branch (new_TextureFont_FromBMFont, a Font appended to
// the list), appends the same Font to the current list (so indexes stay paired), loads its textures as
// SetCurrentLanguage would, and adds `font_ref:`s with _Data::AddFontReference. Each font is registered on its own
// when ui_font is asked for a ref still missing, once its .fnt exists for the game (FileExists(mount 1), i.e. its mod
// is mounted; rechecked at most once a second until then), and each ref once its font is there: a campaign without
// one mod still gets the others' fonts. A registered font stays when its mod is unmounted (its textures are loaded). A language change keeps them (rebuilt from the list); a hotload of
// the game's fonts file (development builds) drops them, and the next ui_font registers them again.
// Fonts' .file and .fnt page paths are relative to the data folder or a mod, as the game's.
// Windows: the same layout (FontLibrary::GetFontFromId 0x26fa50, SetCurrentLanguage 0x270a40); FindDefaultFont and the
// FixedLengthString constructor are stand-ins (syms_win.c), and the lists grow through the game's own
// vector<Font*>::_Emplace_reallocate, so the buffers stay the game's CRT's.
#include "common.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FONTS 16
#define MAX_REFS  32
typedef struct {
    char id[64], file[192];
    bool point_sample, failed;
    void *font;
} ModFont; // font: the game's Font
typedef struct {
    char id[64], font[64];
    unsigned hash;
    bool added, bad;
} ModRef; // bad: names no font at all
static ModFont g_fonts[MAX_FONTS];
static ModRef g_refs[MAX_REFS];
static int g_nfonts, g_nrefs;
static bool g_scanned;
static void *g_registered; // the _Data they were added to
static time_t g_retry;

static void *(*g_get_font)(unsigned id_hash); // FontLibrary::GetFontFromId
static void **g_data;                         // &FontLibrary::s_Data
static void *(*g_find_default)(void *data, const char *id);
static void (*g_add_ref)(void *data, const char *ref, const char *font);
static void *(*g_new_bmfont)(const char *path, bool point_sample, float scale, void *reuse);
static void (*g_load_textures)(void *tf);
static bool (*g_file_exists)(int mount, const char *path);
static void (*g_fls256)(void *s, const char *text); // FixedLengthString<256>(char const*)
static void *(*g_new)(size_t n);                    // operator new / delete, as the game's vectors use
static void (*g_delete)(void *p);
#ifdef _WIN32
static void (*g_grow)(void **v, void **where, void *const *value); // the game's vector<T*>::_Emplace_reallocate
#endif

// ---------------------------------------------------------------- the files
// the quoted value of `.key "value"` (or the bare word) in one entry
static bool field(const char *entry, const char *key, char *out, size_t cap) {
    char k[32];
    snprintf(k, sizeof k, ".%s", key);
    const char *p = entry;
    size_t kl = strlen(k);
    while ((p = strstr(p, k))) {
        const char *v = p + kl;
        if (*v != ' ' && *v != '\t') {
            p = v;
            continue;
        }
        while (*v == ' ' || *v == '\t')
            v++;
        size_t n = 0;
        if (*v == '"') {
            v++;
            while (v[n] && v[n] != '"' && v[n] != '\n')
                n++;
        } else
            while (v[n] && v[n] != ' ' && v[n] != '\t' && v[n] != '\n' && v[n] != '\r')
                n++;
        if (n >= cap)
            n = cap - 1;
        memcpy(out, v, n);
        out[n] = 0;
        return true;
    }
    return false;
}

static void parse(char *text, const char *path) {
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (!strncmp(line, "font:", 5) && g_nfonts < MAX_FONTS) {
            ModFont *f = &g_fonts[g_nfonts];
            char ps[8] = "";
            if (!field(line, "id", f->id, sizeof f->id) || !field(line, "file", f->file, sizeof f->file))
                continue;
            field(line, "point_sample", ps, sizeof ps);
            f->point_sample = !strcmp(ps, "true");
            g_nfonts++;
        } else if (!strncmp(line, "font_ref:", 9) && g_nrefs < MAX_REFS) {
            ModRef *r = &g_refs[g_nrefs];
            if (!field(line, "id", r->id, sizeof r->id) || !field(line, "font", r->font, sizeof r->font))
                continue;
            r->hash = name_hash(r->id);
            g_nrefs++;
        }
    }
    logf_("ui_font: %s: %d font(s), %d ref(s) so far", path, g_nfonts, g_nrefs);
}

// <dir>/<mod>/fonts/*.fonts.darkest
static void scan_mods(const char *dir) {
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char fonts[4096];
        snprintf(fonts, sizeof fonts, "%s/%s/fonts", dir, e->d_name);
        DIR *d2 = opendir(fonts);
        if (!d2)
            continue;
        struct dirent *f;
        while ((f = readdir(d2))) {
            size_t l = strlen(f->d_name);
            if (l <= 14 || strcmp(f->d_name + l - 14, ".fonts.darkest"))
                continue;
            char p[4400];
            snprintf(p, sizeof p, "%s/%s", fonts, f->d_name);
            FILE *fp = fopen(p, "rb");
            if (!fp)
                continue;
            fseek(fp, 0, SEEK_END);
            long len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *text = malloc(len + 1);
            size_t got = fread(text, 1, len, fp);
            text[got] = 0;
            fclose(fp);
            parse(text, p);
            free(text);
        }
        closedir(d2);
    }
    closedir(d);
}

static void scan(void) {
    g_scanned = true;
    char dir[4096];
    if (game_data_dir(dir, sizeof dir)) {
        char mods[4200];
        snprintf(mods, sizeof mods, "%s/mods", dir);
        scan_mods(mods);
    }
    if (workshop_dir(dir, sizeof dir))
        scan_mods(dir);
}

// ---------------------------------------------------------------- registration
// push p onto a libc++ std::vector<T*> {begin, end, capacity}, growing it as libc++ does (doubling)
static bool push(void **v, void *p) {
#ifdef _WIN32
    if (v[1] != v[2]) {
        *(void **)v[1] = p;
        v[1] = (void **)v[1] + 1;
    } else
        g_grow(v, v[1], &p); // the game's allocator frees the old buffer
    return true;
#endif
    void **begin = v[0], **end = v[1], **cap = v[2];
    if (end == cap) {
        size_t n = (size_t)(end - begin), m = n ? 2 * n : 4;
        void **nb = g_new(m * sizeof *nb);
        if (!nb)
            return false;
        if (n)
            memcpy(nb, begin, n * sizeof *nb);
        if (begin)
            g_delete(begin);
        v[0] = nb;
        v[1] = nb + n;
        v[2] = nb + m;
        end = nb + n;
    }
    *end = p;
    v[1] = end + 1;
    return true;
}

static size_t count(void **v) {
    return (size_t)((void **)v[1] - (void **)v[0]);
}

// still in data's list: the hotload's Clear() deletes our Fonts
static bool still_registered(void *data) {
    if (g_registered != data)
        return false;
    for (int i = 0; i < g_nfonts; i++)
        if (g_fonts[i].font && g_find_default(data, g_fonts[i].id) != g_fonts[i].font)
            return false;
    return true;
}

static bool add_font(void *data, ModFont *f) {
    void **list = (void **)((uint8_t *)data + DATA_FONTS), **cur = (void **)((uint8_t *)data + DATA_CURRENT);
    if (count(list) != count(cur)) {
        logf_("ui_font: font lists differ in length, %s not added", f->id);
        return false;
    }
    if (g_find_default(data, f->id)) {
        logf_("ui_font: font %s already exists", f->id);
        return false;
    }
    void *tf = g_new_bmfont(f->file, f->point_sample, 1.0f, NULL);
    if (!tf) {
        logf_("ui_font: %s: could not load %s", f->id, f->file);
        return false;
    }
    uint8_t *font = g_new(FONT_SIZE);
    memset(font, 0, FONT_SIZE);
    *(unsigned *)font = name_hash(f->id);
    g_fls256(font + FONT_ID, f->id);
    *(void **)(font + FONT_TEXTURE_FONT) = tf;
    if (!push(list, font))
        return false;
    if (!push(cur, font)) {
        ((void **)list)[1] = (void **)list[1] - 1;
        return false;
    }
    font[FONT_LOADED] = 1;
    g_load_textures(tf);
    f->font = font;
    logf_("ui_font: added font %s (%s)", f->id, f->file);
    return true;
}

// a hotload's Clear() deleted our Fonts (or the _Data is new): start again
static void reset_if_cleared(void *data) {
    if (still_registered(data))
        return;
    for (int i = 0; i < g_nfonts; i++)
        g_fonts[i].font = NULL, g_fonts[i].failed = false;
    for (int i = 0; i < g_nrefs; i++)
        g_refs[i].added = false;
    g_registered = data;
    g_retry = 0;
}

// the fonts whose files are mounted now, then the refs whose fonts are there
static void register_pending(void *data) {
    time_t now = time(NULL);
    if (now == g_retry)
        return;
    g_retry = now;
    for (int i = 0; i < g_nfonts; i++)
        if (!g_fonts[i].font && !g_fonts[i].failed && g_file_exists(1, g_fonts[i].file))
            g_fonts[i].failed = !add_font(data, &g_fonts[i]); // logged once, not retried
    for (int i = 0; i < g_nrefs; i++) {
        ModRef *r = &g_refs[i];
        if (r->added || r->bad)
            continue;
        bool ours = false, ok = false;
        for (int j = 0; j < g_nfonts; j++)
            if (!strcmp(g_fonts[j].id, r->font)) {
                ours = true;
                ok = g_fonts[j].font != NULL;
            }
        if (!ours)
            ok = g_find_default(data, r->font) != NULL; // a game font
        if (ok) {
            g_add_ref(data, r->id, r->font);
            r->added = true;
        } else if (!ours) {
            r->bad = true;
            logf_("ui_font: font_ref %s: no font %s", r->id, r->font);
        }
    }
}

static bool ready(void) {
    static int state; // 0 unresolved, 1 ok, -1 off
    if (!state) {
        g_get_font = (void *(*)(unsigned))sym("_ZN11FontLibrary13GetFontFromIdEj");
        g_data = (void **)sym("_ZN11FontLibraryL6s_DataE");
        g_find_default = (void *(*)(void *, const char *))sym("_ZN11FontLibrary5_Data15FindDefaultFontEPKc");
        g_add_ref =
            (void (*)(void *, const char *, const char *))sym("_ZN11FontLibrary5_Data16AddFontReferenceEPKcS2_");
        g_new_bmfont = (void *(*)(const char *, bool, float, void *))sym(
            "_Z26new_TextureFont_FromBMFont8c_stringbfP11TextureFont");
        g_load_textures = (void (*)(void *))sym("_Z24TextureFont_LoadTexturesP11TextureFont");
        g_file_exists = (bool (*)(int, const char *))sym("_Z10FileExists12StorageMountPKc");
        g_fls256 = (void (*)(void *, const char *))sym("_ZN17FixedLengthStringILi256EEC1EPKc");
        g_new = (void *(*)(size_t))sym("_Znwm");
        g_delete = (void (*)(void *))sym("_ZdlPv");
#ifdef _WIN32
        g_grow = (void (*)(void **, void **, void *const *))sym("win:std::vector<T*>::_Emplace_reallocate");
        if (!g_grow)
            g_data = NULL; // no way to grow the lists: no mod fonts
#endif
        state = g_get_font ? 1 : -1;
    }
    return state > 0;
}

static bool mod_fonts_ready(void) {
    return g_data && g_find_default && g_add_ref && g_new_bmfont && g_load_textures && g_file_exists && g_fls256 &&
           g_new && g_delete;
}

const void *ui_font(const char *id) {
    if (!id || !ready())
        return NULL;
    unsigned h = name_hash(id);
    if (mod_fonts_ready()) {
        if (!g_scanned)
            scan();
        for (int i = 0; i < g_nrefs; i++) {
            if (g_refs[i].hash != h)
                continue;
            void *data = *g_data;
            if (!data)
                return NULL;
            reset_if_cleared(data);
            if (!g_refs[i].added)
                register_pending(data);
            if (!g_refs[i].added)
                return NULL; // not (yet): no "unknown font" error each frame
            break;
        }
    }
    return g_get_font(h);
}
