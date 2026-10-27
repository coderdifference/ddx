// ddx entry point. macOS: runs when the library is injected (DYLD_INSERT_LIBRARIES), before the game
// starts. Windows: the launcher (win/ddxlaunch.c) loads the DLL and calls ddx_init at the game's entry
// point, before the game's own start-up code.
#include "common.h"

static void ddx_start(void) {
    log_open();
    combat_log_open();
    logf_("ddx loaded");
    crash_report_install();
#ifdef _WIN32
    if (!win_game_supported())
        return;
#endif
    if (!game_init()) {
        logf_("missing symbols, hooks off");
        return;
    }
    mods_install();         // the mods built on the library, before their configs and clients
    ugc_install();          // independent of any mod
    localization_install(); // the string lookups (clients: loc_on_redirect)
    ui_invgrid_install();   // before its clients (stash_view.c, item_sale.c ask if it's in)
    inv_slots_install();
    ui_hotspot_install();
    config_load_all();
    combat_log_install(); // hook point listeners register here, before any *_install
    hook_rules_register();
    game_fixes_install();    // listeners, before any *_install
    announce_install();      // listeners (raid_ready), before any *_install
    quirk_notices_install(); // listeners (quirk_remove), before any *_install
    clients_load_all();      // mods' native clients (clients.c): register with the features below
    effects_install();
    herodata_install();
    monsterdata_install();
    itemdata_install();
    campaigndata_install();
    skills_install();
    riposte_install(); // before events_install (its turn_start listener)
    anim_install();
    popups_install();
    buff_origin_install();
    crit_info_install();
    act_outs_install(); // before events_install: its battle_start / raid_start listeners
    events_install();
    stats_install();
    tooltip_install();
    items_install();
    item_types_install();
    item_tips_install(); // after the types (their tooltip callbacks)
    item_draw_install();
    item_sale_install(); // before stash_view_install (its panel and sale clients)
    stash_view_install();
    equip_slots_install(); // after its clients (registered by native clients)
    cooldown_install();
    combat_hooks_install();
    value_hooks_install();
    expedition_hooks_install();
    prop_hooks_install();
    hero_hooks_install();
    loot_hooks_install();
    barks_install();
    combat_events_install();
    paging_install();
    guild_install();
    sheet_install();
    ui_draw_install();
    ui_text_install();
    ui_tooltip_install();
    ui_panel_install();
    ui_confirm_install();
    ui_input_install();
    modmenu_install();
    story_intro_skip_install();
    ui_install();
}

#ifdef _WIN32
__declspec(dllexport) void ddx_init(void) {
    static bool done;
    if (!done) {
        done = true;
        ddx_start();
    }
}
#else
__attribute__((constructor)) static void ddx_init(void) {
    ddx_start();
}
#endif
