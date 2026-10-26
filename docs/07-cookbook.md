# 7. Cookbook

These examples show individual DDX features. JSON blocks are sections to merge into a class config, rather
than complete files. Replace the example IDs with your own prefixed names and test each mechanic in game.
The [resource-class tutorial](06-tutorial-resource-class.md) combines fields, actions, formulas and tooltips.

## Builder and spender

Define effects that trigger actions on the performer:

```
effect: .name "ex_gain" .target "performer" .chance 100% .on_hit true .on_miss false .queue false .has_description false
effect: .name "ex_spend" .target "performer" .chance 100% .on_hit true .on_miss true .queue false .has_description false
```

Then configure the field, actions and spender condition:

```json
"fields": ["ex_charge"],
"limits": {"ex_charge": [0, 3]},
"actions": {
    "ex_gain": {"add": {"ex_charge": 1}},
    "ex_spend": {"set": {"ex_charge": 0}}
},
"skills": {
    "example_burst": {"ex_charge_min": 1, "dmg": "-100% + 70% * ex_charge"}
}
```

Attach `ex_gain` to the builder skill and `ex_spend` to the spender. To spend a fixed amount instead of
everything, use `{"add": {"ex_charge": -2}}` and require `"ex_charge_min": 2`.

## Reset or decay a field

```json
"actions": {
    "ex_decay": {"add": {"ex_charge": -1}},
    "ex_clear": {"set": {"ex_charge": 0}}
},
"events": {
    "turn_end": ["ex_decay"],
    "battle_start": ["ex_clear"]
}
```

A lower limit of 0 prevents decay from making the value negative. `turn_end` runs after the hero's turn,
so a point gained that turn also decays. Use `turn_start` if the deduction should happen before the hero acts.
Check the event's platform coverage in the [hook reference](12-hook-points.md).

## A supply item that changes a field

A custom supply item's use effect can trigger an action with the same name:

```
inventory_item: .type "supply" .id "ex_refill" .base_stack_limit 4 .purchase_gold_value 250 .sell_gold_value 25
effect: .name "ex_refill" .target "performer" .item 1 .chance 100% .on_hit true .on_miss true .queue false .has_description false
```

Put the inventory and effect definitions in their respective `.darkest` files. In the class config:

```json
"actions": {"ex_refill": {"set": {"ex_charge": 3}}},
"items": {"ex_refill": {"combat": false}}
```

Supply the item's icon, title and description strings, and add it to the provision stock if players should
buy it there. The `items` entry restricts use to the configured class and, here, to outside battle.

## An effect's healing formula

```json
"effects": {"ex_heal": {"heal": "2 + 3 * ex_charge"}}
```

Define `ex_heal` as an ordinary healing effect in the mod data. The formula reads the performer's field,
even when another hero receives the heal. If the skill also clears the field, list the heal before the clear
effect. Use `[[heal_ex_heal]]` in the tooltip to show the formula result.

## A different effect at each resource level

When a formula cannot express the change, define separate effects and select one with a template:

```json
"effect_variants": {"ex_finisher": "ex_finisher_{ex_charge}"}
```

Define `ex_finisher_1`, `ex_finisher_2` and `ex_finisher_3` in the effect data, and list `ex_finisher` on
the skill. The variant uses the performer's field. Define the original `ex_finisher` too: it is the fallback
when the selected variant does not exist.

## Shared cooldown

```json
"cooldowns": {"ex_shared": {"turns": 1, "unit": "own"}},
"skills": {
    "example_strike": {"cooldown": "ex_shared"},
    "example_burst": {"cooldown": "ex_shared"}
}
```

Using either skill blocks both for the hero's next turn. Cooldowns reset each battle. Add
[cooldown tooltip lines](04-tooltips.md#cooldown-lines) so players can see why a skill is unavailable.

## AoE builder that gains once

An action on the performer runs once per skill use, however many targets the skill hits. Use a performer-targeted
trigger effect for a single gain per attack; ordinary per-target effects still follow the game's targeting rules.

## Skills bought in the Guild before first use

Give the skill a placeholder level 0 and set `"guild_first_rank": 2`. Define its first usable rank at level 1
and gate it in the upgrade tree. See [placeholder levels](05-ui-features.md#placeholder-levels-and-guild_first_rank).

## Retire a skill that heroes already own

Removing a skill definition can leave existing heroes with an unknown skill ID. Keep it defined with
`"hidden": true`, or provide a save migration that removes it. Back up the profile and quit the game before
editing saved data.

## Show a field through animation

```json
"animation_variants": ["charge_{ex_charge}"]
```

Provide animations such as `combat_charge_1` in the hero's skeleton. A missing variant falls back to the plain
animation. Restore any changed slot colours or attachments in that plain animation too. See
[animation variants](02-concepts.md#animation-variants).

## Remove a quirk

An effect named `remove_quirk_<quirk id>` removes that quirk from its target hero:

```
effect: .name "remove_quirk_kleptomaniac" .target "performer" .chance 100% .on_hit true .on_miss true .queue false .has_description false
```

This uses the game's quirk-removal operation and needs no class config.

## Tooltip lines by state

Inside a skill's config:

```json
"tooltip": [
    {"text": "@ex_tt_gain", "ex_charge_max": 2},
    {"text": "@ex_tt_full", "ex_charge_min": 3}
]
```

Define the string entries in the mod's localization table. This shows the gain message below 3 charges and
the full message at 3. Conditions read the hero described by the tooltip.
