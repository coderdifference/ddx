# Getting started

DDX runs inside Darkest Dungeon 1. Build it for the game's architecture: x86_64 on macOS and x64 on Windows, even when the host computer is ARM64.

## Build the engine

From the repository root on macOS:

```sh
xcode-select --install  # once, if the command line tools are missing
./engine_hooks/build.sh
./engine_hooks/tests/run.sh
```

The build compiles `engine_hooks/src/*.c`; it does not need game headers or a game dump. The offline suite uses stand-ins for game objects. Its executable hook tests run as Intel processes, so Apple Silicon needs Rosetta.

For Windows cross-compilation on macOS:

```sh
brew install mingw-w64
./engine_hooks/build_windows.sh
```

The compiler must support `-mcrtdll=ucrt`. Keep the launcher and DLL together. See [build and release](build-and-release.md) for staging.

## Install the resources

DDX's UI needs its package as well as its loaded library:

```sh
python3 tools/stage.py --platform macos --output /tmp/DarkestDungeonExtender
```

Copy the output folder to the game's local `mods` directory. On macOS this is usually inside `Darkest.app/Contents/Resources/data/mods`; on Windows it is the installation's `mods` directory. Enable DDX in the campaign's mod list.

## Launch

Keep Steam running. On macOS:

```sh
./engine_hooks/run_game.sh
```

The launcher injects the built library, uses the game's data directory as its working directory and sets Steam's application ID. It clears the previous DDX general and combat logs. Set `DD_GAME` to the executable path for a custom installation; set `DD_INTRO=1` to retain the opening videos.

On Windows, run the staged package's `bin/ddxlaunch.exe` with `--game` and the installation directory. The native launcher starts the game suspended and loads DDX before its entry point. The checkout also has a CrossOver convenience script, `engine_hooks/run_game_windows.sh`; configure its bottle and game path for your installation.

## Read the log

Find `ddx.log` next to the game's `app.log`. Look for the engine load, your config or client, then the hooks your feature requires. `unexpected prologue`, `symbol not found` or an unsupported executable build means a feature stayed off. `none configured` can be normal when no mod registered that feature.

## Add your first mod

Build the [native example](examples/README.md) or [C# starter](../examples/managed/README.md). Copy its mod folder into `mods`, generate its manifest, enable it alongside DDX, and restart the game. Engine configs and client libraries load at process startup. Use a disposable campaign to check activation, deactivation, save/reload and interaction with other mods.
