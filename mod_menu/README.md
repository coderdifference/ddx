# Built-in DDX mod menu service

This is part of Darkest Dungeon Extender, not a separate mod. Native DDX owns the pause-menu integration, rendering, campaign storage and form registry. This NativeAOT service generates views, validates schemas and drafts, and invokes mod-specific C# extension callbacks. It ships in DDX's own `engine/` directory and has no installed .NET runtime requirement.

Supported fields: single and multiple selection lists, text, signed integers, finite floats and enum radio buttons. Forms are JSON. The right pane is blank until a mod is selected. The panels scroll inside a fixed dialog with Save and Back in the footer. Drafts are discarded on selection changes or closing; Save commits only to the loaded campaign.

## Build

Requires .NET 10 and the platform's NativeAOT toolchain:

```sh
dotnet run --project tests/Tests.csproj -- examples/options.json
./tools/build.sh osx-x64
# Run on Windows with the x64 NativeAOT toolchain:
./tools/build.sh win-x64
```

Build outputs go to `artifacts/<rid>/`. DDX's staging tool requires the service artifact and places it in the framework package's `engine/` folder. Do not ship this folder as its own mod.

## Interaction preview

```sh
dotnet run --project src/Preview/DDXModMenu.Preview.csproj
```

Open http://127.0.0.1:5187. This uses the production controller with two simulated estates. Preview saves are held in server memory and reset on restart. It is a development tool and does not touch campaign files.

## Status

The macOS native adapter and service compile. Native offline tests cover registry ownership, campaign lifecycle, commit failure and JSON Unicode; managed tests cover field validation, isolation and extension callbacks. Live-game behavior remains to be verified. The Windows adapter mappings are checked against the UTM guest executable; both platforms await live-game acceptance.

See [../docs/12-mod-options.md](../docs/12-mod-options.md) for form authoring and C# hooks, and [docs/DDX-CHANGES.md](docs/DDX-CHANGES.md) for implementation details and remaining runtime gates.
