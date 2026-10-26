# Managed starter

Install the .NET 10 SDK and NativeAOT prerequisites on the target operating system.

From the repository root on macOS:

```sh
dotnet publish examples/managed/DDXManagedExample.csproj -c Release -r osx-x64
```

On Windows, use `-r win-x64` with the Visual Studio C++ build tools and Windows SDK installed. NativeAOT publishing must run on the target OS.

Create a mod folder named `DDXManagedExample`, with an `engine` subfolder. Copy `bin/Release/net10.0/osx-x64/publish/ddx_managed_example.dylib` from this project into it; on Windows copy the published `.dll` instead. The mod needs no engine config for this example.

Generate its manifest:

```sh
python3 tools/stage.py --manifest-only /path/to/DDXManagedExample
```

Install the folder in the game's local `mods` directory, enable it and DDX in your campaign, then restart with DDX loaded. Enter a battle and inspect `ddx_managed_example.log` beside `ddx.log`. `battle_start` is a per-hero notification, so one battle can produce several lines.

The starter only observes an event. Add your registrations in `OnInitialize` and follow the [managed guide](../../docs/managed-clients.md) for saved data, callbacks and object ownership.
