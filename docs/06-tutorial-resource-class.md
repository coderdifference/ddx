# 6. Tutorial: a class with a resource

This walks through a small class built the way the Paladin is: a **Berserker** (`berserker`) with **Rage**, a
resource of 0-5 that builder skills add to and spender skills use up.

| Skill | Kind | Does |
|---|---|---|
| Rending Strike | builder | melee hit, +1 Rage |
| Execute | spender | melee hit, damage grows with Rage, needs 1+, uses it all |
| Second Wind | spender | heals self 3 per Rage, needs 2+, uses it all |

Rage is kept between battles within an expedition and reset at its start; each hero's Rage shows as a glow
(optional art step).

The Berserker is an illustration, not a mod in this repository. Every engine mechanism it uses is confirmed on
the Paladin, and its skill lines are modelled on the Paladin's, but check any data line you write against a
vanilla example (for a self-heal, the Hellion's Adrenaline Rush) before relying on it. The class's other files (weapons, armour, camp skills, trinkets, art) are an ordinary DD1
class: follow `the game's ordinary modding formats` for those. This page covers what the engine adds.

## 1. Start from a normal class mod
Make the class as you would without the engine; copying a vanilla class (the Paladin started from the
Crusader) is the quickest start. Pick a short class id: string keys like
`combat_skill_name_<class>_<skill>` must stay under about 60 characters (`the game's ordinary modding formats`).

Prefix your effects, buffs and strings with a short tag (`bk_` here) so they can't clash with vanilla or
other mods.

## 2. Skills (`heroes/berserker/berserker.info.darkest`)
Skills are plain DD1 skills. The engine only needs them to apply the right effects. Five levels each, as usual
(two shown):

```
combat_skill: .id "rending_strike" .level 0 .type "melee" .atk 85% .dmg -20% .crit 4% .launch 21 .target 12 .is_crit_valid True .effect "bk_rage_gain" .generation_guaranteed true
combat_skill: .id "rending_strike" .level 1 .type "melee" .atk 90% .dmg -20% .crit 5% .launch 21 .target 12 .is_crit_valid True .effect "bk_rage_gain"

combat_skill: .id "execute" .level 0 .type "melee" .atk 85% .dmg -50% .crit 2% .launch 21 .target 12 .is_crit_valid True .effect "bk_rage_spend" .generation_guaranteed true
combat_skill: .id "execute" .level 1 .type "melee" .atk 90% .dmg -50% .crit 3% .launch 21 .target 12 .is_crit_valid True .effect "bk_rage_spend"

combat_skill: .id "second_wind" .level 0 .heal 6 6 .launch 4321 .target .effect "bk_rage_spend" .is_stall_invalidating false .generation_guaranteed true
combat_skill: .id "second_wind" .level 1 .heal 6 6 .launch 4321 .target .effect "bk_rage_spend" .is_stall_invalidating false
```

- The `.dmg` on Execute and the `.heal` on Second Wind are overwritten by formulas (step 4), but keep sensible
  values: they're what the game shows if the library isn't loaded.
- `.target` with nothing after it is a self-target skill (as the Paladin's Light of Dawn).

## 3. Effects (`effects/berserker.effects.darkest`)
The effects are triggers: named after config actions, with no payload.

```
effect: .name "bk_rage_gain" .target "performer" .chance 100% .on_hit true .on_miss false .queue false .has_description false
effect: .name "bk_rage_spend" .target "performer" .chance 100% .on_hit true .on_miss true .queue false .has_description false
```

- `.target "performer"`: the action changes the hero using the skill.
- `.on_miss false` on the builder: a miss builds no Rage. `.on_miss true` on the spender: a miss still uses it
  (your choice; the Paladin's spenders spend either way).
- `.queue false`: change the field straight away, before the hero's return pose picks the animation.
- `.has_description false`: no tooltip line for the trigger.

## 4. The engine config (`engine/berserker.engine.json`)
```json
{
    "class": "berserker",
    "fields": ["rage"],
    "limits": {"rage": [0, 5]},
    "actions": {
        "bk_rage_gain":  {"add": {"rage": 1}},
        "bk_rage_spend": {"set": {"rage": 0}},
        "bk_rage_reset": {"set": {"rage": 0}}
    },
    "events": {
        "raid_start": ["bk_rage_reset"]
    },
    "skills": {
        "rending_strike": {
            "tooltip": [
                {"text": "@bk_tt_gain", "rage_max": 4},
                {"text": "@bk_tt_full", "rage_min": 5},
                {"text": "@bk_tt_damage"}
            ],
            "tooltip_replace": true
        },
        "execute": {
            "rage_min": 1,
            "dmg": "-60% + 30% * rage",
            "tooltip": [
                {"text": "@bk_tt_spend"},
                {"text": "@bk_tt_damage"},
                {"text": "@bk_tt_next", "base": false, "rage_max": 4},
                {"text": "@bk_tt_damage", "with": "rage+1", "rage_max": 4}
            ],
            "tooltip_replace": true
        },
        "second_wind": {
            "rage_min": 2,
            "heal": "3 * rage",
            "tooltip": [
                {"text": "@bk_tt_spend"},
                {"text": "@bk_tt_heal"}
            ],
            "tooltip_replace": true
        }
    },
    "debug": true
}
```

What each part does:
- `fields` / `limits`: one integer per hero, clamped to 0-5. It's saved with the game as `cp_rage`.
- `actions`: `bk_rage_gain` runs whenever the `bk_rage_gain` effect applies to a Berserker, and so on.
  `bk_rage_reset` has no effect in the data at all: only the event uses it.
- `events.raid_start`: each expedition starts at 0 Rage.
- `skills.execute.rage_min: 1`: Execute is hidden until the hero has Rage. Second Wind needs 2.
- `dmg`: Execute's damage modifier is -30% at 1 Rage, 0% at 2, up to +90% at 5. It's read when the skill is
  selected, so it uses the Rage before the skill's own spend.
- `heal`: Second Wind heals exactly 3 per Rage (6 to 15).
- `tooltip`: live lines, [step 5](#5-strings).
- `debug`: detailed log lines while you test. Remove it before release.

## 5. Strings
In your one string table (`localization/<ModName>.string_table.xml`), with the class's usual skill names and
descriptions:

```xml
<entry id="bk_tt_gain"><![CDATA[{colour_start|notable}Gain 1 Rage{colour_end}]]></entry>
<entry id="bk_tt_full"><![CDATA[{colour_start|notable}Rage Full ([[rage]]/[[rage_max]]){colour_end}]]></entry>
<entry id="bk_tt_spend"><![CDATA[{colour_start|notable}Spend [[rage]] Rage{colour_end}]]></entry>
<entry id="bk_tt_next"><![CDATA[{colour_start|notable}Next Rage:{colour_end}]]></entry>
<entry id="bk_tt_damage"><![CDATA[{colour_start|harmful}Damage{colour_end} [[hit_min]]-[[hit_max]]]]></entry>
<entry id="bk_tt_heal"><![CDATA[{colour_start|healhp}Heal Self{colour_end} [[heal]]]]></entry>
```

At 3 Rage, Execute's tooltip reads:
```
Execute (1)
Spend 3 Rage
Damage 7-13
Next Rage:
Damage 9-17
```
(the ranges depend on the weapon and buffs). In the Guild and on the town character sheet the same skill shows
its base tooltip: "Spend 1 Rage / Damage …" through "Spend 5 Rage / Damage …", one block per Rage value, because
`rage_min` on a limited field makes it a spender ([Tooltips](04-tooltips.md#base-tooltips-outside-a-raid)).

For your own colour (the Paladin's Holy Power gold), add `colours/berserker.colours.darkest`:
```
colour: .id "bk_rage"                                               .rgba #ff5a3c
```
and write `{colour_start|bk_rage}`.

Rebuild the localization after changing strings or adding files:
```sh
python3 tools/localization.py <ModName>/localization/<ModName>.string_table.xml
```

## 6. Deploy and test
```sh
cp -R <ModName> "<game data>/mods/"
engine_hooks/run_game.sh -skipfeflowintro -disablevideo -enabledebuginput -skiptutorial
```

Check `ddx.log` at launch:
```
config berserker: 3 skill rules, 3 actions, 0 animation variants, 0 effect variants, 1 event actions (…)
```
Start an expedition with a Berserker and check, in order:

| Do | Expect in game | Expect in the log (debug on) |
|---|---|---|
| Start the expedition | | `event raid_start: hero N -> bk_rage_reset` |
| First battle, look at the bar | Rending Strike only (Execute and Second Wind hidden) | |
| Hit with Rending Strike | Execute appears | `bk_rage_gain: hero N rage 0 -> 1` |
| Miss with Rending Strike | no change | (no line) |
| Hover Execute | "Spend 1 Rage", a damage range | `dmg formula: … modifier -0.30 …` |
| Use Execute | Execute disappears | `bk_rage_spend: hero N rage 1 -> 0` |
| Build to 2+, use Second Wind | heals 3 × Rage | `heal formula: hero N skill heal 6 …` |
| Build to 5, hit again | stays 5 | `rage 5 -> 5` |
| End the battle, start another | Rage kept | |
| Save, quit, reload | Rage kept | `restored hero N rage = …` |

## 7. Optional: show Rage on the hero
Add `"animation_variants": ["rage_{rage}"]` and give the hero's skeletons animations `combat_rage_1` …
`combat_rage_5` (and `walk_…`, `idle_…` if you want it outside battle too). The hero plays `combat_rage_3`
at 3 Rage and plain `combat` at 0 (no `combat_rage_0` needed: a missing variant falls back to the plain
animation). A plain animation doesn't reset slot colours or attachments the variants set, so key them back
in the plain animation too. your own art or data tools is a worked example of adding such animations to a
Spine 2.1.27 skeleton from Python.

## Where to go next
- A class that chooses a role at the start of each expedition (the Paladin's specs): [Cookbook](07-cookbook.md#choosing-a-spec-or-stance).
- Skills that must be bought before use, more than four skills, a Guild layout: [UI features](05-ui-features.md).
- Something going wrong: [Testing and debugging](08-testing-and-debugging.md).
