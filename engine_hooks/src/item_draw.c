// Item drawing for registered item types (item_types.c; common.h "Item drawing"): a type's icon callback gives the
// item's base picture and its compose callback the layers drawn for it, wherever the item is drawn.
//
// Icons: the game asks Inventory::Item::GetIcon() const (sret handle; the inventory grids) or
// Inventory::Display::GetIconTexture(Item const*) (sret; the dragged item, the battle overlay) for an item's picture,
// normally its class's icon (loaded once by UI::CampaignAssets::Instance::LoadItems). Both hooks hand a registered
// type's icon (a counted copy: the caller releases it) and note the item as pending, with the picture's handle.
// Drawing: Inventory::Display::InventoryButton draws the icon as a GUI_ImageButton (a ui.c client here), and
// InventoryItemDragObject::RenderInventoryObject with GUI_Image_DrawNow, a 10-byte jump to GUI_Draw_Image(WidgetParameters
// const&) (too short to hook; GUI_Draw_Image is hooked instead: every image passes it when it's rendered, a queued one
// long after its note was used); clients drawing an item's picture themselves name it with item_draw_note (GUI_Image). The first image after the note whose texture (ImageParameters+0x40, its
// first 8 bytes: index and generation) is the pending picture is drawn as the item: the layers under the base, the
// base as the game draws it (a button stays a button; with no base layer its colour's alpha is 0, so it still takes
// clicks and drags), then the layers over it. The item's rect: pos +0x18, size +0x100 (0: the texture's), anchor
// +0x110 (LayoutAnchor); layers are drawn in the current transform and GUI colour, as the game's image is.
// The colour: ImageParameters+0x158, an hvec4 (four half floats; alpha the high 16 bits).
// Log: the first time each registered item id meets each outcome at each place (composed, not composed, no type
// icon), one line, so the log shows which path drew it without a line per frame.
// Windows (MSVC): Item::GetIcon is (this, result) (0x5d2d00); GUI_Image is inlined everywhere, each copy ending in
// GUI_AppendWidget(5, params), so that function is hooked for type 5 instead; the parameter offsets are
// game_layout.h's (fields after 0x58 0x30 later).
#include "common.h"
#include <stdio.h>
#include <string.h>

// The pictures waiting to be drawn as items: an icon hook's (one: the game draws it at once, and the next icon
// replaces it) or a client's notes (up to 4, until item_draw_note(NULL, ...)).
#define MAX_NOTES 4
typedef struct {
    bool on;
    const uint8_t *item;
    const ItemTypeDef *def;
    uint8_t key[8];
    ItemDrawPlace place;
} Note;
static Note g_notes[MAX_NOTES];
static bool g_busy; // drawing layers: our own images pass straight through
static void **g_rm; // &ResourceManager::s_Instance
static void (*g_rm_release)(void *rm, const void *handle);
#ifdef _WIN32
static void *(*o_item_icon)(const void *item, void *out); // MSVC member: this, then the result
static void (*o_append)(int type, const uint8_t *params); // GUI_AppendWidget (GUI_Image is type 5)
static void append_image(const uint8_t *params) {
    o_append(5, params);
}
static void (*o_image)(const uint8_t *params) = append_image;
#else
static void *(*o_item_icon)(void *out, const void *item);
static void (*o_image)(const uint8_t *params);
#endif
static void *(*o_display_icon)(void *out, const void *item);
static void (*o_draw_image)(const uint8_t *params); // GUI_Draw_Image: what GUI_Image_DrawNow runs

static const char *const k_place[] = {"grid", "drag", "slot", "other"};
enum { SEEN_COMPOSED, SEEN_NOT_COMPOSED, SEEN_NO_ICON };

// true the first time (item id, place, outcome) is seen; a full table stops logging
static bool first_seen(const uint8_t *item, ItemDrawPlace place, int outcome) {
    static unsigned seen[512];
    static int nseen;
    unsigned key = *(const unsigned *)(item + ITEM_ID_HASH) * 16u + (unsigned)place * 4u + (unsigned)outcome;
    for (int i = 0; i < nseen; i++)
        if (seen[i] == key)
            return false;
    if (nseen == (int)(sizeof seen / sizeof *seen))
        return false;
    seen[nseen++] = key;
    return true;
}

static const ItemTypeDef *def_of(const uint8_t *item) {
    return item ? item_type_find(*(const unsigned *)(item + ITEM_TYPE_HASH)) : NULL;
}

// *out = h as a counted copy (out holds nothing yet)
static void handle_copy(uint8_t out[16], const uint8_t h[16]) {
    const int *n = (const int *)h;
    if (g_rm && *g_rm && n[0] >= 0 && n[1] >= 0) {
        uint8_t *e = *(uint8_t **)((uint8_t *)*g_rm + RM_ENTRIES) + (size_t)n[0] * RM_ENTRY_SIZE;
        if (*(const int *)(e + RM_ENTRY_GENERATION) == n[1])
            __atomic_add_fetch((int *)(e + RM_ENTRY_REFS), 1, __ATOMIC_SEQ_CST);
    }
    memcpy(out, h, 16);
}

static void handle_release(const uint8_t h[16]) {
    const int *n = (const int *)h;
    if (g_rm && *g_rm && g_rm_release && n[0] >= 0 && n[1] >= 0)
        g_rm_release(*g_rm, h);
}

static void note_at(Note *n, const uint8_t *item, const ItemTypeDef *def, const uint8_t tex[16], ItemDrawPlace place) {
    n->on = def && def->compose && ((const int *)tex)[0] >= 0;
    if (!n->on)
        return;
    n->item = item;
    n->def = def;
    n->place = place;
    memcpy(n->key, tex, 8);
}

// an icon hook's note: the only one
static void note(const uint8_t *item, const ItemTypeDef *def, const uint8_t tex[16], ItemDrawPlace place) {
    memset(g_notes, 0, sizeof g_notes);
    note_at(&g_notes[0], item, def, tex, place);
}

void item_draw_note(const uint8_t *item, const uint8_t tex[16], ItemDrawPlace place) {
    if (!item || !tex) {
        memset(g_notes, 0, sizeof g_notes);
        return;
    }
    for (int i = 0; i < MAX_NOTES; i++)
        if (!g_notes[i].on) {
            note_at(&g_notes[i], item, def_of(item), tex, place);
            return;
        }
    logf_("item draw: too many notes, %.64s not noted", (const char *)item + ITEM_ID);
}

// the type's picture into the (uninitialized) sret handle; false: not ours, or no picture (the game's lookup)
static bool type_icon(void *out, const uint8_t *item, const ItemTypeDef *def) {
    uint8_t h[16];
    if (!def || !def->icon)
        return false;
    if (!def->icon(item, h, def->ud) || ((const int *)h)[0] < 0) {
        if (first_seen(item, ITEM_DRAW_OTHER, SEEN_NO_ICON))
            logf_("item draw: %.64s has no type icon, the game's class icon used", (const char *)item + ITEM_ID);
        return false;
    }
    handle_copy(out, h);
    return true;
}

#ifdef _WIN32
static void *h_item_icon(const void *item, void *out) {
    const ItemTypeDef *def = def_of(item);
    if (!type_icon(out, item, def))
        o_item_icon(item, out);
#else
static void *h_item_icon(void *out, const void *item) {
    const ItemTypeDef *def = def_of(item);
    if (!type_icon(out, item, def))
        o_item_icon(out, item);
#endif
    note(item, def, out, ITEM_DRAW_GRID);
    return out;
}

static void *h_display_icon(void *out, const void *item) {
    const ItemTypeDef *def = def_of(item);
    if (!type_icon(out, item, def))
        o_display_icon(out, item);
    note(item, def, out, ITEM_DRAW_DRAG);
    return out;
}

// ---------------------------------------------------------------- composing
typedef struct {
    ItemLayer layer[ITEM_MAX_LAYERS];
    int n, base; // base: the base layer's index (-1: none)
    Vec2 tl, size;
    float scale;
} Composed;

static bool compose(const uint8_t *item, const ItemTypeDef *def, const void *hero, ItemDrawPlace place,
                    const uint8_t base[16], Vec2 tl, Vec2 size, Composed *c) {
    if (!def || !def->compose)
        return false;
    Vec2 own;
    float scale = ui_texture_size(base, &own) && own.x > 0 ? size.x / own.x : 1;
    ItemDrawQuery q = {.item = item,
                       .hero = hero ? hero : items_active_hero(),
                       .place = place,
                       .base = base,
                       .size = size,
                       .scale = scale};
    memset(c, 0, sizeof *c);
    c->n = def->compose(&q, c->layer, ITEM_MAX_LAYERS, def->ud);
    if (c->n < 0) {
        if (first_seen(item, place, SEEN_NOT_COMPOSED))
            logf_("item draw: %.64s not composed at %s (compose said no), drawn as the game draws it",
                  (const char *)item + ITEM_ID, k_place[place]);
        return false;
    }
    if (c->n > ITEM_MAX_LAYERS)
        c->n = ITEM_MAX_LAYERS;
    c->base = -1;
    for (int i = 0; i < c->n && c->base < 0; i++)
        if (c->layer[i].base)
            c->base = i;
    if (first_seen(item, place, SEEN_COMPOSED))
        logf_("item draw: %.64s composed at %s: %d layer(s), %s, %.0fx%.0f", (const char *)item + ITEM_ID,
              k_place[place], c->n, c->base < 0 ? "no base" : "base drawn by the game", size.x, size.y);
    c->tl = tl;
    c->size = size;
    c->scale = scale;
    return true;
}

static void draw_layers(const Composed *c, const uint8_t base[16], int from, int to, bool now) {
    g_busy = true;
    for (int i = from; i < to; i++) {
        const ItemLayer *l = &c->layer[i];
        const uint8_t *tex = l->base ? base : l->tex;
        Vec2 at = ui_anchor_point(l->anchor, c->size);
        at.x += c->tl.x + l->offset.x * c->scale;
        at.y += c->tl.y + l->offset.y * c->scale;
        if (l->sprite && !l->base) {
            ui_sprite(at, l->sprite);
            continue;
        }
        Vec2 size = {l->size.x * c->scale, l->size.y * c->scale}, own;
        if ((size.x <= 0 || size.y <= 0) && ui_texture_size(tex, &own))
            size = (Vec2){own.x * c->scale, own.y * c->scale};
        if (size.x <= 0 || size.y <= 0)
            continue;
        Vec2 a = ui_anchor_point(l->anchor, size);
        Vec2 pos = {at.x - a.x, at.y - a.y};
        if (now)
            ui_image_now(pos, tex, size);
        else
            ui_image_sized(pos, tex, size);
    }
    g_busy = false;
}

// the image's rect from its parameters
static void rect_of(const uint8_t *params, Vec2 *tl, Vec2 *size) {
    *size = *(const Vec2 *)(params + P_SIZE);
    if (size->x <= 0 || size->y <= 0)
        ui_texture_size(params + P_TEXTURE, size);
    Vec2 a = ui_anchor_point((UiAnchor) * (const int *)(params + P_ANCHOR), *size),
         pos = *(const Vec2 *)(params + P_POS);
    *tl = (Vec2){pos.x - a.x, pos.y - a.y};
}

// is this image the pending item's picture? Then it's composed into c (and the note is used up).
static bool pending_image(const uint8_t *params, Composed *c) {
    if (g_busy)
        return false;
    Note *n = NULL;
    for (int i = 0; i < MAX_NOTES && !n; i++)
        if (g_notes[i].on && !memcmp(params + P_TEXTURE, g_notes[i].key, 8))
            n = &g_notes[i];
    if (!n)
        return false;
    n->on = false;
    Vec2 tl, size;
    rect_of(params, &tl, &size);
    return compose(n->item, n->def, NULL, n->place, params + P_TEXTURE, tl, size, c);
}

static void draw_image(const uint8_t *params, bool now) {
    Composed c;
    void (*orig)(const uint8_t *) = now ? o_draw_image : o_image;
    if (!pending_image(params, &c)) {
        orig(params);
        return;
    }
    draw_layers(&c, params + P_TEXTURE, 0, c.base < 0 ? c.n : c.base, now);
    if (c.base >= 0) {
        orig(params);
        draw_layers(&c, params + P_TEXTURE, c.base + 1, c.n, now);
    }
}
#ifdef _WIN32
static void h_append(int type, const uint8_t *params) {
    if (type == 5)
        draw_image(params, false);
    else
        o_append(type, params);
}
#else
static void h_image(const uint8_t *params) {
    draw_image(params, false);
}
#endif
static void h_draw_image(const uint8_t *params) {
    draw_image(params, true);
}

// the grids' item buttons (ui.c's GUI_ImageButton clients): the layers under before the button, over after it
static Composed g_button;
static uint8_t g_button_base[16];
static bool g_button_on;

static bool button_before(void *params, StdFunction *click) {
    (void)click;
    g_button_on = false;
    uint8_t *p = params;
    if (!pending_image(p, &g_button))
        return false;
    memcpy(g_button_base, p + P_TEXTURE, 16);
    draw_layers(&g_button, g_button_base, 0, g_button.base < 0 ? g_button.n : g_button.base, false);
    if (g_button.base < 0)
        *(uint64_t *)(p + P_COLOUR) &= 0x0000ffffffffffffull; // hidden, still a button
    g_button_on = g_button.base >= 0;
    return false;
}

static void button_after(const void *params) {
    (void)params;
    if (!g_button_on)
        return;
    g_button_on = false;
    draw_layers(&g_button, g_button_base, g_button.base + 1, g_button.n, false);
}

bool item_draw(const uint8_t *item, const void *hero, Vec2 pos, Vec2 size, ItemDrawPlace place) {
    if (!item || !o_display_icon)
        return false;
    _Alignas(16) uint8_t icon[16];
    memset(icon, 0, sizeof icon);
    *(int64_t *)icon = (int64_t)0xFFFFFFFF00000000;
    const ItemTypeDef *def = def_of(item);
    if (!type_icon(icon, item, def))
        o_display_icon(icon, item);
    bool ok = ((const int *)icon)[0] >= 0;
    Vec2 own;
    if (ok && (size.x <= 0 || size.y <= 0))
        ok = ui_texture_size(icon, &size);
    Composed c;
    if (ok && compose(item, def, hero, place, icon, pos, size, &c))
        draw_layers(&c, icon, 0, c.n, false);
    else if (ok)
        ui_image_sized(pos, icon, size);
    (void)own;
    handle_release(icon);
    return ok;
}

void item_draw_install(void) {
    bool any = false;
    for (int i = 0; i < item_types_count(); i++) {
        const ItemTypeDef *d = item_type_at(i);
        any |= d->icon || d->compose;
    }
    if (!any)
        return; // nothing to draw differently: the game's code untouched
    g_rm = (void **)sym("_ZN15ResourceManager10s_InstanceE");
    g_rm_release = (void (*)(void *, const void *))sym("_ZN15ResourceManager7ReleaseERK19ResourceEntryHandle");
    if (!g_rm || !g_rm_release) {
        logf_("item draw: ResourceManager symbols missing, off");
        return;
    }
    static const uint8_t icon_prologue[] = {
        0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41,
        0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50}; // push rbp; mov rbp,rsp; push r15-r12, rbx, rax
    *(void **)&o_item_icon = install_hook("Inventory::Item::GetIcon", sym("_ZNK9Inventory4Item7GetIconEv"),
                                          (void *)h_item_icon, icon_prologue, sizeof icon_prologue);
    static const uint8_t tex_prologue[] = {
        0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41,
        0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x38}; // ... push rbx; sub rsp,0x38
    *(void **)&o_display_icon =
        install_hook("Inventory::Display::GetIconTexture", sym("_ZN9Inventory7Display14GetIconTextureEPKNS_4ItemE"),
                     (void *)h_display_icon, tex_prologue, sizeof tex_prologue);
    // push rbp; mov rbp,rsp; push r15; push r14; push rbx; push rax; mov rbx,rdi; cmp qword [rdi+0x48],0
    static const uint8_t image_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53,
                                             0x50, 0x48, 0x89, 0xFB, 0x48, 0x83, 0x7F, 0x48, 0x00};
#ifdef _WIN32
    (void)image_prologue;
    *(void **)&o_append =
        install_hook("GUI_AppendWidget (GUI_Image)", sym("win:GUI_AppendWidget"), (void *)h_append, NULL, 0);
#else
    *(void **)&o_image = install_hook("GUI_Image", sym("_Z9GUI_ImageRK15ImageParameters"), (void *)h_image,
                                      image_prologue, sizeof image_prologue);
#endif
    // push rbp; mov rbp,rsp; push r15; push r14; push rbx; sub rsp,0x78; mov rbx,rdi
    static const uint8_t draw_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                            0x53, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x89, 0xFB};
    *(void **)&o_draw_image = install_hook("GUI_Draw_Image", sym("_Z14GUI_Draw_ImageRK16WidgetParameters"),
                                           (void *)h_draw_image, draw_prologue, sizeof draw_prologue);
    ui_on_image_button(button_before, button_after);
    logf_("item draw: on");
}
