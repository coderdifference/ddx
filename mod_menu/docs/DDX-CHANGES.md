# DDX integration

The menu is a built-in DDX framework service. No separate mod or repository is required. The native service is in `engine_hooks/src/modmenu*.c`; the generated-form controller is in `mod_menu/src/Core`, with a NativeAOT boundary in `mod_menu/src/Client`. DDX loads `engine/ddx_mod_menu.dylib` or `.dll` from its own package before external clients. Staging requires the service artifact and fails if it is missing.

## Implemented native changes

- A campaign pause-menu entry and a fixed 1280 by 800 dialog. Native drawing uses DDX's existing fonts, dialog frame, clipped panels and transforms. The left list and right form scroll independently; Save and Back remain outside the scroll areas. Right content is blank until selection.
- Form discovery at `mod_options/options.json`, after a native client initializes. Forms are scoped to campaign-enabled owners. C# clients can register explicitly with an extension callback.
- JSON-valued campaign options stored as JSON text in `persist.game.json` under `profile_options.ddx_mod_options`. The existing 32-field integer API remains unchanged. The same profile save/restore hooks serialize and restore this additional namespace.
- Save uses `ProfileGameFile::Save`, storage flush, and a read-back comparison through `open_RestoreData`. It preserves the campaign's raid marker and does not call `Darkest::Save`, which can remove raid/map files. Failure retains the controller draft and prior committed RAM state. Read-back verifies the namespace through the storage API; it is not proof of OS-level fsync or Steam Cloud synchronization.
- Post-load notification from `Darkest::LoadCampaign`, unload/reset invalidation, generation tokens and stale-event rejection. Callback replies are copied and explicitly released.
- Native form registration/read APIs and `DDXClient.RegisterModOptions`, `ReadModOptions` and overridable `OnModOptions(ModOptionsContext)`. Events support form construction, validation/normalization and committed-value application. Validation has no gameplay side effects.
- JSON reader support for Unicode escapes and surrogate pairs, needed for managed JSON output. Invalid escapes and embedded NUL are rejected. SDL text-input commit events feed text fields on both platforms.

## Platform evidence

macOS menu functions and whole-instruction prologues were inspected in the symbolled x86_64 executable. Windows mappings were matched in the UTM guest executable against menu_base_element formatting, pause/unpause audio, ShowBase callers, blur/state update logic and starting_roster loading. The new mappings are guarded by the existing executable build timestamp and exact byte checks in `syms_win.c`.

Native-only layouts used by this feature:

| Layout | macOS | Windows |
| --- | --- | --- |
| Menu state | 0x78 | 0x80 |
| Darkest profile object | 0x748 | 0x748 |
| Profile raid flag | via GetIsInRaid | 0xac5 |
| SaveData add string with length | vtable 0x70 | vtable 0x68 |
| RestoreData get c_string | vtable 0x70 | vtable 0x80 |
| RestoreData push key | vtable 0x10 | vtable 0x18 |

These belong in native code. Managed clients never offset game objects.

## Remaining acceptance work

Both native libraries and the macOS/Windows NativeAOT service build. Offline controller, registry, JSON and ABI checks are separate from gameplay proof. The adapters have not yet been exercised in a live loaded campaign.

Verify on both platforms: entry placement and absence outside a campaign; dialog scale and artwork; modal input ownership; all six field types; clipping and wheel scrolling with 256 fields; fixed footer; Escape returning to pause without resuming; save and restart restoration in two campaigns; new/failed loads, disabled/re-enabled mods, malformed schemas and storage failures. Capture loaded library paths, hashes and loader logs. Verify read-back against actual campaign files and normal subsequent saves before release.

Current native text controls append at the end, support UTF-8 commits, Backspace and Enter. Full caret selection, clipboard shortcuts, IME composition display, controller focus/navigation and accessibility are follow-up UI work. The browser preview uses browser-native controls and must not be taken as evidence of those native capabilities.
