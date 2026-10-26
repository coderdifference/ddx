# Building mods with DDX

Choose the smallest integration that gives your mod the behavior it needs. JSON handles class resources, conditional skills, formulas and event rules. A client library adds custom UI, item types and more involved logic.

## First steps

1. [Build and launch DDX](01-getting-started.md), then read its log.
2. For a data mod, read [concepts](02-concepts.md), [the config reference](03-config-reference.md) and [the resource-class tutorial](06-tutorial-resource-class.md).
3. For C, build the [native example](examples/README.md) and read [native clients](13-native-clients.md).
4. For C#, publish the [managed starter](../examples/managed/README.md) and follow [managed clients](managed-clients.md).

## Guides

| Guide | Covers |
| --- | --- |
| [Combat mods](combat-mods.md) | Events, before/after changes, recursion and game actions |
| [UI and UX mods](ux-mods.md) | A working overlay, layout editing, input, dialogs and panels |
| [Items and saved data](items-and-saves.md) | Persistent counters, object lifetimes and custom item integration |
| [Build and release](build-and-release.md) | Platform builds, resource staging, manifests and validation |
| [Architecture](architecture.md) | Loader order, hook ownership, native boundaries and platform maintenance |
| [Testing and debugging](08-testing-and-debugging.md) | Logs, offline checks and runtime diagnosis |
| [Extending DDX](09-extending-the-engine.md) | Adding hooks and services to the engine |

## References

[Config](03-config-reference.md) · [Tooltips](04-tooltips.md) · [Class UI](05-ui-features.md) · [Recipes](07-cookbook.md) · [Limits](10-limits-and-distribution.md) · [UI library](11-ui-library.md) · [Hook points](12-hook-points.md) · [C API](14-api-reference.md) · [Game hook catalogue](15-game-hooks.md) · [Popup sources](popup-sources.md)

Register services during `engine_client_init`, before DDX installs the hooks those registrations require. Prefix fields, strings and other global identifiers. Call the next wrapper when wrapping a catalogued function. Keep game-object operations on the game thread and treat native handles as borrowed.

The API declarations in `engine_hooks/src/common.h` and `game_objects.h` are the signature reference. Check the platform guards and installation logs for the feature you use; API availability alone does not prove the corresponding game hook installed.

Windows currently treats discovered client mods as active in folder order; campaign-scoped activation is implemented on macOS. See [limits](10-limits-and-distribution.md).
