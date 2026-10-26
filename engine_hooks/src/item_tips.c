// Item tooltips and descriptions: content an item's type supplies (ItemTypeDef.tooltip / .describe, item_type_view for
// the game's own types), drawn as layers on a content rect. See common.h ("Item content") and docs/11 "Item
// tooltips and descriptions".
//
// Tooltips: Inventory::Display::CreateItemTooltip(Actor const*, ItemClass const*, Item const*, vec2, int, bool, char
// const*, GuiTooltipParameters&) builds every inventory item's tooltip (estate stash, party inventory, loot, shops,
// equip_slots.c's slots through ui_item_tooltip). The hook runs the game's copy, then asks the item's type for content,
// giving it the game's lines; with content, the tooltip keeps one line (the first's style, not centred), whose string is
// a content line of ui_tooltip.c: the game measures it as the content's size, so it sizes, places, clamps and frames the
// box as for its own lines (the tooltip is set to auto width: CreateItemTooltip leaves most items' tooltips at a fixed
// width, CampaignAssets+0x119c, in which the game wraps its lines), and the content's layers are drawn where the line's text would be. The actor is NULL in the
// estate inventory, the shops, loot and the party inventory: the active hero (items_active_hero) stands in.
//
// The hero's equipment: the equipped trinkets (Hero+ACTOR_TRINKETS, an Inventory::System: items std::vector<Inventory::Item> at
// +0x08, ITEM_SIZE bytes each, an empty slot's amount <= 0) and the registered slot kinds' items (equip_slots_items).
//
// Content is kept in rings (tooltips: one per tooltip built this frame, until the frame's tooltips are drawn; panel
// descriptions: until a few more are built).
#include "common.h"
#include <stdio.h>
#include <string.h>

#define MAX_VIEWS 16
#define TIP_RING  8
#define DESC_RING 4
// GuiTooltipParameters
#define MAX_TIP_LINES 32

static struct {
    unsigned hash;
    ItemContentFn tooltip, describe;
    void *ud;
} g_views[MAX_VIEWS];
static int g_nviews;
static ModsList g_views_mods; // their mods: only an applied mod's view, the top mod's first
#define MAX_PLACES 4
static struct {
    ItemTipPlaceFn fn;
    void *ud;
} g_places[MAX_PLACES];
static int g_nplaces;
static ModsList g_places_mods; // their mods: the top applied mod's place

void item_tips_place(ItemTipPlaceFn fn, void *ud) {
    if (fn && g_nplaces < MAX_PLACES && mods_list_add(&g_places_mods) >= 0)
        g_places[g_nplaces++] = (typeof(g_places[0])){fn, ud};
}

bool item_type_view(const char *type, ItemContentFn tooltip, ItemContentFn describe, void *ud) {
    if (!type || !*type || g_nviews == MAX_VIEWS || mods_list_add(&g_views_mods) < 0) {
        logf_("item tips: view for %s not registered", type ? type : "(none)");
        return false;
    }
    g_views[g_nviews++] = (typeof(g_views[0])){name_hash(type), tooltip, describe, ud};
    return true;
}

// the type's callback (tooltip or describe) and its ud
static ItemContentFn find_fn(unsigned type_hash, bool describe, void **ud) {
    const ItemTypeDef *def = item_type_find(type_hash);
    if (def && (describe ? def->describe : def->tooltip)) {
        *ud = def->ud;
        return describe ? def->describe : def->tooltip;
    }
    MODS_EACH(g_views_mods, true, i)
    if (g_views[i].hash == type_hash && (describe ? g_views[i].describe : g_views[i].tooltip)) {
        *ud = g_views[i].ud;
        return describe ? g_views[i].describe : g_views[i].tooltip;
    }
    return NULL;
}

// ---------------------------------------------------------------- building content
static void content_clear(ItemContent *c) {
    c->head = c->body = (Vec2){0, 0};
    c->nlayers = c->nlines = 0;
    c->used = 0;
}

int item_content_line(ItemContent *c, const void *font, uint64_t colour, const char *text) {
    if (!c || c->nlines == ITEM_CONTENT_LINES)
        return -1;
    size_t n = strlen(text ? text : "") + 1;
    if (c->used + n > sizeof c->text)
        return -1;
    char *s = memcpy(c->text + c->used, text ? text : "", n);
    c->used += n;
    c->line[c->nlines] = (UiTextLine){font, colour, s};
    return c->nlines++;
}

const char *item_content_string(const ItemContent *c, int line) {
    return c && line >= 0 && line < c->nlines ? c->line[line].text : NULL;
}

ItemContentLayer *item_content_add(ItemContent *c, ItemContentLayerKind kind, ItemRegion region) {
    if (!c || c->nlayers == ITEM_CONTENT_LAYERS)
        return NULL;
    ItemContentLayer *l = &c->layer[c->nlayers++];
    memset(l, 0, sizeof *l);
    l->kind = kind;
    l->region = region;
    return l;
}

// lines first .. first + count - 1, clamped to the content's
static int lines_in(const ItemContent *c, int first, int *count) {
    if (first < 0)
        first = 0;
    if (first > c->nlines)
        first = c->nlines;
    if (*count < 0 || first + *count > c->nlines)
        *count = c->nlines - first;
    return first;
}

float item_content_lines_width(const ItemContent *c, int first, int count) {
    if (!c)
        return 0;
    first = lines_in(c, first, &count);
    return ui_text_lines_width(c->line + first, count);
}

float item_content_lines_height(const ItemContent *c, int first, int count, float width) {
    if (!c)
        return 0;
    first = lines_in(c, first, &count);
    return ui_text_lines_height(c->line + first, count, width);
}

bool item_is_equipped(const ItemContentQuery *q, const uint8_t *item) {
    for (int i = 0; q && item && i < q->nequipped; i++)
        if (q->equipped[i] == item)
            return true;
    return false;
}

// ---------------------------------------------------------------- drawing content
Vec2 item_content_size(const ItemContent *c, unsigned regions) {
    Vec2 s = {0, 0};
    if (!c)
        return s;
    if (regions & ITEM_REGIONS_HEAD) {
        s.x = c->head.x;
        s.y += c->head.y;
    }
    if (regions & ITEM_REGIONS_BODY) {
        if (c->body.x > s.x)
            s.x = c->body.x;
        s.y += c->body.y;
    }
    return s;
}

// a layer's size in a region `width` wide
static Vec2 layer_size(const ItemContent *c, const ItemContentLayer *l, float width) {
    if (l->kind == ITEM_LAYER_TEXT) {
        float w = l->width > 0 ? l->width : width;
        return (Vec2){w, item_content_lines_height(c, l->first, l->count, w)};
    }
    Vec2 s = l->size;
    if ((s.x <= 0 || s.y <= 0) && !ui_texture_size(l->tex, &s))
        s = (Vec2){0, 0};
    return s;
}

// the regions drawn at once from pos, `width` wide
static void draw_now(const ItemContent *c, unsigned regions, Vec2 pos, float width) {
    UiRect rect[2] = {{pos, {width, 0}}, {pos, {width, 0}}}; // by ItemRegion: body, head
    if (regions & ITEM_REGIONS_HEAD) {
        rect[ITEM_REGION_HEAD].size.y = c->head.y;
        rect[ITEM_REGION_BODY].pos.y += c->head.y;
    }
    rect[ITEM_REGION_BODY].size.y = c->body.y;
    for (int i = 0; i < c->nlayers; i++) {
        const ItemContentLayer *l = &c->layer[i];
        unsigned bit = l->region == ITEM_REGION_HEAD ? ITEM_REGIONS_HEAD : ITEM_REGIONS_BODY;
        if (!(regions & bit))
            continue;
        const UiRect *r = &rect[l->region == ITEM_REGION_HEAD];
        Vec2 sz = layer_size(c, l, r->size.x), at = ui_anchor_point(l->at, r->size),
             own = ui_anchor_point(l->anchor, sz);
        Vec2 p = {r->pos.x + at.x - own.x + l->offset.x, r->pos.y + at.y - own.y + l->offset.y};
        if (l->kind == ITEM_LAYER_TEXT) {
            int count = l->count, first = lines_in(c, l->first, &count);
            ui_text_lines_now(c->line + first, count, p, sz.x, l->column);
        } else if (sz.x > 0 && sz.y > 0) {
            ui_badge_now(l->tex, sz, (Vec2){p.x + sz.x / 2, p.y + sz.y / 2}, l->label[0] ? l->label : NULL, l->font,
                         l->rgba, l->label_offset);
        }
    }
}

// ---------------------------------------------------------------- the hero's equipment
int hero_equipped(const void *hero, const uint8_t **out, int max) {
    if (!is_hero(hero))
        return 0;
    int n = 0;
    const uint8_t *sys = (const uint8_t *)hero + ACTOR_TRINKETS;
    const uint8_t *b = *(const uint8_t *const *)(sys + SYSTEM_ITEMS),
                  *e = *(const uint8_t *const *)(sys + SYSTEM_ITEMS + 8);
    for (const uint8_t *it = b; b && it + ITEM_SIZE <= e && n < max; it += ITEM_SIZE)
        if (*(const int *)(it + ITEM_AMOUNT) > 0)
            out[n++] = it;
    return n + equip_slots_items(hero, out + n, max - n);
}

static void query_init(ItemContentQuery *q, const uint8_t *item, const void *hero) {
    memset(q, 0, sizeof *q);
    q->item = item;
    q->hero = is_hero(hero) ? hero : NULL;
    q->nequipped = hero_equipped(q->hero, q->equipped, ITEM_MAX_EQUIPPED);
}

// ---------------------------------------------------------------- descriptions
static ItemContent g_desc[DESC_RING];
static unsigned g_next_desc;

const ItemContent *item_describe(const uint8_t *item, const void *hero, float width) {
    void *ud = NULL;
    ItemContentFn fn = item ? find_fn(*(const unsigned *)(item + ITEM_TYPE_HASH), true, &ud) : NULL;
    if (!fn)
        return NULL;
    ItemContent *c = &g_desc[g_next_desc];
    g_next_desc = (g_next_desc + 1) % DESC_RING;
    content_clear(c);
    ItemContentQuery q;
    query_init(&q, item, hero ? hero : items_active_hero());
    q.width = width;
    return fn(&q, c, ud) ? c : NULL;
}

// panel drawing: the content is copied into a slot of its own (the caller's may be rebuilt before the panel draws)
#define DRAW_RING 4
static struct {
    ItemContent c;
    unsigned regions;
    Vec2 pos;
    float width;
} g_draw[DRAW_RING];
static unsigned g_next_draw;

static void draw_queued(const uint8_t *data) {
    unsigned s = data[0];
    if (s < DRAW_RING)
        draw_now(&g_draw[s].c, g_draw[s].regions, g_draw[s].pos, g_draw[s].width);
}

// a copy of content whose line strings point into its own text
static void content_copy(ItemContent *dst, const ItemContent *src) {
    memcpy(dst, src, sizeof *dst);
    for (int i = 0; i < dst->nlines; i++) {
        const char *t = src->line[i].text;
        if (t >= src->text && t < src->text + sizeof src->text)
            dst->line[i].text = dst->text + (t - src->text);
    }
}

float item_content_draw(const ItemContent *c, unsigned regions, Vec2 pos, float width) {
    if (!c)
        return 0;
    Vec2 size = item_content_size(c, regions);
    unsigned s = g_next_draw;
    g_next_draw = (g_next_draw + 1) % DRAW_RING;
    if (&g_draw[s].c != c)
        content_copy(&g_draw[s].c, c);
    g_draw[s].regions = regions;
    g_draw[s].pos = pos;
    g_draw[s].width = width > 0 ? width : size.x;
    uint8_t data[1] = {(uint8_t)s};
    ui_draw_callback(draw_queued, data, sizeof data);
    return size.y;
}

// ---------------------------------------------------------------- tooltips
#ifndef ITEM_TIPS_TEST
static ItemContent g_tips[TIP_RING];
static unsigned g_next_tip;
static void (*g_set_string)(void *line, const char *s);
typedef void (*create_tip_fn)(const void *actor, const void *cls, const void *item, Vec2 pos, int a, bool b,
                              const char *c, void *tip);
static create_tip_fn o_create_tip;

static void draw_tip(Vec2 pos, Vec2 size, void *ud) {
    unsigned s = (unsigned)(uintptr_t)ud;
    if (s < TIP_RING)
        draw_now(&g_tips[s], ITEM_REGIONS_ALL, pos, size.x);
}

static void h_create_tip(const void *actor, const void *cls, const void *item, Vec2 pos, int a, bool b, const char *c,
                         void *tipv) {
    o_create_tip(actor, cls, item, pos, a, b, c, tipv);
    uint8_t *tip = tipv;
    int place = tip ? mods_list_first(&g_places_mods) : -1;
    if (place >= 0) { // the client's place (GUI_Tooltip keeps this transform; the
        ItemTipPlace p = {.slot = pos, .game = *(const Vec2 *)(tip + TIP_POS), .mouse = ui_mouse_local()}; // game
        ui_screen_bounds(&p.screen_min, &p.screen_max); // clamps the box)
        if (p.mouse.x > -1e8f)
            *(Vec2 *)(tip + TIP_POS) = g_places[place].fn(&p, g_places[place].ud);
    }
    void *ud = NULL;
    ItemContentFn fn =
        item && tip ? find_fn(*(const unsigned *)((const uint8_t *)item + ITEM_TYPE_HASH), false, &ud) : NULL;
    unsigned *count = tip ? (unsigned *)(tip + TIP_COUNT) : NULL;
    if (!fn || *count < 1 || *count > MAX_TIP_LINES)
        return;
    // the game's lines
    static UiTextLine game[MAX_TIP_LINES];
    int ngame = 0;
    for (unsigned i = 0; i < *count; i++) {
        const uint8_t *l = tip + TIP_LINES + i * LINE_SIZE;
        const char *text = *(const char *const *)(l + LINE_STRING);
        if (text)
            game[ngame++] = (UiTextLine){*(const void *const *)l, *(const uint64_t *)(l + LINE_COLOUR), text};
    }
    unsigned s = g_next_tip;
    g_next_tip = (g_next_tip + 1) % TIP_RING;
    ItemContent *content = &g_tips[s];
    content_clear(content);
    ItemContentQuery q;
    const void *hero = is_hero(actor) ? actor : items_active_hero();
    query_init(&q, item, hero);
    q.hero_implied = q.hero && !is_hero(actor);
    q.game = game;
    q.ngame = ngame;
    if (!fn(&q, content, ud))
        return;
    // one line: the content
    uint8_t *line = tip + TIP_LINES, saved[LINE_SIZE];
    memcpy(saved, line, LINE_SIZE);
    line[LINE_CENTRED] = 0;
    char blank[TIP_RING + 1]; // a string of its own (s + 1 spaces), never drawn as text
    memset(blank, ' ', s + 1);
    blank[s + 1] = 0;
    g_set_string(line, blank);
    Vec2 size = item_content_size(content, ITEM_REGIONS_ALL);
    if (!ui_tooltip_content_line(*(const char **)(line + LINE_STRING), size, draw_tip, (void *)(uintptr_t)s)) {
        memcpy(line, saved, LINE_SIZE); // can't be drawn: the game's tooltip
        return;
    }
    *count = 1;
    tip[TIP_AUTO_WIDTH] = 1; // the box as wide as the content (the game's may be fixed)
}

void item_tips_install(void) {
    bool any = g_nviews > 0 || g_nplaces;
    for (int i = 0; i < item_types_count(); i++)
        any |= item_type_at(i)->tooltip != NULL;
    if (!any)
        return; // no tooltip callbacks: nothing hooked
    g_set_string = (void (*)(void *, const char *))sym("_ZN20GuiTooltipParameters4Text4Line9SetStringEPKc");
    if (!g_set_string) {
        logf_("item tips: tooltip symbols missing, the game's tooltips");
        return;
    }
    static const uint8_t prologue[] = PROLOGUE_SUB32(0x88, 0x08); // sub rsp,0x888
    *(void **)&o_create_tip = install_hook("Inventory::Display::CreateItemTooltip",
                                           sym("_ZN9Inventory7Display17CreateItemTooltipEPK5ActorPKNS_9ItemClassEPKNS_"
                                               "4ItemE4vec2ibPKcR20GuiTooltipParameters"),
                                           (void *)h_create_tip, prologue, sizeof prologue);
}
#endif
