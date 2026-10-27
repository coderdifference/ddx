# Validation — 27 October 2026

- Managed controller: 32 assertions passed, covering all field kinds, bounds, choices, Unicode values, campaign isolation, invalid form isolation, draft discard, failed saves, preserved unknown keys and custom/dynamic C# handlers.
- Existing native offline regression suite passed, including the new registry/Unicode/commit/lifecycle checks.
- Real macOS x86_64 NativeAOT library passed the native host smoke test: missing capability rejection, registration, blank initial view, selection, editing, commit and owned reply release.
- Shared C# starter builds with the extended DDXClient API, zero warnings/errors.
- macOS native DDX and service build; Windows native DDX cross-build and NativeAOT service build in local UTM guest storage. Existing Windows compiler warnings remain outside the new menu sources.
- Windows menu and storage entry prologues checked against the guest executable. This is static mapping evidence, not live-game proof.
- Both platform packages stage successfully and include the built-in service in their manifests. Missing service artifacts stop staging.
- Browser preview exercised pause gating, blank initial right pane, generated controls and a Unicode text save using the controller.

The initial offline checks did not modify a game installation or campaign save. Both platform installations were subsequently updated with backups and hash verification. Campaign save/reload and full modal/input behavior still need verification on both platforms. Current native text fields append, Backspace and commit with Enter; full caret editing, composition display and controller navigation remain follow-up UI work.

## Initial build SHA-256

- `engine_hooks/build/ddx.dylib`: `9c00fb3bc280b255a506b2c38298a1c7c3bbfc1cf10d71a675d52bbbd7f986cd`
- `engine_hooks/build/win/ddx.dll`: `d50578eea520775d100c227ce021e4e0a95d199f6d7bfc001cefb037f5491c3c`
- `mod_menu/artifacts/osx-x64/ddx_mod_menu.dylib`: `8496b7c9a46b869b7a136cc0e5afab5c39c5a2831db94c0bf7eb9ef33caf83d4`
- `mod_menu/artifacts/win-x64/ddx_mod_menu.dll`: `a2f8fd244cf9d25c42cc56fafa8c267b098d873497d5ba24b6407626deb0cbc8`

## Windows visual feedback and layout revision

The supplied Windows campaign screenshot confirms that the native pause-menu entry, service, mod selection and generated DDX scale field render in-game. It does not establish save/reload correctness. The follow-up revision enlarges the fixed native dialog to 1280 by 800, moves its title below the crest, pads both panels, and gives the text input and Back/Save actions visible frame surfaces. Both native platform builds pass. Subsequent Windows feedback accepted the larger interactive panel and revised styling. The final buttons use a top diamond divider, a flat bottom divider, no side borders, and a narrower 128 by 96 layout to provide more padding above and below the text. Geometry is configurable under `modmenu` in the shared dialog layout JSON.
