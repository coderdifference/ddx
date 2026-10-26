# 13. Native clients: a mod with its own code

A **native client** is your mod's own compiled code, running inside the game on DDX's API. Write one when a config
isn't enough: your own UI, a new item type, data that follows the hero, or logic too involved for a hook rule. This
chapter builds one from the **DDX Example** mod ([`examples/`](examples/)), which you can copy as your starting point.

**Platforms:** macOS x86_64 (Rosetta on Apple Silicon) and Windows x64. Check each required hook in the installation log; support differs by feature.

## How a client is loaded
At start-up, before the game's own code runs, DDX:
1. finds every mod folder that has an `engine/` folder, in the game's `mods` folder and then the Workshop's, and gives
   each an index (`ddx.log`: `mods: 0 = <folder> ("<Title>")`);
2. reads every `engine/*.engine.json` (configs);
3. loads each platform's client libraries (`engine/*.dylib` or `engine/*.dll`) and calls its exported `engine_client_init` (`client <path>: loaded`, then
   `clients: N loaded, M failed`);
4. installs its own features and the game hooks: only the ones some config or client asked for.

So **everything you register, register in `engine_client_init`**. A hook point that nobody listened to at step 3 has
no game hook at all. While your init runs, DDX records your mod as the owner of whatever you register: your listeners,
wrappers, item types and settings act only in campaigns that apply your mod (and the player can change that per
campaign, without restarting).

The game hasn't started when your init runs: its rules, strings and textures aren't loaded yet. Look up symbols and
register callbacks there; read game data (`ui_loc`, `find_effect`, textures) later, from the callbacks.

## The project
Two folders: the **mod** (what you deploy and upload) and the client's **source** (never shipped).

```
DDXExample/                          the mod: deploy this folder to the game's mods folder
├─ project.xml                       Title "DDX Example"
├─ engine/
│  ├─ ddx_example.dylib              the built client (build.sh writes it)
│  ├─ ddx_example.engine.json        optional: config sections (hook rules here)
│  └─ ui/ddx_example.layout.json     optional: positions, fonts, colours your code reads
├─ localization/
│  ├─ DDXExample.string_table.xml    your strings
│  └─ DDXExample_english.loc2        compiled by tools/localization.py
└─ DDXExample_modfiles.txt           written by tools/localization.py
ddx_example_engine/                  the client's source
├─ build.sh, build_windows.sh
└─ src/example.h, example_main.c, example_kills.c, example_overlay.c
```

The headers are DDX's own: `engine_hooks/src/common.h` declares the whole API. Point `-I` at that folder (the build
scripts take `DDX_SRC`).

### Starting your own from the example
1. Copy `examples/DDXExample/` and `examples/ddx_example_engine/` and rename both (`MyMod/`, `my_mod_engine/`).
2. In `project.xml`, set `<Title>` and `<ItemDescription>`.
3. Rename the string table to `MyMod.string_table.xml` (the `.loc2` compiler names its output after the mod folder,
   so the folder name must stay the same afterwards) and give every string id your own prefix.
4. In `build.sh`, set `OUT` to `../MyMod/engine/my_mod.dylib` and `DDX_SRC` to your copy of `engine_hooks/src`.
5. In `example_main.c`, change the `log_file` name and the announcement's string id. Rename the field, campaign field
   and layout file names: every one of these is shared with other mods.

## The entry point
```c
#include "common.h"

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int engine_client_init(const EngineClientInfo *info) {
    if (!info || info->api != ENGINE_CLIENT_API) {      // built against another library version: stay off
        logf_("DDX Example: library client API %d, built for %d", info ? info->api : -1, ENGINE_CLIENT_API);
        return 1;
    }
    log_file("DDXExample");                              // our logf_ lines go to DDXExample.log beside ddx.log
    announce_mod("str_ddx_example_name", NULL, NULL);    // listed in the library's "mods built on DDX" announcement
    example_kills_install();
    example_overlay_install();
    return 0;                                            // anything else: logged, and the client stays off
}
```
- `info->api` is the client API version of the DDX that loaded you; refuse to run on another.
- `info->engine_dir` is your mod's `engine/` folder (absolute), for reading files of your own.
- `log_file(name)` gives your client its own `<name>.log` and `<name>_combat.log`, fresh each run, next to the game's
  `app.log`. Call it first.
- `announce_mod` lists your mod in the dialog DDX shows when a campaign first runs with it (or gains a new DDX mod).
  The string id is looked up later, when the dialog shows.

## Listening to hook points
A listener gets a `HookContext` describing the moment. **After** listeners observe; **before** listeners may change
the game's input (`amount`, sometimes `type`) or set `cancel`. [Chapter 12](12-hook-points.md#the-points) lists every
point and which fields it fills.

```c
static void on_kill(HookContext *ctx, void *ud) {        // after: the game has acted
    if (!is_hero(ctx->actor)) return;                    // ctx->actor is the attacker; monsters kill too
    unsigned guid = hero_guid(ctx->actor);
    herodata_set(guid, f_kills, herodata_get(guid, f_kills) + 1);
}

static void on_stress(HookContext *ctx, void *ud) {      // before: ctx->amount is what the game is about to apply
    if (!is_hero(ctx->actor) || ctx->synthetic) return;
    CombatContext battle;
    if (combat_context_build(&battle, ctx->actor) && battle.torch >= 75) ctx->amount *= 0.9f;
}

void example_kills_install(void) {
    hook_on("kill", HOOK_AFTER, on_kill, NULL);
    hook_on("stress_damage", HOOK_BEFORE, on_stress, NULL);
}
```
- `hook_on` returns false (and logs why) for an unknown point or a phase the point doesn't have.
- Pointers in the context are the game's objects, **borrowed for the call**: don't keep them.
- `synthetic` is set when a point fires from inside another listener (your listener dealt damage, which fired
  `hp_damage`). Skip synthetic points if your listener could loop.
- Listeners of all mods run in campaign order: the library's first, then the mods from the lowest in the campaign's
  list to the highest. So in a `before` phase, the top mod's change stands.

To act on the game from a listener, use the game actions (`game_damage`, `game_heal`, `game_stress`, `game_torch`,
`game_apply_effect`, `game_add_quirk`, `game_give_item`, `game_queue_loot`...): they go through the game's own code,
so resistances, popups and other mods' hook points all apply ([reference](14-api-reference.md#game-actions)).

## Saving data
DDX keeps integer fields in the game's own save files. Register the names in your init:

```c
static int f_kills;                                      // a hero field, saved in the hero's save as ddx_exm_kills
void example_kills_install(void) {
    f_kills = field_register("exm_kills");               // up to 31 characters; 64 names across all mods
    campaigndata_field("exm_kills");                     // a campaign field, saved with the campaign
}
// later: herodata_get(guid, f_kills), herodata_set(...); campaigndata_get("exm_kills"), campaigndata_set(...)
```
| Kind | API | Saved in |
|---|---|---|
| per hero | `field_register`, `herodata_get` / `_set` / `_has` (by `hero_guid`) | the hero's save |
| per monster | `monsterdata_get` / `_set` (by the monster) | the battle's save |
| per item | `itemdata_enable` (in init), `itemdata_get` / `_set` | the item's save (trinket-like items that never stack) |
| per campaign | `campaigndata_field` (in init), `campaigndata_get` / `_set` | `persist.game.json` |

A save made with your mod and played without it loses these fields the next time the game saves.

## Wrapping a game function
When no hook point fits, wrap a function from the [catalogue](15-game-hooks.md). Your wrapper has the game function's
signature and calls on through `next`, then does its own work:

```c
static void **g_raid_display;                            // RaidDisplay::s_Instance: the expedition screen
static void (*g_pre_panel)(void *display);               // the next wrapper inside ours, or the game

static void h_pre_panel(void *display) {
    g_pre_panel(display);                                // always call on: other mods wrap this function too
    if (!g_raid_display || display != *g_raid_display) return;
    char kills[16], line[128];
    snprintf(kills, sizeof kills, "%d", example_campaign_kills());
    const char *keys[] = {"kills"}, *vals[] = {kills};
    ui_format(line, sizeof line, "str_ddx_example_overlay", keys, vals, 1);   // "Kills this campaign: {kills}"
    UiLayout *l = ui_layout_mod("ddx_example.layout.json");
    Vec2 centre = ui_screen_centre();
    Vec2 offset = ui_layout_vec2(l, "overlay.pos", (Vec2){0, -500});
    ui_text_anchored((Vec2){centre.x + offset.x, centre.y + offset.y}, line,
                     ui_layout_str(l, "overlay.font", "tooltip"), ui_layout_str(l, "overlay.colour", "notable"), 1, 0);
}

void example_overlay_install(void) {
    g_raid_display = (void **)sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    if (!g_raid_display || !game_hook_wrap("RaidDisplay::PrePanelShowInternal", (void *)h_pre_panel, (void **)&g_pre_panel))
        logf_("DDX Example: expedition screen hook unavailable, no overlay");
}
```
- `game_hook_wrap` returns false (and leaves `next` NULL) for an unknown id, or when the game's bytes don't match
  (another game version). Check it, and turn your feature off rather than crash.
- The chain is relinked whenever a campaign loads or closes, so `next` can change: always call through the variable.
- The top mod's wrapper is outermost: it runs first and has the last word on the result.
- `sym(mangled)` finds any game function or global by its mangled name (the macOS binary exports them; `nm` the game
  binary to look one up). Reading the game's objects means knowing their layout: [Extending](09-extending-the-engine.md#object-layout).

## Strings, layouts and art
- **Strings** go in your `localization/<Mod>.string_table.xml`; compile with
  `python3 tools/localization.py <Mod>.string_table.xml` from that folder. `ui_loc(id)` looks one up (the id itself
  when missing), `ui_format` fills `{key}` placeholders, `loc_exists` tests for an optional one. Colour markup
  `{colour_start|<colour id>}...{colour_end}` works in drawn text.
- **Layouts** are JSON files in `engine/ui/` that your code reads by name (`ui_layout_mod("my.layout.json")`, then
  `ui_layout_num` / `_vec2` / `_str` with a fallback). They reload when the file changes, so you can move things while
  the game runs. Another mod can override any name in your file with its own `engine/ui/<same file>`; the DDX
  package's files (`dialog.layout.json`) are the last fallback.
- **Art** is any path under your mod (`panels/my_button.png`): `ui_texture`, `ui_image_path`. A mod's files override
  the game's by path, as in any DD1 mod.
- **Fonts**: a `fonts/*.fonts.darkest` in your mod (vanilla syntax) adds font ids `ui_font` and the text calls can use.

## Building and running
```sh
docs/examples/ddx_example_engine/build.sh                      # writes DDXExample/engine/ddx_example.dylib
cp -R docs/examples/DDXExample "<game>/Contents/Resources/data/mods/"
engine_hooks/run_game.sh -enabledebuginput -skiptutorial   # Steam must be running
```
The build compiles for x86_64 with `-undefined dynamic_lookup`: DDX's functions stay unresolved in your client and are
found in the injected library when it loads. Then, in game, add your mod (and the DDX package) to a campaign's mod
list and load the campaign.

Check the logs in the game's `data` folder:
- `ddx.log`: `mods: N = <your folder>`, `client .../engine/ddx_example.dylib: loaded`, each game hook `hooked at
  0x...`, and at a campaign load `mods: campaign loaded: ...` with your mod's place in its list.
- `DDXExample.log`: your own `logf_` lines.
- `ddx_combat.log`: a line for every hook point that fires (all of them with `"debug": true` in any config).

Typical failures: `can't load: ...` (built for the wrong architecture, or a missing symbol: a DDX function your copy of
`common.h` has but the installed DDX doesn't), `no engine_client_init, not a client` (the export is missing or not
visible), `init failed (1)` (your init returned non-zero: see your log).

**Windows:** `build_windows.sh` links against DDX's import library `libddx.dll.a` and writes
`<name>.dll`; ship both binaries in `engine/`, each platform loads its own. Wrap UI code that DDX hasn't ported yet in
`#ifndef _WIN32`, as an event-log client does.

## Testing without the game
Keep game-free logic (parsing, calculations and formatting) in isolated functions. Test those with stubs for DDX services. Anything that touches the game is tested in game.

## Shipping
- Ship the mod folder with the built `.dylib`; never the source, `_reference/` files, or the DDX binary itself.
- Mark the DDX package as a required item on your Workshop page; players need it applied to their campaign too.
- Say in your description that the mod needs the game started with DDX, and what happens to a save without it.
- Your client's API version is `ENGINE_CLIENT_API` from the `common.h` you built against. When DDX raises it, rebuild.

## Where to look next
- [API reference](14-api-reference.md): everything you can call.
- [Hook points](12-hook-points.md): every point and its context.
- [Game hooks](15-game-hooks.md): the functions you can wrap.
- [The UI library](11-ui-library.md): panels, buttons, tooltips, dialogs, item types, equipment slots.
