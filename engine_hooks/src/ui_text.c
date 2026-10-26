// One owner for GUI_Text, so any number of features can look at, hide or replace the text the game draws. See
// common.h ("ui_text.c").
//
// GUI_Text(TextParameters const&, char const*) and GUI_Text(TextParameters const&, LocalizedString const&) queue a text
// widget. Both are hooked here (a function can be hooked once); each call passes the registered filters, then is drawn
// as is, not at all, or with a replacement string through the char const* overload with the same parameters.
// String table entries are matched by the text pointer: a LocalizedString's text is the loaded table's own string, so
// LocalizeStringFromId(id).text is the same pointer wherever that entry is drawn (looked up again each time: the
// language can change). On Windows the hooks take their prologues from syms_win.c (GUI_Text 0x276af0, the
// LocalizedString overload 0x276cc0; both return the widget, its text at +0x138).
#include "common.h"
#include <string.h>

#define MAX_FILTERS 8
#define MAX_SUBS    16

static UiTextFilter g_filters[MAX_FILTERS];
static int g_nfilters;
static struct {
    const char *id;
    const char *(*replacement)(void);
} g_subs[MAX_SUBS];
static int g_nsubs;
static ModsList g_filters_mods, g_subs_mods; // their mods: the top mod's asked first
static uint64_t (*o_text)(const void *params, const char *s);
static uint64_t (*o_loc_text)(const void *params, const GameLocString *s);

void ui_on_text(UiTextFilter fn) {
    if (fn && g_nfilters < MAX_FILTERS && mods_list_add(&g_filters_mods) >= 0)
        g_filters[g_nfilters++] = fn;
    else if (fn)
        logf_("ui_text: too many filters");
}

void ui_text_substitute(const char *id, const char *(*replacement)(void)) {
    if (!id || !replacement)
        return;
    if (g_nsubs == MAX_SUBS || mods_list_add(&g_subs_mods) < 0) {
        logf_("ui_text: too many substitutions, %s off", id);
        return;
    }
    g_subs[g_nsubs].id = id;
    g_subs[g_nsubs++].replacement = replacement;
}

// the filters' verdict on one call; *out: the text to draw instead (with UI_TEXT_REPLACE)
static UiTextAction filter(const void *caller, const char *text, const char **out) {
    MODS_EACH(g_filters_mods, true, i) {
        const char *r = NULL;
        UiTextAction a = g_filters[i](caller, text, &r);
        if (a == UI_TEXT_SKIP)
            return a;
        if (a == UI_TEXT_REPLACE && r) {
            *out = r;
            return a;
        }
    }
    return UI_TEXT_KEEP;
}

static uint64_t h_text(const void *params, const char *s) {
    const char *r = NULL;
    switch (filter(__builtin_return_address(0), s, &r)) {
    case UI_TEXT_SKIP:
        return 0;
    case UI_TEXT_REPLACE:
        return o_text(params, r);
    default:
        return o_text(params, s);
    }
}

static uint64_t h_loc_text(const void *params, const GameLocString *s) {
    const char *text = s ? s->text : NULL;
    if (text)
        MODS_EACH(g_subs_mods, true, i) {
            if (ui_loc_string(g_subs[i].id).text != text)
                continue;
            const char *id = g_subs[i].replacement();
            if (id && !*id)
                return 0; // hidden
            if (id) {
                GameLocString l = ui_loc_string(id);
                if (l.text)
                    return o_loc_text(params, &l);
            }
            break;
        }
    const char *r = NULL;
    switch (filter(__builtin_return_address(0), text, &r)) {
    case UI_TEXT_SKIP:
        return 0;
    case UI_TEXT_REPLACE:
        return o_text(params, r);
    default:
        return o_loc_text(params, s);
    }
}

void ui_text_install(void) {
    if (!g_nfilters && !g_nsubs)
        return; // nobody asked: GUI_Text stays untouched
    // push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; push rax
    static const uint8_t text_p[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41,
                                     0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
    // push rbp; mov rbp,rsp; push r14; push rbx; sub rsp,0x10; mov rbx,rsi
    static const uint8_t loc_p[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x10, 0x48, 0x89, 0xF3};
    *(void **)&o_text = install_hook("GUI_Text(char const*)", sym("_Z8GUI_TextRK14TextParametersPKc"), (void *)h_text,
                                     text_p, sizeof text_p);
    if (!o_text)
        return; // the replacements draw through it: both or neither
    *(void **)&o_loc_text = install_hook("GUI_Text(LocalizedString)",
                                         sym("_Z8GUI_TextRK14TextParametersRKN12Localization15LocalizedStringE"),
                                         (void *)h_loc_text, loc_p, sizeof loc_p);
}
