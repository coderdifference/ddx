// Tooltips: drawn as part of a panel, icons on tooltip lines, and a callback at the frame's end. See common.h
// ("ui_tooltip.c").
//
// How the game draws tooltips: GUI_Tooltip(params, force) queues a GuiTooltipRenderCall (the params and
// Transform_Top()); GUI_RenderTooltips() draws the queue after the whole GUI (Draw_Colour(white), shader "fade" and
// "Saturation" 1.0, then per call Transform_Load(its matrix) and GUI_TooltipRender(params)). So a tooltip lies over
// everything, dialogs included; and GUI_TooltipRender called during a panel's draw shows nothing, since the panel's
// queued widgets are drawn after it.
//
// In a panel: GUI_Callback(WidgetCallbackParameters const&) queues a widget (type 0xc) that GUI_DrawWidget draws by
// calling its std::function<void(WidgetParameters const*)> (+0x60, __base* at +0x80) in the widget's transform, as
// ConfirmDialog::System::Show does. WidgetCallbackParameters(std::function) is a local symbol (C2); D1 destroys it.
// The callback does what GUI_RenderTooltips does for one call: Draw_Colour(white), GUI_TooltipRender(params). The
// params (GuiTooltipParameters, 0x568) are copied raw as GUI_Tooltip copies them (a line's text lives in the frame's
// widget string pool, not in the struct) into a ring of slots that own them until the slot is reused.
//
// Line icons: a tooltip line's string comes from the frame's widget string pool (GuiTooltipParameters::Text::Line::
// SetString), so the pointer a line gets when it's built is the one GUI_TooltipRender draws that frame. It draws each
// line with TextureFont_Display(font, LayoutAnchor (params+0x10), vec2 pos, float width, text, ...) (a 0x14-byte thunk);
// our hook draws the text, then a marked line's icons at once (GUI_Image_DrawNow, in the tooltip's transform) beside the
// text as measured (TextureFont_GetWidthOfText, flags 0x80000000 as the tooltip measures; placed by the anchor's
// column) and centred on its first row (TextureFont_GetHeightOfText). GUI_TooltipRender sizes its box by its widest
// line (GetWidthOfText), so the width hook adds the icons' room to a marked line, and a left (or right) anchored
// line's text is drawn that far in. Marks are cleared at the frame's end (the string pool is reused).
//
// Windows (MSVC): TextureFont_Display(font, anchor, vec2, width, ...) is inlined; GUI_TooltipRender calls
// TextureFont_DisplayRaw(font, anchor, x, y, width, +inf, text, flag, 0x7fffffff, true, false, -1) for each line, so
// DisplayRaw is hooked, and a call of that shape (+inf, 0x7fffffff) is the macOS overload's. Draw_Colour(hvec4), the
// encoded colour push / pop, GUI_Callback, WidgetCallbackParameters' constructor, GuiTooltipParameters' destructor
// and Texture_GetSize are inlined too (syms_win.c stand-ins). GuiTooltipParameters is laid out as macOS.
//
// Content lines: a line drawn by its owner, not as text (item_tips.c's item content). GUI_TooltipRender sizes its box
// from its lines (width: the widest line by TextureFont_GetWidthOfText, plus 10, unless +0x18 is 0 (then +0x14);
// height: the sum of TextureFont_GetHeightOfText(font, that width, line)), places and clamps it on the screen, draws
// the frame, then the lines; so a content line measures as its content's size (the width hook, and a hook on
// GetHeightOfText) and, where the line would be drawn, the owner's callback draws the content in its rect instead
// (left, centred or right in the text area by the anchor's column, as the text would be). The game keeps the box's
// art, placement and clamping; the content may reach outside the box on purpose (a badge on its corner).
//
// Inline badges: a marked line's space at a byte offset is widened, when the line is measured and drawn, to as many
// spaces as the badge needs in the line's own font, and the badge is drawn in that room. Colour codes in the line stay
// as they are, since the text is still drawn in one piece.
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TIP_SLOTS            8 // panel tooltips per frame, at most
#define CALLBACK_PARAMS_SIZE 0x400
#define MAX_MARKS            32
#define MAX_FRAME_END        8
#define MAX_BADGES           8
#define MAX_CONTENT          8

// ---------------------------------------------------------------- the frame's end
static void (*g_frame_end[MAX_FRAME_END])(void);
static int g_nframe_end;
static ModsList g_frame_end_mods; // their mods (only an applied mod's run)
static void (*o_render_tooltips)(void);

void ui_on_frame_end(void (*fn)(void)) {
    if (fn && g_nframe_end < MAX_FRAME_END && mods_list_add(&g_frame_end_mods) >= 0)
        g_frame_end[g_nframe_end++] = fn;
}

// ---------------------------------------------------------------- line icons
static struct {
    const char *line;
    uint8_t tex[16];
    Vec2 size;
    float gap;
} g_marks[MAX_MARKS];
static int g_nmarks;
static void (*o_display)(const void *font, int anchor, Vec2 pos, float width, const char *s, bool a, bool b, bool c,
                         int d);
#ifdef _WIN32
typedef void (*display_raw_fn)(const void *font, int anchor, float x, float y, float width, float height, const char *s,
                               bool a, int max, bool b, bool c, int d);
static display_raw_fn o_raw;
static void display_win(const void *font, int anchor, Vec2 pos, float width, const char *s, bool a, bool b, bool c,
                        int d) {
    o_raw(font, anchor, pos.x, pos.y, width, INFINITY, s, a, 0x7fffffff, b, c, d);
}
#endif
static float (*o_width)(const void *font, const char *s, unsigned flags);
static float (*g_height)(const void *font, float width, const char *s);
static Vec2 (*g_texture_size)(const void *tex);
static void (*g_colour)(uint32_t rgba);
static float (*o_height)(const void *font, float width, const char *s);

// ---------------------------------------------------------------- content lines (drawn by their owner)
static struct {
    const char *line;
    Vec2 size;
    void (*draw)(Vec2 pos, Vec2 size, void *ud);
    void *ud;
} g_content[MAX_CONTENT];
static int g_ncontent;

bool ui_tooltip_content_line(const char *line, Vec2 size, void (*draw)(Vec2 pos, Vec2 size, void *ud), void *ud) {
    if (!line || !draw || !o_height || !o_width || !o_display || g_ncontent == MAX_CONTENT)
        return false;
    g_content[g_ncontent++] = (typeof(g_content[0])){line, size, draw, ud};
    return true;
}

static int content_of(const char *s) {
    for (int i = 0; s && i < g_ncontent; i++)
        if (g_content[i].line == s)
            return i;
    return -1;
}

// ---------------------------------------------------------------- badges (a tooltip's top right corner)
static struct {
    const char *line;
    uint8_t tex[16];
    Vec2 size, offset, text_offset;
    char text[16];
    const void *font;
    uint32_t rgba;
} g_badges[MAX_BADGES];
static int g_nbadges;

// an icon of size centred on c, with text centred on it (moved by text_offset) in a font and colour
static void draw_badge(const uint8_t *tex, Vec2 sz, Vec2 c, const char *text, const void *font, uint32_t rgba,
                       Vec2 text_offset) {
    g_colour(0xffffffffu);
    ui_image_now((Vec2){c.x - sz.x / 2, c.y - sz.y / 2}, tex, sz);
    if (text[0] && font) {
        g_colour(rgba);
        o_display(font, 4, (Vec2){c.x + text_offset.x, c.y + text_offset.y}, 1000, text, false, true, false,
                  -1); // 4: centred
    }
}

void ui_badge_now(const uint8_t tex[16], Vec2 size, Vec2 centre, const char *text, const char *font, uint32_t rgba,
                  Vec2 text_offset) {
    if (!tex || !g_colour || !o_display)
        return;
    Vec2 sz = size.x > 0 && size.y > 0 ? size : g_texture_size ? g_texture_size(tex) : (Vec2){32, 32};
    draw_badge(tex, sz, centre, text ? text : "", font && *font ? ui_font(font) : NULL, rgba, text_offset);
}

void ui_tooltip_badge(const char *line, const char *icon_path, Vec2 size, Vec2 offset, const char *text,
                      const char *font, uint32_t rgba, Vec2 text_offset) {
    if (!line || !o_display || g_nbadges == MAX_BADGES || !ui_texture(icon_path, g_badges[g_nbadges].tex))
        return;
    g_badges[g_nbadges].line = line;
    g_badges[g_nbadges].size = size.x > 0 && size.y > 0 ? size
                               : g_texture_size         ? g_texture_size(g_badges[g_nbadges].tex)
                                                        : (Vec2){32, 32};
    g_badges[g_nbadges].offset = offset;
    g_badges[g_nbadges].text_offset = text_offset;
    snprintf(g_badges[g_nbadges].text, sizeof g_badges[g_nbadges].text, "%s", text ? text : "");
    g_badges[g_nbadges].font = font ? ui_font(font) : NULL;
    g_badges[g_nbadges++].rgba = rgba;
}

// a marked line was just drawn at pos (its anchor's column) in a box `width` wide: its badge on the box's top right
static void badge(const char *s, int anchor, Vec2 pos, float width) {
    for (int i = 0; i < g_nbadges; i++) {
        if (g_badges[i].line != s)
            continue;
        int col = anchor % 3; // the text area: centred on pos.x, from it, or ending at it
        float right = col == 1 ? pos.x + width / 2 : col == 2 ? pos.x : pos.x + width;
        Vec2 c = {right + g_badges[i].offset.x, pos.y + g_badges[i].offset.y};
        draw_badge(g_badges[i].tex, g_badges[i].size, c, g_badges[i].text, g_badges[i].font, g_badges[i].rgba,
                   g_badges[i].text_offset);
    }
}

// ---------------------------------------------------------------- inline badges (inside a line's text)
static struct {
    const char *line;
    size_t at;
    uint8_t tex[16];
    Vec2 size, offset, text_offset;
    float gap;
    char text[16];
    const void *font;
    uint32_t rgba;
} g_inline[MAX_BADGES];
static int g_ninline;

void ui_tooltip_inline_badge(const char *line, size_t at, const char *icon_path, Vec2 size, Vec2 offset, float gap,
                             const char *text, const char *font, uint32_t rgba, Vec2 text_offset) {
    if (!line || at >= strlen(line) || !o_display || g_ninline == MAX_BADGES ||
        !ui_texture(icon_path, g_inline[g_ninline].tex))
        return;
    g_inline[g_ninline].line = line;
    g_inline[g_ninline].at = at;
    g_inline[g_ninline].size = size.x > 0 && size.y > 0 ? size
                               : g_texture_size         ? g_texture_size(g_inline[g_ninline].tex)
                                                        : (Vec2){32, 32};
    g_inline[g_ninline].offset = offset;
    g_inline[g_ninline].gap = gap;
    g_inline[g_ninline].text_offset = text_offset;
    snprintf(g_inline[g_ninline].text, sizeof g_inline[g_ninline].text, "%s", text ? text : "");
    g_inline[g_ninline].font = font ? ui_font(font) : NULL;
    g_inline[g_ninline++].rgba = rgba;
}

static int inline_of(const char *s) {
    for (int i = 0; s && i < g_ninline; i++)
        if (g_inline[i].line == s)
            return i;
    return -1;
}

// line i as measured and drawn in font: its space widened to the badge's room (at most 40 spaces); *n: the spaces
static const char *inline_text(int i, const void *font, char *buf, size_t cap, int *n) {
    const char *s = g_inline[i].line;
    size_t at = g_inline[i].at, len = strlen(s);
    float sw = o_width(font, "a a", 0x80000000u) - o_width(font, "aa", 0x80000000u); // a space between glyphs
    float want = g_inline[i].size.x + 2 * g_inline[i].gap;
    *n = sw > 0 ? (int)ceilf(want / sw) : 1;
    if (*n < 1)
        *n = 1;
    if (*n > 40)
        *n = 40;
    if (len + (size_t)*n >= cap) {
        *n = 1;
        return s;
    }
    memcpy(buf, s, at);
    memset(buf + at, ' ', (size_t)*n);
    memcpy(buf + at + *n, s + at + 1, len - at); // the rest, with its terminator
    return buf;
}

// line i was drawn as `drawn` at pos: its badge in the middle of the room, centred on the first row
static void inline_badge(int i, const void *font, int anchor, Vec2 pos, const char *drawn, int n) {
    char prefix[600];
    size_t at = g_inline[i].at < sizeof prefix ? g_inline[i].at : sizeof prefix - 1;
    memcpy(prefix, drawn, at);
    prefix[at] = 0;
    float w = o_width(font, drawn, 0x80000000u), pw = o_width(font, prefix, 0x80000000u);
    float sw = o_width(font, "a a", 0x80000000u) - o_width(font, "aa", 0x80000000u);
    int col = anchor % 3;
    float x0 = col == 1 ? pos.x - w / 2 : col == 2 ? pos.x - w : pos.x;
    float h = g_height ? g_height(font, INFINITY, "0") : g_inline[i].size.y;
    Vec2 c = {x0 + pw + n * sw / 2 + g_inline[i].offset.x, pos.y + h / 2 + g_inline[i].offset.y};
    draw_badge(g_inline[i].tex, g_inline[i].size, c, g_inline[i].text, g_inline[i].font, g_inline[i].rgba,
               g_inline[i].text_offset);
}

void ui_tooltip_line_icons(const char *line, const char *icon_path, float gap) {
    if (!line || !o_display || g_nmarks == MAX_MARKS || !ui_texture(icon_path, g_marks[g_nmarks].tex))
        return;
    g_marks[g_nmarks].gap = gap;
    g_marks[g_nmarks].size = g_texture_size ? g_texture_size(g_marks[g_nmarks].tex) : (Vec2){16, 16};
    g_marks[g_nmarks++].line = line;
}

static int mark_of(const char *s) {
    for (int i = 0; s && i < g_nmarks; i++)
        if (g_marks[i].line == s)
            return i;
    return -1;
}

static float room(int m) {
    return g_marks[m].gap + g_marks[m].size.x;
} // each side

// a badge's line (the name) measures wider by the part of the badge over the text area, twice (the name is centred, so
// the box grows both ways), plus a small gap: the badge never covers the name
#define BADGE_GAP 4.0f
static float badge_room(const char *s) {
    for (int i = 0; s && i < g_nbadges; i++) {
        if (g_badges[i].line != s)
            continue;
        float over = g_badges[i].size.x / 2 - g_badges[i].offset.x + BADGE_GAP;
        return over > 0 ? 2 * over : 0;
    }
    return 0;
}

static float h_width(const void *font, const char *s, unsigned flags) {
    int cl = g_ncontent ? content_of(s) : -1;
    if (cl >= 0)
        return g_content[cl].size.x;
    int ib = g_ninline && font ? inline_of(s) : -1;
    if (ib >= 0) {
        char buf[600];
        int n;
        return o_width(font, inline_text(ib, font, buf, sizeof buf, &n), flags);
    }
    float w = o_width(font, s, flags);
    int m = mark_of(s);
    return (m >= 0 ? w + 2 * room(m) : w) + (g_nbadges ? badge_room(s) : 0);
}

static float h_height(const void *font, float width, const char *s) {
    int cl = g_ncontent ? content_of(s) : -1;
    return cl >= 0 ? g_content[cl].size.y : o_height(font, width, s);
}

static void h_display(const void *font, int anchor, Vec2 pos, float width, const char *s, bool a, bool b, bool c,
                      int d) {
    int cl = g_ncontent ? content_of(s) : -1;
    if (cl >= 0) { // the owner draws it, in the text's place
        int col = anchor % 3;
        Vec2 sz = g_content[cl].size;
        float x0 = col == 1 ? pos.x - sz.x / 2 : col == 2 ? pos.x - sz.x : pos.x;
        g_content[cl].draw((Vec2){x0, pos.y}, sz, g_content[cl].ud);
        return;
    }
    int ib = g_ninline && font ? inline_of(s) : -1;
    if (ib >= 0) {
        char buf[600];
        int n;
        const char *t = inline_text(ib, font, buf, sizeof buf, &n);
        o_display(font, anchor, pos, width, t, a, b, c, d);
        inline_badge(ib, font, anchor, pos, t, n);
        return;
    }
    int m = mark_of(s);
    if (m < 0 || !font) {
        o_display(font, anchor, pos, width, s, a, b, c, d);
        if (g_nbadges)
            badge(s, anchor, pos, width);
        return;
    }
    int col = anchor % 3; // the box was measured with the icons' room: keep them inside it
    if (col == 0)
        pos.x += room(m);
    else if (col == 2)
        pos.x -= room(m);
    o_display(font, anchor, pos, width, s, a, b, c, d);
    Vec2 sz = g_marks[m].size;
    float w = o_width(font, s, 0x80000000u), h = g_height ? g_height(font, INFINITY, s) : sz.y;
    float x0 = col == 1 ? pos.x - w / 2 : col == 2 ? pos.x - w : pos.x;
    float y = pos.y + (h - sz.y) / 2;
    g_colour(0xffffffffu);
    ui_image_now((Vec2){x0 - g_marks[m].gap - sz.x, y}, g_marks[m].tex, sz);
    ui_image_now((Vec2){x0 + w + g_marks[m].gap, y}, g_marks[m].tex, sz);
    if (g_nbadges)
        badge(s, anchor, pos, width);
}

#ifdef _WIN32
static void h_raw(const void *font, int anchor, float x, float y, float width, float height, const char *s, bool a,
                  int max, bool b, bool c, int d) {
    if (isinf(height) && max == 0x7fffffff && (g_ncontent || g_ninline || g_nmarks || g_nbadges))
        h_display(font, anchor, (Vec2){x, y}, width, s, a, b, c, d); // TextureFont_Display(anchor, width)'s shape
    else
        o_raw(font, anchor, x, y, width, height, s, a, max, b, c, d);
}
#endif

static void h_render_tooltips(void) {
    o_render_tooltips();
    g_nmarks = 0; // the frame's tooltips are drawn: its strings get reused
    g_nbadges = 0;
    g_ninline = 0;
    g_ncontent = 0;
    MODS_EACH(g_frame_end_mods, false, i) g_frame_end[i]();
}

// ---------------------------------------------------------------- tooltips in a panel
static _Alignas(16) uint8_t g_slots[TIP_SLOTS][TIP_PARAMS_SIZE];
static bool g_live[TIP_SLOTS];
static unsigned g_next;
static void (*g_cb_ctor)(void *self, StdFunction *fn);
static void (*g_cb_dtor)(void *self);
static void (*g_callback)(const void *params);
static void (*g_render)(const void *tip);
static void (*g_tip_dtor)(void *tip);

static void draw_slot(const uint8_t *data) {
    unsigned s = data[0];
    if (s >= TIP_SLOTS || !g_live[s])
        return;
    g_colour(0xffffffffu);
    g_render(g_slots[s]);
}

bool ui_tooltip_in_panel(void *tip) {
    if (!tip || !g_cb_ctor || !g_cb_dtor || !g_callback || !g_render || !g_colour || !g_tip_dtor)
        return false;
    unsigned s = g_next;
    g_next = (g_next + 1) % TIP_SLOTS;
    if (g_live[s])
        g_tip_dtor(g_slots[s]);
    memcpy(g_slots[s], tip, TIP_PARAMS_SIZE);
    g_live[s] = true;
    StdFunction fn;
    uint8_t data[1] = {(uint8_t)s};
    ui_function(&fn, draw_slot, data, sizeof data);
    _Alignas(16) uint8_t p[CALLBACK_PARAMS_SIZE];
    memset(p, 0, sizeof p);
    g_cb_ctor(p, &fn);
    g_callback(p);
    g_cb_dtor(p);
    return true;
}

// ---------------------------------------------------------------- a tooltip's lines as panel content
// GuiTooltipParameters' lines as GUI_TooltipRender draws them (no box): the text anchor at +0x10 for every line;
// lines of 0x28 from +0x20 (font +0x00, colour hvec4 +0x08, encoded colours on +0x10, "centred" +0x11: drawn half a line
// higher, a flag passed on +0x20, text +0x18), the count at +0x520. Each line: TextureFont_PushEncodedColourEnabled,
// Draw_Colour(hvec4), TextureFont_Display(font, anchor, (x, y), width, text, flag, true, false, -1) with x by the
// anchor's column (left, round(width / 2), right), then y += TextureFont_GetHeightOfText(font, width, text). The lines
// are copied (their text stays in the frame's string pool until the frame ends), so the caller keeps and frees `tip`.
#define TL_SETS  16
#define TL_LINES 64
typedef struct {
    const void *font;
    uint64_t colour;
    uint8_t encoded, centred, flag;
    const char *text;
} TipLine;
static struct {
    TipLine line[TL_LINES];
    int n, anchor;
    Vec2 pos;
    float width;
} g_sets[TL_SETS];
static unsigned g_next_set;
static void (*g_colour_hvec4)(uint64_t c);
static void (*g_colour_entry)(void *out, unsigned id); // ColourLibrary::GetEntry
static uint64_t (*g_entry_hvec4)(const void *entry);   // ColourLibrary::Entry::operator hvec4
static void (*g_encoded_push)(bool on);
static void (*g_encoded_pop)(void);

static int tip_lines(const uint8_t *tip, int first, int count, TipLine *out) {
    int n = *(const int *)(tip + 0x520), k = 0;
    if (n > TL_LINES)
        n = TL_LINES;
    for (int i = first < 0 ? 0 : first; i < n && (count < 0 || k < count); i++, k++) {
        const uint8_t *l = tip + 0x20 + i * 0x28;
        out[k] = (TipLine){*(const void *const *)l,         *(const uint64_t *)(l + 0x08), l[0x10], l[0x11], l[0x20],
                           *(const char *const *)(l + 0x18)};
    }
    return k;
}

static float line_height(const TipLine *l, float width) {
    return l->font && l->text && g_height ? g_height(l->font, width, l->text) : 0;
}

float ui_tooltip_lines_height(const void *tip, int first, int count, float width) {
    TipLine lines[TL_LINES];
    int n = tip ? tip_lines(tip, first, count, lines) : 0;
    float h = 0;
    for (int i = 0; i < n; i++)
        h += line_height(&lines[i], width);
    return h;
}

// lines drawn at once from pos (top left of the text area), wrapped at w, in the anchor's column
static void draw_lines(const TipLine *lines, int n, int anchor, Vec2 pos, float w) {
    if (!g_colour_hvec4 || !g_encoded_push || !g_encoded_pop || !o_display)
        return;
    int col = anchor % 3;
    float x = pos.x + (col == 1 ? roundf(w * 0.5f) : col == 2 ? w : 0), y = pos.y;
    for (int i = 0; i < n; i++) {
        const TipLine *l = &lines[i];
        float h = line_height(l, w);
        if (l->font && l->text) {
            g_encoded_push(l->encoded);
            g_colour_hvec4(l->colour);
            h_display(l->font, anchor, (Vec2){x, l->centred == 1 ? y - h * 0.5f : y}, w, l->text, l->flag, true, false,
                      -1);
            g_encoded_pop();
        }
        y += h;
    }
}

static void draw_set(const uint8_t *data) {
    unsigned s = data[0];
    if (s < TL_SETS)
        draw_lines(g_sets[s].line, g_sets[s].n, g_sets[s].anchor, g_sets[s].pos, g_sets[s].width);
}

bool ui_draw_callback(void (*fn)(const uint8_t *data), const void *data, size_t n) {
    if (!fn || n > 16 || !g_cb_ctor || !g_cb_dtor || !g_callback)
        return false;
    StdFunction f;
    ui_function(&f, fn, data, n);
    _Alignas(16) uint8_t p[CALLBACK_PARAMS_SIZE];
    memset(p, 0, sizeof p);
    g_cb_ctor(p, &f);
    g_callback(p);
    g_cb_dtor(p);
    return true;
}

static void queue_set(unsigned s) {
    uint8_t data[1] = {(uint8_t)s};
    ui_draw_callback(draw_set, data, sizeof data);
}

float ui_tooltip_lines(const void *tip, int first, int count, Vec2 pos, float width) {
    if (!tip || !g_cb_ctor || !g_cb_dtor || !g_callback || !o_display)
        return 0;
    unsigned s = g_next_set;
    g_next_set = (g_next_set + 1) % TL_SETS;
    g_sets[s].n = tip_lines(tip, first, count, g_sets[s].line);
    g_sets[s].anchor = *(const int *)((const uint8_t *)tip + 0x10);
    g_sets[s].pos = pos;
    g_sets[s].width = width;
    queue_set(s);
    return ui_tooltip_lines_height(tip, first, count, width);
}

static TipLine text_line(const UiTextLine *l) {
    return (TipLine){l->font, l->colour, 1, 0, 1, l->text};
}

float ui_text_lines_height(const UiTextLine *lines, int n, float width) {
    float h = 0;
    for (int i = 0; i < n; i++) {
        TipLine l = text_line(&lines[i]);
        h += line_height(&l, width);
    }
    return h;
}

float ui_text_lines_now(const UiTextLine *lines, int n, Vec2 pos, float width, int anchor) {
    if (!lines || n <= 0)
        return 0;
    TipLine t[TL_LINES];
    if (n > TL_LINES)
        n = TL_LINES;
    for (int i = 0; i < n; i++)
        t[i] = text_line(&lines[i]);
    draw_lines(t, n, anchor, pos, width);
    return ui_text_lines_height(lines, n, width);
}

float ui_text_lines_width(const UiTextLine *lines, int n) {
    float w = 0;
    for (int i = 0; lines && o_width && i < n; i++) {
        float lw = lines[i].font && lines[i].text ? h_width(lines[i].font, lines[i].text, 0x80000000u) : 0;
        if (lw > w)
            w = lw;
    }
    return w;
}

float ui_text_lines(const UiTextLine *lines, int n, Vec2 pos, float width, int anchor) {
    if (!lines || n <= 0 || !g_cb_ctor || !g_cb_dtor || !g_callback || !o_display)
        return 0;
    if (n > TL_LINES)
        n = TL_LINES;
    unsigned s = g_next_set;
    g_next_set = (g_next_set + 1) % TL_SETS;
    for (int i = 0; i < n; i++)
        g_sets[s].line[i] = text_line(&lines[i]);
    g_sets[s].n = n;
    g_sets[s].anchor = anchor;
    g_sets[s].pos = pos;
    g_sets[s].width = width;
    queue_set(s);
    return ui_text_lines_height(lines, n, width);
}

uint64_t ui_colour_hvec4(const char *id) {
    if (!g_colour_entry || !g_entry_hvec4 || !id)
        return 0xffffffffffffffffull;
    uint8_t entry[256];
    g_colour_entry(entry, name_hash(id));
    return g_entry_hvec4(entry);
}

// ---------------------------------------------------------------- item tooltips
// GuiTooltipParameters as the game builds it inline (no constructor; e.g. PrizeBoothDisplay's trinket reward): u32 0,
// vec2 0 at +0x04, scale 1.0 at +0x0c, then 32 lines of 0x28 from +0x20 (each: alignment u16 1 at +0x10, a flag 1 at
// +0x20), the line count at +0x520, an empty texture handle at +0x528, zeros to 0x568.
#define TIP_NLINES 32
static void (*g_gui_tooltip)(const void *tip, bool force); // GUI_Tooltip
static void (*g_create_item_tip)(const void *actor, const void *cls, const void *item, Vec2 pos, int a, bool b,
                                 const char *c, void *tip);                     // Inventory::Display::CreateItemTooltip
static void **g_system_class;                                                   // &Inventory::SystemClass::s_Instance
static const void *(*g_item_class)(const void *sc, unsigned type, unsigned id); // SystemClass::GetItemClass

void ui_tooltip_params_init(void *tip) {
    uint8_t *t = tip;
    memset(t, 0, UI_TIP_SIZE);
    *(float *)(t + 0x0c) = 1.0f;
    for (int i = 0; i < TIP_NLINES; i++) {
        *(uint16_t *)(t + TIP_LINES + i * LINE_SIZE + 0x10) = 1;
        t[TIP_LINES + i * LINE_SIZE + 0x20] = 1;
    }
    *(uint64_t *)(t + TIP_HANDLE) = 0xFFFFFFFF00000000ull;
}

void ui_tooltip_params_free(void *tip) {
    if (g_tip_dtor)
        g_tip_dtor(tip);
}

bool ui_item_tooltip(const void *actor, const uint8_t *item, Vec2 pos) {
    if (!item || !g_gui_tooltip || !g_create_item_tip || !g_tip_dtor || !g_system_class || !*g_system_class ||
        !g_item_class)
        return false;
    const void *cls = g_item_class(*g_system_class, *(const unsigned *)(item + ITEM_TYPE_HASH),
                                   *(const unsigned *)(item + ITEM_ID_HASH));
    if (!cls)
        return false;
    _Alignas(16) uint8_t tip[UI_TIP_SIZE];
    ui_tooltip_params_init(tip);
    g_create_item_tip(actor, cls, item, pos, 0, false, NULL, tip);
    g_gui_tooltip(tip, false);
    g_tip_dtor(tip);
    return true;
}

// ---------------------------------------------------------------- install
// GUI_Tooltip(params, force) shows a tooltip the caller has built; clients (ui_tooltip_on_show) may add to it first.
// They get the call's return address and the caller's frame (its saved rbp), to tell which game code is showing it.
#define MAX_SHOW_CLIENTS 4
static TooltipShowHook g_show_clients[MAX_SHOW_CLIENTS];
static int g_nshow_clients;
static ModsList g_show_mods; // their mods (only an applied mod's run; the top mod's last)
static void (*o_show)(void *params, bool force);

void ui_tooltip_on_show(TooltipShowHook fn) {
    if (fn && g_nshow_clients < MAX_SHOW_CLIENTS && mods_list_add(&g_show_mods) >= 0)
        g_show_clients[g_nshow_clients++] = fn;
}

static void show_hook(void *params, bool force) {
    const void *ret = __builtin_return_address(0);
#ifdef _WIN32
    const uint8_t *caller = NULL; // MSVC code keeps no frame pointer: no caller's frame
#else
    const uint8_t *caller = *(const uint8_t *const *)__builtin_frame_address(0);
#endif
    MODS_EACH(g_show_mods, false, i) g_show_clients[i](params, ret, caller);
    o_show(params, force);
}

void ui_tooltip_install(void) {
    if (g_nshow_clients) {
        // push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp,0x5e8
        static const uint8_t tip[] = PROLOGUE_SUB32(0xE8, 0x05);
        *(void **)&o_show = install_hook("GUI_Tooltip", sym("_Z11GUI_TooltipRK20GuiTooltipParametersb"),
                                         (void *)show_hook, tip, sizeof tip);
        if (!o_show)
            logf_("ui tooltip: GUI_Tooltip not hooked, %d client(s) add nothing", g_nshow_clients);
    }
    g_colour = (void (*)(uint32_t))sym("_Z11Draw_Colour4rgba");
    g_cb_ctor = (void (*)(void *, StdFunction *))sym(
        "_ZN24WidgetCallbackParametersC2ENSt3__18functionIFvPK16WidgetParametersEEE");
    g_cb_dtor = (void (*)(void *))sym("_ZN24WidgetCallbackParametersD1Ev");
    g_callback = (void (*)(const void *))sym("_Z12GUI_CallbackRK24WidgetCallbackParameters");
    g_render = (void (*)(const void *))sym("_Z17GUI_TooltipRenderRK20GuiTooltipParameters");
    g_tip_dtor = (void (*)(void *))sym("_ZN20GuiTooltipParametersD1Ev");
    g_colour_hvec4 = (void (*)(uint64_t))sym("_Z11Draw_Colour5hvec4");
    g_colour_entry = (void (*)(void *, unsigned))sym("_ZN13ColourLibrary8GetEntryEj");
    g_entry_hvec4 = (uint64_t (*)(const void *))sym("_ZNK13ColourLibrary5Entrycv5hvec4Ev");
    g_encoded_push = (void (*)(bool))sym("_Z36TextureFont_PushEncodedColourEnabledb");
    g_encoded_pop = (void (*)(void))sym("_Z35TextureFont_PopEncodedColourEnabledv");
    g_height = (float (*)(const void *, float, const char *))sym("_Z27TextureFont_GetHeightOfTextPK11TextureFontfPKc");
    g_texture_size = (Vec2 (*)(const void *))sym("_Z15Texture_GetSizeRK14ResourceHandleI7TextureE");
    g_gui_tooltip = (void (*)(const void *, bool))sym("_Z11GUI_TooltipRK20GuiTooltipParametersb");
    g_create_item_tip = (void (*)(const void *, const void *, const void *, Vec2, int, bool, const char *, void *))sym(
        "_ZN9Inventory7Display17CreateItemTooltipEPK5ActorPKNS_9ItemClassEPKNS_"
        "4ItemE4vec2ibPKcR20GuiTooltipParameters");
    g_system_class = (void **)sym("_ZN9Inventory11SystemClass10s_InstanceE");
    g_item_class =
        (const void *(*)(const void *, unsigned, unsigned))sym("_ZNK9Inventory11SystemClass12GetItemClassEjj");
    if (!g_colour)
        return;
    // push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp,0x598
    static const uint8_t tips[] = PROLOGUE_SUB32(0x98, 0x05);
    *(void **)&o_render_tooltips = install_hook("GUI_RenderTooltips", sym("_Z18GUI_RenderTooltipsv"),
                                                (void *)h_render_tooltips, tips, sizeof tips);
    if (!o_render_tooltips)
        return; // without the frame's end, line marks would go stale: no icons
    // push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp,0x678
    static const uint8_t wid[] = PROLOGUE_SUB32(0x78, 0x06);
    *(void **)&o_width =
        install_hook("TextureFont_GetWidthOfText", sym("_Z26TextureFont_GetWidthOfTextPK11TextureFontPKcj"),
                     (void *)h_width, wid, sizeof wid);
    if (!o_width)
        return;
#ifdef _WIN32
    *(void **)&o_raw = install_hook("TextureFont_DisplayRaw (for Display(anchor, width))",
                                    sym("win:TextureFont_DisplayRaw"), (void *)h_raw, NULL, 0);
    if (o_raw)
        o_display = display_win;
#else
    // push rbp; mov rbp,rsp; mov r8d,r9d; movaps xmm2,xmm1; movshdup xmm1,xmm0 (then mov ecx,imm; jmp DisplayRaw)
    static const uint8_t disp[] = {0x55, 0x48, 0x89, 0xE5, 0x45, 0x89, 0xC8, 0x0F, 0x28, 0xD1, 0xF3, 0x0F, 0x16, 0xC8};
    *(void **)&o_display = install_hook("TextureFont_Display(anchor, width)",
                                        sym("_Z19TextureFont_DisplayPK11TextureFont12LayoutAnchor4vec2fPKcbbbi"),
                                        (void *)h_display, disp, sizeof disp);
#endif
    // push rbp; mov rbp,rsp; push r15; push r14; push rbx; sub rsp,0x678
    static const uint8_t height[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                     0x53, 0x48, 0x81, 0xEC, 0x78, 0x06, 0x00, 0x00};
    *(void **)&o_height =
        install_hook("TextureFont_GetHeightOfText", (void *)g_height, (void *)h_height, height, sizeof height);
    if (o_height)
        g_height = o_height; // measuring lines: the game's own (content lines need the hook)
}
