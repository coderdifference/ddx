# Campaign mod options

The mod menu is built into DDX. It is available from Mod Options in a loaded campaign's pause menu, when the native adapter and framework menu service are available. The implementation covers macOS and Windows x64; Windows mappings are checked against the guest executable. Live-game acceptance remains pending on both platforms. There is no separate mod-menu Workshop dependency.

Place a JSON form at `mod_options/options.json` in your DDX mod. DDX discovers forms only after the owning native client initializes successfully, and includes only enabled campaign mods. A file alone does not make an ordinary content mod a DDX client.

```json
{
  "version": 1,
  "id": "example.settings",
  "title": "Example Settings",
  "fields": [
    {"id":"title","label":"Journal title","type":"text","default":"The Estate","maxLength":80},
    {"id":"limit","label":"Entry limit","type":"integer","default":20,"min":1,"max":100},
    {"id":"duration","label":"Duration","type":"float","default":1.5,"min":0.1,"max":10},
    {"id":"detail","label":"Detail","type":"enum","default":"normal","choices":[{"id":"brief","label":"Brief"},{"id":"normal","label":"Normal"}]},
    {"id":"sound","label":"Sound","type":"single","default":"bell","choices":[{"id":"bell","label":"Bell"},{"id":"chime","label":"Chime"}]},
    {"id":"events","label":"Events","type":"multi","default":["quests"],"choices":[{"id":"quests","label":"Quests"},{"id":"loot","label":"Loot"}]}
  ]
}
```

Keep mod IDs, field IDs and choice IDs stable. IDs are persistence keys; labels may change. Every field requires a valid default. Numeric bounds are inclusive. Single/enum values are choice ID strings; multi values are unique arrays of IDs. Fields restore valid saved values over defaults. Unknown saved keys survive saves, including settings for disabled mods.

## C# extension

C# clients can register the same file with an event callback, rather than relying on automatic discovery. Add `RegisterModOptions()` inside `OnInitialize`, and override `OnModOptions`. Native DDX records the current owner, so callbacks run only for enabled mods.

```csharp
using DDX.Managed;

public sealed unsafe class ExampleClient : DDXClient
{
    protected override string LogName => "example";

    protected override void OnInitialize()
    {
        if (!RegisterModOptions())
            throw new InvalidOperationException("DDX mod options registration failed.");
    }

    protected override void OnModOptions(ModOptionsContext context)
    {
        if (context.Event == "validate" &&
            context.Values["limit"]!.GetValue<int>() > 50 &&
            context.Values["detail"]!.GetValue<string>() == "brief")
        {
            context.Error = "Brief journals support at most 50 entries.";
            return;
        }
        if (context.Event is "loaded" or "changed")
        {
            // Apply committed settings; new campaigns may omit keys, so use defaults.
            int limit = context.Values["limit"]?.GetValue<int>() ?? 20;
            ApplyLimit(limit);
        }
        if (context.Event == "unloaded")
            ClearCampaignCache();
    }

    private void ApplyLimit(int limit) { /* Update this mod's campaign state. */ }
    private void ClearCampaignCache() { /* Release all campaign-specific state. */ }
}
```

| Event | Data and permitted behavior |
| --- | --- |
| `build` | Selected form and restored draft. Modify `Form` and `Values` to construct dependent fields; the result is revalidated. Preserve the mod ID. Supply valid values for new fields. |
| `validate` | Proposed draft before edits and Save. Reject with `Error`, or normalize `Values`. Keep this free of gameplay side effects because the draft can be discarded. |
| `loaded` | Saved committed values after campaign restore. Missing keys need defaults. |
| `changed` | Committed values after successful Save. Apply settings here. |
| `unloaded` | Clear mod campaign caches. |

The callback runs synchronously on the game thread. JSON objects are detached copies. Exceptions produce validation errors, never cross native boundaries, and do not commit a failed operation. `ReadModOptions("example.settings")` copies committed values; it returns null outside a loaded campaign. Native clients use the same `modmenu_form_register` dispatch/release ABI in `common.h`.

## Saving

Values are stored in the campaign's `persist.game.json`, as JSON text in `profile_options.ddx_mod_options`. Save uses the game's profile serializer and storage flush, then reads the namespace back through the game's storage reader. A mismatch reports failure and retains the editable draft. Closing the campaign clears all state and invalidates old generation tokens. There is no global preferences file or shared estate data.

The fixed dialog keeps Save/Back outside scrolling content. Switching mods discards unsaved edits. Native controls currently support mouse selection, wheel scrolling and text entry at the end of the field with Backspace/Enter. Full caret/selection, IME composition UI and controller navigation remain runtime/UI follow-up work; do not advertise those as complete.

## Forms generated from mod data

`RegisterModOptionsJson(JsonObject form)` registers a detached form constructed
from merged mod data during `OnInitialize`. Use a `build` callback to replace its
definitions and defaults for the selected campaign. Both the native host and
managed controller accept up to 2048 fields; the existing payload limits still
apply. Keep stable field IDs across releases so saved values retain their meaning.

For numeric upgrade costs, `Api.Objects.TryUpgradeCost(treeHash, level,
currencyHash, out amount)` reads an existing requirement and
`Api.Objects.SetUpgradeCost(treeHash, level, currencyHash, amount)` updates it.
Level zero is requirement code `a`. Call on the game thread after campaign load.
These methods change the game-owned requirement used by display and purchase;
they cannot add a new currency row. Validate availability before committing and
reapply campaign-specific amounts on every load. The native adapter owns all
layout access and retains no requirement pointers between calls.

The native renderer measures all fields to retain accurate scrolling, but submits
only visible labels, inputs and choices to the game GUI. Clipping alone does not
prevent allocation from the game's fixed widget-string pool. The native renderer
regression test exercises 525/2048-field forms and long choice lists with a small
text budget at several scroll positions.


## Nested objects and compact rows

Add `"layout": "table"` at the form root for scalar label/value rows. Give each
field a `"group": ["Blacksmith", "Reforge", "Uncommon"]` path and a short leaf
label such as `"Gold"`. Group paths contain up to 12 nonempty names (128 characters
each). Fields with no group remain at the root; existing forms remain compatible.
The controller exposes immediate child objects and their direct fields, while
retaining the complete draft and original field IDs. Navigating with Up or into
another group does not save, discard, or rebuild that draft. Save validates and
commits every field across all groups. The native renderer commits the active
input before changing groups and keeps validation failures on the current object.


## Property comments

Data-backed forms may use a sibling `_key` string to describe a property named
`key`, for example `{"_chance":"Chance of adding a gear drop, from 0 to 1.","chance":0.05}`.
LootAMatic carries the nearest property comment into the field's `description`.
Array entries inherit the comment on their array property; a nested property's
own comment takes precedence. Comment keys never become editable values.

Standalone form authors set `"description": "Help text"` on a field. This optional
string may contain up to 2048 characters. The native renderer shows it below the
input, across the full form width, using the smaller `banner_sub_header` font
and a light grey colour. Height and wrapping contribute to scrolling, and help
text outside the viewport does not allocate widget strings. Layout overrides
`modmenu.comment_font` and `modmenu.comment_colour` can change its appearance.


The DDX resource package has no placeholder settings form. The menu lists clients
that supply configuration; its own dialog fits the screen automatically. Legacy
`ddx.framework` saved values remain in the namespace for compatibility but do not
create a menu entry or change the automatic fit.
