# 2. Core concepts

The engine adds one kind of state (per-hero **fields**) and several ways to read and write it. Everything in a
config is one of these:

| Concept | Writes fields | Reads fields | Config keys |
|---|---|---|---|
| Fields | | | `fields`, `enums`, `limits` |
| Actions | yes | | `actions` |
| Hook rules | yes (run actions) | | `hooks` (global) |
| Skill conditions | | yes | `skills.<id>` condition keys, `hidden` |
| Formulas | | yes | `skills.<id>.dmg` / `.heal`, `effects` |
| Effect variants | | yes | `effect_variants` |
| Animation variants | | yes | `animation_variants` |
| Cooldowns | own counters | yes | `cooldowns`, `skills.<id>.cooldown` |
| Item limits | | yes | `items` |
| Tooltips | | yes | `skills.<id>.tooltip`, `tooltip_title`, … |

## Fields
A field is a named integer stored per hero for the config features. Fields are declared in `fields` (and
implicitly by any other key that names them).

- **Default 0.** A field the hero has never had set reads 0 everywhere (conditions, formulas, templates).
- **Per hero, not per class.** Values are keyed by the hero's id (`Hero::GetGuid`), so every hero has their
  own, and a hero keeps theirs through battles, walking, curios, camping, town and quitting the game.
- **Saved with the game.** On save the library writes each field the hero has as an extra key `ddx_<field>` in
  the hero's own block of the save file; on load it reads them back. Fields are cleared for a hero before
  their data is read, so values can't leak between saves or campaigns. (The `ddx_` prefix is fixed for every
  mod; see [Limits](10-limits-and-distribution.md).)
- **Saves stay loadable without the library.** The game ignores the extra keys (confirmed). They are dropped
  the next time that profile is saved without the library.
- **One global list of names.** Field names are shared by all loaded classes: two classes that both declare
  `charges` use the same name, but still separate values per hero. The library holds at most **64 field
  names** in total, including `test` (always registered, for experiments) and one `cd_<name>` per cooldown.
- **Integers only.** Fractions come from formulas (`"dmg": "-90% + 40% * ex_charge"`), not from fields.

**Monster values.** Native engine hooks can also store an integer on a Monster. The built-in
`ddx_set_<field>_<n>` / `ddx_add_<field>_<n>` effects write the effect target's Monster value, and the
library writes those values into the Monster's own save block. These values use the same globally registered
field names but a separate per-Monster store; config conditions, formulas, actions, and tooltips still read hero
fields only. Native skill-resolution code can query the active performer, skill, targets, and a target Monster's
value. There is not yet a JSON rule for applying an action based on a Monster value.

### Enums
`"enums": {"ex_mode": ["idle", "active"]}` names a field's values by index: `idle` = 0,
`active` = 1. Anywhere the config takes a value for that field (conditions, `set`, item limits) you
can write the name instead of the number, and templates and tooltips print the name (`{ex_mode}` → `active`).
Unset fields read 0, so make the first name your "nothing chosen yet" state.

### Limits
`"limits": {"ex_charge": [0, 3]}` clamps the field after every action and every tooltip `@` change. A limited
field also gives formulas and tooltips `ex_charge_min` / `ex_charge_max` (0 and 3 here), and it's what lets base tooltips
list a spender's values ([Tooltips](04-tooltips.md#base-tooltips-outside-a-raid)). Unlimited fields aren't
clamped.

## Campaign data
Native clients can also keep integers for the **whole campaign** (`campaigndata.c`): `campaigndata_field(name)` in
the client's init (a field is read back from a save only once its name is known), then `campaigndata_get` /
`campaigndata_has` / `campaigndata_set`. They're saved with the campaign's `persist.game.json` (inside its
`profile_options`, as `ddx_<name>`), read 0 until set, and clear when a campaign ends. Up to 32 fields, names under 40
characters. The library's own announcement uses one (`announced`). macOS only for now.

## Actions: effects that change fields
An action is named after an effect. **When an effect with that name applies to a hero whose class config
has the action, the action runs on that hero.** The library sees every effect application (it hooks the
game's `EffectApplyInternal`), so any way an effect can apply works: a skill, a camp skill, a supply item's use
effect, a trinket, a curio.

```json
"actions": {
    "ex_reset": {
        "remove_buffs": ["EX_TEMP_*"],
        "set": {"ex_charge": 0}
    },
    "ex_gain": {"add": {"ex_charge": 1}}
}
```

An action does, in order:
1. `remove_buffs`: removes every buff on the hero whose id matches one of the glob patterns (`*`, `?`, `[…]`),
   then lets the game recompute the hero's stats. No match is fine.
2. `set`: sets fields.
3. `add`: adds to fields (negative numbers subtract).
4. Clamps each changed field to its `limits`.

Rules that matter when you design skills:
- **The action runs on the effect's target.** An effect with `.target "performer"` changes the user of the
  skill; one with `.target "target"` changes the actor it lands on (for an ally-targeting skill, this is a hero).
  If the target isn't a hero of a class with that action, nothing happens.
- **The effect must really apply.** `.on_miss false` means a missed attack doesn't run the action. `.chance` applies too.
- **Once per skill use on the performer.** A multi-target skill applies its effects once per target, so an AoE
  builder would otherwise gain once per enemy hit. The library runs a given action on the performer at most
  once per skill use. (Outside a skill, from items or events, it always runs.)
- **Give the effect no payload.** It's a trigger. Add `.has_description false` so the tooltip doesn't show a
  meaningless line, and `.queue false` so it applies straight away (see [timing](#timing)).
- **A hook rule can run an action** without any effect (`{"action": "name"}`, [chapter 12](12-hook-points.md#from-a-config)).

The library also recognises two built-in effect name patterns, handy for experiments:
`ddx_set_<field>_<n>` and `ddx_add_<field>_<n>` (write `m` before the number for a negative:
`ddx_add_hp_m1`). They work for every Hero or Monster regardless of class and aren't clamped. Monster values
are saved in the monster's save block. And `remove_quirk_<quirk id>` removes that quirk from the target hero.

## Skill conditions: which skills a hero can use
A skill listed under `skills` with condition keys is only **shown and usable** while all its conditions hold:

```json
"skills": {
    "example_strike": {"ex_mode": "active"},
    "example_burst":  {"ex_charge_min": 1, "ex_charge_max": 3}
}
```

| Key | Holds when |
|---|---|
| `"<field>": value` | the field equals the value |
| `"<field>": [a, b, …]` | the field equals any of them |
| `"<field>_min": n` | field ≥ n |
| `"<field>_max": n` | field ≤ n |

The library filters the game's own list of the hero's usable skills (`Hero::GetModeSelectedCombatSkills`, the
same place vanilla checks `.valid_modes`). Everything that asks for that list agrees: the skill bar, the 1-4
hotkeys, out-of-turn skills, afflicted act-outs and the AI. A skill with no entry, or with an entry that has
no conditions, is always allowed.

`"hidden": true` removes a skill from the list for good while keeping it defined in the class (useful when
retiring a skill that existing heroes already own).

**Placeholder levels.** A skill with `"guild_first_rank": 2` treats its level 0 as a placeholder that every
hero owns but can't use: it's kept off the list until the hero buys level 1 in the Guild.
Details in [UI features](05-ui-features.md#placeholder-levels-and-guild_first_rank).

## Formulas: numbers that follow fields
A formula is a string with numbers, `%` (70% = 0.7), field names, `+ - * /` and brackets:
`"4 + 5 * ex_charge"`, `"-100% + 70% * ex_charge"`, `"(2 + 3 * ex_charge) / 2"`. Names are fields, plus `<field>_min` /
`<field>_max` for limited fields. Division by 0 gives 0. A syntax error or unknown name logs
`formula error: "…"` and the formula is ignored.

| Where | Replaces | Read from | When it's read |
|---|---|---|---|
| `skills.<id>.dmg` | the skill's `.dmg` (the damage modifier) | the user's fields | whenever the game computes the hero's stats: when the skill is selected, and again after changes |
| `skills.<id>.heal` | the skill's `.heal min max` (both, so no roll) | the user's fields | when the skill heals |
| `effects.<name>.heal` | that effect's `.heal` | the **performer's** fields (the effect's source) | each time the effect applies |

- `dmg` is a fraction, like `.dmg`: `-0.3` is -30%. The game still adds the skill's rank damage bonus on top,
  and buffs, crits and protection apply as normal.
- `heal` is healing points, rounded to the nearest integer. Heal buffs still apply.
- Because the damage modifier is computed when the skill is selected, a spender's damage uses the resource
  *before* the skill's own spend effect runs.
- Effect heal formulas read the caster's fields, including when the effect heals another hero.
- Formulas also drive tooltip numbers, so a tooltip can never disagree with what the skill does.

## Effect variants
`"effect_variants": {"ex_effect": "ex_effect_{ex_charge}"}` swaps an effect for another one, chosen by the **performer's**
fields, before it applies: `ex_effect` becomes `ex_effect_2` at 2 Charge, if an effect with that name exists (if not,
the original is used and the log says so). Templates use `{field}`, with enum fields as names. Actions then match
the swapped name. Use this when a formula isn't enough: a different buff, a different target, a stun that
only lands at full resource. For plain heal amounts prefer an `effects` formula (one effect instead of one per
value).

## Events
`events` runs actions on every hero of the class at points in the game, with no effect needed:

| Event | When (per hero of the class) | Game function |
|---|---|---|
| `raid_start` | an expedition starts | `Hero::OnRaidStart` |
| `raid_finish` | an expedition ends | `Hero::OnRaidFinish` |
| `battle_start` | a battle begins | `Hero::OnBattleCreate` |
| `battle_end` | a battle ends | `Hero::OnBattleFinish` |
| `battle_retreat` | after retreating from a battle | `Hero::OnBattleAfterRetreat` |
| `turn_start` | the hero's turn starts: after its DoT ticks, before it acts (a stunned turn too) | `Battle::IncrementState` (into `bs_before_turn_Bark`, before act-outs), `Battle::SetState` (stunned) |
| `turn_end` | the hero's turn ends: after its burn tick (stunned too) | `Battle::IncrementState` (out of `bs_after_turn_actor_dot`) |
| `round_end` | at the end of each round | `Hero::OnRoundEnd` |
| `camp_start` | camping begins (**untested**) | `Hero::OnCampStart` |

Actions run after the game's own function (for `turn_start`, before the hero acts).
For example, `"raid_start": ["ex_reset"]` can clear a temporary resource at expedition start.
Check the hook's platform coverage and installation log before relying on it.

## Animation variants
`"animation_variants": ["charge_{ex_charge}"]` tries `<anim>_charge_<value>` in place of `<anim>`.
At `ex_charge` = 2, the game tries `combat_charge_2` instead of `combat`. If the sprite lacks the variant,
it plays the original animation. Templates are tried in order, and the first existing variant wins.

Supply variants for each animation state you want to change. Restore any modified slot colours or attachments
in the plain animation too. The game's skeleton format is Spine 2.1.27.

## Cooldowns
```json
"cooldowns": {"ex_shared": {"turns": 1, "unit": "own"}},
"skills": {
    "example_strike": {"cooldown": "ex_shared"},
    "example_burst":                 {"cooldown": "ex_shared"}
}
```
After a hero uses a skill with a cooldown, every skill naming the same cooldown is greyed out and refused for
the next `turns` turns. `"unit": "own"` counts the hero's own turns; `"actor"` counts every actor's turn
(heroes and monsters; **untested**). Cooldowns reset when a battle starts or ends. The AI and afflicted
act-outs respect them. The counter is a field `cd_<name>`, so it's saved and readable in tooltips.

## Item limits
DD1 hard-codes who may use each vanilla supply item, and has no rule for mod items (anyone could use them).
`"items": {"ex_refill": {"combat": false}}` limits a custom supply item to this class outside battle.
The item's use effect goes through the same effect hook, so an action named after the item can change the
recipient's fields. Add field conditions to restrict when the item can be used.

## Timing
- **Effects apply in the order the skill lists them.** Put an effect that reads a resource before the
  effect that clears it.
- **Change fields with unqueued effects** (`.queue false`). The hero's return-to-combat pose picks the
  animation variant; a queued spend landed after the pose and left stale visuals until the next animation.
- **Damage formulas read the fields when the skill is chosen**, so a spender's hit uses the resource before
  its own spend.

## Who "the hero" is
| Feature | Hero whose fields are read or written |
|---|---|
| Action from an effect | the effect's target (performer if `.target "performer"`) |
| Effect variant, effect heal formula | the performer |
| Skill conditions, `dmg`/`heal` formulas, cooldowns | the skill's user |
| Animation variants | the animated hero |
| Events | each hero of the class the event fires for |
| Item limits | the hero the item is used on |
| Tooltips | the hero the tooltip is about (combat bar: the selected hero) |
