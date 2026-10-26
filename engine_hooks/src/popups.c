// Combat popup text log: every RaidDisplay popup (Bleed / Resist / damage numbers / Buff ...) goes through
// RaidDisplay::AddPlusPopupText(actor, type, text, number, base colour, outline colour, sfx, plus) on its way to
// QueuePopupText, and is written to ddx_combat.log as "popup actor=... type=0x.. text=...". It tells a popup that was never
// asked for from one the display queued and later dropped (ProcessQueuedPopupText; RemoveActorFromQueuedPopupText
// clears a dead actor's). AddPopupText(actor, type) fills text, colours and sfx from the type
// (k_PopupTextTypeData) and calls it. ProcessQueuedPopupText(index, dt) handles one queue entry per frame (from
// PrePanelShowInternal): it waits (delay, attack zoom, halos, side effects, the type's on-screen limit, one popup per
// actor at a time) or consumes it (+0xb8 = 1): shows it, or drops it if the actor has no sprite. Each consumption is
// logged as "popup shown|dropped ...". On Windows AddPlusPopupText reads the type's colours itself (no colour
// arguments) and AddPopupText takes the plus flag (Buff! / Debuff! go through it).
//
// Named popups: a client names the next application of an effect on a target (buff_source_effect, buff_origin.c; e.g.
// a client mod affix triggers, "Ravenous!"). The status popups that application raises on the target (bleed, blight,
// marked, regen onset) show the name instead of the game's (buff_origin_scope_popup), and so do the tray icons its buffs light
// (buff_origin_popup, by the buffs' guids): the hook owns
// Overlay_Basic::OnBuffActivate(actor, icon type, sub), which the overlay's tray icon pass calls (twice per frame, from
// its two icon loops in ShowScreen's $_5) when an icon turns on. Vanilla, per type: 13 (buffs) "Buff!" (0x15) and 14
// (debuffs) "Debuff!" (0x16), both only if the icon data's +4 is clear, plus = its +3 (AddPlusPopupText); 18 (riposte)
// "Riposte!" (0x26, AddPopupText); 0 quirk entering text, 4/5/15 queued lambdas, 22 a tutorial event, 25 "can't be
// guarded" (0x38). For 13/14/18 the hook lists the icon's buffs the way the game does
// (GetBuffTrayBuffInstanceList(actor, positive) / GetTrayRiposteBuffInstanceList(actor): Buff::Instance pointers into
// the actor's group, the entry's guid 8 bytes before each); if one has a named popup, it shows that text with the same
// type, sound and plus flag (AddPlusPopupStringText); else a client callback (popup_on_buff) may name a buff of the icon
// (e.g. a client mod class crit bonuses, "Crit: Healing!"); else it calls the game's.
#include "common.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
typedef void (*add_plus_popup_text_fn)(void *display, void *actor, int type, const char *text, unsigned number,
                                       const void *sfx, bool plus);
#else
typedef void (*add_plus_popup_text_fn)(void *display, void *actor, int type, const char *text, unsigned number,
                                       unsigned base_colour, unsigned outline_colour, const void *sfx, bool plus);
#endif
static add_plus_popup_text_fn g_orig;

#define POPUP_BLIGHT            0x1b
#define POPUP_BLEED             0x1d
#define POPUP_MARKED            0x23
#define POPUP_HP_HEAL_DOT_ONSET 0x2 // regen onset (hp_heal_dot_onset); named like bleed: unverified
#define ICON_BUFFS              13
#define ICON_DEBUFFS            14
#define ICON_RIPOSTE            18
#define POPUP_BUFF              0x15
#define POPUP_DEBUFF            0x16
#define POPUP_RIPOSTE           0x26
#define ICON_PLUS               3 // tray icon data: the popup's plus flag
#define ICON_NO_POPUP           4 // tray icon data: set, no Buff! / Debuff! popup

#define MAX_BUFF_CLIENTS 4
static PopupBuffText g_buff_clients[MAX_BUFF_CLIENTS];
static int g_nbuff_clients;
static const char *(*g_source_id)(unsigned source);
static ModsList g_buff_mods; // their mods: the top mod's asked first

void popup_on_buff(PopupBuffText fn) {
    if (fn && g_nbuff_clients < MAX_BUFF_CLIENTS && mods_list_add(&g_buff_mods) >= 0)
        g_buff_clients[g_nbuff_clients++] = fn;
}

#define MAX_OBSERVERS 4
static PopupObserver g_observers[MAX_OBSERVERS];
static int g_nobservers;
static ModsList g_observers_mods; // their mods (only an applied mod's observe)

void popup_on_add(PopupObserver fn) {
    if (fn && g_nobservers < MAX_OBSERVERS && mods_list_add(&g_observers_mods) >= 0)
        g_observers[g_nobservers++] = fn;
}

// RaidUI::Screen::GetIdFromType: its k_PopupTextTypeData entry, whose id (FixedLengthString<64>) comes first.
static const char *(*g_type_entry)(int type);
#define POPUP_TYPES 0x80 // more than the game has (0x38 "can't be guarded" is among the last)

const char *popup_type_id(int type) {
    if (!g_type_entry || type < 0 || type >= POPUP_TYPES)
        return NULL;
    const char *id = g_type_entry(type);
    return id && id[0] ? id : NULL;
}

static bool g_naming;                  // a popup with our text is being added (popup_text, a named tray icon)
static int g_icon_rounds, g_icon_type; // the tray icon activating: its longest-lasting buff's duration
static const void *g_icon_buff;        // and the buff its popup is named by

// a popup being added: its text (a named status popup's), logged and shown to the observers
static const char *popup_added(void *actor, int type, const char *text, unsigned number, bool plus) {
    bool status =
        type == POPUP_BLEED || type == POPUP_BLIGHT || type == POPUP_MARKED || type == POPUP_HP_HEAL_DOT_ONSET;
    const char *named = status ? buff_origin_scope_popup(actor) : NULL;
    if (named)
        text = named;
    bool ours = named || g_naming;
    PopupEvent ev = {actor, type, text ? text : "", number, ours, g_icon_rounds, g_icon_buff, g_icon_type};
    char name[64] = "";
    if (actor) {
        const char *s = (const char *)actor + 8;
        size_t i = 0;
        while (i + 1 < sizeof name && s[i] >= 0x20 && s[i] != 0x7f) {
            name[i] = s[i];
            i++;
        }
        name[i] = 0;
    }
    combat_logf("popup actor=%s:%u name=\"%s\" type=0x%x text=\"%s\" number=%u plus=%u",
                is_hero(actor)      ? "hero"
                : is_monster(actor) ? "monster"
                                    : "actor",
                is_hero(actor)      ? hero_guid(actor)
                : is_monster(actor) ? monster_guid(actor)
                                    : 0,
                name, type, text ? text : "", number, plus);
    MODS_EACH(g_observers_mods, false, i) g_observers[i](&ev);
    return text;
}

#ifdef _WIN32
static void hook(void *display, void *actor, int type, const char *text, unsigned number, const void *sfx, bool plus) {
    g_orig(display, actor, type, popup_added(actor, type, text, number, plus), number, sfx, plus);
}
#else
static void hook(void *display, void *actor, int type, const char *text, unsigned number, unsigned base_colour,
                 unsigned outline_colour, const void *sfx, bool plus) {
    g_orig(display, actor, type, popup_added(actor, type, text, number, plus), number, base_colour, outline_colour, sfx,
           plus);
}
#endif

// RaidDisplay DISPLAY_POPUP_QUEUE: std::vector<QueuedPopupText> (0xc8 each: actor +0, type +0x58, consumed +0xb8, delay +0xc0)
typedef void (*process_queued_fn)(void *display, unsigned index, float dt);
static process_queued_fn g_process_orig;
static const void *(*g_get_sprite)(const void *actor);

static void process_hook(void *display, unsigned index, float dt) {
    uint8_t **queue = (uint8_t **)((uint8_t *)display + DISPLAY_POPUP_QUEUE);
    uint8_t *e = queue[0] && index < (size_t)(queue[1] - queue[0]) / QUEUED_POPUP_SIZE
                     ? queue[0] + index * QUEUED_POPUP_SIZE
                     : NULL;
    void *actor = e ? *(void **)e : NULL;
    int type = e ? *(int *)(e + QUEUED_POPUP_TYPE) : 0;
    bool was_done = e && e[QUEUED_POPUP_DONE];
    bool sprite = actor && g_get_sprite && g_get_sprite(actor);
    g_process_orig(display, index, dt);
    if (!e || was_done || !e[QUEUED_POPUP_DONE])
        return;
    combat_logf("popup %s actor=%s:%u type=0x%x", sprite ? "shown" : "dropped (no sprite)",
                is_hero(actor)      ? "hero"
                : is_monster(actor) ? "monster"
                                    : "actor",
                is_hero(actor)      ? hero_guid(actor)
                : is_monster(actor) ? monster_guid(actor)
                                    : 0,
                type);
}

typedef struct {
    uint8_t *begin, *end, *cap;
} PtrVec;
static void (*o_buff_activate)(void *overlay, void *actor, int type, unsigned sub);
static PtrVec (*g_tray_buffs)(const void *overlay, const void *actor, bool positive);
static PtrVec (*g_tray_ripostes)(void *overlay, void *actor);
static const uint8_t *(*g_icon_data)(void *display_data, int type, unsigned sub);
static void (*g_add_plus_string)(void *display, void *actor, int type, const char *text, bool plus);
static void (*g_delete)(void *);
static void **g_display;

void popup_text(void *actor, int type, const char *text) {
    if (!actor || !text || !*text || !g_add_plus_string || !g_display || !*g_display)
        return;
    g_naming = true;
    g_add_plus_string(*g_display, actor, type, text, false);
    g_naming = false;
}

// The buffs added since an actor's tray icon last lit up (Buff::Group::AddBuff's guids; an actor's group entry holds
// the guid 8 bytes before the Buff::Instance): only these name its popup and give its duration. An icon lights up
// for its new buffs, but lists every buff it holds; an older one (a crit bonus with a round left) mustn't name a
// later skill's popup. A buff leaves the list once an icon holding it has lit up.
#define MAX_NEW_BUFFS 128
static struct {
    const void *actor;
    unsigned guid;
} g_new[MAX_NEW_BUFFS];
static int g_nnew;
static bool g_new_seen; // the add hook reports (else every buff on the icon counts, as before)

void popups_buff_added(const void *actor, unsigned guid) {
    g_new_seen = true;
    if (g_nnew == MAX_NEW_BUFFS) {
        memmove(g_new, g_new + 1, sizeof g_new - sizeof g_new[0]);
        g_nnew--;
    }
    g_new[g_nnew].actor = actor;
    g_new[g_nnew++].guid = guid;
}

static bool is_new(const void *actor, const uint8_t *buff) {
    if (!g_new_seen)
        return true;
    unsigned guid = *(const unsigned *)(buff - 8);
    for (int i = 0; i < g_nnew; i++)
        if (g_new[i].actor == actor && g_new[i].guid == guid)
            return true;
    return false;
}

// the icon's buffs have had their popup
static void forget_new(const void *actor, const PtrVec *v) {
    for (int i = 0; i < g_nnew;) {
        bool on = false;
        for (uint8_t **p = (uint8_t **)v->begin; p && (uint8_t *)(p + 1) <= v->end && !on; p++)
            on = *p && g_new[i].actor == actor && *(const unsigned *)(*p - 8) == g_new[i].guid;
        if (on) {
            memmove(g_new + i, g_new + i + 1, (size_t)(g_nnew - i - 1) * sizeof g_new[0]);
            g_nnew--;
        } else
            i++;
    }
}

// how long a buff lasts, comparable: until removed / camp / the quest's end > battles > rounds
static long long lasting(int amount, int type) {
    if (amount < 0 || (type >= DURATION_QUEST_COMPLETE && type <= DURATION_NONE))
        return 1LL << 40;
    return type == DURATION_COMBAT_END ? (1LL << 20) + amount : amount;
}

// The text of a tagged buff behind the tray icon that just turned on, or NULL; rounds, dtype: its longest-lasting
// buff's duration.
static const char *icon_text(void *overlay, void *actor, int type, int *rounds, int *dtype, const void **by) {
    PtrVec v =
        type == ICON_RIPOSTE ? g_tray_ripostes(overlay, actor) : g_tray_buffs(overlay, actor, type == ICON_BUFFS);
    const char *text = NULL;
    *rounds = 0;
    *dtype = DURATION_AFTER_TURN;
    long long best = 0;
    bool any_new = false;
    for (uint8_t **p = (uint8_t **)v.begin; p && (uint8_t *)(p + 1) <= v.end; p++)
        any_new |= *p && is_new(actor, *p);
    for (uint8_t **p = (uint8_t **)v.begin; p && (uint8_t *)(p + 1) <= v.end; p++) {
        if (!*p || (any_new && !is_new(actor, *p)))
            continue; // the new buffs' duration (all, if none is known)
        int n = *(const int *)(*p + BUFF_DURATION), t = *(const int *)(*p + BUFF_DURATION_TYPE);
        if (lasting(n, t) > best) {
            best = lasting(n, t);
            *rounds = n;
            *dtype = t;
        }
    }
    *by = NULL;
    for (uint8_t **p = (uint8_t **)v.begin; p && (uint8_t *)(p + 1) <= v.end && !text; p++)
        if (*p && is_new(actor, *p) && (text = buff_origin_popup(actor, *(const unsigned *)(*p - 8))))
            *by = *p;
    for (uint8_t **p = (uint8_t **)v.begin; p && (uint8_t *)(p + 1) <= v.end && !text; p++)
        if (*p && is_new(actor, *p))
            MODS_EACH(g_buff_mods, true, i)
    if (!text && (text = g_buff_clients[i](actor, *p)))
        *by = *p;
    // Vanilla origins are recorded for skills too, but sources such as quirks, trinkets,
    // recovery and battle modifiers can be added outside EffectApplyInternal.
    static char fallback[512];
    fallback[0] = 0;
    for (uint8_t **p = (uint8_t **)v.begin; p && (uint8_t *)(p + 1) <= v.end && !text; p++) {
        if (!*p || !is_new(actor, *p))
            continue;
        char label[160] = "";
        if (!buff_origin_label(actor, *p, label, sizeof label) && g_source_id) {
            const char *source = g_source_id(*(const unsigned *)(*p + BUFF_SOURCE));
            char key[96];
            snprintf(key, sizeof key, "buff_%s", source ? source : "");
            if (source && loc_exists(key))
                snprintf(label, sizeof label, "%s", ui_loc(key));
        }
        // Tooltip labels may contain colour markup; popup text is plain text.
        char plain[160];
        size_t n = 0;
        for (const char *c = label; *c && n + 1 < sizeof plain;) {
            if (!strncmp(c, "{colour_start|", 14) || !strncmp(c, "{colour_end}", 12)) {
                const char *end = strchr(c, '}');
                if (end) { c = end + 1; continue; }
            }
            plain[n++] = *c++;
        }
        plain[n] = 0;
        if (n) {
            snprintf(fallback, sizeof fallback, "%s", plain);
            text = fallback;
            *by = *p;
        }
    }
    if (text && *by) { // which buff named it (of how many on the icon)
        const uint8_t *b = *by;
        combat_logf("popup named \"%s\" by buff id=\"%.40s\" source=%u duration=%d type=%d (%d buff(s) on the icon)",
                    text, (const char *)b + 0x61, *(const unsigned *)(b + 0x54), *(const int *)(b + BUFF_DURATION),
                    *(const int *)(b + BUFF_DURATION_TYPE), (int)(((uint8_t **)v.end) - ((uint8_t **)v.begin)));
    }
    forget_new(actor, &v);
    if (v.begin && g_delete)
        g_delete(v.begin);
    return text;
}

// ShowScreen's $_5 (one actor's tray) runs its activation check in both of its passes over the icon types, the
// counting pass (+0x11e) and the drawing pass (+0x404), and nothing clears the icon's flag between them: the game asks
// for each activation's popup twice in the frame. The drawing pass's call is skipped (a game bug fix).
#define TRAY_LAMBDA "_ZZN13Overlay_Basic10ShowScreenERN6RaidUI6Screen11RaidDisplayEENK3$_5clEP5Actor4vec2b"
#ifdef _WIN32
#define TRAY_COUNT_CALL 0x121 // 0x6edc50: OnBuffActivate at 0x6edd71 and 0x6edf40
#define TRAY_DRAW_CALL  0x2f0
#else
#define TRAY_COUNT_CALL 0x11e
#define TRAY_DRAW_CALL  0x404
#endif
static const uint8_t *g_draw_pass_ret;

static void buff_activate_hook(void *overlay, void *actor, int type, unsigned sub) {
    if (g_draw_pass_ret && __builtin_return_address(0) == g_draw_pass_ret)
        return;
    int rounds = 0, dtype = DURATION_AFTER_TURN;
    const char *text = (type == ICON_BUFFS || type == ICON_DEBUFFS || type == ICON_RIPOSTE) && actor && *g_display
                           ? icon_text(overlay, actor, type, &rounds, &dtype, &g_icon_buff)
                           : NULL;
    g_icon_rounds = rounds;
    g_icon_type = dtype;
    if (!text) {
        o_buff_activate(overlay, actor, type, sub);
        g_icon_rounds = 0;
        g_icon_type = DURATION_AFTER_TURN;
        g_icon_buff = NULL;
        return;
    }
    const uint8_t *icon = type == ICON_RIPOSTE ? NULL : g_icon_data((uint8_t *)actor + ACTOR_DISPLAY_DATA, type, sub);
    if (type != ICON_RIPOSTE && (!icon || icon[ICON_NO_POPUP])) {
        g_icon_rounds = 0;
        g_icon_buff = NULL;
        return;
    }
    g_naming = true;
    if (type == ICON_RIPOSTE)
        g_add_plus_string(*g_display, actor, POPUP_RIPOSTE, text, false);
    else
        g_add_plus_string(*g_display, actor, type == ICON_BUFFS ? POPUP_BUFF : POPUP_DEBUFF, text,
                          icon[ICON_PLUS] != 0);
    g_naming = false;
    g_icon_rounds = 0;
    g_icon_buff = NULL;
}

void popups_install(void) {
    *(void **)&g_source_id = sym("_ZN4Buff11GetSourceIdENS_6SourceE");
    static const uint8_t prologue[] = PROLOGUE_SUB32(0xc8, 0x00); // sub rsp,0xc8
    g_orig = (add_plus_popup_text_fn)install_hook("RaidDisplay::AddPlusPopupText (popup log)",
                                                  sym("_ZN6RaidUI6Screen11RaidDisplay16AddPlusPopupTextEP5ActorNS0_"
                                                      "14PopupTextTypes5eTypeEPKcjjjRK17FixedLengthStringILi64EEb"),
                                                  (void *)hook, prologue, sizeof prologue);
    g_get_sprite = (const void *(*)(const void *))sym("_ZNK5Actor9GetSpriteEv");
    *(void **)&g_type_entry = sym("_ZN6RaidUI6Screen13GetIdFromTypeENS0_14PopupTextTypes5eTypeE");
    static const uint8_t process_prologue[] = PROLOGUE_SUB32(0x88, 0x01); // sub rsp,0x188
    g_process_orig = (process_queued_fn)install_hook("RaidDisplay::ProcessQueuedPopupText (popup log)",
                                                     sym("_ZN6RaidUI6Screen11RaidDisplay22ProcessQueuedPopupTextEjf"),
                                                     (void *)process_hook, process_prologue, sizeof process_prologue);
    void *activate = sym("_ZN13Overlay_Basic14OnBuffActivateEP5ActorN13TrayIconTypes13eTrayIconTypeEj");
    *(void **)&g_tray_buffs = sym("_ZNK13Overlay_Basic27GetBuffTrayBuffInstanceListEPK5Actorb");
    *(void **)&g_tray_ripostes = sym("_ZN13Overlay_Basic30GetTrayRiposteBuffInstanceListEP5Actor");
    *(void **)&g_icon_data = sym("_ZN16ActorDisplayData15GetTrayIconDataEN13TrayIconTypes13eTrayIconTypeEj");
    *(void **)&g_add_plus_string =
        sym("_ZN6RaidUI6Screen11RaidDisplay22AddPlusPopupStringTextEP5ActorNS0_14PopupTextTypes5eTypeEPKcb");
    *(void **)&g_delete = sym("_ZdlPv");
    g_display = (void **)sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    if (!g_add_plus_string || !g_display)
        logf_("popups: AddPlusPopupStringText not found, popup_text shows nothing");
    if (activate && g_tray_buffs && g_tray_ripostes && g_icon_data && g_add_plus_string && g_delete && g_display) {
        // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; sub rsp,0x1f0
        static const uint8_t activate_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                                    0x54, 0x53, 0x48, 0x81, 0xEC, 0xF0, 0x01, 0x00, 0x00};
        *(void **)&o_buff_activate =
            install_hook("Overlay_Basic::OnBuffActivate (named popups)", activate, (void *)buff_activate_hook,
                         activate_prologue, sizeof activate_prologue);
    }
    if (!o_buff_activate)
        logf_("popups: OnBuffActivate not hooked, tray icon popups keep the game's text");
    else {
        const void *tray = sym(TRAY_LAMBDA);
        if (call_site_return(tray, TRAY_COUNT_CALL, activate))
            g_draw_pass_ret = call_site_return(tray, TRAY_DRAW_CALL, activate);
        if (!g_draw_pass_ret)
            logf_("popups: tray pass calls not found (other game version?), activation popups come twice");
    }
}

// ---------------------------------------------------------------- a popup in words (common.h, popup_on_line)
#define MAX_LINE_CLIENTS 4
static PopupLineFn g_line_clients[MAX_LINE_CLIENTS];
static int g_nline_clients;
static ModsList g_line_mods; // their mods: the top mod's wording first

void popup_on_line(PopupLineFn fn) {
    if (fn && g_nline_clients < MAX_LINE_CLIENTS && mods_list_add(&g_line_mods) >= 0)
        g_line_clients[g_nline_clients++] = fn;
}

bool popup_line(const PopupEvent *p, PopupLine *out) {
    if (p && out)
        MODS_EACH(g_line_mods, true, i) {
            memset(out, 0, sizeof *out);
            if (g_line_clients[i](p, out) && out->wording[0])
                return true;
        }
    // Effect popups without a tray icon (resists, cures, heals, movement, etc.)
    // are observed synchronously while their originating effect is still live.
    const char *source = p ? buff_origin_scope_popup(p->actor) : NULL;
    if (!p || !out || p->named || !source)
        return false;
    memset(out, 0, sizeof *out);
    snprintf(out->wording, sizeof out->wording, "{actor}: {detail} (from {source})");
    snprintf(out->source, sizeof out->source, "%s", source);
    const char *id = popup_type_id(p->type);
    const char *text = p->text && p->text[0] ? p->text : id ? id : "effect";
    if (p->number)
        snprintf(out->detail, sizeof out->detail, "%s %u", text, p->number);
    else
        snprintf(out->detail, sizeof out->detail, "%s", text);
    return true;
}

static const char *str_or(const char *id, const char *fallback) {
    return loc_exists(id) ? ui_loc(id) : fallback;
}

void duration_text(int rounds, int type, char *out, size_t cap) {
    if (!cap)
        return;
    char n[16];
    snprintf(n, sizeof n, "%d", type == DURATION_COMBAT_END ? rounds : rounds - 1);
    bool by_rounds = type == DURATION_BEFORE_TURN || type == DURATION_AFTER_TURN || type == DURATION_AFTER_ROUND;
    if (rounds <= 0 || (!by_rounds && type != DURATION_COMBAT_END))
        out[0] = 0;
    else if (type == DURATION_COMBAT_END) {
        const char *f = rounds == 1 ? str_or("engine_duration_battle_end", " (until end of battle)")
                                    : str_or("engine_duration_battles", " ({n} battles)");
        const char *k = strstr(f, "{n}");
        if (k)
            snprintf(out, cap, "%.*s%s%s", (int)(k - f), f, n, k + 3);
        else
            snprintf(out, cap, "%s", f);
    } else if (rounds == 1)
        snprintf(out, cap, "%s", str_or("engine_duration_round_end", " (until end of round)"));
    else if (rounds == 2)
        snprintf(out, cap, "%s", str_or("engine_duration_one", " (1 rd)"));
    else {
        const char *f = str_or("engine_duration", " ({n} rds)");
        const char *k = strstr(f, "{n}");
        if (k)
            snprintf(out, cap, "%.*s%s%s", (int)(k - f), f, n, k + 3);
        else
            snprintf(out, cap, "%s", f);
    }
}
