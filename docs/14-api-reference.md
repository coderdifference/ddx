# 14. API reference

Everything a native client can call, grouped by area. The declarations, with full comments, are in
`engine_hooks/src/common.h`; this chapter says what each one is for and points to the chapter that explains it.

**Conventions.** "Register" functions must be called from `engine_client_init` unless marked *any time* (DDX installs
its game hooks after the clients' inits, and only for what was registered). A registration belongs to your mod and
acts only in campaigns that apply it. Limits ("up to 4") count every mod's registrations together. Game objects are
`void *` (an `Actor`, `Hero`, `Inventory::Item` as `uint8_t *`...); pointers handed to callbacks are borrowed for the
call. **macOS** marks APIs not yet available on Windows (check the platform guards and installation logs for current coverage).

Contents: [Client and mods](#client-and-mods) · [Logging](#logging) · [Hook points](#hook-points) ·
[Game hooks](#game-hooks) · [Game objects](#game-objects) · [Combat](#combat) · [Game actions](#game-actions) ·
[Saved data](#saved-data) · [Effects and buffs](#effects-and-buffs) · [Text, popups and barks](#text-popups-and-barks) ·
[Stats](#stats) · [Items](#items) · [UI](#ui) · [Utilities](#utilities) · [Class configs](#class-configs)

## Client and mods
| API | Does |
|---|---|
| `int engine_client_init(const EngineClientInfo *info)` | **you export this.** `info->api` (compare with `ENGINE_CLIENT_API`), `info->engine_dir` (your mod's `engine/`). Return 0, else the client stays off |
| `mods_current()` | the mod registering now (yours, during your init); -1 the library |
| `mods_find(path)` | the mod whose folder holds `path` (-1: none) |
| `mods_count()`, `mods_title(mod)`, `mods_folder(mod)` | every mod with an `engine/` folder |
| `mods_campaign()` | a campaign's mod list is known (a campaign is loaded) |
| `mods_active(mod)`, `mods_pos(mod)` | the loaded campaign applies the mod; its place in the campaign's list (0 = top, -1 not applied) |
| `mods_by_priority(out, max)` | the active mods, highest priority first |
| `mods_package_applied()` | the DDX package is applied to the campaign |
| `mods_on_campaign(fn)` | `fn` runs after each campaign load's list is read and when the campaign closes (up to 16) |
| `mods_generation()` | changes at each campaign load and close: cache keys |
| `ModsList`, `mods_list_add`, `mods_list_order`, `mods_list_first`, `mods_list_active`, `MODS_EACH` | campaign ordering for a registry of your own (other mods registering with *your* client) |
| `announce_mod(name_id, font, colour)` | list your mod in DDX's announcement dialog (string id in your table; NULLs: the list's font and colour) |

## Logging
| API | Does |
|---|---|
| `log_file(name)` | your client's own `<name>.log` / `<name>_combat.log` (call first in your init) |
| `logf_(fmt, ...)` | a line in your log (`ddx.log` before `log_file`) |
| `dbgf_(fmt, ...)` | the same, only with `"debug": true` in any config (`g_debug`) |
| `combat_logf(fmt, ...)` | a line in your combat log |

## Hook points
Every point, its phases and context fields: [chapter 12](12-hook-points.md).

| API | Does |
|---|---|
| `hook_on(point, HOOK_BEFORE / HOOK_AFTER, callback, userdata)` | listen to a point; `"*"`: every point installed for someone else |
| `hook_on_all(phase, callback, userdata)` | every point, and installs all of them (a full log) |
| `HookContext` | `point`, `phase`, `raid`, `battle`, `area`, `prop`, `inventory`, `actor`, `attacker`, `target`, `skill`, `item`, `attack`, `target_data`, `amount`, `result`, `kind`, `source`, `type`, `skip`, `refuse`, `hit`, `crit`, `synthetic`, `cancel`, `text` |
| `hook_emit(point, phase, ctx)` | fire a point (library modules; false: a before listener cancelled) |
| `hook_wanted(point)`, `hook_any_wanted(points, n)` | something listens |
| `hook_point_exists`, `hook_point_count`, `hook_point_name(i)`, `hook_point_about(i)`, `hook_point_has_phase(i, phase)`, `hook_phase_name` | the registry, for tools and logs |

## Game hooks
The catalogue and each function's signature: [chapter 15](15-game-hooks.md).

| API | Does |
|---|---|
| `game_hook_wrap(id, fn, &next)` | wrap a catalogued game function; `fn` has its signature and calls `next` |
| `game_hook_site(id, fn)` | observe the game's registers (`const Regs *`) at a catalogued mid-function site (macOS) |
| `game_site_regs()` | inside a site observer: the game's general registers there, writable (the game goes on with them) |
| `game_site_xmm(i)` | inside a site observer, Windows: the game's `xmm<i>` there (4 floats), which the observer may change; NULL on macOS |
| `sym(mangled)` | a game function or global by mangled name (no leading underscore), NULL if missing |
| `call_site_return(fn, off, target)` | the return address of the `call` at `fn + off`, checked: tell one caller apart in a filter |
| `install_hook`, `install_hook_rip`, `install_cmp_site`, ... | **library code only** (one hook per function): [Extending](09-extending-the-engine.md) |

## Game objects
| API | Does |
|---|---|
| `is_hero(actor)`, `is_monster(actor)` | what an actor is |
| `hero_guid(hero)`, `monster_guid(monster)` | the game's ids (keys for saved data) |
| `hero_class_hash(hero)`, `skill_id_hash(skill)`, `name_hash(s)` | ids as the game hashes them (`name_hash("crusader")`) |
| `hero_has_quirk(hero, glob)`, `hero_has_disease(hero, glob or NULL)`, `hero_has_item(hero, glob)` | quirks, diseases, equipped items (trinkets and registered slots) |
| `actor_stat(actor, stat)` | a combat stat with buffs: `damage_low`, `damage_high`, `defense_rating`, `protection_rating`, `speed_rating`, `attack_rating`, `crit_chance`, `max_hp`, `riposte_on_hit_chance`, `riposte_on_miss_chance` (NAN: unknown) |
| `actor_has_buff(actor, glob)`, `actor_count_buffs(actor, glob)`, `actor_marked(actor)` | buffs by id; marked (1, 0, -1 unknown) |
| `actor_skill_name(actor, skill, out, n)` | a skill's name as the combat UI shows it |
| `skills_usable(hero, out, max)` | the skills the hero can use now (every page of a paged bar) |
| `skill_resolution_performer()`, `_skill()`, `_target_count()`, `_target(i)` | inside a skill use (`Battle::Actor_DoSkill`): who, what and every target |
| `skill_use_id()`, `skill_last_used(actor, &use_id)` | the skill use being performed now; an actor's last skill (queued effects land later) |
| `prop_id(prop)`, `prop_title(prop)` | a curio's, obstacle's or door's id (`crate`) and display name (Crate) (macOS) |
| `find_effect(name)` | an effect from the game's or a mod's `.effects.darkest` (NULL: none) |
| `ITEM_AMOUNT`, `ITEM_TYPE_STRING`, `ITEM_TYPE_HASH`, `ITEM_ID`, `ITEM_ID_HASH` | offsets into an `Inventory::Item` |

## Combat
| API | Does |
|---|---|
| `combat_context_build(&ctx, self)` | the battle now: round, torch, every hero and monster (`CtxActor`: class, rank, HP, stress, Death's Door, trait, resolve level), `ctx.self` (macOS) |
| `combat_context_lookup`, `combat_context_call` | the context's formula names and calls, for `expr_eval_fn` ([12](12-hook-points.md#combat-context)) |
| `combat_apply_followup_hit(source, target, damage_scale, crit, filter, ud)` | a second hit through the game's hit resolver, from a combat point's context; `filter` picks which skill side effects replay (macOS) |
| `combat_apply_side_effect(source, effect)` | an effect on the hit's target, as the skill's own side effects (chance and resistance apply) |
| `combat_apply_effect(source, performer, target, effect)` | the same with any performer and target |
| `buff_stack_limit(id, max)` | at most `max` instances of a buff id from one kind of source; a new one replaces the oldest (macOS) |
| `act_out_rule_register(&ActOutRule)` | weigh afflicted heroes' turn-start act-outs from the battle's state ([12](12-hook-points.md#act-out-rules)) |
| `act_out_type_register(&ActOutTypeDef)` | a new act-out type that trait and quirk files can list ([3](03-config-reference.md#new-act-out-types)) |
| `act_out_heal`, `act_out_side_effect`, `act_out_targets(name, ...)` | helpers for an act-out's outcome |
| `riposte_on(scope, skill)` | observe DDX's ripostes (config `riposte`) and the skill they use |
| `config_act_outs()`, `config_riposte()` | the merged `act_outs` and `riposte` config sections |

## Game actions
Through the game's own code, so its modifiers, popups and every mod's hook points apply. False: not done. macOS.

| API | Does |
|---|---|
| `game_damage(actor, amount, type, source, attacker)` | HP damage; `type` / `source` are the game's ids (`"bleed"`, `"trap"`; NULL: the first) |
| `game_heal(actor, amount, healer, source)` | HP heal |
| `game_stress(actor, amount, by, source)` | stress; negative heals |
| `game_torch(change)` | torchlight, signed |
| `game_apply_effect(performer, target, effect)` | any `.effects.darkest` effect, through the side effect queue |
| `game_add_quirk(hero, id)`, `game_remove_quirk(hero, id)` | as a curio does (resists apply) |
| `game_announce(text)` | the dungeon screen's centre announcement, as a curio's "Quirk Removed!" (false outside the dungeon screen) |
| `game_give_item(type, id, amount)` | to the party inventory, else the estate |
| `game_inventory_add(inventory, type, id, amount)` | into an `Inventory::System` (the loot window's: `loot_generated`'s `inventory`) |
| `game_queue_loot(loot_type, code)` | a loot table code into the expedition's loot |
| `item_store(item)` | a copy of an item into the party inventory, else the estate's storage |
| `game_rng()`, `game_random()` | the game's prng; `0 <= x < 1` from it |
| `actor_remove_buffs(actor, patterns, n)`, `actor_remove_buffs_by_source_ids(...)` | remove buffs by id glob, or by source |
| `audio_play_file(path, AUDIO_SFX / AUDIO_MUSIC / AUDIO_VOICE)` | a WAV, OGG, FLAC or MP3 from your mod, at the game's volume for that channel |

## Saved data
| API | Does |
|---|---|
| `field_register(name)`, `field_find(name)` | a hero field's index (names under 32 characters, 64 across all mods) |
| `herodata_get(guid, f)`, `herodata_set`, `herodata_has`, `herodata_foreach(fn, ctx)` | per hero (by `hero_guid`), saved as `ddx_<field>` |
| `herodata_on_restore(fn)` | after a hero's fields are restored from a save (up to 4) |
| `monsterdata_get(monster, f)`, `monsterdata_set` | per monster in a battle, saved as `ddx_actor_<field>` |
| `itemdata_enable()` | your client needs item data (call in init) |
| `itemdata_id`, `itemdata_ensure_id`, `itemdata_get(item, f)`, `itemdata_set`, `itemdata_has` | per item (items that never stack), saved as `ddx_iid` and `ddx_<field>` (macOS) |
| `itemdata_on_save(fn)` | before each item is saved (up to 4) |
| `campaigndata_field(name)` | declare a campaign field (in init; up to 32, names under 40 characters) |
| `campaigndata_get(name)`, `campaigndata_set`, `campaigndata_has` | per campaign, saved in `persist.game.json` as `ddx_<name>` (macOS) |

## Effects and buffs
| API | Does |
|---|---|
| `effects_on_prefix(prefix, fn)` | `fn(name, target, performer)` for every applied effect whose name starts with `prefix`: effects as commands (give them no payload; up to 4) |
| `effects_on_patch(fn)` | change an effect's float fields for one application (`EffectPatch`: offset, value) |
| `effects_on_swap(fn)` | replace an effect with another for one application |
| `effects_on_apply(enter, leave)` | observe effects as they apply (target, DoT rate, duration and its type) |
| `buff_source_effect(target, effect, popup, label)` | name the next application of an effect: its popups and the stat tooltips' source label (macOS) |
| `buff_origin_on(fn)` | name buffs for the character sheet's stat tooltips |
| `buff_origin_label(actor, buff, out, cap)` | a buff's source name as the sheet shows it |
| `stat_label_on(fn)` | relabel a stat tooltip line by the game's label id (`str_stat_base_weapon`) |
| `duration_text(amount, type, out, cap)` | a buff duration in words (`" (2 rds)"`); `DURATION_*` types |

Library effects any mod's data can use: `remove_quirk_<quirk id>`, `ddx_set_<field>_<n>`, `ddx_add_<field>_<n>`
(`m1` = -1), and effects named after config actions ([Concepts](02-concepts.md#actions-effects-that-change-fields)).

## Text, popups and barks
| API | Does |
|---|---|
| `ui_loc(id)`, `ui_loc_string(id)` | a string table entry (any mod's), or the id itself |
| `loc_exists(id)` | the entry exists (for optional strings) |
| `ui_format(out, cap, id, keys, vals, n)` | `ui_loc(id)` with each `{key}` filled |
| `loc_on_redirect(fn)` | look up another id instead (up to 4) |
| `loc_capture_begin(out, cap)`, `loc_capture_end()` | capture the text of the lookups between them |
| `ui_text_substitute(id, fn)` | a string drawn as another (or hidden) wherever the GUI draws it (up to 16) |
| `ui_on_text(filter)` | every `GUI_Text` call: keep, skip or replace (up to 8) |
| `tooltip_encode_colours(s, cap)` | `{colour_start|x}...{colour_end}` markup into the game's encoding |
| `popup_text(actor, type, text)` | a text popup on an actor with a popup type's colours and sound (`0x15` buff, `0x21` cured...) |
| `popup_on_buff(fn)` | name the Buff! / Debuff! / Riposte! popup of a status icon |
| `popup_on_add(fn)` | observe every popup (`PopupEvent`) |
| `popup_on_line(fn)`, `popup_line(popup, out)` | a popup in words, for text that reports popups (an expedition log) |
| `popup_type_id(type)` | a popup type's id (`damage`, `bleed`, `buff`) |
| `bark_queue(actor, id)` | a bark by string id, as the game's skill barks (macOS) |
| `items_on_display_name(fn)`, `items_display_name(item, ...)` | an item's name and colour id in words, for any text that names items |

## Stats
| API | Does |
|---|---|
| `stats_on_calc(before, after)` | around each `Hero::CalculateDerivedStatsInternal`: change a hero's stats (up to 4) |
| `stats_rows_begin(hero)`, `stats_rows_end(hero)` | the same override outside the calculation (reading the weapon row in a tooltip) |

## Items
New item types, drawing, tooltips, prices: [UI library](11-ui-library.md) from "Item drawing" on.

| API | Does |
|---|---|
| `item_type_register(&ItemTypeDef)` | a new `inventory_item` type: where it goes at the end of an expedition, which trinket checks it joins, pickup sound, `can_use`, `icon`, `compose`, `tooltip`, `describe`, `price`, `salvage_quote`, `salvage` |
| `item_type_find(type_hash)`, `item_types_count()`, `item_type_at(i)` | registered types |
| `item_type_view(type, tooltip, describe, ud)` | tooltip and description content for one of the game's own types |
| `item_content_line`, `item_content_add`, `item_content_string`, `item_content_lines_width` / `_height` | build an `ItemContent` (head and body regions, text and image layers) |
| `item_describe(item, hero, width)`, `item_content_draw(c, regions, pos, width)`, `item_content_size` | an item's description, drawn in your panel |
| `item_is_equipped(q, item)`, `hero_equipped(hero, out, max)` | what the hero wears |
| `item_draw(item, hero, pos, size, place)`, `item_draw_note(item, tex, place)` | draw an item as the grids do (layers, sparkle) |
| `item_tips_place(fn, ud)` | where every item tooltip goes |
| `ui_item_tooltip(actor, item, pos)` | show an item's tooltip as the inventories do |
| `items_on_use(rule)` | who may use an item, in an expedition and in town (up to 4) |
| `items_town_hero(fallback)`, `items_active_hero()` | the hero town item marks are for |
| `item_drag_kind`, `item_drag_item`, `item_drag_mask`, `item_drag_current`, `item_drag_hand_back` | item drags: what is dragged, from where, and what goes back |
| `equip_slot_kind_register(&EquipSlotKind)` | a new equipment slot kind on the character sheet, hero panel and embark screen (up to 8) |
| `equip_slots_layout(l, key)`, `equip_slots_active(fn)`, `equip_slots_on_show(fn)` | where slots are drawn, when, and who is shown |
| `equip_slots_items(hero, out, max)`, `equip_slots_unequip(hero, into, want, ud)` | the hero's items in registered slots; empty them |
| `stash_tab_register(&StashTabDef)` | a tab of the estate stash with its own sorts and Unequip all button |
| `stash_view_layout`, `stash_view_active`, `stash_view_on_panel`, `stash_view_on_sell`, `stash_view_tab()` | the stash's layout, when the tabs apply, its draw and sale, the open tab |
| `stash_key_trinket_rarity`, `stash_key_trinket_class` | the game's sorts, for your tabs |
| `item_sale_layout(l, key)`, `item_sale_prompting()` | the salvage prompt's look; whether it shows this frame |
| `inv_slots_provider(config_type, extra)`, `inv_slots(config_type)` | more slots in every inventory of a config type |

## UI
Explained, with examples: [the UI library](11-ui-library.md).

**Drawing** (`ui_draw.c`)
| API | Does |
|---|---|
| `ui_texture(path, tex)`, `ui_texture_size(tex, &size)` | load a picture by path (kept), its size |
| `ui_image(pos, tex)`, `ui_image_sized`, `ui_image_path(pos, path)`, `ui_image_now` | draw it (in the GUI's order; `_now` at once, inside another draw) |
| `ui_sprite(pos, sprite)`, `ui_trinket_sparkle(kind, index)` | a Spine sprite; the game's trinket sparkle |
| `ui_button(pos, tex, id, enabled, on_click, data, n)`, `ui_button_sized`, `ui_button_keep`, `ui_params_free` | a clickable picture with the game's hover and click sounds |
| `ui_image_button(params, click)`, `ui_image_button_rect`, `ui_on_image_button(before, after)` | the game's `GUI_ImageButton`: draw one, or change every one drawn |
| `ui_drop_target_init(t, on_drop, ctx)`, `ui_drop_target(t, accepts, pos, size)`, `ui_contexts(drag, drop, pos, size)` | a rect things can be dropped on |
| `ui_texture_redirect(fn)` | load another file when the game loads a texture (up to 8) |
| `ui_colour(rgba)`, `ui_anchor_point(anchor, size)`, `ui_function(out, call, data, n)` | the GUI colour; a rect's anchor point; a `std::function` that calls you |

**Text and fonts**
| API | Does |
|---|---|
| `ui_text(pos, s, font, colour)`, `ui_text_anchored(pos, s, font, colour, anchor, width)` | text with a font id and colour id |
| `ui_rich_text(pos, s, font, colour, width, icon, gap, centred, draw)`, `ui_rich_has_icons` | text with `{icon|path}` and `{currency|id}` pictures |
| `ui_font(id)`, `ui_text_width(font, s)`, `ui_text_height(font, s, width)` | fonts (a mod's `fonts/*.fonts.darkest` too) and measuring |
| `ui_text_lines`, `_now`, `_height`, `_width`, `ui_colour_hvec4(id)` | lines of text drawn as a tooltip draws them |

**Tooltips** (`ui_tooltip.c`)
| API | Does |
|---|---|
| `ui_tooltip_params_init(tip)`, `ui_tooltip_params_free(tip)`, `UI_TIP_SIZE` | an empty `GuiTooltipParameters` |
| `ui_tooltip_in_panel(tip)` | a tooltip drawn as part of your panel |
| `ui_tooltip_lines(tip, first, count, pos, width)`, `_height` | a tooltip's lines as panel content |
| `ui_tooltip_on_show(fn)` | before the game shows any tooltip (up to 4) |
| `ui_tooltip_content_line(line, size, draw, ud)` | a tooltip line you draw yourself |
| `ui_tooltip_line_icons`, `ui_tooltip_badge`, `ui_tooltip_inline_badge`, `ui_badge_now` | icons and badges on tooltip lines |
| `ui_draw_callback(fn, data, n)`, `ui_on_frame_end(fn)` | code in the panel's draw order; once a frame after the tooltips |

**Input** (`ui_input.c`)
| API | Does |
|---|---|
| `ui_mouse(UI_MOUSE_LEFT / _RIGHT / _MIDDLE)` | `down`, `pressed`, `released`, positions and modifiers |
| `ui_mouse_pos()`, `ui_mouse_local()`, `ui_mouse_in(pos, size)`, `ui_click(button, slop)` | where the mouse is; a click in a rect |
| `ui_modifiers()`, `ui_ctrl_pressed()`, `ui_ctrl_click(slop)` | `UI_MOD_SHIFT`, `_CTRL`, `_ALT`, `_CMD` (Ctrl-click is a right click on macOS) |
| `ui_mouse_wheel()`, `ui_mouse_wheel_take()`, `ui_frame()` | the wheel; take it; frames drawn |

**Panels, scrolling and dialogs**
| API | Does |
|---|---|
| `ui_clip_push(pos, size)`, `ui_clip_pop()` | clip drawing and input to a rect |
| `ui_scrollpanel_begin(p, l, key, type, pos, size)`, `ui_scrollpanel_end(p, h)`, `ui_scrollpanel_reset`, `ui_scroll_type_parse`, `ui_scroll_rows` | a scrolling panel (arrows or scroll bar, eased) |
| `ui_scrollbar(...)` | the game's scroll bar alone |
| `ui_panelbox_begin`, `_part`, `_part_end`, `_centre`, `_centre_end` | a box of anchored panels around a scrolling centre |
| `ui_dialog_frame(l, style, pos, size)` | a framed box at any size (the package's `dialog.layout.json` styles) |
| `ui_dialog_place(place, point, size)`, `ui_place_parse`, `ui_screen_centre()`, `ui_screen_bounds(&tl, &br)` | where a dialog goes; the screen in the current transform |
| `ui_confirm(text, opts, n, data, len)`, `ui_confirm_yes_always_no`, `ui_confirm_styled(section, ...)`, `ui_confirm_lines(name, fn)`, `ui_confirm_busy()` | question dialogs with your own buttons |
| `ui_on_hotspot(before, after)`, `ui_hotspot_caller()`, `ui_hotspot_ready()` | the game's invisible clickable rects (*any time*) |
| `ui_invgrid_on(begin, end)`, `ui_invgrid_scroll(config_type, l, key)`, `ui_invgrid_scroll_height`, `ui_invgrid_reset`, `ui_invgrid_ready()` | every inventory grid: observe its draw, scroll it |

**Layout files** (`ui_layout.c`)
| API | Does |
|---|---|
| `ui_layout_mod(file)` | the active mods' `engine/ui/<file>`, highest priority first, chained per name, then the DDX package's |
| `ui_layout_load(path)`, `ui_layout_chain(l, next)` | a layout by absolute path; chain two |
| `ui_layout_num`, `ui_layout_vec2`, `ui_layout_str` (with a fallback), `ui_layout_has`, `ui_layout_keys` | read names (`"anvil.pos"`); files reload when changed |

## Utilities
| API | Does |
|---|---|
| `json_parse(text, err, errlen)`, `json_get(obj, key)`, `json_free(v)` | a small JSON reader (`JVal`) |
| `expr_eval(text, lookup, ctx, &out)`, `expr_eval_fn(...)` | formulas and conditions: numbers, `70%`, names, `+ - * /`, comparisons, `&& || !`, calls |
| `game_data_dir(out, n)`, `workshop_dir(out, n)` | the game's `data` folder; Steam's Workshop folder for DD |
| `mod_engine_dirs(fn, ud)`, `library_engine_dir(out, n)` | every mod's `engine/` folder; the DDX package's |

## Class configs
Read-only queries of the loaded `*.engine.json` class configs, for clients that extend them: `config_for_hero`,
`config_for_class_hash`, `config_class_at`, `config_skill_allowed`, `config_item_allowed`, `config_skill_dmg`,
`config_skill_heal`, `config_effect_heal`, `config_apply_action`, `config_cooldown_left` / `_start` / `_turns`,
`config_tooltip_fill`, `config_skill_tooltip`, `config_page_size`, `config_guild_row`, `config_sheet_page`. The config
format itself: [chapters 2 and 3](03-config-reference.md).

## Library internals
The `*_install` functions, `install_hook` and its variants, `hook_emit`, `buff_origin_enter` / `_leave` / `_added`,
`popups_buff_added`, `cooldown_on_*`, `paging_*`, `sheet_skill_index`, `tooltip_rewrite_base`, `hook_rules_*`,
`expedition_raid_ready_turn` and the `WinSym` table are DDX's own wiring. Don't call them from a client.
