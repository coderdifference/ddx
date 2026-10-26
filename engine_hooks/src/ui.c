// Shared immediate-mode UI helpers: the GUI_ImageButton hook (paging.c and guild.c are its clients), a
// std::function<void()> built in C, and text drawing.
//
// GUI_ImageButton(ImageButtonParameters const&, std::function<void()> x5): the first function is the click.
// ImageButtonParameters (~0x328 bytes, base shared with TextParameters): id (u64) 0x08, position (vec2) 0x18,
// texture handle 0x40, tooltip (GuiTooltipParameters*) 0x50, hover sound 0x1ec and click sound 0x22c
// (FixedLengthString<64>), enabled 0x2ee (u32-id ctor; false = drawn, no hover or click).
// std::function<void()> (libc++): a 0x20 buffer, then the __base* at 0x20 (pointing into the buffer for small
// functions); __base vtable: ~ x2, __clone(), __clone(dest), destroy, destroy_deallocate, operator(), target,
// target_type. By-value std::function arguments are the caller's temporaries (Itanium ABI: the caller destroys
// them), so a client can pass its own function in place of the game's for one call.
//
// GUI_ImageButton's hit rect is the texture's own size from the position (Texture_GetSize, OffsetFromAnchor), whatever
// the drawn size (WidgetParameters 0x100); it calls GUI_ImageButton(params, vec4 rect, std::function x5), the rect
// {left, right} in xmm0 and {top, bottom} in xmm1, which ui_image_button_rect calls directly (no clients).
//
// TextParameters has no constructor (callers fill it inline); its defaults are ImageButtonParameters' (the
// u32-id ctor with no texture) except enabled 0x2ee = 0, 0x100 = +inf (wrap width), 0x110 = 1.
//
// Windows (MSVC): GUI_ImageButton(result*, ImageButtonParameters const&, std::function<void()> x5) returns a small
// struct through the hidden pointer; the parameters' fields after 0x58 sit later (enabled 0x31e, wrap width 0x130,
// 0x140 = 1) and their constructors are inlined (syms_win.c has stand-ins). std::function<void()> is 0x40 bytes,
// its _Func_base* at 0x38 (the rest is its small-object buffer); _Func_base vtable: _Copy(where), _Move(where),
// _Do_call, _Target_type, _Delete_this(bool dealloc), _Get. By-value std::function arguments are the caller's
// temporaries but the CALLEE destroys them: a client that replaces the click destroys the game's one itself.
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- std::function<void()> made in C
typedef struct FakeFn {
    void **vtbl;
    void (*call)(const uint8_t *data);
    uint8_t data[16];
} FakeFn;
#ifdef _WIN32
static void *fn_copy(const FakeFn *self, void *where) {
    memcpy(where, self, sizeof *self);
    return where;
}
static void fn_call(FakeFn *self) {
    self->call(self->data);
}
static const void *fn_target_type(const void *self) {
    static const char dummy[16];
    (void)self;
    return dummy;
}
static void fn_delete(FakeFn *self, bool dealloc) {
    if (dealloc)
        free(self);
} // (ours live in the buffer)
static const void *fn_get(const void *self) {
    (void)self;
    return NULL;
}
static void *g_fn_vtbl[6] = {(void *)fn_copy,        (void *)fn_copy,   (void *)fn_call,
                             (void *)fn_target_type, (void *)fn_delete, (void *)fn_get};

// destroy a by-value std::function argument we don't pass on
void ui_function_destroy(StdFunction *f) {
    void **impl = f->f;
    if (!impl)
        return;
    ((void (*)(void *, bool))(*(void ***)impl)[4])(impl, (void *)impl != (void *)f->buf);
    f->f = NULL;
}

// copy a std::function (its _Func_base::_Copy, vtable slot 0: into the buffer if it fits, else a new one)
void ui_function_copy(StdFunction *dst, const StdFunction *src) {
    void **impl = src ? src->f : NULL;
    dst->f = impl ? ((void *(*)(void *, void *))(*(void ***)impl)[0])(impl, dst->buf) : NULL;
}
#else
static void fn_nop(void *self) {
    (void)self;
}
static void *fn_clone(const FakeFn *self) {
    FakeFn *f = malloc(sizeof *f);
    *f = *self;
    return f;
}
static void fn_clone_into(const FakeFn *self, FakeFn *dest) {
    *dest = *self;
}
static void fn_free(FakeFn *self) {
    free(self);
}
static void fn_call(FakeFn *self) {
    self->call(self->data);
}
static const void *fn_target(const void *self, const void *ti) {
    (void)self;
    (void)ti;
    return NULL;
}
static const void *fn_target_type(const void *self) {
    static const char dummy[16];
    (void)self;
    return dummy;
}
static void *g_fn_vtbl[9] = {(void *)fn_nop,  (void *)fn_nop,  (void *)fn_clone,  (void *)fn_clone_into, (void *)fn_nop,
                             (void *)fn_free, (void *)fn_call, (void *)fn_target, (void *)fn_target_type};
#endif

void ui_function(StdFunction *out, void (*call)(const uint8_t *data), const void *data, size_t n) {
    memset(out, 0, sizeof *out);
    FakeFn *f = (FakeFn *)out->buf;
    f->vtbl = g_fn_vtbl;
    f->call = call;
    memcpy(f->data, data, n < sizeof f->data ? n : sizeof f->data);
    out->f = f;
}

// ---------------------------------------------------------------- text
static void (*g_params_ctor32)(void *self, Vec2 pos, const void *texture, unsigned id, const void *tooltip);
static void (*g_text)(const void *params, const char *s);
static void (*g_text_dtor)(void *params);
static void (*g_set_font)(unsigned font_hash);
static void (*g_colour)(unsigned colour_hash);
static void (*g_colour_rgba)(uint32_t rgba);

void ui_text_anchored(Vec2 pos, const char *s, const char *font, const char *colour, int anchor, float width) {
    if (!g_params_ctor32 || !g_text || !g_text_dtor || !g_set_font || !g_colour)
        return;
    // ui_font first: it registers mod fonts on first use, so the game's SetFont never meets an unknown id
    if (font && !ui_font(font))
        return;
    uint8_t p[0x400];
    memset(p, 0, sizeof p);
    uint64_t none[2] = {0xFFFFFFFF00000000ull, 0}; // an empty ResourceHandle
    g_params_ctor32(p, pos, none, 0, NULL);
    p[PARAMS_ENABLED] = 0;
    *(float *)(p + TEXT_WRAP) = width > 0 ? width : INFINITY;
    *(uint32_t *)(p + TEXT_WRAP + 4) = 0;
    *(uint64_t *)(p + TEXT_MODE) = (uint64_t)anchor; // LayoutAnchor: 0 top left, 1 centred on pos.x
    if (colour)
        g_colour(name_hash(colour));
    if (font)
        g_set_font(name_hash(font));
    g_text(p, s);
    g_text_dtor(p);
    if (g_colour_rgba)
        g_colour_rgba(0xffffffffu);
}

void ui_colour(uint32_t rgba) {
    if (g_colour_rgba)
        g_colour_rgba(rgba);
}

void ui_text(Vec2 pos, const char *s, const char *font, const char *colour) {
    ui_text_anchored(pos, s, font, colour, 1, 0);
}

// ---------------------------------------------------------------- text with inline pictures
// Words (split at spaces; '\n' ends a line) laid out by hand: each run of text is measured and drawn on its own, each
// picture is a box `icon` square with `gap` either side. A token is "{icon|<path>}" or "{currency|<id>}"
// (shared/estate/currency.<id>.icon.png, the game's estate currency icons and a mod's own).
static bool rich_token(const char *s, char *path, size_t cap, size_t *len) {
    const char *kinds[2] = {"{icon|", "{currency|"};
    for (int k = 0; k < 2; k++) {
        size_t kl = strlen(kinds[k]);
        if (strncmp(s, kinds[k], kl))
            continue;
        const char *e = strchr(s + kl, '}');
        if (!e)
            return false;
        if (k == 0)
            snprintf(path, cap, "%.*s", (int)(e - s - kl), s + kl);
        else
            snprintf(path, cap, "shared/estate/currency.%.*s.icon.png", (int)(e - s - kl), s + kl);
        *len = (size_t)(e + 1 - s);
        return true;
    }
    return false;
}

bool ui_rich_has_icons(const char *s) {
    return s && (strstr(s, "{icon|") || strstr(s, "{currency|"));
}

// one word's width (text runs and pictures), up to the next space or newline; *end: where it stops
static float rich_word(const char *s, const char **end, const char *font, float icon, float gap) {
    float w = 0;
    char run[256], path[160];
    size_t rn = 0, tl;
    while (*s && *s != ' ' && *s != '\n') {
        if (*s == '{' && rich_token(s, path, sizeof path, &tl)) {
            run[rn] = 0;
            w += rn ? ui_text_width(font, run) : 0;
            rn = 0;
            w += icon + 2 * gap;
            s += tl;
        } else {
            if (rn + 1 < sizeof run)
                run[rn++] = *s;
            s++;
        }
    }
    run[rn] = 0;
    if (rn)
        w += ui_text_width(font, run);
    *end = s;
    return w;
}

static void rich_draw_word(const char *s, const char *end, Vec2 at, float lh, const char *font, const char *colour,
                           float icon, float gap) {
    char run[256], path[160];
    size_t rn = 0, tl;
    uint8_t tex[16];
    while (s < end) {
        if (*s == '{' && rich_token(s, path, sizeof path, &tl)) {
            if (rn) {
                run[rn] = 0;
                ui_text_anchored(at, run, font, colour, 0, 0);
                at.x += ui_text_width(font, run);
                rn = 0;
            }
            if (ui_texture(path, tex))
                ui_image_sized((Vec2){at.x + gap, at.y + (lh - icon) / 2}, tex, (Vec2){icon, icon});
            at.x += icon + 2 * gap;
            s += tl;
        } else {
            if (rn + 1 < sizeof run)
                run[rn++] = *s;
            s++;
        }
    }
    if (rn) {
        run[rn] = 0;
        ui_text_anchored(at, run, font, colour, 0, 0);
    }
}

float ui_rich_text(Vec2 pos, const char *s, const char *font, const char *colour, float width, float icon, float gap,
                   bool centred, bool draw) {
    if (!s || !*s)
        return 0;
    float lh = ui_text_height(font, "Ag", 0), space = ui_text_width(font, "a a") - ui_text_width(font, "aa");
    if (width <= 0)
        width = INFINITY;
    float y = 0;
    while (*s) {
        // a line: as many words as fit (at least one)
        const char *ls = s, *le = s, *p = s;
        float lw = 0;
        while (*p && *p != '\n') {
            const char *we;
            float ww = rich_word(p, &we, font, icon, gap);
            float nw = lw + (le > ls ? space : 0) + ww;
            if (le > ls && nw > width)
                break;
            lw = nw;
            le = we;
            p = we;
            while (*p == ' ')
                p++;
        }
        if (draw) {
            Vec2 at = {centred ? pos.x - lw / 2 : pos.x, pos.y + y};
            for (const char *w = ls; w < le;) {
                const char *we;
                float ww = rich_word(w, &we, font, icon, gap);
                rich_draw_word(w, we, at, lh, font, colour, icon, gap);
                at.x += ww + space;
                w = we;
                while (w < le && *w == ' ')
                    w++;
            }
        }
        y += lh;
        s = p;
        if (*s == '\n')
            s++;
        else
            while (*s == ' ')
                s++;
    }
    return y;
}

// ---------------------------------------------------------------- GUI_ImageButton clients
#define MAX_CLIENTS 4
static struct {
    UiButtonBefore before;
    UiButtonAfter after;
} g_clients[MAX_CLIENTS];
static int g_nclients;
static ModsList g_clients_mods; // their mods: a higher mod's click replaces a lower one's

#ifdef _WIN32
typedef void *(*image_button_fn)(void *result, const void *params, void *f1, void *f2, void *f3, void *f4, void *f5);
static image_button_fn g_orig;

static void *hook(void *result, const void *params, void *f1, void *f2, void *f3, void *f4, void *f5) {
    StdFunction mine;
    void *click = f1;
    if (params)
        MODS_EACH(g_clients_mods, false, i)
    if (g_clients[i].before && g_clients[i].before((void *)params, &mine))
        click = &mine;
    if (click != f1)
        ui_function_destroy(f1); // the game's click: the callee destroys its arguments
    void *r = g_orig(result, params, click, f2, f3, f4, f5);
    if (params)
        MODS_EACH(g_clients_mods, false, i)
    if (g_clients[i].after)
        g_clients[i].after(params);
    return r;
}

bool ui_image_button(const void *params, StdFunction *click) {
    if (!g_orig)
        return false;
    StdFunction none[4];
    memset(none, 0, sizeof none);
    uint8_t result[64];
    g_orig(result, params, click, &none[0], &none[1], &none[2], &none[3]);
    return true;
}

// the vec4 overload (0x275770; the plain one calls it with {left, right, top, bottom}): the rect by pointer, the
// result through the hidden pointer; it destroys its std::function arguments, as the plain one
bool ui_image_button_rect(const void *params, StdFunction *click, Vec2 pos, Vec2 size) {
    static void *(*rect_fn)(void *, const void *, const float *, void *, void *, void *, void *, void *);
    static bool resolved;
    if (!resolved) {
        resolved = true;
        *(void **)&rect_fn =
            sym("_Z15GUI_ImageButtonRK21ImageButtonParameters4vec4NSt3__18functionIFvvEEES6_S6_S6_S6_");
    }
    if (!rect_fn)
        return false;
    StdFunction none[4];
    memset(none, 0, sizeof none);
    float rect[4] = {pos.x, pos.x + size.x, pos.y, pos.y + size.y};
    uint8_t result[64];
    rect_fn(result, params, rect, click, &none[0], &none[1], &none[2], &none[3]);
    return true;
}
#else
typedef uint64_t (*image_button_fn)(const void *params, void *f1, void *f2, void *f3, void *f4, void *f5);
static image_button_fn g_orig;

static uint64_t hook(const void *params, void *f1, void *f2, void *f3, void *f4, void *f5) {
    StdFunction mine;
    void *click = f1;
    if (params)
        MODS_EACH(g_clients_mods, false, i)
    if (g_clients[i].before && g_clients[i].before((void *)params, &mine))
        click = &mine;
    uint64_t r = g_orig(params, click, f2, f3, f4, f5);
    if (params)
        MODS_EACH(g_clients_mods, false, i)
    if (g_clients[i].after)
        g_clients[i].after(params);
    return r;
}

bool ui_image_button(const void *params, StdFunction *click) {
    if (!g_orig)
        return false;
    StdFunction none[4];
    memset(none, 0, sizeof none);
    g_orig(params, click, &none[0], &none[1], &none[2], &none[3]);
    return true;
}

bool ui_image_button_rect(const void *params, StdFunction *click, Vec2 pos, Vec2 size) {
    static uint64_t (*rect_fn)(const void *, Vec2 lr, Vec2 tb, void *, void *, void *, void *, void *);
    static bool resolved;
    if (!resolved) {
        resolved = true;
        *(void **)&rect_fn =
            sym("_Z15GUI_ImageButtonRK21ImageButtonParameters4vec4NSt3__18functionIFvvEEES6_S6_S6_S6_");
    }
    if (!rect_fn)
        return false;
    StdFunction none[4];
    memset(none, 0, sizeof none);
    rect_fn(params, (Vec2){pos.x, pos.x + size.x}, (Vec2){pos.y, pos.y + size.y}, click, &none[0], &none[1], &none[2],
            &none[3]);
    return true;
}
#endif

void ui_on_image_button(UiButtonBefore before, UiButtonAfter after) {
    if (g_nclients < MAX_CLIENTS && mods_list_add(&g_clients_mods) >= 0)
        g_clients[g_nclients++] = (typeof(g_clients[0])){before, after};
}

void ui_install(void) {
    g_params_ctor32 = (void (*)(void *, Vec2, const void *, unsigned, const void *))sym(
        "_ZN21ImageButtonParametersC2E4vec214ResourceHandleI7TextureEjP20GuiTooltipParameters");
    g_text = (void (*)(const void *, const char *))sym("_Z8GUI_TextRK14TextParametersPKc");
    g_text_dtor = (void (*)(void *))sym("_ZN14TextParametersD1Ev");
    g_set_font = (void (*)(unsigned))sym("_Z11GUI_SetFontj");
    g_colour = (void (*)(unsigned))sym("_Z10GUI_Colourj");
    g_colour_rgba = (void (*)(uint32_t))sym("_Z10GUI_Colour4rgba");
    if (!g_nclients) {
        logf_("ui: no image button clients, hook not installed");
        return;
    }
    static const uint8_t prologue[] = PROLOGUE_SUB32(0x48, 0x01); // sub rsp,0x148
    g_orig = (image_button_fn)install_hook(
        "GUI_ImageButton", sym("_Z15GUI_ImageButtonRK21ImageButtonParametersNSt3__18functionIFvvEEES5_S5_S5_S5_"),
        (void *)hook, prologue, sizeof prologue);
}
