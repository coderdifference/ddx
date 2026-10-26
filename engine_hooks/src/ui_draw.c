// Drawing for panels and overlays: textures, images, buttons, drop targets, localized text. See common.h ("ui_draw.c")
// for the API and docs/11-ui-library.md for how to use it.
//
// The game's GUI is queued: GUI_Image, GUI_Text, GUI_ImageButton and the rest append WidgetParameters
// (GUI_AppendWidget) that GUI_Render sorts and draws after the frame's panels, each in the transform it was queued in.
// So ui_image / ui_button draw in the panel's order (later calls on top); ui_image_now draws at once (GUI_Image_DrawNow),
// for code that runs while something else is being drawn (a tooltip line, a GUI_Callback).
//
// Game types used:
// - ImageParameters(vec2, ResourceHandle<Texture>) / ImageButtonParameters(vec2, ResourceHandle<Texture>, u32 id,
//   GuiTooltipParameters*): both copy the handle with a reference added; ImageButtonParameters::~ (shared with
//   ImageParameters) releases it. WidgetParameters: size (vec2) 0x100 (0: the texture's), LayoutAnchor 0x110, enabled
//   0x2ee, DropContext* 0x28, transform (mat4) 0x118, clip flag 0x188; 0x330 bytes.
// - Textures: new_Texture_FromFile(out handle, path relative to the data folder or a mod, 0); an empty handle is
//   {-1 in the high half, 0}. Loaded once and kept (no reference released).
// - A drop target is a DropContext (vtable; accepted drag type mask at 8; enabled at 0x10; 0x40 bytes) registered for
//   the frame with GUI_HandleDropContext on a widget whose rect is the target; the input manager calls vtable slot 5
//   (Windows: 4, MSVC has one destructor slot), Drop(pos, DragContext*), when something is released over it (0: refused, the drag goes back). A drag source is a
//   DragContext (vtable; the drag object's type id at 8; enabled at 0xc) at WidgetParameters+0x30, registered the same
//   way with GUI_HandleDragContext: a press on the rect starts its drag.
// - A texture's size: ResourceManager::GetResource(handle) gives the Texture*, whose width and height are u32s at
//   +0x14 (Texture::GetSize). OffsetFromAnchor(LayoutAnchor, vec2 const&): anchors 0-8 are the rows top, middle,
//   bottom of left, centre, right.
// - Localization::LocalizeStringFromId(id) returns a LocalizedString {const char *, info}; the text is its first word.
// Windows (MSVC): WidgetParameters is 0x358 bytes, its fields after 0x58 0x30 later (game_layout.h); the parameter
// constructors, GUI_Image, GUI_Image_DrawNow, GUI_Sprite, Transform_Top and the one-argument drop / drag handlers are
// inlined (syms_win.c stand-ins); TextureFactory::CreateTexture is inlined in new_Texture_FromFile, so the redirect
// hook is ResourceManager::ObtainASync(manager, result handle*, path, type, options*) for the texture type only (a
// superset of macOS's: any texture obtained by path).
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEXTURE_CACHE 128

static bool g_ready;
static void (*g_texture_from_file)(void *out, const char *path, unsigned flags);
static void (*g_image_ctor)(void *p, Vec2 pos, const void *tex);
static void (*g_button_ctor)(void *p, Vec2 pos, const void *tex, unsigned id, const void *tooltip);
static void (*g_params_dtor)(void *p);
static void (*g_image)(const void *p);
static void (*g_image_now)(const void *p);
static void (*g_handle_drop)(const void *widget);
static void (*g_handle_drag)(const void *widget); // GUI_HandleDragContext(WidgetParameters const&)
static const uint8_t *(*g_transform_top)(void);
static GameLocString (*g_localize)(const char *id);
static float (*g_text_width)(const void *font, const char *s, unsigned flags);
static float (*g_text_height)(const void *font, float width, const char *s);

static bool ready(void) {
    if (g_ready)
        return g_image_ctor != NULL;
    g_ready = true;
    g_texture_from_file = (void (*)(void *, const char *, unsigned))sym("_Z20new_Texture_FromFile8c_stringj");
    g_image_ctor = (void (*)(void *, Vec2, const void *))sym("_ZN15ImageParametersC2E4vec214ResourceHandleI7TextureE");
    g_button_ctor = (void (*)(void *, Vec2, const void *, unsigned, const void *))sym(
        "_ZN21ImageButtonParametersC2E4vec214ResourceHandleI7TextureEjP20GuiTooltipParameters");
    g_params_dtor = (void (*)(void *))sym("_ZN21ImageButtonParametersD1Ev");
    g_image = (void (*)(const void *))sym("_Z9GUI_ImageRK15ImageParameters");
    g_image_now = (void (*)(const void *))sym("_Z17GUI_Image_DrawNowRK15ImageParameters");
    g_handle_drop = (void (*)(const void *))sym("_Z21GUI_HandleDropContextRK16WidgetParameters");
    g_handle_drag = (void (*)(const void *))sym("_Z21GUI_HandleDragContextRK16WidgetParameters");
    g_transform_top = (const uint8_t *(*)(void))sym("_Z13Transform_Topv");
    g_localize = (GameLocString (*)(const char *))sym("_ZN12Localization20LocalizeStringFromIdEPKc");
    g_text_width =
        (float (*)(const void *, const char *, unsigned))sym("_Z26TextureFont_GetWidthOfTextPK11TextureFontPKcj");
    g_text_height =
        (float (*)(const void *, float, const char *))sym("_Z27TextureFont_GetHeightOfTextPK11TextureFontfPKc");
    if (!g_texture_from_file || !g_image_ctor || !g_button_ctor || !g_params_dtor || !g_image || !g_image_now) {
        logf_("ui_draw: symbols missing, drawing off");
        g_image_ctor = NULL;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- textures
bool ui_texture(const char *path, uint8_t out[16]) {
    static struct {
        char path[128];
        signed char state;
        uint8_t h[16];
    } cache[TEXTURE_CACHE]; // 0 unknown, 1, -1
    static bool full_logged;
    if (!path || !ready())
        return false;
    int i = 0;
    while (i < TEXTURE_CACHE && cache[i].state && strcmp(cache[i].path, path))
        i++;
    if (i == TEXTURE_CACHE) {
        if (!full_logged) {
            logf_("ui_texture: cache full, %s not loaded", path);
            full_logged = true;
        }
        return false;
    }
    if (!cache[i].state) {
        snprintf(cache[i].path, sizeof cache[i].path, "%s", path);
        memset(cache[i].h, 0, 16);
        *(int64_t *)cache[i].h = (int64_t)0xFFFFFFFF00000000; // an empty handle, as the game makes them
        g_texture_from_file(cache[i].h, path, 0);
        cache[i].state = ((const int *)cache[i].h)[0] >= 0 ? 1 : -1;
        if (cache[i].state < 0)
            logf_("ui_texture: %s missing", path);
    }
    if (cache[i].state < 0)
        return false;
    memcpy(out, cache[i].h, 16);
    return true;
}

// Texture redirects: the one owner of TextureFactory::CreateTexture(sret handle, char const *path, unsigned flags),
// which every load by path goes through (new_Texture_FromFile and new_Texture_FromFileImmediate's
// TextureFactory::CreateTextureImmediate are thin wrappers). The resource manager copies the path, so a redirect's
// buffer only has to last the call.
#define MAX_REDIRECTS 8
static UiTextureRedirect g_redirects[MAX_REDIRECTS];
static int g_nredirects;
static ModsList g_redirects_mods; // their mods: the top mod's asked first

void ui_texture_redirect(UiTextureRedirect fn) {
    if (fn && g_nredirects < MAX_REDIRECTS && mods_list_add(&g_redirects_mods) >= 0)
        g_redirects[g_nredirects++] = fn;
    else if (fn)
        logf_("ui_draw: too many texture redirects");
}

#ifdef _WIN32
static void *(*o_obtain)(void *rm, void *out, const char *path, unsigned type, const void *options);
static const unsigned *(*g_texture_type)(void);

static void *h_obtain(void *rm, void *out, const char *path, unsigned type, const void *options) {
    char buf[512];
    if (path && type == *g_texture_type())
        MODS_EACH(g_redirects_mods, true, i) {
            const char *p = g_redirects[i](path, buf, sizeof buf);
            if (p) {
                path = p;
                break;
            }
        }
    return o_obtain(rm, out, path, type, options);
}

void ui_draw_install(void) {
    if (!g_nredirects)
        return;
    g_texture_type = (const unsigned *(*)(void))sym("win:Texture type id");
    if (g_texture_type)
        *(void **)&o_obtain = install_hook("ResourceManager::ObtainASync", sym("win:ResourceManager::ObtainASync"),
                                           (void *)h_obtain, NULL, 0);
}
#else
static void *(*o_create_texture)(void *out, const char *path, unsigned flags);

static void *h_create_texture(void *out, const char *path, unsigned flags) {
    char buf[512];
    if (path)
        MODS_EACH(g_redirects_mods, true, i) {
            const char *p = g_redirects[i](path, buf, sizeof buf);
            if (p) {
                path = p;
                break;
            }
        }
    return o_create_texture(out, path, flags);
}

void ui_draw_install(void) {
    if (!g_nredirects)
        return;
    // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; sub rsp,0x20
    static const uint8_t prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                       0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x20};
    *(void **)&o_create_texture =
        install_hook("TextureFactory::CreateTexture", sym("_ZN14TextureFactory13CreateTextureEPKcj"),
                     (void *)h_create_texture, prologue, sizeof prologue);
}
#endif

static bool empty(const uint8_t tex[16]) {
    return !tex || ((const int *)tex)[0] < 0;
}

// ---------------------------------------------------------------- images
static void image(Vec2 pos, const uint8_t tex[16], Vec2 size, bool now) {
    if (empty(tex) || !ready())
        return;
    UiParams p;
    memset(&p, 0, sizeof p);
    g_image_ctor(p.b, pos, tex);
    if (size.x > 0 && size.y > 0)
        *(Vec2 *)(p.b + W_SIZE_V) = size;
    (now ? g_image_now : g_image)(p.b);
    g_params_dtor(p.b);
}

void ui_image(Vec2 pos, const uint8_t tex[16]) {
    image(pos, tex, (Vec2){0, 0}, false);
}
void ui_image_sized(Vec2 pos, const uint8_t tex[16], Vec2 size) {
    image(pos, tex, size, false);
}
void ui_image_now(Vec2 pos, const uint8_t tex[16], Vec2 size) {
    image(pos, tex, size, true);
}

#ifdef _WIN32
void ui_texture_copy(uint8_t dst[16], const uint8_t src[16]) {
    static void (*copy)(void *dst, const void *src);
    if (!copy)
        copy = (void (*)(void *, const void *))sym("win:ResourceHandle<Texture> copy");
    if (copy) {
        copy(dst, src);
        return;
    }
    memset(dst, 0, 16);
    *(int64_t *)dst = (int64_t)0xFFFFFFFF00000000; // an empty handle
}
#endif

bool ui_texture_size(const uint8_t tex[16], Vec2 *size) {
    static void **rm;
    static const uint8_t *(*get)(void *rm, const void *handle);
    if (!rm) {
        rm = (void **)sym("_ZN15ResourceManager10s_InstanceE");
        get =
            (const uint8_t *(*)(void *, const void *))sym("_ZN15ResourceManager11GetResourceERK19ResourceEntryHandle");
    }
    const uint8_t *t = !empty(tex) && rm && *rm && get ? get(*rm, tex) : NULL;
    if (!t)
        return false;
    *size = (Vec2){(float)*(const uint32_t *)(t + TEXTURE_SIZE), (float)*(const uint32_t *)(t + TEXTURE_SIZE + 4)};
    return size->x > 0 && size->y > 0 && size->x <= 16384 && size->y <= 16384;
}

Vec2 ui_anchor_point(UiAnchor a, Vec2 size) {
    int col = (int)a % 3, row = (int)a / 3;
    return (Vec2){size.x * (float)col / 2, size.y * (float)row / 2};
}

bool ui_image_path(Vec2 pos, const char *path) {
    uint8_t tex[16];
    if (!ui_texture(path, tex))
        return false;
    ui_image(pos, tex);
    return true;
}

// ---------------------------------------------------------------- Spine sprites
// SpriteParameters(SpineSprite const*, vec2 const& pos, std::function<void(WidgetParameters const*)> before, ... after)
// (the overload Inventory::Display::InventoryButton uses; the functions by value, so by pointer to a caller's
// temporary: an empty one is 0x30 zero bytes, __f_ NULL at +0x20). GUI_Sprite = GUI_AppendWidget(9, params);
// GUI_Draw_Sprite translates to pos (+0x18), runs the functions if set and SpineSprite::Draw(colour +0x158) on the
// sprite at +0x38. The parameters own nothing more with empty functions (the vector at +0x310 stays empty), so no
// destructor is needed. Inventory::OutlineManager::s_Instance (a pointer): the trinket sparkles, vector<SpineSprite*>
// boss at +0x00, ancestral at +0x18, advanced every frame by OutlineManager::Update.
static void (*g_sprite_ctor)(void *p, const void *sprite, const Vec2 *pos, void *before, void *after);
static void (*g_sprite)(const void *p);
static void **g_outlines;

static bool sprites_ready(void) {
    static int state;
    if (!state) {
        g_sprite_ctor = (void (*)(void *, const void *, const Vec2 *, void *, void *))sym(
            "_ZN16SpriteParametersC2EPK11SpineSpriteRK4vec2NSt3__18functionIFvPK16WidgetParametersEEESC_");
        g_sprite = (void (*)(const void *))sym("_Z10GUI_SpriteRK16SpriteParameters");
        g_outlines = (void **)sym("_ZN9Inventory14OutlineManager10s_InstanceE");
        state = g_sprite_ctor && g_sprite ? 1 : -1;
        if (state < 0)
            logf_("ui_draw: sprite symbols missing, sprites off");
    }
    return state > 0;
}

bool ui_sprite(Vec2 pos, const void *sprite) {
    if (!sprite || !ready() || !sprites_ready())
        return false;
    UiParams p;
    _Alignas(16) uint8_t before[SPRITE_FN_SIZE], after[SPRITE_FN_SIZE];
    memset(&p, 0, sizeof p);
    memset(before, 0, sizeof before);
    memset(after, 0, sizeof after);
    g_sprite_ctor(p.b, sprite, &pos, before, after);
    g_sprite(p.b);
    if (*(void **)(p.b + SPRITE_VECTOR))
        logf_("ui_sprite: parameters own a vector (leaked)");
    return true;
}

const void *ui_trinket_sparkle(int kind, unsigned index) {
    if (!sprites_ready() || !g_outlines || !*g_outlines || kind < 0 || kind > 1)
        return NULL;
    void *const *v = (void *const *)((uint8_t *)*g_outlines + kind * 0x18); // begin, end, capacity
    size_t n = (size_t)((void *const *)v[1] - (void *const *)v[0]);
    return n ? ((void *const *)v[0])[index % n] : NULL;
}

// ---------------------------------------------------------------- buttons
bool ui_button_keep(UiParams *p, Vec2 pos, const uint8_t tex[16], unsigned id, bool enabled,
                    void (*on_click)(const uint8_t *data), const void *data, size_t n) {
    if (empty(tex) || !ready())
        return false;
    memset(p, 0, sizeof *p);
    g_button_ctor(p->b, pos, tex, id, NULL);
    p->b[W_ENABLED] = enabled;
    StdFunction click;
    ui_function(&click, on_click, data, n);
    ui_image_button(p->b, &click);
    return true;
}

void ui_params_free(UiParams *p) {
    if (g_params_dtor)
        g_params_dtor(p->b);
}

bool ui_button(Vec2 pos, const uint8_t tex[16], unsigned id, bool enabled, void (*on_click)(const uint8_t *data),
               const void *data, size_t n) {
    UiParams p;
    if (!ui_button_keep(&p, pos, tex, id, enabled, on_click, data, n))
        return false;
    ui_params_free(&p);
    return true;
}

bool ui_button_sized(Vec2 pos, Vec2 size, const uint8_t tex[16], unsigned id, bool enabled,
                     void (*on_click)(const uint8_t *data), const void *data, size_t n) {
    if (empty(tex) || !ready())
        return false;
    UiParams p;
    memset(&p, 0, sizeof p);
    g_button_ctor(p.b, pos, tex, id, NULL);
    *(Vec2 *)(p.b + W_SIZE_V) = size;
    p.b[W_ENABLED] = enabled;
    StdFunction click;
    ui_function(&click, on_click, data, n);
    if (!ui_image_button_rect(p.b, &click, pos, size))
        ui_image_button(p.b, &click); // clicked within size
    ui_params_free(&p);
    return true;
}

// ---------------------------------------------------------------- drop targets
static void dt_nop(void) {}
static uint64_t dt_drop(UiDropTarget *t, const void *pos, void *drag) {
    return t->on_drop ? t->on_drop(t, pos, drag) : 0;
}
#ifdef _WIN32
static void *g_dt_vtbl[7] = {(void *)dt_nop,  (void *)dt_nop, (void *)dt_nop, (void *)dt_nop, // MSVC: one destructor
                             (void *)dt_drop, (void *)dt_nop, (void *)dt_nop};                // slot, Drop is slot 4
#else
static void *g_dt_vtbl[8] = {(void *)dt_nop, (void *)dt_nop,  (void *)dt_nop, (void *)dt_nop,
                             (void *)dt_nop, (void *)dt_drop, (void *)dt_nop, (void *)dt_nop};
#endif

void ui_drop_target_init(UiDropTarget *t, UiDropFn on_drop, void *ctx) {
    memset(t, 0, sizeof *t);
    t->vtbl = g_dt_vtbl;
    t->enabled = true;
    t->on_drop = on_drop;
    t->ctx = ctx;
}

void ui_contexts(void *drag, void *drop, Vec2 pos, Vec2 size) {
    if ((!drag && !drop) || !ready() || !g_transform_top || (drop && !g_handle_drop) || (drag && !g_handle_drag))
        return;
    UiParams p;
    memset(&p, 0, sizeof p);
    uint64_t none[2] = {0xFFFFFFFF00000000ull, 0};
    g_button_ctor(p.b, pos, none, 0, NULL);
    _Alignas(16) uint8_t w[W_SIZE];
    memcpy(w, p.b, W_SIZE);
    *(void **)(w + W_DROP_CONTEXT) = drop;
    *(void **)(w + W_DRAG_CONTEXT) = drag;
    *(Vec2 *)(w + W_SIZE_V) = size;
    *(int *)(w + W_ANCHOR) = 0;
    memcpy(w + W_TRANSFORM, g_transform_top(), 64);
    w[W_CLIP] = 0;
    if (drop)
        g_handle_drop(w);
    if (drag)
        g_handle_drag(w);
    g_params_dtor(p.b);
}

void ui_drop_target(UiDropTarget *t, uint32_t accepts, Vec2 pos, Vec2 size) {
    if (!t)
        return;
    t->accepts = accepts;
    t->enabled = true;
    ui_contexts(NULL, t, pos, size);
}

// ---------------------------------------------------------------- localized text
float ui_text_width(const char *font, const char *s) {
    const void *f = font && s && ready() && g_text_width ? ui_font(font) : NULL;
    return f ? g_text_width(f, s, 0x80000000u) : 0; // flags as the GUI measures
}

float ui_text_height(const char *font, const char *s, float width) {
    const void *f = font && s && ready() && g_text_height ? ui_font(font) : NULL;
    return f ? g_text_height(f, width > 0 ? width : INFINITY, s) : 0; // TextureFont_GetHeightOfText
}

GameLocString ui_loc_string(const char *id) {
    GameLocString none = {NULL, 0};
    return id && ready() && g_localize ? g_localize(id) : none;
}

// A missing entry comes back as the game's marker, the id in colour codes: "<c>...[<id>]</c>".
const char *ui_loc(const char *id) {
    GameLocString s = ui_loc_string(id);
    if (!s.text || !*s.text)
        return id;
    char marker[256];
    snprintf(marker, sizeof marker, "[<%s>]", id);
    return strstr(s.text, marker) ? id : s.text;
}

// the localized string with each {key} replaced by its value
void ui_format(char *out, size_t cap, const char *id, const char *const *keys, const char *const *vals, int n) {
    const char *s = ui_loc(id);
    size_t len = 0;
    if (!cap)
        return;
    while (s && *s && len + 1 < cap) {
        int k = -1;
        if (*s == '{')
            for (int i = 0; i < n && k < 0; i++) {
                size_t l = strlen(keys[i]);
                if (!strncmp(s + 1, keys[i], l) && s[1 + l] == '}')
                    k = i;
            }
        if (k >= 0) {
            len += (size_t)snprintf(out + len, cap - len, "%s", vals[k] ? vals[k] : "");
            s += strlen(keys[k]) + 2;
        } else
            out[len++] = *s++;
        if (len >= cap)
            len = cap - 1;
    }
    out[len] = 0;
}
