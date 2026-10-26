# 7. Cookbook

Short recipes, each with the data and config it needs. Where a recipe comes from the Paladin, the files named
are in `CataclysmicPaladin/`; recipes marked *combination* put together confirmed pieces in a way the Paladin
doesn't use yet, so test them.

## Builder and spender
The whole pattern is in the [tutorial](06-tutorial-resource-class.md). In short:

```
effect: .name "x_gain"  .target "performer" .chance 100% .on_hit true .on_miss false .queue false .has_description false
effect: .name "x_spend" .target "performer" .chance 100% .on_hit true .on_miss true  .queue false .has_description false
```
```json
"fields": ["res"], "limits": {"res": [0, 3]},
"actions": {"x_gain": {"add": {"res": 1}}, "x_spend": {"set": {"res": 0}}},
"skills": {"spender": {"res_min": 1, "dmg": "-100% + 70% * res"}}
```

To spend a fixed amount instead of everything, use `{"add": {"res": -2}}` and `"res_min": 2`.

## Choosing a spec or stance
The Paladin picks one of three specialisations at its first battle of each expedition. Pieces:

1. **Picker skills**, one per spec, shown only without a spec. Free actions so the hero still acts that turn:
   ```
   combat_skill: .id "pick_holy" .level 0 .type "ranged" .atk 0% .dmg 0% .crit 0% .launch 4321 .target  .is_crit_valid True .effect "cp_pick_holy" "cp_holy_spec_buff_0" .is_continue_turn true .per_turn_limit 1 .is_stall_invalidating false .generation_guaranteed true
   ```
2. **An action** per picker that sets the spec (and resets the resource):
   ```json
   "enums": {"spec": ["none", "holy", "prot", "ret"]},
   "actions": {"cp_pick_holy": {"set": {"spec": "holy", "hp": 0}}},
   "skills": {"pick_holy": {"spec": "none"}, "holy_shock": {"spec": "holy"}}
   ```
3. **A spec buff** applied by the same skill, lasting the expedition:
   ```
   effect: .name "cp_holy_spec_buff_0" .target "performer" .curio_result_type "positive" .chance 100% .buff_ids CP_HOLY_SPEC_0 .buff_duration_type quest_end .duration 1 .on_hit true .on_miss true .queue false
   ```
   The buff is a JSON buff in `shared/buffs/cpaladin.buffs.json` (template in `the game's ordinary modding formats` §4).
   One buff per picker level lets the spec skill's rank scale it.
4. **A reset** that removes the buff and clears the spec, run at the start of each expedition:
   ```json
   "actions": {"cp_reset": {"remove_buffs": ["CP_HOLY_SPEC_*", "CP_PROT_SPEC_*", "CP_RET_SPEC_*"],
                            "set": {"spec": "none", "hp": 0}}},
   "events": {"raid_start": ["cp_reset"]}
   ```
5. **Grouping** (optional): `guild_row` / `sheet_page` so each spec's skills sit together in the Guild, on
   the character sheet and on bar pages ([UI features](05-ui-features.md)).

An afflicted hero that acts out while without a spec can pick one; the Paladin accepts this.

## A supply item that changes fields
The Paladin's Libram of Rededication resets the spec outside battle.

`inventory/cpaladin.supply.inventory.items.darkest`:
```
inventory_item:	.type "supply"		.id "cp_respec"		.base_stack_limit 4		.purchase_gold_value 250		.sell_gold_value 25
```
`effects/cpaladin.effects.darkest` (the item's use effect is the effect named after the item; `.item 1`):
```
effect: .name "cp_respec" .target "performer" .item 1 .chance 100% .on_hit true .on_miss true .queue false .has_description false
```
`engine/cpaladin.engine.json`:
```json
"actions": {"cp_respec": {"remove_buffs": ["CP_HOLY_SPEC_*", "CP_PROT_SPEC_*", "CP_RET_SPEC_*"],
                          "set": {"spec": "none", "hp": 0}}},
"items": {"cp_respec": {"spec": ["holy", "prot", "ret"], "combat": false}}
```
Plus: the provision store stock in `campaign/provision/cpaladin.provision.json` (per raid length), the icon
`panels/icons_equip/supply/inv_supply+cp_respec.png` (72 × 144; your own art or data tools), and the strings
`str_inventory_title_supplycp_respec` / `str_inventory_description_supplycp_respec`. Without the `items` entry
any hero could use it, in battle too.

## Heal the party by the caster's resource
Light of Dawn heals every other hero 2 + 3 per Holy Power. The heal is on an effect, so it's an `effects`
formula, which reads the **caster's** fields:
```
combat_skill: .id "light_of_dawn" .level 1 … .target  … .effect "cp_lod_heal" "cp_hp_spend" …
effect: .name "cp_lod_heal" .target "performer_group_other" .curio_result_type "positive" .chance 100% .heal 5 .on_hit true .on_miss true .queue false .source_heal_type hero_skill
```
```json
"effects": {"cp_lod_heal": {"heal": "2 + 3 * hp"}}
```
List the heal before the spend: effects apply in list order. Tooltip token: `[[heal_cp_lod_heal]]`.

## A different effect at each resource level
When a number isn't enough (a stun only at full, a different buff per level), make one effect per level and
let `effect_variants` pick:
```
effect: .name "x_finisher_1" …
effect: .name "x_finisher_2" …
effect: .name "x_finisher_3" .stun 1 …
```
```json
"effect_variants": {"x_finisher": "x_finisher_{res}"}
```
The skill lists `.effect "x_finisher"`. The variant is chosen by the performer's fields; if `x_finisher_0` doesn't
exist, `x_finisher` itself applies (so define `x_finisher` as the fallback). Confirmed on the Paladin's earlier
Light of Dawn (per-level heal effects); now replaced by a formula.

## Shared cooldown
```json
"cooldowns": {"prot_builder": {"turns": 1, "unit": "own"}},
"skills": {"prot_crusader_strike": {"cooldown": "prot_builder"},
           "hotr": {"cooldown": "prot_builder"}}
```
Using either greys out both for the hero's next turn. Resets each battle. Add `cooldown_tooltip` lines so
players see why ([Tooltips](04-tooltips.md#cooldown-lines)).

## AoE builder that gains once
Nothing to do: an action on the performer runs once per skill use, however many targets the skill hits. (A
vanilla per-mode effect list, by contrast, applies once per target.)

## Skills bought in the Guild before first use
Placeholder level 0 plus `"guild_first_rank": 2`, then gate level 1 in the upgrade tree
([UI features](05-ui-features.md#placeholder-levels-and-guild_first_rank)). The Paladin's tier 1 skills need
their spec skill at rank 2 or 3.

## Retire a skill that heroes already own
Removing a skill from the class leaves existing heroes with a skill id the class no longer defines. Two options:
- keep it defined and add `"hidden": true` (the Paladin did this before removing Change Specialisation);
- provide an explicit save migration that removes the retired skill. Quit the game and back up the profile before editing saves.

## Show state on the hero
`"animation_variants": ["{spec}_{hp}"]` plus skeleton animations `combat_holy_1` … (see
[concepts](02-concepts.md#animation-variants)). Several templates give fallbacks:
`["{spec}_{hp}", "{spec}"]` plays `combat_holy` when there's no `combat_holy_2`.

## Remove a quirk
An effect named `remove_quirk_<quirk id>` (no payload) removes that quirk from the hero it applies to, like the
Sanitarium does:
```
effect: .name "remove_quirk_kleptomaniac" .target "performer" .chance 100% .on_hit true .on_miss true .queue false .has_description false
```
Confirmed mid-battle (no announcement; the character sheet updates). Works for any class, no config needed.

## A resource that decays (*combination*)
Lose 1 each time the hero's turn ends, and start each battle empty:
```json
"actions": {"x_decay": {"add": {"res": -1}}, "x_clear": {"set": {"res": 0}}},
"events": {"turn_end": ["x_decay"], "battle_start": ["x_clear"]}
```
With `limits` the value can't go below the minimum. Note `turn_end` runs after the hero's turn, so a point
gained this turn decays at its end: add 1 more per build, or decay on `turn_start` instead.

## Tooltip lines by state
Any condition works on a line:
```json
"tooltip": [
    {"text": "@cp_tt_gain_hp", "hp_max": 2},
    {"text": "@cp_tt_hp_full", "hp_min": 3}
]
```
"Gain 1 Holy Power" below 3, "Holy Power Full (3/3)" at 3.
