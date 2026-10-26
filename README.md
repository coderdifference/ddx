# DDX — Darkest Dungeon Extender

DDX adds code-level modding to Darkest Dungeon 1. It loads alongside the game and gives mods shared hooks, saved fields, combat services and UI controls. Mods can use JSON configuration, native C clients, or C# compiled with NativeAOT.

The engine and clients target the Intel macOS game and Windows x64 game. Apple Silicon needs Rosetta. Linux is not supported. Windows currently activates all discovered clients rather than filtering by campaign. Game updates can invalidate hooks; DDX checks known patch bytes and logs features it cannot install.

## Build

On macOS, install the Xcode command line tools, then:

```sh
git clone https://github.com/coderdifference/ddx.git
cd ddx
./engine_hooks/build.sh
./engine_hooks/tests/run.sh
```

The library is written to `engine_hooks/build/ddx.dylib`.

For Windows, build the DLL and launcher with a recent mingw-w64 toolchain supporting UCRT. On macOS:

```sh
brew install mingw-w64
./engine_hooks/build_windows.sh
```

Outputs: `engine_hooks/build/win/ddx.dll`, `ddxlaunch.exe`, and the import library for native clients. C# clients also need the .NET 10 SDK and the platform's NativeAOT toolchain.

## Run

Keep Steam running. Stage the DDX resource package first:

```sh
python3 tools/stage.py --platform macos --output /tmp/DarkestDungeonExtender
```

Copy that folder into the game's `mods` directory and enable DDX in your campaign. Then launch from the checkout:

```sh
./engine_hooks/run_game.sh
```

For a custom installation, set `DD_GAME` to the full path of the executable inside `Darkest.app/Contents/MacOS/`. On Windows, stage with `--platform windows`, then run the staged `bin/ddxlaunch.exe --game "C:\path\to\DarkestDungeon"`. Keep `ddx.dll` beside the launcher.

Read `ddx.log` in the game's working directory. It reports client loading, config parsing and hook installation. Building successfully does not confirm a feature works in a running campaign.

## Make a mod

Start with the [modder's guide](docs/README.md). The repository includes a [native example](docs/examples/README.md) with an expedition overlay, saved kill counters and combat listeners, and a [C# starter](examples/managed/README.md).

- [JSON mods and custom classes](docs/06-tutorial-resource-class.md)
- [C# clients](docs/managed-clients.md)
- [UI and UX walkthrough](docs/ux-mods.md)
- [Combat and event mods](docs/combat-mods.md)
- [Items and saved data](docs/items-and-saves.md)
- [Build, packaging and testing](docs/build-and-release.md)
- [Engine architecture](docs/architecture.md)

## Source layout

| Path | Contents |
| --- | --- |
| `engine_hooks/src` | Native engine, public C declarations, game adapters and platform hooks |
| `engine_hooks/csharp` | Shared C# API sources, compiled into each client |
| `engine_hooks/mod` | DDX layouts, textures, colours, localization and package marker |
| `engine_hooks/tests` | Offline tests with game stand-ins and executable hook fixtures |
| `engine_hooks/win` | Windows injector/launcher |
| `docs` | Tutorials and API references |
| `examples` | Managed starter project |
| `tools` | Localization compiler and package staging |

This repository contains DDX and its examples. No game executable, game data dump or installed game is included. An installed copy of Darkest Dungeon is required to use it.
