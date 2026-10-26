# 12. Hook points

**Status:** 49 points. Points marked *confirmed* below were tested in game; the rest are built and offline-tested,
and the combat, loot and per-hero points are in daily use by a client mod and an event-log client. Points marked
*untested* haven't been seen firing in game yet. The plan for covering
every expedition event is in `design/docs/13-expedition-hooks.md`.

A **hook point** is a named moment in the game (`"hit"`, `"raid_start"`, later `"hp_damage"`, `"curio_result"`, ...).
The library hooks the game function behind each point and tells every listener when it happens. Mods listen from
their JSON config (the global `hooks` rules) or, in C, with `hook_on`.

## Phases
- **after**: the game has acted. Listeners read the context; changes are ignored.
- **before**: the game is about to act. The context holds the game's inputs (an amount, a chance, an outcome), and
  listeners may change them, or set `cancel` to stop the game acting. The value points below have it.

A point's own listeners run, then the `"*"` listeners (the log), so the log shows what the game actually received.
Each listener belongs to the mod that registered it and runs only while the loaded campaign applies that mod: the
library's first, then the campaign's mods from the lowest in its mod list to the highest (so the top mod's `before`
changes stand), one mod's in the order they registered.

## The points
| Point | Phases | Actor | Notes |
|---|---|---|---|
| `raid_start`, `raid_finish` | after | the hero | |
| `battle_start`, `battle_end`, `battle_retreat` | after | the hero | |
| `turn_start` | after | any actor, monsters too | the turn starts: after the actor's DoT ticks, before its act-outs and before it acts; `skip`: `stun` when the stun takes the turn (the actor won't act), else none |
| `turn_end` | after | any actor, monsters too | the turn ends: after the actor's burn tick; stunned actors too |
| `round_start` | after | none (`battle`) | a combat round starts, before its first turn; `kind`: its number (1 the first). Fires as `Battle::StartRound` begins (macOS) |
| `round_end` | after | the hero | |
| `act_out` | before, after | the afflicted hero | a turn-start act-out (macOS). `type`: its id (`ignore_command`, `attack_friendly`, `random_command`...), `kind`: its number; `nothing` isn't reported. Before: just rolled, not yet checked or set up; `cancel`: no act-out this turn. After: set up and queued, its outcome not yet applied (the game plays it afterwards); `result`: the turn result, 0 a normal turn, 1 the hero then acts, 2 the turn is lost. A rolled act-out that fails the game's checks (disabled, raid limit) has a before but no after |
| `act_out_check` | before, after | the afflicted hero | the game's three checks of a rolled act-out (macOS), between `act_out` before and after. `type` / `kind` as `act_out`; `amount`: the act-out's `raid_limit` (0: none). Before: `refuse` holds the game's verdict as bits, `ACT_OUT_REPEAT` 1 (the party's last act-out: none twice in a row; never set when config `act_outs.allow_back_to_back` is on), `ACT_OUT_DISABLED` 2 (a monster in the battle disables it), `ACT_OUT_LIMIT` 4 (the raid limit reached); 0 means allowed. Listeners may set or clear bits (clear them all to force it through); `cancel` refuses it. After: `refuse` as decided (`ACT_OUT_CANCELLED` 8 for a cancel), `result` 1 allowed, 0 refused (a refused act-out is a normal turn) |
| `affliction_roll` | before, after | the hero | a hero's resolve test picks an affliction or virtue (stress reached 100 with no trait). Before: set `type` to a trait id (an affliction or virtue, a mod's own included) to give that trait instead of the game's pick; `cancel`: nothing is picked. After: `type`: the trait given (NULL: none), `source`: `affliction` or `virtue`. The trait is pending: the display's resolve test then shows and applies it |
| `camp_start` | after | the hero | |
| `skill_use` | after | the performer | a combat skill has been used (its own effects applied or queued); `skill`, `amount` (target count), `target` (the first; from C, `skill_resolution_target(i)` for each). *Unverified in game* |
| `hit`, `miss`, `crit`, `kill` | after | the attacker | per target; `target`, `skill`, `amount` (damage), `hit`, `crit`; `miss` from `Battle::ApplyMiss`, the rest from `Battle::ApplyHit` |
| `dodge`, `hit_taken`, `crit_taken` | after | the hero targeted | per target, as above; `dodge` is a miss against a hero (the game's "Dodge!") |
| `deathblow_resist` | after | the hero | a hero at Death's Door survives a deathblow |
| `hp_damage` | before, after | the actor damaged (`target`; `attacker`: the source actor, if any) | every HP loss: hits, DoT ticks, traps, hunger, obstacles, riposte, reflect. `amount`: before resistances; `type`: `attack`, `bleed`, `poisoned`, `burn`, `trap`, `hunger`, `obstacle`, `riposte`, `reflect`...; `source`: `monster`, `hero`, `trap`, `hunger`, `item`, `quirk`, `trinket`...; after: `result` = HP lost |
| `hp_heal` | before, after | the actor healed | `amount` before modifiers; `source`: the game's heal source id; after: `result` = HP healed |
| `stress_damage` | before, after | the actor stressed | `amount` before resistance, darkness and difficulty; `source`: the game's stress source id, or `raw` for stress the game applies unmodified (missed attacks, skill side effects); after: `result` = stress gained |
| `stress_heal` | before, after | the actor | `amount` before modifiers; after: `result` = stress removed |
| `torch_change` | before, after | none (`raid`) | `amount`: the change (signed); after: `result` = the change applied after the game's limits |
| `dot_tick` | before, after | the actor | an actor's DoTs tick, at its round start or each corridor step; `source`: `battle` or `corridor` (`Raid::ApplyDot`), or `burn` (an actor's burn stacks, in battle, `Raid::ApplyBurnDot`); cancel skips the tick (no damage, DoTs don't count down; burn: no decay or spread); after: `result` = HP lost. Each DoT's damage is also an `hp_damage` |
| `step` | after | none (`raid`) | the party steps onto a new corridor tile |
| `raid_ready` | after | none (`raid`) | the expedition is set up and on screen and the player is about to act: once each time the dungeon is entered (a new expedition or a loaded save), on the first frame the game takes the player's input (the quest intro, loot window and other overlays done). `source`: `explore`, or `battle` for a save loaded mid-battle (then on the first hero's turn). After `raid_start`, which runs while the expedition is still loading (nothing on screen yet). *Confirmed in game 2026-10-16 (a new expedition, a loaded save, a save loaded mid-battle)* |
| `area_enter` | after | none (`raid`, `area`) | the party enters a room or corridor; `source`: `room` or `corridor` |
| `hunger_check` | before, after | none (`raid`) | the party reaches a hunger tile; cancel: no hunger this time; after: `result` 1 if the party goes hungry |
| `scout` | before, after | none (`raid`, `area`) | an area is scouted: on entering a room (the game's scouting roll succeeded; `source` `room`) or at the expedition's start (`raid_start`); `kind`: the depth (2, 4 on a critical scout); cancel: not scouted. The chance can't be changed (the game rolls it inline) |
| `curio_result` | after | the hero (`prop`) | the game picks an interaction's outcome: a curio investigated or given an item, an obstacle, a door, a prisoner. `source`: `nothing`, `loot`, `quirk`, `effect`, `scouting`, `purge`, `disease`, `quest_item`, `summon`, `obstacle_default`, `clear_obstacle`, `default_door`, `unlock_door`, `prisoner_talk`, `prisoner_display` (`kind`: its number). An obstacle cleared with a shovel has no hero (the party). `prop_id(prop)` / `prop_title(prop)`: its id (`crate`, `rubble`) and display name (Crate, Rubble) |
| `curio_apply` | before, after | the hero (`prop`) | that outcome happens (same `source`); cancel skips it for `loot`, `quirk`, `effect`, `scouting`, `purge` and `disease` (the others always apply: skipping them could leave the game waiting). `nothing` has no `curio_apply` |
| `trap` | before, after | the hero | a trap is resolved, walked into (`source` `walk`) or disarmed (`disarm`). `amount`: the hero's chance (0-1) to avoid or disarm it; before may change it. After: `result` 1 safe, 0 sprung; `type`: `avoided`, `disarmed` or `sprung`. The trap's damage is an `hp_damage` (type `trap`) |
| `loot_queue` | before, after | none (`raid`) | a loot table code is queued; `source`: the loot type (`battle`, `chest` for curios, `camping`, `quest`, `threshold`, `hero_death`); `type`: the code; before may set `type` to another code (up to 31 characters) or cancel. Reloading a save re-queues a saved queue through it |
| `loot_item` | before, after | none (`raid`, `item`: the `Inventory::Item`) | a given item is queued as loot (not rolled from a table): an item placed on a battle's tile (awarded when the battle ends), a quest curio's quest item, the Farmstead's threshold rewards, flashbacks, some camping and trinket effects; cancel drops it |
| `loot_generated` | before, after | none (`raid`, `inventory`: the loot window's `Inventory::System`) | the loot queue was generated into the loot window, fired once per code in queue order; `source`: the loot type; `type`: the code. Callbacks may add items to `inventory` (a client mod adds its gear drops and its gold found bonus here; `game_inventory_add` stacks as the game's own loot bonus does). `kind`: the code's position in the batch (0 first), `amount`: the batch's code count. The game generates every code into the one inventory before the first call, so what a single code produced isn't known: act on the whole window once, at `kind` 0. Before: fired the same way just before generation; a callback may queue more codes (`game_queue_loot`), which are generated into the same window and fire `after` with the rest (a client mod Pile of Fragments). `cancel` does nothing |
| `quirk_add` | before, after | the hero | a hero gains a quirk or disease (macOS; every source: curios, battle, camping, the estate). `type`: the quirk's id; before: set `type` to another quirk id to add that one instead, `cancel`: not added. After: `result` 1 if added (0: resisted, incompatible, already had it, or cancelled). *After confirmed in game 2026-10-16 (recruits, a new week)* |
| `quirk_remove` | before, after | the hero | a hero loses a quirk (treatment, a quirk replaced at the limit, transitions). `type`: its id, `kind`: its index in the hero's list; `cancel`: kept; after: `result` 1 if removed. *After confirmed in game 2026-10-16* |
| `deaths_door` | after | the hero | a hero reaches Death's Door; `type` / `source`: the damage's type and source ids (as `hp_damage`). *Untested in game* |
| `heart_attack` | before, after | the hero | a heart attack at max stress (also at the raid start or a retreat with max stress); `cancel`: none, the stress stays. *Untested in game* |
| `death` | after | the hero | a hero dies (any cause). *Untested in game* |
| `camp_skill` | before, after | the performer | a camping skill is used; `target`: the hero it's used on (the performer for self and party skills' first pick), `type`: the skill's id, `amount`: its respite cost; `cancel`: no effect, but the points are spent and the skill counts as used (the game's own path when a hero refuses); after: `result` 1 if performed. *Untested in game* |
| `camp_end` | after | the hero | the camp ends, once per party hero (pairs with `camp_start`). *Untested in game* |
| `item_used` | after | the hero (`item`) | a hero uses an item on an expedition (the bus's `HeroUsedItemEvent`). *Confirmed in game 2026-10-16* |
| `trinket_triggered` | after | the hero | one of the hero's trinket effects triggers (the bus's `TrinketTriggeredEvent`; it names no trinket). *Untested in game* |
| `bark` | after | who barks | a bark is queued (`RaidDisplay::QueueBark`: stress, afflictions and virtues, act-outs, monsters, camping, the torch, ripostes; curio trigger barks, `RaidDisplay::QueueTriggerCurioBark`, with `type` `trigger_curio_<type>_<id>` and `prop` the curio). `text`: the text shown (the variation the game picked); `type`: its string id; `source`: the bark type, `neutral`, `afflicted`, `virtued` or `cursed` (`kind` 0-3; colours `bark_neutral`, `bark_affllicted`, `bark_virtued`, `bark_cursed`). *Untested in game* (curio barks: built, untested) |

In the before phase only `amount` and `cancel` are read back (and `type` for `loot_queue`, `affliction_roll` and `quirk_add`). Damage, heal and stress amounts below 0 become 0.
`synthetic` is set on a value point fired from inside another value point's callback (a listener dealing damage).
On Windows the combat, value, hero, camping, expedition, loot, curio and trap points work (`loot_item` there sees
the expedition's loot only; it, a scouting curio's `scout` and a disarm's `trap` are untested in game), except `trinket_triggered` (macOS only), and `camp_end`
fires once the whole camp wrap-up is done.

## From a config
The global section `hooks` (any engine file, with or without `class`) is a list of rules. Each listens to one point
and phase; when its filters pass, it changes the point (before) and runs its actions through the game's own code.
```json
"hooks": [
  {"point": "hp_damage", "phase": "before", "match": {"type": "bleed"}, "when": "torch < 25", "scale": 0.5},
  {"point": "quirk_add", "phase": "before", "class": "crusader", "match": {"type": "fear_of_*"}, "cancel": true},
  {"point": "loot_queue", "phase": "before", "match": {"source": "chest"}, "chance": "10%", "set_type": "my_chest"},
  {"point": "kill", "do": [{"stress": -5}, {"effect": "my_kill_buff"}, {"heal": 2, "on": "party"}]},
  {"point": "camp_end", "do": [{"item": "torch", "type": "supply", "amount": 2}]}
]
```
| Key | What it does |
|---|---|
| `point` | the point's name (the table above); required |
| `phase` | `after` (default) or `before` (only where the point has it) |
| **Filters** | all must pass |
| `class` | the point's actor is a hero of this class id |
| `match` | globs on the point's strings: `type`, `source`, `skip` (`{"type": "fear_of_*"}`) |
| `when` | a formula that must be non-zero: the [combat context](#combat-context) names and calls (for the point's actor) plus the point's own `amount`, `result`, `kind`, `hit`, `crit`, `refuse`, `synthetic` |
| `chance` | 0-1 or a formula (`"25%"`), rolled on the game's rng |
| **Changes** | before only, in this order |
| `amount` | a formula for the new amount (`"amount * 2 + torch / 10"`; `amount` is the current one) |
| `scale`, `add` | amount × scale + add |
| `set_type` | the context's type: another loot code (`loot_queue`), quirk (`quirk_add`) or trait (`affliction_roll`) |
| `refuse` | `act_out_check`'s bits |
| `cancel` | `true`: the game doesn't act |
| `do` | **actions**, either phase, in order; each an object with one verb and `on` (a target, default `actor`) |
| `nested` | `true`: also run inside another rule's actions and on synthetic points (off by default, so rules can't loop) |

Actions (`on`: `actor`, `target`, `attacker`, `party`, `party_random`, `ally_random`, `party_lowest_hp`,
`party_lowest_hp_pct`; values are numbers or formulas, as `when`):
| Action | Does |
|---|---|
| `{"effect": "id"}` | applies an effect from any `.effects.darkest` (the game's side effect queue, as traps and act-outs do): stress, heals, buffs, DoTs, cures, anything an effect can do |
| `{"damage": n, "type": "bleed", "source": "trap"}` | HP damage (the game's resistances apply); `type` / `source` are the game's ids (default the first) |
| `{"heal": n, "source": "id"}` | HP heal, the actor as the healer |
| `{"stress": n, "source": "id"}` | stress; negative heals it |
| `{"torch": n}` | torchlight change (signed) |
| `{"quirk": "id"}`, `{"remove_quirk": "id"}` | add a quirk or disease as a curio does (resists apply), or remove one |
| `{"remove_buffs": ["glob"]}` | removes the target's buffs whose ids match |
| `{"action": "name"}` | runs the target hero's class config action of that name (`actions`, field changes) |
| `{"item": "id", "type": "trinket", "amount": 1}` | gives an item (any type: `trinket`, `supply`, `gold`, `gem`, a mod's own) to the party inventory, else the estate |
| `{"loot": "code", "type": "chest"}` | queues a loot table code into the expedition's loot (`type`: the loot type, `battle`, `chest` (curios, the default), `camping`, `quest`, `hero_death` or `threshold`) |
| `{"log": "text"}` | a line in the log |

*Confirmed in game 2026-10-16 (every filter and change; the `effect`, `heal`, `stress`, `torch`, `quirk`, `item`,
`loot` and `log` actions).* Each fired rule logs with `"debug": true` (`hooks: <file> rule <n> fires on <point> <phase>`); a rule with an
unknown point or phase, a formula error or an unknown verb is logged. The class configs' old `events` map is gone:
`{"point": "raid_start", "class": "my_class", "do": [{"action": "my_reset"}]}` does what it did. C mods can call the
same actions directly (`game_damage`, `game_heal`, `game_stress`, `game_torch`, `game_apply_effect`,
`game_add_quirk`, `game_remove_quirk`, `game_give_item`, `game_inventory_add`, `game_queue_loot`; `common.h`).
The library announces every quirk lost on the dungeon screen ("<quirk> Quirk Removed!", the game's purge popup) but a
curio's purge, which shows its own: `remove_quirk`, a quirk replaced at the limit, transitions. The game announces a
quirk gained itself (out of battle). *Confirmed in game on Windows (a remove_quirk after a battle).*
To name an effect's application, call `buff_source_effect(target, "effect name", "Ravenous!", "Ravenous")` before
applying it: its bleed / blight / marked popups on the target, and the Buff! / Debuff! / Riposte! popup of a status
icon its buffs light, show the popup text with the game's colours and sound, and the character sheet's stat tooltips
list its buffs under the label instead of the game's source name (macOS; *popups confirmed in game, labels built,
untested*). Without a name, a buff an effect adds is listed under the skill and who used it ("Battle Ballad
(Junia)"), a trinket's or quirk's under the trinket or quirk; `buff_origin_on(fn)` lets a mod name its own buffs.
`popup_text(actor, type, "Kindling!")` queues a text popup of your own with a popup type's colours, sound and display
rules; queued just before a number popup whose type has the same rules, it shows first.
`popup_on_add(fn)` sees every popup as the display is asked for it (a `PopupEvent`: actor, type, text, number, whether
the text is a mod's name rather than the game's, and a tray icon popup's longest buff rounds left; after naming), and
`popup_type_id(type)` gives a type's id (`damage`, `crit_damage`, `bleed`, `buff`...; its colours are `pop_text_<id>`).

### The expedition log
A client queues a bark of its own by string id with `bark_queue(actor, id)` (barks.c, as the game's skill barks; it goes
through the `bark` point, so the log shows it).

An event-log mod can subscribe to hook points and popup observers, copy display text during callbacks, and keep its own bounded list of messages. Use `items_on_display_name` and `popup_on_line` to preserve client-provided wording. Render the list with the scroll-panel services described in the [UI guide](ux-mods.md). Do not retain borrowed event pointers in queued messages.

## From C
```c
static void on_crit(HookContext *c, void *userdata) {
    // c->actor is the attacker, c->target the target, c->amount the damage
}
int engine_client_init(const EngineClientInfo *info) {   // your native client's entry (chapter 13)
    /* ... check info->api ... */
    hook_on("crit", HOOK_AFTER, on_crit, NULL);           // register here: DDX installs hooks after the inits
    return 0;
}
```
`hook_on` refuses an unknown point or a phase the point doesn't have, and logs why. `hook_on("*", ...)` hears every
point that's installed for someone else (a log); `hook_on_all(phase, ...)` hears every point and installs them all
(a client mod in-game log panel does this). The context's pointers are
borrowed for the callback only. Combat listeners can act through the game's own code with
`combat_apply_followup_hit`, `combat_apply_side_effect` (the hit's attacker on its target) and `combat_apply_effect` (any performer on any target: the performer is whose effect it is, for its stats, `.target performer` and the log) (`common.h`).

## Act-out rules
A rule weighs a hero's turn-start act-outs each turn, from the state of the battle (macOS). The library rolls the
act-out itself; for every candidate act-out, the factors of all matching rules multiply its weight: 0 rules it out
this turn, 1 leaves it, 2 doubles it. Candidates come from the hero's trait (an affliction or virtue) and its quirks
(Crimson Curse stages, `imposter_syndrome`), with the percentages from `act_outs.chances` (else the game's weights);
then the roll picks one with chance = its weight / the total of all of them (with `act_outs.use_max`, each act-out
keeps only its highest source's weight, after the rules; with `act_outs.per_source`, each source instead rolls once
on its own table and every act-out given runs, each with its own `act_out` and `act_out_check` lines). With
`"debug": true` every roll logs to `ddx_combat.log` with the hero's name (per source: `act-out rolls (per source) for
Reynauld (hero 3): <source>: <act-out>, …; n queued`; single roll: `act-out pool for …`)
(single roll: `act-out pool (total …): source/act-out weight (percent), …`). A rule matches on `source` (the
trait's or quirk's id, a glob: `"*"`, `"disease_vampire_*"`) and `act_out` (the act-out's id, a glob). Rules apply
before `act_out` and `act_out_check`. With `"debug": true`, each factor other than 1 is logged
(`act-out rule <name> for <hero>: <source>/<act-out> weight a x f`, in `ddx_combat.log`).

**From a config** (`act_outs.rules`, any engine file):
```json
"act_outs": {"rules": [
    {"source": "fearful", "act_out": "ignore_command", "when": "torch < 25", "weight": 3},
    {"source": "*", "act_out": "attack_friendly", "when": "heroes_at_deaths_door > 0", "weight": 0},
    {"source": "courageous", "act_out": "stress_heal_party", "weight": "1 + average_stress / 50"}
]}
```
`when` (optional): a condition; the rule applies only while it's true. `weight`: a number or a formula. Both read the
[combat context](#combat-context). A rule without `weight` is skipped (logged); a broken formula leaves the weight
and logs `formula error`.

**From C** (register before `act_outs_install`, up to 64 rules in all):
```c
static float dark_fear(const CombatContext *ctx, const char *source, const char *act_out, void *ud) {
    const CtxActor *me = ctx->self;                       // the hero rolling
    if (ctx->torch < 25 && hero_has_quirk(me->actor, "fear_of_*")) return 4;
    return ctx->round == 1 ? 0 : 1;                       // never on the first round
}
act_out_rule_register(&(ActOutRule){.name = "dark fear", .source = "fearful", .act_out = "ignore_command",
                                    .check = dark_fear});
```

## Combat context
`CombatContext` (`combat_context_build(&ctx, self)`, macOS) is the battle at the moment of the check:
- the battle: `in_battle`, `round` (from 1), `torch` (0-100), `raid`, `battle`;
- `heroes[nheroes]` and `monsters[nmonsters]` (`CtxActor`): `actor`, `hero`, `class_id`, `rank` (1-4), `hp`,
  `max_hp`, `hp_pct`, `stress`, `alive`, `corpse`, `deaths_door`, `trait` (affliction or virtue id), `afflicted`,
  `virtuous`, `resolve_level`;
- `self`: the actor it's for.

Accessors for the rest (on any `CtxActor.actor`): `actor_stat(actor, "attack_rating")` (with buffs: `damage_low`,
`damage_high`, `defense_rating`, `protection_rating`, `speed_rating`, `attack_rating`, `crit_chance`, `max_hp`,
`riposte_on_hit_chance`, `riposte_on_miss_chance`), `actor_has_buff` / `actor_count_buffs` (buff ids, globs),
`hero_has_quirk` (quirk ids, diseases included), `hero_has_disease` (NULL: any), `hero_has_item` (equipped trinkets
and gear, item ids).

In formulas (`when`, `weight`):
| Name | Value |
|---|---|
| `torch`, `round`, `in_battle` | the battle |
| `heroes`, `monsters` | living heroes, living monsters |
| `heroes_at_deaths_door`, `afflicted_heroes`, `virtuous_heroes` | counts |
| `party_stress`, `average_stress`, `lowest_hero_hp_pct`, `monsters_hp_pct` | the party's total and average stress, the lowest hero HP %, the monsters' HP % |
| `hp`, `max_hp`, `hp_pct`, `stress`, `rank`, `resolve_level`, `deaths_door`, `afflicted`, `virtuous` | the hero rolling |
| `stat("id")`, `has_buff("glob")`, `buffs("glob")`, `has_quirk("glob")`, `has_disease()`, `has_disease("glob")`, `has_item("glob")`, `trait("id")`, `class("glob")` | the hero rolling |
| `heroes_with_trait("id")`, `heroes_with_quirk("glob")`, `heroes_with_disease()`, `heroes_with_buff("glob")`, `monsters_of_class("glob")`, `monsters_with_buff("glob")` | counts over the living |

Formulas take `+ - * /`, `< <= > >= == !=`, `&& || !`, brackets and `70%` (0.7); a condition is true when non-zero.

## Adding a point (library code)
1. Declare it in `k_points` in `src/hooks.c`: name, phases, one line on what it is.
2. In the module that hooks the game function, install the hook only if `hook_wanted("<point>")` (or
   `hook_any_wanted`), build a `HookContext` (set `actor`, and `kind = -1` unless the amount has a game type) and call
   `hook_emit("<point>", HOOK_BEFORE or HOOK_AFTER, &ctx)`. For a before point, use the context's values when calling
   the game, and skip the call when `hook_emit` returns false.
3. Add it to the table above and to `tests/test_hooks.c` if it changes the registry.

## Debugging
`ddx_combat.log` (next to `ddx.log`) gets a `point <name> <phase> actor=… attacker=… target=… amount=…` line for
every point that fires. A point's game hook is installed only if something listens to it (`hook_on_all` counts);
with `"debug": true` in any config every point is installed, so all of them are logged.
