# 3. Config reference

One file per class (plus files of global sections, such as `act_outs`): `<mod>/engine/<anything>.engine.json`. Plain JSON: no comments, no trailing commas. It's read once
at game launch.

Unknown top-level keys are ignored silently, so check spelling against this page. **Unknown keys inside a
skill entry are read as conditions on a field of that name** (see [Pitfalls](#pitfalls)).

## Top level
| Key | Type | Default | What it does |
|---|---|---|---|
| `class` | string | **required** | The class id, as in `heroes/<class>/<class>.info.darkest`. A file without it is skipped. |
| `fields` | [string] | `[]` | Per-hero integer fields ([concepts](02-concepts.md#fields)). Names under 32 characters. Fields named anywhere else in the file are registered too; listing them here is documentation. |
| `enums` | {field: [string]} | `{}` | Value names by index. Up to 16 names per field, each under 32 characters. |
| `limits` | {field: [lo, hi]} | none | Clamp range, applied after actions and tooltip `@` changes. Also defines `<field>_min` / `<field>_max` in formulas. |
| `skills` | {skill id: object} | `{}` | Per-skill settings; [below](#skills). |
| `actions` | {effect name: object} | `{}` | Field changes run by an effect of that name; [below](#actions). |
| `hooks` | [rule] | `[]` | **Global**: rules on hook points, from any engine file: filters, changes before the game acts and actions through the game's code; [chapter 12](12-hook-points.md#from-a-config). Replaces the old per-class `events` map. |
| `effects` | {effect name: {"heal": formula}} | `{}` | Heal formula for an effect, on the performer's fields. Up to 32. |
| `effect_variants` | {effect name: template} | `{}` | Apply the effect the template names instead (`"ex_effect": "ex_effect_{ex_charge}"`), if it exists. Up to 32. |
| `animation_variants` | [template] | `[]` | `<anim>_<variant>` templates, first match wins. Up to 8, each under 64 characters. |
| `cooldowns` | {name: {"turns": n, "unit": "own" \| "actor"}} | none | Named cooldowns skills can share. `turns` defaults to 1, `unit` to `own`. Up to 16. Each adds a field `cd_<name>`. |
| `cooldown_tooltip` | string or [one, many] | none | Line added under the name of a skill that's cooling down (`[[cooldown]]` = turns left). The first is used at 1 turn, the second at 2+. A single string serves both. |
| `cooldown_ready_tooltip` | string or [one, many] | none | The same place, for a ready skill that has a cooldown (`[[cooldown_turns]]` = its length). |
| `tooltip_title` | string | `[[name]] {colour_start\|neutral}([[rank]]){colour_end}` | Template for every skill's name line; [Tooltips](04-tooltips.md#titles). |
| `skill_pages` | {"size": n, "icons": [prev, next, prev disabled, next disabled]} | none (no paging) | Combat bar and character sheet paging; [UI features](05-ui-features.md#combat-skill-bar-paging). `size` defaults to 4. |
| `guild_centred_rows` | [row] | `[]` | Guild rows drawn centred and without a divider; [UI features](05-ui-features.md#the-guild-grid). Up to 16. |
| `items` | {item id: object} | `{}` | Who may use a supply item; [below](#items). Up to 16. |
| `debug` | bool | `false` | Verbose log lines (animations, formulas, actions, events, restores, tooltips), and every hook point installed and logged to `ddx_combat.log`. **Global**: one config with `true` turns it on for everything. Turn it off for release. |
| `act_outs` | object | `{}` | **Global** settings for heroes' turn-start act-outs (afflictions, virtues, quirks), from any engine file; a file may hold only global sections (no `class`). `use_max` (bool, default `false`): each act-out at its highest source's percentage, [below](#act-out-chances). `allow_back_to_back` (bool, default `false`): turns off the game's no-repeat rule, so the same act-out may happen twice in a row, by one hero or several, across battles too. On if any mod turns it on. `chances` ({trait id: {act-out id: weight}}): replaces a trait's act-out chances (an affliction such as `fearful`, or a virtue such as `stalwart`); see [Act-out chances](#act-out-chances). `rules` ([{source, act_out, when, weight}]): act-out weights from the battle's state, [chapter 12](12-hook-points.md#act-out-rules). `types` ({id: {...}}): new act-out types, [below](#new-act-out-types). macOS only. |
| `announce` | object | none | **Global**, any engine file: this mod in the list of the library's once-per-campaign announcement ([chapter 11](11-ui-library.md#the-announcement-announcec)). `name` (string id in the mod's own table; colour markup welcome), `font` (font_ref id) and `colour` (colour id), both optional (the list's own). One entry per name. |
| `riposte` | object | `{}` | **Global**, any engine file. `skills` ([skill type]): heroes riposte with one of their own skills instead of their class's `riposte_skill`: a usable skill (selected, valid in the hero's mode, allowed by its class config, uses left, not cooling down) whose `.type` is listed (`"melee"`, `"ranged"`), single target, not a move skill, launchable from the hero's rank and able to target the attacker; one at random if several. A skill that moves the hero (`.move`) moves it after the attack, as on its turn. It plays as a vanilla riposte (the riposte buff's modifiers and `riposte_effect`, trinkets' riposte effects) and counts as a use of the skill, as on the hero's turn but as a free action: it spends a per-battle use and starts the skill's cooldowns. `no_skill_bark` (string id): what the hero says when a riposte would have fired but no skill qualifies. Monsters keep their riposte skills. macOS only. |

## Act-out chances
`"act_outs": {"chances": {"fearful": {"nothing": 60, "bark_stress": 20, "ignore_command": 20}}}` sets what a hero
rolls at the start of each turn, in place of the game's weights. The library does the roll itself.
- **Sources.** Keys are the ids of everything that gives act-outs: afflictions (`fearful`, `paranoid`, `selfish`,
  `masochistic`, `abusive`, `depressed`, `irrational`, `berserk`, `color_madness`), virtues (`stalwart`,
  `courageous`, `focused`, `powerful`, `vigorous`) and quirks (`disease_vampire_crave`, `disease_vampire_wasting`,
  `disease_vampire_blood_lust`, `imposter_syndrome`), or a mod's own.
- **Values are percentages per source**, adding up to 100 (the log warns otherwise; they're used as written).
  Act-out ids: `nothing`, `bark_stress`, `change_pos`, `ignore_command`, `random_command`, `retreat_from_combat`,
  `attack_friendly`, `attack_self`, `mark_self`, `stress_heal_self`, `stress_heal_party`, `buff_random_party_member`,
  `buff_party`, `heal_self`, `consume_item`. A listed source rolls only the act-outs listed above 0; one at 0 in the
  game's file can be switched on (it then uses that file's effect for it). A misspelt id is logged and weighs nothing.
- **Several sources at once** (an affliction and a Crimson Curse stage): their percentages go into one pool and the
  roll picks out of the pool's total, so each source counts equally: Abusive (100) plus `disease_vampire_crave`
  (100) is a pool of 200, and Abusive's `bark_stress` at 23.53 is 23.53 / 200 = 11.8% of turns. Rules
  ([chapter 12](12-hook-points.md#act-out-rules)) multiply entries before the total is taken.
- **`use_max`** (bool, in `act_outs`, default `false`): instead of adding the sources up, each act-out takes the
  highest percentage any active source gives it (that source's effect is used; a tie goes to the trait), and the roll
  picks out of the sum of those highest values. Abusive plus `disease_vampire_crave`: `nothing` max(70.59, 84.91) =
  84.91, `bark_stress` 23.53, `change_pos` 3.77, `ignore_command` 1.89, `attack_friendly` 5.88; total 120, so
  `bark_stress` is 23.53 / 120 = 19.6% of turns. Rules apply to each source's entries first, then the maximum is
  taken. On if any mod turns it on.
- **`per_source`** (bool, default `false`): instead of one roll over the pooled sources, **each source rolls once on
  its own** (the trait first, then each quirk), with its own percentages, `nothing` included. So each source gives one
  act-out or none, and an Abusive hero with the `disease_vampire_crave` stage can act out twice in one turn, once
  per source. The sources run **one at a time**: a source rolls only once the previous source's act-out has fully
  played out, so its roll, its rules (`when`) and its setup (a target, a rank to move to) see what the earlier
  act-outs did. Each act-out has its own bark, effect, `act_out` points and checks. (The game's turn sound plays again
  before each extra act-out.) If an earlier act-out leaves the hero unable to act (stunned, dead), the rest are
  dropped. `ignore_command` doesn't stop the others: it's flagged and the turn is lost once all have run.
  `retreat_from_combat` stops there: the ones before it run and the hero retreats. Otherwise the hero then takes its
  turn (with `random_command`, the skill chosen for it). A source alone rolls exactly as vanilla. Two sources may give
  the same act-out: both run if `allow_back_to_back` is on (otherwise the game refuses the second). `use_max` doesn't
  apply (sources aren't pooled). `max_per_turn` (number, default 0: no limit): once that many have run, the remaining
  sources don't roll.
- **`prerequisites`** (with `per_source`): a source rolls only once other sources have fired, for the same hero.
  ```json
  "prerequisites": {
      "disease_vampire_crave": {"match": "all", "requires": [
          {"source": "abusive", "within": "turn", "nothing_counts": false},
          {"source": "disease_vampire_wasting", "within": "expedition"}
      ]}
  }
  ```
  A source has *fired* when its roll gave an act-out that ran (refused or cancelled ones don't count). Per
  requirement: `within`: `turn` (earlier in this turn), `battle` (this battle) or `expedition` (since this
  expedition started; not kept over a reload), default `turn`; `nothing_counts` (default `false`): the source rolling
  `nothing` counts as having fired. `match`: `all` (default) or `any`. A requirement can be just the source's id
  (`"fearful"`: this turn, nothing doesn't count). Within a turn a blocked source waits: if a source later in the
  order fires its prerequisite, it rolls after that; one still blocked at the end doesn't roll this turn. A source
  that needs a source the hero doesn't have never rolls (with `any`, the others can still unlock it). Requirements can
  chain (a tree). With `"debug": true`, a source that couldn't roll is logged (`prerequisites not met, no roll`).
- `attack_friendly` exists once per vanilla class in the game's files (each with its own effect): its percentage goes
  to the hero's class's entry; a class with none (mod classes) can't roll it, and the source's total is then below
  100.
- A source not listed uses the game's own weights. A source named in several mods: the last one read wins.
- a client mod `engine/a client mod.engine.json` lists every source in the game and its DLC at vanilla odds.

## New act-out types
`act_outs.types` declares turn-start act-outs the game doesn't have. Once
declared, a trait or quirk file lists the id in `combat_start_turn_act_outs` like a vanilla one, and `chances` may
weigh it:
```json
"act_outs": {
    "types": {
        "heal_lowest_ally": {"like": "heal_self", "outcome": "heal", "target": "party_lowest_hp"}
    },
    "chances": {"graceful": {"nothing": 75, "heal_lowest_ally": 25}}
}
```
and in the trait (`shared/trait/<anything>.trait_library.json`):
`{"id": "heal_lowest_ally", "data": {"number_value": 0.15, "string_value": ""}, "chance": 1}`.

| Field | Default | Meaning |
|---|---|---|
| `like` | none | a vanilla act-out whose turn result, bark chance and burndown the type takes (`heal_self`: the hero then acts, the game's bark chance) |
| `result` | `like`'s, else 0 | the turn result: 0 a normal turn, 1 the hero then acts, 2 the turn is lost |
| `bark_chance` | `like`'s, else 1 | 0-1 |
| `burndown` | `like`'s, else 0 | seconds added to the battle's burndown |
| `outcome` | none | `heal`: the target gains `number_value` of its max HP (as `heal_self`); `effect`: the effect named in `string_value` on the target (as the stress heals). None: a bark only |
| `target` | `self` | `self`, `party_lowest_hp`, `party_lowest_hp_pct`, `party_random`, `ally_random` (not the hero), `party` (every living hero) |

The library runs the act-out as the game runs a vanilla one: `act_out` and `act_out_check` points, the no-repeat
check, the count (saved with the raid, by id), the trait's or quirk's sound and halo, then the bark
`str_<id>_<source>` (e.g. `str_heal_lowest_ally_graceful`, with `<class>+` variants as for vanilla barks), then the
outcome, after the bark. Targets are picked when the outcome plays. From C: `act_out_type_register` (an
`ActOutTypeDef` with an outcome callback; `act_out_heal`, `act_out_side_effect` and `act_out_targets` help), before
`act_outs_install`. A type registered again (by id) is replaced. The game itself never learns these types: played
without the library, its data loads them as unknown (an assert, then nothing happens).

## Skills
Keys are skill ids exactly as `.id` in the class's `info.darkest` (`combat_skill: .id "example_mend" …`). The Move
skill's id is `move` (useful for `tooltip_title`). All keys are optional.

| Key | Type | What it does |
|---|---|---|
| `"<field>"` | value or [values] | Condition: field equals the value (any of the list). Values are numbers, `true`/`false` (1/0), or enum names. Up to 8 values. |
| `"<field>_min"` / `"<field>_max"` | number | Condition: field ≥ / ≤ the number. |
| `hidden` | bool | Never show or allow the skill. |
| `dmg` | formula | Damage modifier (fraction) from the user's fields, replacing `.dmg`. Under 96 characters. |
| `heal` | formula | Heal amount from the user's fields, replacing `.heal min max`. Under 96 characters. |
| `cooldown` | name or [names] | Cooldowns from the top-level `cooldowns` the skill starts and obeys. Up to 4. An unknown name logs `unknown cooldown`. |
| `tooltip` | [line] | Lines added to the skill's tooltip; [Tooltips](04-tooltips.md). |
| `tooltip_replace` | bool | The `tooltip` lines replace the game's info text instead of following it. |
| `tooltip_title` | string | This skill's name line template (wins over the top-level one). |
| `guild_row` | number | Guild row (1 = top). Also the character sheet page and bar paging group when there's no `sheet_page`. |
| `sheet_page` | number | Character sheet page (1 = first) and combat bar paging group. |
| `guild_first_rank` | number | Levels below `guild_first_rank - 1` are placeholders: owned by every hero, never usable, not counted as a rank. Default 1 (no placeholder). Set 2 to make level 0 the placeholder. |

A skill can have up to 8 conditions (`<field>` / `_min` / `_max` keys together); all must hold.

### Tooltip lines
Each entry of `tooltip` is a string or an object:

| Key | Type | What it does |
|---|---|---|
| `text` | string | **Required.** The line. `@<string id>` takes it from the string table. May hold `[[tokens]]` and colour markup, and start with `{font\|<font id>}`. |
| `font` | string | A `font_ref` id from the game's `fonts/fonts.darkest` for this line. |
| `with` | string | A field change for every token in the line without its own `@`: `"ex_charge+1"`, `"ex_charge-1"`, `"ex_charge=3"`. For "next" lines. |
| `base` | bool | `false` leaves the line out of base (town and Guild) tooltips. Lines with `with` are always left out of them. |
| any condition key | as for skills | The line shows only while its conditions hold. |

## Actions
Keys are effect names (under 96 characters). The effect must exist in an `.effects.darkest` file for anything
to apply it; a hook rule can run an action without an effect (`{"action": "name"}`).

| Key | Type | What it does |
|---|---|---|
| `remove_buffs` | [glob] | Remove the hero's buffs whose ids match (`*`, `?`, `[…]`). Up to 16 patterns, each under 64 characters. |
| `set` | {field: value} | Set fields. Values are numbers, booleans or enum names. |
| `add` | {field: number} | Add to fields (negative to subtract). |

Order: remove buffs, set, add, then clamp to `limits`. `set` and `add` together hold up to 8 changes.

The built-in effect names `ddx_set_<field>_<n>` and `ddx_add_<field>_<n>` write an integer directly to
the effect target, without class checks or clamping (`m` before the number means negative, e.g.
`ddx_add_mark_m1`). They work on Heroes and Monsters. Monster values are stored in the Monster's save block;
the field name must already be registered by a loaded class config. Hero config features continue to read only
hero fields.

While `Battle::Actor_DoSkill` runs, native hook code can query `skill_resolution_performer()`,
`skill_resolution_skill()`, `skill_resolution_target_count()` and `skill_resolution_target(i)`. These accessors
are for engine-hook code; there is not yet a JSON rule for checking Monster values during skill resolution.

## Items
Keys are supply item ids (`inventory_item: .type "supply" .id "<id>"`). The value is an object of conditions
(as for skills) plus:

| Key | Type | What it does |
|---|---|---|
| `combat` | bool | `false`: not usable during a battle. |

Only heroes of this class whose conditions hold may use the item. An item that no config lists keeps the
game's own rules. The vanilla rule still runs first: the library can only forbid, never allow what the game
forbids.

## Capacity
| Limit | Value |
|---|---|
| Classes with a config | 16 |
| Field names, all classes and native clients together (incl. `test` and `cd_*`) | 64 |
| Heroes with field data | 512 |
| Conditions per skill / tooltip line / item | 8 |
| Values in one condition list | 8 |
| Skill id | under 64 characters |

Going over a limit logs a line (`too many fields / name too long`, `too many classes`) or silently drops the
extra entries, so keep an eye on the `config <class>: …` summary line.

## Pitfalls
- **A typo in a skill key makes a condition.** Any key the skill section doesn't know is read as a field
  condition: `"guild_rows": 3` creates a field `guild_rows` and requires it to equal 3, so the skill never
  shows. If a skill vanishes, check its keys.
- **Condition values must be valid.** An enum name that isn't in `enums` logs `bad value for field …` and
  the condition is dropped (so it no longer restricts anything).
- **Skill ids are matched by hash of the exact id.** Case and underscores matter.
- **`debug` is global**; enable it while testing and turn it off for release.
