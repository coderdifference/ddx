# DDX Example: a starter mod

A small, complete mod built on Darkest Dungeon Extender, to copy when starting your own. Chapter
[13, Native clients](../13-native-clients.md) walks through it.

| Path | What it shows |
|---|---|
| `DDXExample/` | the mod folder: deploy this to the game's `mods` folder |
| `DDXExample/engine/ddx_example.engine.json` | **no code:** hook rules (a Crusader's kill heals 2 stress; bleed damage halved while the torch is at 75 or more; one torch at the end of each camp) |
| `DDXExample/engine/ui/ddx_example.layout.json` | a layout file the client reads every frame (edit it while the game runs) |
| `DDXExample/localization/` | the mod's strings (compile them before installing) |
| `ddx_example_engine/src/example_main.c` | the entry point: API check, own log file, announcement, registrations |
| `ddx_example_engine/src/example_kills.c` | hook point listeners (`kill`, `crit`, `stress_damage` before), a hero field and a campaign field, a popup |
| `ddx_example_engine/src/example_overlay.c` | a wrapped game function (`RaidDisplay::PrePanelShowInternal`) drawing text on the expedition screen from a layout |
| `ddx_example_engine/build.sh`, `build_windows.sh` | building the client into `DDXExample/engine/` |

What it does in game: heroes' kills are counted per hero and per campaign (both saved), the campaign's count shows at
the top of the expedition screen, a hero's crit shows a "Bloodied!" popup, and heroes take 10% less stress while the
torch is at 75 or more.

**State:** builds cleanly against the current library; not yet run in game.

## Build and try it

From the repository root:

```sh
./engine_hooks/build.sh
./docs/examples/ddx_example_engine/build.sh
python3 tools/localization.py docs/examples/DDXExample/localization/DDXExample.string_table.xml
python3 tools/stage.py --manifest-only docs/examples/DDXExample
cp -R docs/examples/DDXExample "<game data>/mods/"
./engine_hooks/run_game.sh
```

Install the DDX resource package too. Enable both mods in your campaign, restart with DDX loaded and enter an expedition. Inspect `ddx.log` for client loading and `DDXExample.log` for counter messages.

For Windows, build the engine with `engine_hooks/build_windows.sh`, then the client with `docs/examples/ddx_example_engine/build_windows.sh`. Install the same mod resources with the resulting DLL and launch through `ddxlaunch.exe`.

The example combines a JSON config and native client. Remove the JSON rules or native registrations you do not want when copying it. Both builds are checked separately from gameplay validation.
