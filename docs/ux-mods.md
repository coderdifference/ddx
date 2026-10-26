# Building UI and UX mods

The native example includes a working expedition overlay. Start there before building a panel: it exercises a shared game wrapper, localization, layout lookup and drawing with very little state.

## Build the overlay

```sh
./engine_hooks/build.sh
./docs/examples/ddx_example_engine/build.sh
```

Install `docs/examples/DDXExample` as a local mod, compile its localization and generate its manifest as described in [the example README](examples/README.md). Enable it alongside DDX, restart, and enter an expedition. Its overlay shows a saved campaign kill count. The example also changes stress and supplies event rules; remove those registrations and config rules if you only want the overlay.

`example_overlay.c` wraps `RaidDisplay::PrePanelShowInternal`. The wrapper calls the next function first, checks that it is drawing the expedition display, then renders text. Its registration checks both the screen symbol and the catalogue entry; if either is missing it logs that the overlay is unavailable.

## Layout and text

The example reads `engine/ui/ddx_example.layout.json` with `ui_layout_mod`. It uses `overlay.show`, `overlay.pos`, `overlay.font` and `overlay.colour`, with fallback values in code. Positions are offsets from the screen centre rather than fixed desktop coordinates.

Change the layout while the game is running to adjust placement. Config and binary changes still require a restart. Keep user-facing text in string tables: the example formats `str_ddx_example_overlay` with a `kills` value. Compile localization before packaging.

## Turn the overlay into a panel

Use the [UI library reference](11-ui-library.md) for signatures and working patterns:

1. Keep open/closed state in your client. Register the screen wrapper once during initialization.
2. Draw an opening control only on the screen it belongs to. Use a prefixed layout path for position and size.
3. Draw the panel with DDX's panel, text and button services. Keep the resources in your mod folder and resolve paths through DDX rather than the checkout.
4. For a long list, use `UiScrollPanel`; pair begin/end calls every frame and provide the actual content height.
5. For a destructive action, use a confirmation dialog and apply the action only when the player confirms. Disable overlapping controls while the dialog owns input.
6. Close or reset the panel when the campaign changes. Store durable game data separately from temporary UI state.

Draw callbacks run frequently. Avoid scanning every hero, loading images or writing save data every frame. Cache display data with explicit invalidation when game state changes. Never keep a borrowed game pointer solely because a panel remains open.

## Tooltips, inventory and existing screens

Use tooltip services for item explanations, source labels and formatted text rather than drawing a second tooltip over the game's own. [Tooltips](04-tooltips.md) covers config tokens; [the UI reference](11-ui-library.md) covers native builders, icons and positioning.

For custom items, register item-type drawing, price, tooltip and interaction callbacks. For stash tabs, use the shared stash services so items stay in native storage. For changes to an existing screen, use the [game hook catalogue](15-game-hooks.md), preserve its signature and call on to the next wrapper.

## Check the interaction

Test different resolutions and window modes, long translated strings, empty and full lists, disabled controls, repeated clicks, scrolling at both ends, keyboard dismissal, campaign transitions and another mod wrapping the same screen. Confirm that hidden controls cannot receive clicks. Check each target platform in game; a compiled Windows wrapper can still have an incorrect game ABI.
