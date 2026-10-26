# 4. Tooltips

DD1 builds each skill's tooltip text once, when the game loads, per class and skill level. It can't show a
number that depends on the hero. The engine rewrites skill tooltips as they're shown, so they can carry live
values (the hero's fields, formula results, the real damage range) and lines that appear only in some states.

## Where it applies
- **Combat skill tooltips** everywhere the game builds them: the combat bar, the raid's skill bar out of
  battle, the character sheet and the town's party quick-select. These are **live**: they read the hero's
  fields. (On the combat bar the hero is the raid's selected hero, as the bar shows.)
- **Base tooltips** (not live) in the Guild and on the character sheet in town, [below](#base-tooltips-outside-a-raid).
- Classes without a config only get the [title](#titles) change.

## What happens to a configured class's tooltip
1. **Info text rebuilt with formula values.** For a skill with a `dmg` or `heal` formula, or an effect with
   an `effects` heal formula, the game's own tooltip builder is run again with the formula values put on the
   skill, so the game's lines ("Heal 14-14", "DMG mod +40%", "Other Heroes: Heal 8 HP") show the hero's
   numbers. The result is cached per skill and hero state.
2. **Tokens filled** in every line: `[[…]]`, [below](#tokens).
3. **Title** (the name line) from the title template.
4. **Cooldown line** first, under the name, if the class has `cooldown_tooltip` / `cooldown_ready_tooltip`
   and the skill has a cooldown.
5. **Config lines** (`skills.<id>.tooltip`) whose conditions hold, after the info text, or in its place with
   `"tooltip_replace": true`.

## Config lines
```json
"example_mend": {
    "tooltip": [
        {"text": "@ex_tt_spend"},
        {"text": "@ex_tt_heal_target"},
        {"text": "@ex_tt_next", "base": false, "ex_charge_max": 2},
        {"text": "@ex_tt_spend", "with": "ex_charge+1", "ex_charge_max": 2},
        {"text": "@ex_tt_heal_target", "with": "ex_charge+1", "ex_charge_max": 2}
    ],
    "tooltip_replace": true
}
```
with, in the string table:
```xml
<entry id="ex_tt_spend"><![CDATA[{colour_start|ex_charge_colour}Spend [[ex_charge]] charges{colour_end}]]></entry>
<entry id="ex_tt_heal_target"><![CDATA[{colour_start|healhp}Heal Target{colour_end} [[heal]]]]></entry>
<entry id="ex_tt_next"><![CDATA[{colour_start|notable}Next Charge:{colour_end}]]></entry>
```
With `"heal": "4 + 5 * ex_charge"`, at 2 charges the tooltip reads:
```
Example Mend (1)
Spend 2 charges
Heal Target 14
Next Charge:
Spend 3 charges
Heal Target 19
```
and at 3 charges the last three lines are gone (`ex_charge_max: 2`).

- A line is a string, or an object with `text` and optional `font`, `with`, `base` and conditions
  ([reference](03-config-reference.md#tooltip-lines)).
- `@<string id>` takes the text from the string table (the game's `Localization::LocalizeStringFromId`, so
  it's translated like any string). Keep tooltip text in the string table; inline text works but isn't
  translatable.
- Conditions on a line are the same as skill conditions, read on the tooltip's hero.
- `"tooltip_replace": true` drops the game's info text (stats and effect lines). `[[info]]` in a config line
  puts that text back where you want it, with live values.

## Tokens
`[[expr]]` or `[[expr|fmt]]` anywhere in a tooltip line: in config lines, in your string table entries, and in
the game's own lines for the class.

`expr` is a [formula](02-concepts.md#formulas-numbers-that-follow-fields) over these names:

| Name | Value |
|---|---|
| any field | the hero's value (0 without a hero). A bare field with no format prints the enum name if it has one. |
| `<field>_min`, `<field>_max` | the field's `limits` |
| `dmg` | the skill's `dmg` formula, else its `.dmg` (fraction) |
| `heal` | the skill's `heal` formula, else its `.heal` minimum |
| `heal_min`, `heal_max` | the skill's `.heal` range |
| `heal_<effect>` | that effect's `effects` heal formula (`[[heal_ex_heal]]`) |
| `acc`, `crit` | the skill's `.atk` and `.crit` (fractions: use `\|%`) |
| `hit_min`, `hit_max` | the hero's damage range with this skill, [below](#damage-ranges) |
| `level` | the skill's level (0-based) |
| `rank` | the rank players see: `level + 2 - guild_first_rank` (level 0 is rank 1, or with a placeholder level 1 is rank 1) |
| `cooldown` | turns left on the skill's cooldown (0 when ready) |
| `cooldown_turns` | the skill's cooldown length |

`fmt` is empty (a rounded number), `+` (signed: `+40`), `%` (× 100 with a `%`: 0.4 → `40%`) or `+%`
(`+40%`).

A token that can't be evaluated prints as `[[?expr]]`, which makes mistakes easy to spot.

### "What if" tokens: `@`
`[[expr@field+n]]`, `@field-n` or `@field=n` evaluates the token with that field changed (clamped by
`limits`), leaving the hero untouched: `[[heal@ex_charge+1]]` is the heal at one more charge. A config line's
`"with": "ex_charge+1"` applies that change to every token in the line that has no `@` of its own, so one string serves
for both the current line and the "Next Charge" line.

### Damage ranges
`[[hit_min]]-[[hit_max]]` prints the damage the hero would deal with the skill, computed the way the game does:
the hero's weapon damage × (1 + the skill's damage modifier + its rank bonus), plus the flat and percentage
damage from buffs, rounded up. It's before the target's protection and before crits. The modifier is the
skill's `dmg` formula (with any `@` change) or its `.dmg`. With the `with`/`@` mechanism this gives "next
charge" damage ranges too:
```xml
<entry id="ex_tt_damage"><![CDATA[{colour_start|harmful}Damage Target{colour_end} [[hit_min]]-[[hit_max]]]]></entry>
```

## Titles
The name line comes from a template:
- `[[name]]` is the skill's plain name (without the game's level suffix);
- any other token works (`[[rank]]` usually).

The class-wide template is the top-level `tooltip_title`; a skill's own `tooltip_title` wins.
Use `"[[name]] {colour_start|neutral}([[rank]]){colour_end}"` to include the rank,
or `"[[name]]"` for an unranked skill such as Move.

**Every class**, configured or not, gets `Name (rank)` (the rank in the `neutral` colour) instead of the
game's `Name 1` in every skill tooltip. That's a global change the library makes; see
[Limits](10-limits-and-distribution.md#shared-behavior).

## Cooldown lines
```json
"cooldown_tooltip": ["@ex_tt_cooldown_1", "@ex_tt_cooldown_n"],
"cooldown_ready_tooltip": ["@ex_tt_cooldown_ready_1", "@ex_tt_cooldown_ready_n"]
```
```xml
<entry id="ex_tt_cooldown_n"><![CDATA[{colour_start|harmful}Cools down in [[cooldown]] turns{colour_end}]]></entry>
<entry id="ex_tt_cooldown_ready_n"><![CDATA[{colour_start|neutral}Cooldown: [[cooldown_turns]] turns{colour_end}]]></entry>
```
The first entry of each is used for 1 turn, the second for more. The line goes right under the name.

## Fonts
A line's font can be set with `"font": "<font_ref id>"` (ids from the game's `fonts/fonts.darkest`, e.g.
`tooltip_header`, `banner_hero_name`), or by starting the text with `{font|<id>}`, which lets a string table entry
pick its own font. An unknown id logs an error and keeps the line's usual font. Fonts change per line only:
the game's text renderer has inline codes for colour but none for fonts.

## Colours
Use the game's markup, `{colour_start|<colour id>}…{colour_end}`, in config lines and strings. Colour ids are
the game's (`harmful`, `healhp`, `notable`, `neutral`…) or your mod's own, from a `colours/<name>.colours.darkest` file:
```
colour: .id "ex_charge_colour"                                         .rgba #ffd966
```
The library encodes the markup the way the game's localization loader does (the renderer only understands the
encoded form), so the same text works in strings and in config lines.

## Base tooltips (outside a raid)
In the **Guild** and on the **character sheet in town**, a tooltip describes the skill, not the hero's current
state. There the library shows:
- the title template (with that level's `[[rank]]`);
- instead of the info text, the skill's config lines without `with` and without `"base": false`, with no live
  field values (fields read 0 unless listed per value, next point);
- for a skill with a `<field>_min` condition on a limited field (a spender), those lines once per value from
  the minimum to the limit: `Spend 1 … Heal Target 9`, `Spend 2 … Heal Target 14`, `Spend 3 … Heal Target 19`;
- damage ranges still from the hero's weapon;
- no cooldown lines.

The Guild shows the current and next rank separated by a blank line, and drops a placeholder level's block.
In a raid the character sheet stays live.

So write tooltip lines to work in both modes: a line that only makes sense live (like "Next Charge:") gets
`"base": false`.
