# Limits and distribution

## Platforms

DDX builds for the Intel macOS game and Windows x64. Apple Silicon requires Rosetta. Linux is unsupported. Hook installation depends on the game build and expected instruction bytes. Check `ddx.log`; a present export does not guarantee that its game hook installed.

**Campaign scoping currently differs:** macOS reads the active campaign's mod list. Windows currently treats discovered mods as active in folder order (`mods.c`). Do not rely on campaign deactivation to disable a Windows client. Test with only the intended client packages installed.

## Shared behavior

Loading DDX can change vanilla UI even without a client: the Guild grid, skill tooltip titles, buff-source descriptions, critical-bonus information and story-intro continuation are engine features. Shared textures and strings come from the DDX resource package. Missing resources can leave a working engine with incomplete UI.

## Several mods

On macOS, registrations made during `engine_client_init` belong to that mod. Campaign priority controls wrapper chains, settings and config lookup. Higher-priority callbacks can override queried values; observers and wrapper chains follow their own ordering contracts. Read the relevant API before relying on order.

Fields, effect IDs, strings, layout names and colour IDs are shared namespaces. Prefix them. Capacity is finite; inspect constants in `common.h` and the registry implementation for current limits, and check registration return values. One mod can exhaust a shared registry.

## Without DDX

The base game ignores engine JSON and client binaries. It still reads ordinary mod data. Resources, skill conditions, formulas and custom UI supplied by DDX disappear, which can make a class or item mod unusable. State the dependency clearly. Test what happens when loading and saving without your client; do not promise that custom data survives a save written without its owner.

## Distribution

Stage the engine with `tools/stage.py`; ship client libraries in their own mod's `engine/` folder. Keep DDX's binary and shared resources in its separate package. Declare DDX as a Workshop dependency and explain that it must be loaded at process startup. See [build and release](build-and-release.md).

Compilation and offline tests do not establish gameplay correctness. Check each required feature in game on each platform, including mod ordering, transitions, saves and UI input.
