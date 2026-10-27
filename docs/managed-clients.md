# C# clients

A C# DDX mod publishes a native shared library with .NET NativeAOT. DDX calls its exported `engine_client_init`; no .NET installation is required on the player's computer.

## Start from the included project

The [managed example](../examples/managed/README.md) contains a project and two small source files. It compiles the shared `engine_hooks/csharp/*.cs` API into the client. It registers `battle_start` and writes a message to its own log.

```sh
dotnet publish examples/managed/DDXManagedExample.csproj -c Release -r osx-x64
```

For Windows, publish with `-r win-x64` on Windows with the NativeAOT C++ build prerequisites installed. NativeAOT does not support arbitrary cross-OS publishing. On a Windows ARM64 host, target x64 explicitly and use an x64-capable native toolchain.

Copy only the published `ddx_managed_example.dylib` or `.dll` into `DDXManagedExample/engine/` in your mod folder. The exported entry point returns zero for success; `DDXClient.InitializeClient` rejects incompatible hosts and logs initialization exceptions.

## Register and react

Derive from `DDXClient`. Use `OnInitialize` to register hooks and fields. `RegisterHook` roots callback state for the process lifetime. `OnHook` receives a pointer to the event context; check which point fired before interpreting its handles. The starter registers only one point.

Use `Api.Objects` for semantic hero, monster, item and inventory views. Object pointers and native strings belong to the game. Copy strings you need later, and reacquire objects after operations that can destroy or relocate them. Equipment-row scopes own a native copy and must be disposed on the game thread in reverse nesting order.

For saved values, register a prefixed field with `Api.FieldRegister`, then use `HeroDataGet` and `HeroDataSet` with a hero guid. Register campaign, item and restore callbacks before the game loads campaign data. See [items and saves](items-and-saves.md).

## Advanced callbacks

Low-level APIs expose unmanaged function pointers. Use exact native signatures and calling conventions, root callback state, and catch exceptions inside every unmanaged entry point you write. Do not let an exception unwind through game code. The base client catches exceptions in its own dispatchers; it cannot protect a custom unmanaged callback it does not own.

`NativeExport` is for services without a typed wrapper. Read the C declaration before casting. Use semantic adapters for game objects; putting offsets and raw C++ layouts into a managed client makes every platform change harder to maintain.

Restart the game after replacing the client. Check its log and `ddx.log`, then verify campaign activation and deactivation. A successful `dotnet publish` proves compilation, not DDX-hosted loading.

## Campaign configuration forms

Call `RegisterModOptions()` during `OnInitialize` to register `mod_options/options.json`, then override `OnModOptions(ModOptionsContext)` for dynamic forms, validation and committed settings. See [Campaign mod options](12-mod-options.md) for a complete JSON form and C# example.
