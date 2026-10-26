// ui_confirm: every question dialog, the game's and ours, drawn by the library (see common.h, "ui_confirm.c").
//
// Asking: ConfirmDialog::System::Instance()->PushEntry(EntryParameters const&), as RealmInventory::DrawUnequipButton's
// $_0 does: EntryParameters (EntryParameters::EntryParameters(), ~0x800 bytes) with its text a FixedLengthString<512>
// at 0 and a std::vector<Answer> at 0x200 (begin, end, capacity). An Answer (0x140 bytes): its text
// FixedLengthString<128> at 0, its std::function<void()> at 0x80 (run when it's picked; __f_ at 0xa0), a
// FixedLengthString<128> at 0xb0 and the controller button id at 0x130 (-1: k_AnswerControllerButtonIdListDefault[i]).
// PushEntry copies the entry; its destructor frees the answers (operator delete, so they're allocated with operator new)
// and its own members.
//
// Drawing: System::Show (the blur, Input_ClearFocusElements, Transform_Translate(10000) for depth) calls
// System::ShowEntry(Entry const&) on the front entry every frame; this file is its one owner. An Entry (0x330) is a
// 0x10 header and the copied parameters: text at 0x10, answers at 0x210, the back-out std::function at 0x260 (__f_ at
// 0x280; set: Esc, controller B and a back button back out). Picking an answer (the game's ShowEntry $_0) runs its
// function, then System+0x20++ (the entries Update pops next frame); CloseEntry does the same under the System's mutex
// (+0x18). BackoutOfCurrentEntry runs the back-out function and closes. Vanilla keys: one answer is picked by Enter,
// keypad Enter or Esc; with a back-out, Esc backs out. The hook draws an entry with answers itself (a dialog frame sized
// to the question, the answers as a row of buttons; dialog.layout.json "confirm") and leaves the waiting spinner
// (PushWaitingEntry, no answers) and controller play to the game's ShowEntry. Windows: the layouts are in game_layout.h;
// CloseEntry and BackoutOfCurrentEntry are inlined (stand-ins in syms_win.c) and the back button's texture is the
// entry's (its texture set at E_TEXTURES, +0x18) where macOS reads UI::AppAssets.
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_ENTER    0x0d
#define KEY_ESCAPE   0x1b
#define KEY_KP_ENTER 0x40000058
#define PENDING      16 // our dialogs waiting for an answer (a ring)
#define KEY_MAX      32
#define SECTION_MAX  32
#define MAX_BLOCKS   16
#define BUTTON_ID    0x6c636630u // 'lcf0' + the answer's index

// ---------------------------------------------------------------- game functions
static bool g_resolved;
static void (*g_entry_ctor)(void *), (*g_entry_dtor)(void *), (*g_fls512)(void *, const char *),
    (*g_fls128)(void *, const char *);
static void *(*g_dialogs)(void);
static void (*g_push_entry)(void *, const void *);
static void *(*g_op_new)(size_t);
static void (*g_close_entry)(void *sys);
static void (*g_backout)(void *sys);
static bool (*g_key_pressed)(unsigned key, bool, bool);
static bool (*g_using_controller)(void);
#ifndef _WIN32
static void *(*g_app_assets)(void);
#endif
static void (*o_show_entry)(void *sys, const uint8_t *entry);

static void resolve(void) {
    if (g_resolved)
        return;
    g_resolved = true;
    g_entry_ctor = (void (*)(void *))sym("_ZN13ConfirmDialog15EntryParametersC1Ev");
    g_entry_dtor = (void (*)(void *))sym("_ZN13ConfirmDialog15EntryParametersD1Ev");
    g_fls512 = (void (*)(void *, const char *))sym("_ZN17FixedLengthStringILi512EEaSEPKc");
    g_fls128 = (void (*)(void *, const char *))sym("_ZN17FixedLengthStringILi128EEaSEPKc");
    g_dialogs = (void *(*)(void))sym("_ZN13ConfirmDialog6System8InstanceEv");
    g_push_entry = (void (*)(void *, const void *))sym("_ZN13ConfirmDialog6System9PushEntryERKNS_15EntryParametersE");
    g_op_new = (void *(*)(size_t))sym("_Znwm"); // Windows: MSVC's std::_Allocate (the vector frees with its pair)
    g_close_entry = (void (*)(void *))sym("_ZN13ConfirmDialog6System10CloseEntryEv");
    g_backout = (void (*)(void *))sym("_ZN13ConfirmDialog6System21BackoutOfCurrentEntryEv");
    g_key_pressed = (bool (*)(unsigned, bool, bool))sym("_Z18Input_IsKeyPressedjbb");
    g_using_controller = (bool (*)(void))sym("_Z23Input_IsUsingControllerv");
#ifndef _WIN32
    g_app_assets = (void *(*)(void))sym("_ZN2UI9AppAssets7CurrentEv");
#endif
}

// ---------------------------------------------------------------- our dialogs' options, waiting for an answer
typedef struct {
    uint32_t gen; // 0: free
    int n;
    char keys[UI_CONFIRM_MAX][KEY_MAX];
    UiConfirmFn fns[UI_CONFIRM_MAX];
    uint8_t data[UI_CONFIRM_DATA];
    char section[SECTION_MAX]; // its layout section ("": "confirm")
    const uint8_t *answers;    // the queued copy's answers (how the drawing finds the section)
} Pending;
static Pending g_pending[PENDING];
static uint32_t g_gen;
static int g_next;

// an answer's call: {slot (1), option (1), generation (4)}
static void picked(const uint8_t *d) {
    uint32_t gen;
    memcpy(&gen, d + 2, sizeof gen);
    if (d[0] >= PENDING || g_pending[d[0]].gen != gen || d[1] >= g_pending[d[0]].n) {
        logf_("ui_confirm: an answer to a dialog that's gone (more than %d waiting?)", PENDING);
        return;
    }
    Pending p = g_pending[d[0]]; // a copy: the callback may ask again
    g_pending[d[0]].gen = 0;
    if (p.fns[d[1]])
        p.fns[d[1]](p.keys[d[1]], p.data);
}

void ui_confirm_styled(const char *section, const char *text, const UiConfirmOption *opts, int n, const void *data,
                       size_t len) {
    resolve();
    if (n < 1 || n > UI_CONFIRM_MAX || len > UI_CONFIRM_DATA) {
        logf_("ui_confirm: %d options, %u bytes of data: refused", n, (unsigned)len);
        return;
    }
    if (!g_entry_ctor || !g_entry_dtor || !g_fls512 || !g_fls128 || !g_dialogs || !g_push_entry || !g_op_new) {
        logf_("ui_confirm: symbols missing, no dialog");
        return;
    }
    int slot = g_next;
    g_next = (g_next + 1) % PENDING;
    Pending *pd = &g_pending[slot];
    memset(pd, 0, sizeof *pd);
    if (++g_gen == 0)
        g_gen = 1;
    pd->gen = g_gen;
    pd->n = n;
    if (data && len)
        memcpy(pd->data, data, len);
    if (section)
        snprintf(pd->section, sizeof pd->section, "%s", section);
    uint8_t *p = calloc(1, ENTRY_SIZE); // 16-byte aligned on both platforms (as the game's own)
    if (!p)
        return;
    g_entry_ctor(p);
    g_fls512(p, text && text[0] ? ui_loc(text) : ""); // a string id or finished text (the game's dialog shows it as is)
    uint8_t *a = g_op_new((size_t)n * ANSWER_SIZE);
    memset(a, 0, (size_t)n * ANSWER_SIZE);
    for (int i = 0; i < n; i++) {
        uint8_t *e = a + i * ANSWER_SIZE;
        g_fls128(e, opts[i].label ? ui_loc(opts[i].label) : "");
        if (opts[i].key) {
            strncpy(pd->keys[i], opts[i].key, KEY_MAX - 1);
        }
        pd->fns[i] = opts[i].fn;
        uint8_t d[6] = {(uint8_t)slot, (uint8_t)i};
        memcpy(d + 2, &pd->gen, sizeof pd->gen);
        ui_function((StdFunction *)(e + ANSWER_FN), picked, d, sizeof d);
        *(int *)(e + ANSWER_KEY) = -1;
    }
    uint8_t **v = (uint8_t **)(p + ENTRY_ANSWERS);
    v[0] = a;
    v[1] = a + n * ANSWER_SIZE;
    v[2] = a + n * ANSWER_SIZE;
    uint8_t *sys = g_dialogs();
    g_push_entry(sys, p);
    uint8_t *const *ev = (uint8_t *const *)(sys + SYS_ENTRIES); // ours is the queue's last entry now
    if (ev[1] - ev[0] >= ENTRY_QUEUED)
        pd->answers = *(const uint8_t *const *)(ev[1] - ENTRY_QUEUED + E_ANSWERS);
    g_entry_dtor(p); // frees the answers (operator delete) and its own members
    free(p);
}

void ui_confirm(const char *text, const UiConfirmOption *opts, int n, const void *data, size_t len) {
    ui_confirm_styled(NULL, text, opts, n, data, len);
}

bool ui_confirm_busy(void) {
    resolve();
    if (!g_dialogs)
        return false;
    const uint8_t *sys = g_dialogs();
    uint8_t *const *ev = (uint8_t *const *)(sys + SYS_ENTRIES);
    return sys && ev[0] != ev[1];
}

void ui_confirm_yes_always_no(const char *text, UiConfirmFn fn, const void *data, size_t len) {
    const UiConfirmOption opts[3] = {
        {"yes", "engine_confirm_yes", fn}, {"always", "engine_confirm_always", fn}, {"no", "engine_confirm_no", fn}};
    ui_confirm(text, opts, 3, data, len);
}

// ---------------------------------------------------------------- drawing (ShowEntry's owner)
// run a std::function<void()> (libc++: operator() in vtable slot 6; MSVC: _Do_call, slot 2)
static void call_function(const uint8_t *fn) {
    void *f = ((const StdFunction *)fn)->f;
#ifdef _WIN32
    if (f)
        (*(void (**)(void *))(*(uint8_t **)f + 0x10))(f);
#else
    if (f)
        (*(void (**)(void *))(*(uint8_t **)f + 0x30))(f);
#endif
}

// the game's back button texture (id 'mbck'): macOS UI::AppAssets +0x468, Windows the entry's texture set +0x18
static const uint8_t *back_texture(const uint8_t *entry) {
#ifdef _WIN32
    const uint8_t *set = *(const uint8_t *const *)(entry + E_TEXTURES);
    return set ? set + 0x18 : NULL;
#else
    return g_app_assets ? (const uint8_t *)g_app_assets() + 0x468 : NULL;
#endif
}

// A click: {System*, Answer*} (the answers' buffer stays put while its entry is queued). Taken only while the answer
// still belongs to the front entry and nothing is closing (two clicks in a frame pick once).
static bool front_has(uint8_t *sys, const uint8_t *answer) {
    uint8_t *const *ev = (uint8_t *const *)(sys + SYS_ENTRIES);
    if (ev[0] == ev[1] || *(const int *)(sys + SYS_CLOSING) > 0)
        return false;
    const uint8_t *const *av = (const uint8_t *const *)(ev[0] + E_ANSWERS);
    return answer >= av[0] && answer < av[1];
}

static void pick(uint8_t *sys, const uint8_t *answer) {
    if (!front_has(sys, answer))
        return;
    call_function(answer + ANSWER_FN);
    g_close_entry(sys);
}

static void on_click(const uint8_t *d) {
    uint8_t *sys;
    const uint8_t *answer;
    memcpy(&sys, d, sizeof sys);
    memcpy(&answer, d + 8, sizeof answer);
    pick(sys, answer);
}

static void on_back(const uint8_t *d) {
    uint8_t *sys;
    memcpy(&sys, d, sizeof sys);
    if (*(const int *)(sys + SYS_CLOSING) <= 0)
        g_backout(sys);
}

// The layout section of the entry whose answers these are: ours carry one, every other dialog is "confirm".
static const char *entry_section(const uint8_t *answers) {
    for (int i = 0; i < PENDING; i++)
        if (g_pending[i].gen && g_pending[i].answers == answers && g_pending[i].section[0])
            return g_pending[i].section;
    return "confirm";
}

// <section>.<name>, else confirm.<name> (a styled dialog names only what it changes)
static const char *g_sec;
static char g_key[2][128];
static int g_keyi;
static const char *lk(UiLayout *l, const char *name) {
    char *k = g_key[g_keyi ^= 1];
    snprintf(k, sizeof g_key[0], "%s.%s", g_sec, name);
    if (strcmp(g_sec, "confirm") && !ui_layout_has(l, k))
        snprintf(k, sizeof g_key[0], "confirm.%s", name);
    return k;
}

// Line lists for "lines" blocks, by name (ui_confirm_lines)
#define MAX_LINE_LISTS 8
#define MAX_LIST_LINES 24
static struct {
    char name[32];
    UiConfirmLinesFn fn;
} g_lists[MAX_LINE_LISTS];
static int g_nlists;
static ModsList g_lists_mods; // their mods: the top mod's list of a name wins

void ui_confirm_lines(const char *name, UiConfirmLinesFn fn) {
    if (!name || !fn || g_nlists >= MAX_LINE_LISTS || mods_list_add(&g_lists_mods) < 0) {
        logf_("ui_confirm: line list %s refused", name ? name : "?");
        return;
    }
    snprintf(g_lists[g_nlists].name, sizeof g_lists[0].name, "%s", name);
    g_lists[g_nlists++].fn = fn;
}

static int list_lines(const char *name, UiConfirmLine *out, int max) {
    MODS_EACH(g_lists_mods, true, i) if (!strcmp(g_lists[i].name, name)) return g_lists[i].fn(out, max);
    return 0;
}

// The section's blocks (<section>.blocks, in the file's order), above the question: a line of text (a string id, its
// font, colour and wrap width), a picture (path, size: its own if absent), or a list's lines (each in its own font and
// colour, else the block's; line_gap apart; bullet: a string id put before each; heading: a string id over them in
// heading_font / heading_colour, shown only with lines), each with gap below it. Measures when draw is false. Returns the height.
static float blocks(UiLayout *l, float cx, float y, float tw, bool draw) {
    const char *keys[MAX_BLOCKS];
    char base[96], k[160];
    snprintf(base, sizeof base, "%s.blocks", g_sec);
    int nb = ui_layout_keys(l, base, keys, MAX_BLOCKS);
    float y0 = y;
    for (int i = 0; i < nb; i++) {
        snprintf(k, sizeof k, "%s.%s.gap", base, keys[i]);
        float gap = ui_layout_num(l, k, 12);
        snprintf(k, sizeof k, "%s.%s.image", base, keys[i]);
        if (ui_layout_has(l, k)) {
            uint8_t tex[16];
            Vec2 size = {0, 0};
            bool have = ui_texture(ui_layout_str(l, k, ""), tex);
            if (have)
                ui_texture_size(tex, &size);
            snprintf(k, sizeof k, "%s.%s.size", base, keys[i]);
            size = ui_layout_vec2(l, k, size);
            if (draw && have) {
                ui_colour(0xFFFFFFFFu);
                ui_image_sized((Vec2){cx - size.x / 2, y}, tex, size);
            }
            y += size.y + gap;
            continue;
        }
        snprintf(k, sizeof k, "%s.%s.lines", base, keys[i]);
        if (ui_layout_has(l, k)) {
            UiConfirmLine lines[MAX_LIST_LINES];
            int nl = list_lines(ui_layout_str(l, k, ""), lines, MAX_LIST_LINES);
            if (nl <= 0)
                continue;
            char f[160], c[160];
            snprintf(f, sizeof f, "%s.%s.font", base, keys[i]);
            snprintf(c, sizeof c, "%s.%s.colour", base, keys[i]);
            const char *font = ui_layout_str(l, f, "confirm_dialog_question");
            const char *colour = ui_layout_str(l, c, "confirm_dialog_question");
            snprintf(k, sizeof k, "%s.%s.width", base, keys[i]);
            float w = ui_layout_num(l, k, tw);
            snprintf(k, sizeof k, "%s.%s.line_gap", base, keys[i]);
            float lg = ui_layout_num(l, k, 4);
            snprintf(k, sizeof k, "%s.%s.bullet", base, keys[i]);
            const char *bid = ui_layout_str(l, k, "");
            const char *bullet = bid && bid[0] ? ui_loc(bid) : "";
            snprintf(k, sizeof k, "%s.%s.heading", base, keys[i]);
            const char *hid = ui_layout_str(l, k, "");
            if (hid && hid[0]) {
                snprintf(f, sizeof f, "%s.%s.heading_font", base, keys[i]);
                snprintf(c, sizeof c, "%s.%s.heading_colour", base, keys[i]);
                const char *hf = ui_layout_str(l, f, font), *hc = ui_layout_str(l, c, colour), *ht = ui_loc(hid);
                snprintf(k, sizeof k, "%s.%s.heading_gap", base, keys[i]);
                if (draw)
                    ui_text_anchored((Vec2){cx, y}, ht, hf, hc, 1, w);
                y += ui_text_height(hf, ht, w) + ui_layout_num(l, k, 8);
            }
            for (int j = 0; j < nl; j++) {
                const char *lf = lines[j].font && lines[j].font[0] ? lines[j].font : font;
                const char *lc = lines[j].colour && lines[j].colour[0] ? lines[j].colour : colour;
                char line[1024];
                snprintf(line, sizeof line, "%s%s", bullet, lines[j].text);
                if (draw)
                    ui_text_anchored((Vec2){cx, y}, line, lf, lc, 1, w);
                y += ui_text_height(lf, line, w) + (j + 1 < nl ? lg : 0);
            }
            y += gap;
            continue;
        }
        snprintf(k, sizeof k, "%s.%s.text", base, keys[i]);
        const char *id = ui_layout_str(l, k, "");
        const char *text = id && id[0] ? ui_loc(id) : NULL;
        if (!text || !text[0])
            continue;
        snprintf(k, sizeof k, "%s.%s.font", base, keys[i]);
        const char *font = ui_layout_str(l, k, "confirm_dialog_question");
        snprintf(k, sizeof k, "%s.%s.colour", base, keys[i]);
        const char *colour = ui_layout_str(l, k, "confirm_dialog_question");
        snprintf(k, sizeof k, "%s.%s.width", base, keys[i]);
        float w = ui_layout_num(l, k, tw);
        if (draw)
            ui_text_anchored((Vec2){cx, y}, text, font, colour, 1, w);
        y += ui_text_height(font, text, w) + gap;
    }
    return y - y0;
}

static void h_show_entry(void *sys_, const uint8_t *entry) {
    uint8_t *sys = sys_;
    const uint8_t *const *av = (const uint8_t *const *)(entry + E_ANSWERS);
    int n = (int)((av[1] - av[0]) / ANSWER_SIZE);
    UiLayout *l = ui_layout_mod("dialog.layout.json");
    g_sec = n >= 1 ? entry_section(av[0]) : "confirm";
    // the frame's art is the library package's: the game has it only while a campaign applies the package (not on
    // the campaign select screen), so the game draws its own dialog otherwise
    if (n < 1 || !l || !mods_package_applied() || (g_using_controller && g_using_controller()) ||
        !ui_layout_num(l, lk(l, "enabled"), 1)) {
        o_show_entry(sys, entry);
        return;
    }
    bool backout = *(void *const *)(entry + E_BACKOUT_F) != NULL;
    const char *qf = ui_layout_str(l, lk(l, "question_font"), "confirm_dialog_question");
    const char *qc = ui_layout_str(l, lk(l, "question_colour"), "confirm_dialog_question");
    const char *af = ui_layout_str(l, lk(l, "answer_font"), "confirm_dialog_answer");
    const char *ac = ui_layout_str(l, lk(l, "answer_colour"), "confirm_dialog_answer");
    float tw = ui_layout_num(l, lk(l, "text_width"), 520), side = ui_layout_num(l, lk(l, "side"), 60);
    float top = ui_layout_num(l, lk(l, "top"), 110), gap = ui_layout_num(l, lk(l, "text_gap"), 30);
    float bottom = ui_layout_num(l, lk(l, "bottom"), 40), bh = ui_layout_num(l, lk(l, "button.height"), 38);
    float pad = ui_layout_num(l, lk(l, "button.pad"), 24), bmin = ui_layout_num(l, lk(l, "button.min_width"), 140);
    float bgap = ui_layout_num(l, lk(l, "button.gap"), 16);

    const char *text = (const char *)entry + E_TEXT;
    bool rich = ui_rich_has_icons(text); // inline pictures: laid out by ui_rich_text
    float icon = ui_layout_num(l, lk(l, "icon.size"), 32), igap = ui_layout_num(l, lk(l, "icon.gap"), 2);
    float bh_ = blocks(l, 0, 0, tw, false); // the section's headings and pictures, above the question
    float th = !text[0] ? 0
               : rich   ? ui_rich_text((Vec2){0, 0}, text, qf, qc, tw, icon, igap, true, false)
                        : ui_text_height(qf, text, tw);
    float bw[64], row = 0;
    if (n > 64)
        n = 64;
    for (int i = 0; i < n; i++) {
        float w = ui_text_width(af, (const char *)av[0] + i * ANSWER_SIZE) + 2 * pad;
        bw[i] = w > bmin ? w : bmin;
        row += bw[i] + (i ? bgap : 0);
    }
    float inner = tw > row ? tw : row, minw = ui_layout_num(l, lk(l, "min_width"), 640);
    Vec2 size = {inner + 2 * side, top + bh_ + th + gap + bh + bottom};
    if (size.x < minw)
        size.x = minw;
    Vec2 at = ui_dialog_place(ui_place_parse(ui_layout_str(l, lk(l, "place"), "screen")),
                              ui_layout_vec2(l, lk(l, "point"), (Vec2){0, 0}), size);
    ui_colour(0xFFFFFFFFu);
    ui_dialog_frame(l, ui_layout_str(l, lk(l, "style"), "frame"), at, size);
    float cx = at.x + size.x / 2;
    blocks(l, cx, at.y + top, tw, true);
    if (rich)
        ui_rich_text((Vec2){cx, at.y + top + bh_}, text, qf, qc, tw, icon, igap, true, true);
    else if (text[0])
        ui_text_anchored((Vec2){cx, at.y + top + bh_}, text, qf, qc, 1, tw);

    uint8_t hover[16];
    bool have_hover = ui_texture(ui_layout_str(l, lk(l, "button.hover"),
                                               "shared/confirm_dialog/confirm_dialog.answer_text_selected_overlay.png"),
                                 hover);
    float x = cx - row / 2, y = at.y + size.y - bottom - bh, tdy = ui_layout_num(l, lk(l, "button.text_dy"), 0);
    float hpad = ui_layout_num(l, lk(l, "button.hover_pad_y"), 8); // the bar (and its click area) past the row

    for (int i = 0; i < n; i++) {
        const uint8_t *answer = av[0] + i * ANSWER_SIZE;
        Vec2 pos = {x, y - hpad}, bs = {bw[i], bh + 2 * hpad};
        uint8_t d[16];
        memcpy(d, &sys, 8);
        memcpy(d + 8, &answer, 8);
        if (have_hover) {
            ui_colour(ui_mouse_in(pos, bs) ? 0xFFFFFFFFu : 0); // the bar shows on the hovered button only
            ui_button_sized(pos, bs, hover, BUTTON_ID + (unsigned)i, true, on_click, d, sizeof d);
            ui_colour(0xFFFFFFFFu);
        }
        ui_text_anchored((Vec2){x + bw[i] / 2, y + bh / 2 + tdy}, (const char *)answer, af, ac, 4, 0);
        x += bw[i] + bgap;
    }
    const uint8_t *tex = backout ? back_texture(entry) : NULL;
    if (tex) { // the game's back button
        Vec2 ts, off = ui_layout_vec2(l, lk(l, "back.offset"), (Vec2){-20, 20});
        if (ui_texture_size(tex, &ts)) {
            uint8_t d[8];
            memcpy(d, &sys, 8);
            ui_button((Vec2){at.x + size.x + off.x - ts.x, at.y + off.y}, tex, 0x6d62636bu, true, on_back, d, sizeof d);
        }
    }

    // the game's keys: one answer takes Enter, keypad Enter or Esc; a back-out takes Esc
    if (!g_key_pressed)
        return;
    if (n == 1 && (g_key_pressed(KEY_KP_ENTER, false, false) || g_key_pressed(KEY_ENTER, false, false) ||
                   g_key_pressed(KEY_ESCAPE, false, false)))
        pick(sys, av[0]);
    else if (backout && g_key_pressed(KEY_ESCAPE, false, false) && *(const int *)(sys + SYS_CLOSING) <= 0)
        g_backout(sys);
}

void ui_confirm_install(void) {
    resolve();
    if (!g_close_entry || !g_backout || !g_key_pressed) {
        logf_("ui_confirm: symbols missing, the game draws its dialogs");
        return;
    }
    static const uint8_t prologue[] = PROLOGUE_SUB32(0xc8, 0x09); // sub rsp,0x9c8
    *(void **)&o_show_entry =
        install_hook("ConfirmDialog::System::ShowEntry", sym("_ZN13ConfirmDialog6System9ShowEntryERKNS_5EntryE"),
                     (void *)h_show_entry, prologue, sizeof prologue);
}
