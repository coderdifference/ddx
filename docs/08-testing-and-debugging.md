# 8. Testing and debugging

## The log
`ddx.log`, next to the game's `app.log` in `…/Darkest.app/Contents/Resources/data/`. It's appended to on
every launch, one timestamped line per event; clear it between sessions if it gets long. A native client can log
to files of its own beside it: `log_file("<name>")` (no `.log`) in its `engine_client_init`, and its `logf_` lines go
to `<name>.log` and its `combat_logf` lines to `<name>_combat.log` (both started fresh each run; `ddx.log` says
`log_file: <mod> logs to <name>.log ...`). The library tells callers apart by their binary, so the client calls the
same functions. What the library logs about a mod (its config files, its client loading) stays in `ddx.log`.

Always logged:
- which hooks installed and which stayed off, and why ([Getting started](01-getting-started.md#read-the-log));
- one summary line per config file: `config <class>: N skill rules, N actions, …`, then the total classes and
  fields;
- config problems: `JSON error`, `missing "class"`, `bad value for field`, `unknown cooldown`,
  `bad tooltip line`, `too many fields / name too long`;
- runtime problems: `formula error: "…"`, `event …: no action named …`, `variant effect … not found`,
  `hero data table full`;
- `ddx_set_* / ddx_add_*` and `remove_quirk_*` effects, every time.

With `"debug": true` in any config (it's global), also:

| Line | Means |
|---|---|
| `<effect>: hero N <field> a -> b` | an action changed a field |
| `<effect>: hero N removed K buff(s)` | an action's `remove_buffs` ran |
| `<effect>: hero N, already applied this skill use` | a performer action skipped for the 2nd+ target of a skill |
| `event <event>: hero N -> <action>` | an event ran an action |
| `restored hero N <field> = v` | a field loaded from the save |
| `dmg formula: hero N skill <hash> modifier m (base b, game g)` | a damage formula applied (logged when it changes) |
| `heal formula: hero N skill heal v (was a-b)` | a skill heal formula applied |
| `effect heal formula: <effect> by hero N heal v (was w)` | an effect heal formula applied |
| `anim hero N: <anim> (file …) -> <variant>` / `no <variant> on the current sprite` | animation variant chosen or missing |
| `cooldown <name>: hero N skill <hash> -> t` | a cooldown started |
| `tooltip: rebuilt skill <hash> for hero N: …` | a tooltip's info text was rebuilt with formula values |
| `sheet page: hero N page p/n` | a character sheet tab was clicked |

Hero N is the hero's guid (the same number in every line for one hero). Skill hashes are the game's
`string_hash` of the skill id.

Turn `debug` off for release: it writes a line for every animation change.

## Offline tests

Run `engine_hooks/tests/run.sh` on macOS. It exercises config parsing, registries, rule dispatch, layout lookup, object adapters, ownership and hook relocation using stand-ins and executable fixtures. `tests/example_class.engine.json` is the checked-in config fixture. New config keys should receive checks in the relevant harness.

## Testing in game

Use a disposable campaign. Check every required hook's log entry, then trigger the behavior on each target platform. Include invalid and empty inputs, multiple targets, save/reload, campaign changes and interaction with another client. The macOS launch script starts fresh DDX logs; preserve logs before launching if you need the previous run.

Back up saves before changing persistent formats. Engine fields use prefixed save keys; test persistence by exiting and restarting the game rather than only reopening a screen.

## Known traps
| Symptom | Cause |
|---|---|
| A skill never appears | a typo'd key in its config entry became a condition ([Config reference](03-config-reference.md#pitfalls)); or a placeholder level with `guild_first_rank` not yet bought |
| An action does nothing | the effect's target isn't a hero of this class (check `.target`); the effect missed with `.on_miss false`; the effect name differs from the action name |
| An AoE skill changes a field once, not per target | by design: performer actions run once per skill use |
| A spender's damage ignores the resource | no `dmg` formula, or the formula has an error (look for `formula error`) |
| The hero's look updates one action late | the field change is in a queued effect (`.queue true`); make it unqueued |
| A heal effect reads the wrong resource | effect formulas read the performer, and effects apply in list order: put the heal before the spend |
| `[[?something]]` in a tooltip | an unknown name in a token, or `@` on an unknown field |
| Colour markup shows as text | a typo in `{colour_start|…}`, or an unknown colour id |
| A hook shows `already hooked` | two modules hook the same function: one must own it and share it ([Extending](09-extending-the-engine.md)) |
| A hook shows `unexpected prologue` | the game was updated; the library needs updating for that function ([Extending](09-extending-the-engine.md)) |
| Changes to the config don't show | the config is read at launch only: restart the game |
| Everything engine-related does nothing | the game was started from Steam (no library): use `run_game.sh` |

