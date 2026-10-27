# Design

The menu belongs to DDX. Native DDX owns game hooks, input, rendering, campaign identity, form discovery and save operations. Its built-in NativeAOT service owns schema validation, defaults, selection, draft editing and generated view descriptions. The boundary uses copied JSON; managed code never accesses game layouts. The service ships with DDX and needs no separately installed .NET runtime.

The dialog is 1280 by 800 logical units, scaled to fit the viewport. The 312-unit left panel lists active forms by title. The right starts blank, then shows fields from the selected form. Both panels scroll independently, with Save and Back in a fixed footer. Selecting another mod or closing discards the draft.

Saved valid values overlay schema defaults. Edits validate types, bounds and choice membership. C# build/validate hooks may construct a form, reject a combination or normalize values, and their output is revalidated. Save merges the draft into a candidate namespace while preserving unknown mod/field keys, then asks native DDX to serialize, flush and read it back. Only successful commits publish changed events. Campaign unload clears state and invalidates the generation token.

The authoring API is documented in ../../docs/12-mod-options.md. The local preview exercises the same controller using simulated in-memory estates; it is not deployed as part of the game UI.
