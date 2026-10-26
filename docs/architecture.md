# How DDX fits into the game

DDX is a native library loaded before the game's normal startup. macOS uses `DYLD_INSERT_LIBRARIES`; Windows uses the launcher in `engine_hooks/win/ddxlaunch.c`. Both builds compile the shared C sources.

## Startup and ownership

`main.c` coordinates startup. `mods.c`, `clients.c` and `paths.c` discover mod folders, engine configs and client libraries. A client exports `engine_client_init` and registers its services there. DDX then installs feature hooks based on the registrations and configuration it collected.

Discovery happens at startup; campaign activation determines which mod-owned callbacks and configuration take effect. Keep registrations stable for the process lifetime and reset campaign caches when campaign state changes. Restart after changing client binaries or engine configs.

The DDX resource package is identified by `engine/library.json`. Its layouts, textures, localization and colours are shared defaults. It is distinct from a client package, whose `engine` folder contains a config or client library.

## Three extension layers

- `config.c`, `expr.c`, `hook_rules.c` and the JSON parser implement declarative rules.
- `hooks.c` dispatches named events using `HookContext`. These are the normal way to react to gameplay.
- `game_hooks.c` owns shared wrapper chains for catalogued game functions and observation sites. A wrapper calls its stored next function so multiple mods can coexist.

Direct patch installation belongs to DDX itself. Client code should use the event registry or catalogue rather than patching the same entry point independently.

## Native game boundary

`game_layout.h`, `game_objects.h` and the `game_*.c` adapters own game memory layouts, platform differences and object operations. Managed object views call these adapters. They borrow native handles; retaining a C# wrapper does not keep the underlying hero or item alive.

`csharp/` is source shared by NativeAOT clients, not a separate managed runtime hosted by DDX. Each client is an x64 native shared library with a C entry point. Callback delegates, strings and function-pointer storage must outlive every native registration that retains them.

## Platform hooks

On macOS, `hook.c` resolves exported C++ symbols. On Windows, `hook_win.c` and `syms_win.c` use an executable-build check, known RVAs and expected bytes. `hook_reloc.c` and `hook_cmp.c` handle copied instructions and compare-site stubs.

When adding a hook, record the exact ABI, complete instruction boundaries and relative operands. Validate the expected bytes and fail closed when they differ. Windows uses MSVC's game ABI even though the DDX DLL is built with mingw-w64; hidden return storage and C++ value destruction need explicit care. UCRT compatibility matters whenever allocations cross the boundary.

## Finding a feature

Start with declarations in `common.h` or `game_objects.h`, then locate its implementation with `rg`. Feature installation logs explain missing symbols or rejected patch sites. The [extension guide](09-extending-the-engine.md) describes adding a service; the offline suite covers registries, layouts, config rules, ownership and patch machinery. Runtime testing is still required for game ABI and screen behavior.
