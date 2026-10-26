# Combat and event mods

Use a named hook for gameplay events. Use a catalogued wrapper only when the event API cannot express the change. Both routes keep patch ownership in DDX.

## A config-only rule

The native example's `ddx_example.engine.json` also works as a data-only example:

```json
{
  "hooks": [
    {"point": "kill", "class": "crusader", "do": [{"stress": -2}]},
    {"point": "hp_damage", "phase": "before", "match": {"type": "bleed"}, "when": "torch >= 75", "scale": 0.5}
  ]
}
```

The first rule eases a Crusader's stress after a kill. The second scales bleed damage before application while torch is high. Put the file under your mod's `engine/` directory with a `.engine.json` extension. Prefix any fields and actions you add. Read [hook rules](12-hook-points.md) for the context and filters available at each point.

## A native listener

`example_kills.c` registers `kill`, `crit` and `stress_damage`. The kill listener checks that the actor is a hero before updating its saved counter. The crit listener calls `popup_text`. The stress listener changes `ctx->amount` during the before phase and ignores synthetic events.

An after listener observes the result. A before listener can change only the fields that the specific hook supports. `cancel` is not universally implemented; consult the point's contract rather than assuming it suppresses every event.

## Effects and secondary damage

Use DDX game actions and combat services to route damage, healing, stress and effects through the game's own logic. For follow-up hits, read `combat_apply_followup_hit` and the side-effect filter contract. For additional effects, use `combat_apply_side_effect` so native chance and resistance handling remain involved.

Secondary actions can dispatch more hooks. Use synthetic-event checks or a bounded re-entry guard where the mechanic would otherwise trigger itself. Avoid retaining event-context pointers after the callback.

## Validate the mechanic

Test hero and monster actors, multiple targets, misses, dodges, critical hits, deaths, repeated effects and nested triggers. Save/reload if you keep counters. Disable the mod for a campaign and confirm its callbacks stop taking effect. Review the relevant hook installation messages on both platforms; the references include platform limitations that compilation cannot detect.
