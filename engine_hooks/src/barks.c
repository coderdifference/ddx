// The bark hook point (hooks.c).
//   bark  RaidUI::Screen::RaidDisplay::QueueBark(actor, c_string id, bool, bool, ActOut::SourceIdentifier): nearly every
//         expedition bark goes through it (stress, afflictions and virtues, act-outs, monsters, camping, the torch,
//         ripostes; QueuePartyBark too). After its early returns (a corpse, an actor that can't bark now) it looks the
//         id up (Localization::LocalizeStringFromId, which picks a variation), then always queues a bark action
//         (BaseAction type 0x15) on the display's action deque (+0x30b8). The hook captures that lookup's text
//         (loc_capture_begin) and fires after the original when there was one: text: the text shown; type: the id;
//         source: the bark type, Actor::GetBarkType(source) (0 neutral, 1 afflicted: the actor's affliction +0x11b8,
//         2 virtued: its virtue +0x11bc, 3 cursed: a vtable check and source 2 or 3), whose colours are
//         colours.darkest bark_neutral, bark_affllicted (sic), bark_virtued and bark_cursed.
//         Curio trigger barks too: QueueTriggerCurioBark(actor, trigger type, trigger id, prop, bool) (the quirk and
//         affliction curio barks go through it) looks its text up through FindCurioTriggerLocalizedString (the
//         "<class>+trigger_curio_<type>_<id>_<curio>" fallbacks, then LocalizeStringFromId) and queues the same bark
//         action with an ActOut::SourceIdentifier of kind 3 (+0xac), so GetBarkType reads it as for QueueBark. Its
//         point: type: "trigger_curio_<type>_<id>"; prop: the curio.
// bark_queue(actor, id): a client's bark by string id, through QueueBark (so the bark point and the log see it too).
// QueueBark returns the bark's duration (a float; 0 when it didn't queue one), which the hook passes on.
// Windows: the same functions (the SourceIdentifier comes by hidden pointer, as a C struct by value does there);
// GetBarkType is inlined (a stand-in in syms_win.c).
#include "common.h"
#include <stdio.h>
#include <string.h>

static const char *const k_types[] = {"neutral", "afflicted", "virtued", "cursed"};
static void **g_raid;
static int (*g_bark_type)(const void *actor, SourceId source);
static float (*o_queue)(void *display, void *actor, const char *id, bool b1, bool b2, SourceId source);

static float h_queue(void *display, void *actor, const char *id, bool b1, bool b2, SourceId source) {
    char text[1024];
    loc_capture_begin(text, sizeof text);
    float duration = o_queue(display, actor, id, b1, b2, source);
    loc_capture_end();
    if (!text[0] || !actor)
        return duration;
    int type = g_bark_type ? g_bark_type(actor, source) : 0;
    HookContext c = {.raid = g_raid ? *g_raid : NULL,
                     .actor = actor,
                     .type = id,
                     .text = text,
                     .source = type >= 0 && type < 4 ? k_types[type] : k_types[0],
                     .kind = type};
    hook_emit("bark", HOOK_AFTER, &c);
    return duration;
}

bool bark_queue(void *actor, const char *id) {
    static void **display;
    static float (*queue)(void *display, void *actor, const char *id, bool b1, bool b2, SourceId source);
    if (!display)
        display = (void **)sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    if (!queue)
        *(void **)&queue =
            sym("_ZN6RaidUI6Screen11RaidDisplay9QueueBarkEP5Actor8c_stringbbN6ActOut16SourceIdentifierE");
    if (!actor || !id || !id[0] || !display || !*display || !queue)
        return false;
    SourceId none = {{3}};                         // as Battle::PostSkillSideEffects' skill barks
    queue(*display, actor, id, false, true, none); // the function's start: the bark point's hook runs too
    return true;
}

static void (*o_curio)(void *display, void *actor, const char *trigger, const char *id, const void *prop, bool b);

static void h_curio(void *display, void *actor, const char *trigger, const char *id, const void *prop, bool b) {
    char text[1024];
    loc_capture_begin(text, sizeof text);
    o_curio(display, actor, trigger, id, prop, b);
    loc_capture_end();
    if (!text[0] || !actor)
        return;
    SourceId source = {{3}}; // the curio bark action's source (+0xac)
    int type = g_bark_type ? g_bark_type(actor, source) : 0;
    char bark_id[160];
    snprintf(bark_id, sizeof bark_id, "trigger_curio_%s_%s", trigger ? trigger : "", id ? id : "");
    HookContext c = {.raid = g_raid ? *g_raid : NULL,
                     .actor = actor,
                     .prop = (void *)prop,
                     .type = bark_id,
                     .text = text,
                     .source = type >= 0 && type < 4 ? k_types[type] : k_types[0],
                     .kind = type};
    hook_emit("bark", HOOK_AFTER, &c);
}

void barks_install(void) {
    if (!hook_wanted("bark"))
        return;
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    *(void **)&g_bark_type = sym("_ZNK5Actor11GetBarkTypeEN6ActOut16SourceIdentifierE");
    static const uint8_t prologue[] = PROLOGUE_SUB32(0x88, 0x02); // sub rsp,0x288
    *(void **)&o_queue =
        install_hook("RaidDisplay::QueueBark (bark)",
                     sym("_ZN6RaidUI6Screen11RaidDisplay9QueueBarkEP5Actor8c_stringbbN6ActOut16SourceIdentifierE"),
                     (void *)h_queue, prologue, sizeof prologue);
    if (!o_queue)
        logf_("barks: QueueBark not hooked, no bark point");
    static const uint8_t curio_prologue[] = PROLOGUE_SUB32(0x88, 0x01); // sub rsp,0x188
    *(void **)&o_curio =
        install_hook("RaidDisplay::QueueTriggerCurioBark (bark)",
                     sym("_ZN6RaidUI6Screen11RaidDisplay21QueueTriggerCurioBarkEP5ActorPKcS5_PK4Propb"),
                     (void *)h_curio, curio_prologue, sizeof curio_prologue);
    if (!o_curio)
        logf_("barks: QueueTriggerCurioBark not hooked, no curio barks");
}
