// Hook point listeners (docs/12-hook-points.md), per-hero and per-campaign data.
#include "example.h"
#include <stdio.h>

static int f_kills = -1;           // a hero field: saved in the hero's save as ddx_exm_kills
#define CAMPAIGN_KILLS "exm_kills" // a campaign field: saved as ddx_exm_kills in the campaign

int example_campaign_kills(void) {
    return campaigndata_get(CAMPAIGN_KILLS);
}

// after: the game has acted. ctx->actor is the attacker, ctx->target the monster or hero that died.
static void on_kill(HookContext *ctx, void *ud) {
    (void)ud;
    if (!is_hero(ctx->actor))
        return; // monsters kill too
    unsigned guid = hero_guid(ctx->actor);
    herodata_set(guid, f_kills, herodata_get(guid, f_kills) + 1);
    campaigndata_set(CAMPAIGN_KILLS, campaigndata_get(CAMPAIGN_KILLS) + 1);
    logf_("hero %u: %d kills, campaign %d", guid, herodata_get(guid, f_kills), example_campaign_kills());
}

// A text popup over the attacker, with the game's buff popup colours and sound (type 0x15).
static void on_crit(HookContext *ctx, void *ud) {
    (void)ud;
    if (is_hero(ctx->actor))
        popup_text(ctx->actor, 0x15, ui_loc("str_ddx_example_popup"));
}

// before: the game is about to act and ctx->amount is its input. Change it (or set ctx->cancel) to change what happens.
static void on_stress(HookContext *ctx, void *ud) {
    (void)ud;
    if (!is_hero(ctx->actor) || ctx->synthetic)
        return;
    CombatContext battle;
    if (combat_context_build(&battle, ctx->actor) && battle.torch >= 75)
        ctx->amount *= 0.9f; // 10% less in the light
}

void example_kills_install(void) {
    f_kills = field_register("exm_kills"); // names are shared by every mod: prefix yours
    campaigndata_field(CAMPAIGN_KILLS);    // before a campaign loads, so its saved value is read back
    hook_on("kill", HOOK_AFTER, on_kill, NULL);
    hook_on("crit", HOOK_AFTER, on_crit, NULL);
    hook_on("stress_damage", HOOK_BEFORE, on_stress, NULL);
}
