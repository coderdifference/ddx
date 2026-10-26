# 11. The UI library

For engineers adding screens, panels or controls to the game with `engine_hooks`. The library gives you the game's
own drawing, text, tooltips, dialogs and drag and drop as plain C calls, and keeps positions in layout files you
can edit while the game runs. Everything here is declared in `engine_hooks/src/common.h`.

## Principles
- **Draw it yourself rather than bend the game.** If the game almost does what you want, it's usually cleaner to
  draw your own control with these calls than to hook the game's code and change its answers. The stash's Ctrl-click
  salvage, for example, draws its own "Salvage for:" line and handles its own click, without touching the game's
  sell mode.
- **One owner per game function.** A game function can only be hooked once. Shared ones (`GUI_Text`,
  `GUI_RenderTooltips`, the tooltip text drawing) are owned by the library, and features register with it
  (`ui_on_text`, `ui_text_substitute`, `ui_on_frame_end`, `ui_tooltip_line_icons`) instead of hooking them again.
- **Positions live in layout files**, not in C constants (see Layout files below).
- **Fail safe.** Every library call checks what it needs and does nothing (logging once) when the game differs.

## Where your code runs
Panels in the game are drawn every frame. Wrap a catalogued panel body function, such as
`BlacksmithDisplay::OnActionShowBodyInternal`, and draw from there: your calls are in the panel's coordinates and
transform, and fade with it.

The game's GUI is **queued**: `ui_image`, `ui_button`, `ui_text*` add widgets that the game draws after all panels,
in the order they were added. So a later call is drawn on top, and you can't draw "under" something already queued.
`ui_image_now` is the exception: it draws at once, for code that runs while something else is being drawn (inside
a tooltip, or a `GUI_Callback`).

## Drawing (`ui_draw.c`)
```c
uint8_t tex[16];
if (ui_texture("my_mod/panel_button.png", tex))   // loaded once, kept
    ui_button((Vec2){83, 456}, tex, 'lbsh', enabled, on_hammer, NULL, 0);     // on_hammer(data) when clicked
ui_image_path((Vec2){-225, 300}, "my_mod/panel_button.png");
ui_image_sized(pos, tex, (Vec2){24, 24});                                       // scaled
ui_text_anchored(pos, ui_loc("str_my_title"), "town_activity_name", "notable", 0, 0);
```
- Paths are relative to the game's data folder; a mod's files override the game's.
- `ui_texture_redirect(fn)`: every texture the game loads by path (`TextureFactory::CreateTexture`) asks `fn(path,
  buf, cap)` first, which returns another path to load (written into `buf` if needed) or NULL. Use it for a file the
  game loads but your mod never shows, or shows from elsewhere, so you can redirect it to an existing resource.
  Register before `ui_draw_install` (from `main.c`); no redirects, no hook.
- A button's `id` is any number unique on the screen: it keys the hover state. `data` (up to 16 bytes) is copied
  and handed to the click callback.
- `ui_loc(id)` is a string table entry (your mod's `.loc2` included). `ui_format(out, cap, id, keys, vals, n)`
  fills `{key}` placeholders in it.
- Fonts and colours are ids from `fonts/fonts.darkest` and `colours/*.colours.darkest`; a mod can add its own.
  Colours: the game loads a mod's `colours/*.colours.darkest`. Fonts: the game reads only its own `fonts.darkest`, so
  the library adds a mod's `fonts/*.fonts.darkest` (same `font:` / `font_ref:` lines, paths relative to the mod) the
  first time one of its `font_ref` ids is asked for once the mod is loaded (each font on its own, as its mod's files
  are mounted: a campaign without one mod still gets the others' fonts). A mod `font_ref` may also name one of the
  game's fonts (`.font "dwarven_axe_medium"`), giving it the mod's own name. `ui_font(id)` returns a font by id, mod
  fonts included; every library font id (tooltip `"font"` / `{font|id}`, `ui_text*`, badges) goes through it. A
  font made with a bitmap font tool (BMFont text `.fnt` + `.png` page) works; give each page a unique file name.
  macOS only for now.
- `ui_text_anchored`'s anchor is the game's LayoutAnchor: 0-2 put pos at the text's top left / top centre / top
  right, 3-5 on its middle line, 6-8 on its bottom. `ui_text_width(font, text)` measures text as the GUI does, for
  centring a group of pictures and text yourself.

### Drop targets
```c
static UiDropTarget g_drop;
static uint64_t on_drop(UiDropTarget *t, const void *pos, void *drag) { ...; return 0; }   // 0: refused
// every frame, after drawing the slot:
if (!g_drop.vtbl) ui_drop_target_init(&g_drop, on_drop, NULL);
ui_drop_target(&g_drop, accepts_mask, slot_pos, slot_size);
```
`accepts_mask` has a bit per drag type the target takes.
Refusing (returning 0) sends the dragged thing back where it came from.

## Text (`ui_text.c`)
```c
// draw another string table entry in place of one, wherever the game draws it (decided each time; "" hides it)
static const char *instruction(void) { return gear_tab_open() ? "str_my_instruction" : NULL; }
ui_text_substitute("realm_inventory_trinket_sell_instruction", instruction);

// or see every GUI_Text call: keep it, skip it, or replace its text
static UiTextAction filter(const void *caller, const char *text, const char **replacement) { ... }
ui_on_text(filter);
```
Register before `ui_text_install` (in `main.c`, after the features). Substitutions match entries by the string
table's own pointer, so they don't depend on where in the game's code the text is drawn. A filter's `caller` is the
call's return address: compare it with `call_site_return(fn, offset, target)` (`callsite.c`), which also checks
that the instruction there is the call you expect, so a game update turns your filter off instead of breaking it.

## Tooltips (`ui_tooltip.c`)
- **`ui_tooltip_in_panel(tip)`**: the game's tooltips are drawn over everything, dialogs included. For a tooltip
  that's part of your panel (an item's details beside a slot), build the `GuiTooltipParameters` with the game's
  function (`Inventory::Display::CreateItemTooltip`, ...) and hand it to this: it's drawn in your panel's order, so
  later controls and dialogs cover it. It takes ownership of `tip`.
- **`ui_tooltip_line_icons(line, icon_path, gap)`**: an icon at each end of one tooltip line this frame, `gap` units from the text (a client mod
  stars on perfect affixes). `line` is the string pointer the line got from `GuiTooltipParameters::Text::Line::
  SetString`; the tooltip is widened to fit the icons.
- **`ui_tooltip_badge(line, icon, size, offset, text, font, rgba, text_offset)`**: a picture with text on it at the
  top right of the tooltip whose name line is `line` (a client mod resolve disc); the name line measures wider so the
  badge never covers it. `text_offset` nudges the text on the picture (a font's digits can sit off centre).
- **`ui_tooltip_inline_badge(line, at, icon, size, offset, gap, text, font, rgba, text_offset)`**: the same picture
  inside a line's text. Put a single space at byte `at` (encode any colour markup first); when the tooltip measures and
  draws the line, that space is widened to fit the picture in the line's own font, and the picture is drawn in the gap.
  a client mod "Needs resolve (disc) to equip".
- **`ui_on_frame_end(fn)`**: `fn` runs once a frame after the last tooltips are drawn.
- **`ui_tooltip_lines(tip, first, count, pos, width)`**: a tooltip's lines drawn as ordinary panel content, with no
  box (`ui_tooltip_lines_height` measures them). Build the `GuiTooltipParameters` with the game's function, draw the
  lines you want inside a panel, then free `tip` yourself. Line icons and badges work as in a tooltip. Use it for
  item details inside a panel: the game's own tooltip renderer always pushes its box back onto the screen, so it
  can't be scrolled or clipped. For an item of a type with a description callback, prefer `item_describe` (below).
- **`ui_text_lines_now(lines, n, pos, width, anchor)`**, **`ui_badge_now(...)`**: text lines and a picture with text
  on it, drawn at once, for code that runs inside another draw. **`ui_text_lines_width(lines, n)`** measures the widest
  line as a tooltip would (this frame's line icons and badges counted). **`ui_draw_callback(fn, data, n)`** runs
  `fn(data)` in your panel's order and transform, to draw there at once.
- **`ui_tooltip_content_line(line, size, draw, ud)`**: this frame, a tooltip line becomes content you draw. The game
  measures it as `size`, so it sizes, places, clamps and frames the box around it as for text; where the text would
  be drawn, `draw(pos, size, ud)` runs instead (the content's top left, in the tooltip's transform). Item tooltips
  use it (below).

## Mouse and modifier keys (`ui_input.c`)
```c
const UiMouseButton *m = ui_mouse(UI_MOUSE_LEFT);
if (m->pressed && (m->press_mods & UI_MOD_CTRL)) pressed_on = item_hovered_last_frame;
if (m->released && pressed_on && (m->release_mods & UI_MOD_CTRL) && ui_click(UI_MOUSE_LEFT, 8)) act_on(pressed_on);
```
The game's controls take a press as they draw (an inventory cell starts its drag), so asking the game's GUI whether
the mouse was pressed can say no by the time your code runs. `ui_input` reads SDL directly instead: each button's
`down`, `pressed` and `released`, where it was pressed and released (GUI coordinates) and which modifiers
(`UI_MOD_SHIFT`, `UI_MOD_CTRL`, `UI_MOD_ALT`, `UI_MOD_CMD`) were held at each moment. It's sampled once a frame, at
the first call, so a press shows up in the frame it happened in. `ui_click(button, slop)` is a release within `slop` units of its
press. On macOS, SDL reports Ctrl + left click as a **right** click (the one-button Mac habit), so for a
Ctrl-click use `ui_ctrl_pressed()` / `ui_ctrl_click(slop)`, which take either button. `item_sale.c` (the stash's Ctrl-click salvage) is the worked example.

`ui_mouse_wheel()` is the wheel this frame (the game's own delta, > 0 is up), `ui_mouse_local()` the mouse in the
current transform and `ui_mouse_in(pos, size)` a hit test there (as `GUI_MouseOver` does), for controls you draw and
drag yourself.

## Panels (`ui_panel.c`) and scroll bars (`ui_scroll.c`)
Panels draw nothing of their own (no frame, no padding); a background belongs to the content. Anything drawn inside a
panel with the game's GUI (images, text, buttons, item grids, drop targets, `ui_tooltip_lines`) is clipped to it and
can't be hovered or clicked outside it, as in the stash; `ui_mouse_in` tests the clip too. Panels nest.
```c
static UiScrollPanel list;                              // yours to keep
float w = ui_scrollpanel_begin(&list, layout, "list", UI_SCROLL_AUTO_SCROLLER, pos, size);
float h = draw_my_rows(w);                              // from (0, 0): the panel's top left, scrolled
ui_scrollpanel_end(&list, h);                           // the content's height

// a long list of fixed-height rows: draw only the ones in view (+ a margin), the height still covers them all
int first, last;
if (ui_scroll_rows(list.shown, size.y, row_h, 0, 2, nrows, &first, &last))   // before ui_scrollpanel_begin
    for (int i = first; i <= last; i++) draw_row(i, i * row_h);
// ... ui_scrollpanel_end(&list, nrows * row_h);

static UiPanelBox box;                                  // anchored panels + a scrolling centre, sizes from the layout
ui_panelbox_begin(&box, layout, "details", pos, size);
Vec2 ts;
if (ui_panelbox_part(&box, UI_BOX_TOP, &ts)) { draw_title(ts); ui_panelbox_part_end(&box); }
float cw = ui_panelbox_centre(&box, NULL);
ui_panelbox_centre_end(&box, draw_body(cw));
```
- **Scroll types** (`ui_scroll_type_parse`): `off` (clipped, never scrolls), `hidden` (the wheel scrolls it, nothing is
  drawn), `arrow` / `auto_arrow` (------v------- over the bottom while there's more below, flipped over the top while
  there's more above, in the style of the town bar's open-tab line; a click pages that way), `scroller` / `auto_scroller` (the game's scroll bar inset on the right, the content
  narrower by its width). Scrolling is by pixel and eased; the wheel goes to the innermost panel under the mouse.
- **Panel box:** `<key>.top` / `.bottom` (heights, full width) and `.left` / `.right` (widths, between them); a part
  with size 0 is absent. The centre takes the rest and scrolls as `<key>.scroll` says (`auto`, `hidden`, `on`, or any
  type above), with its settings under `<key>.centre`.
- **`ui_clip_push(pos, size)` / `ui_clip_pop()`**: a clip of your own, within the active one.
- **`ui_scrollbar(...)`**: the bar alone (the game's `shared/widgets/` art) for code that scrolls something else; the
  inventory grids use it by rows.
- How the stash does it, for reference: `GUI_PushClip(left, right, top, bottom)`, `Transform_Translate(0, -scroll)`,

## Hot spots (`ui_hotspot.c`)
`GUI_HotSpot` (an invisible clickable rect: the dungeon panel's tabs, slot drop areas) has one owner. Register
`ui_on_hotspot(before, after)`: `before(params)` returning false skips that hot spot this frame (no hover, no click;
the id is the u64 at params+0x08), `after(params)` runs once it is drawn.

## Inventory grids (`ui_invgrid.c`) and sizes (`inv_slots.c`)
Every mouse-driven inventory grid (stash, Provisioner, the dungeon's party inventory) is drawn by one game function the
library owns.
```c
ui_invgrid_on(my_begin, my_end);                       // around every grid's draw: my_end(iface, hovered item)
ui_invgrid_scroll("provision", layout, "store");       // grids of that config type scroll: visible rows and a bar
inv_slots_provider("raid", my_extra_slots);            // + slots for every party inventory (int my_extra_slots(void))
```
- Config types are the `.type` of `inventory/*.inventory.system_configs.darkest` (`raid` is the party's supplies,
  `provision` the Provisioner's store). A mod's file with the same type replaces the base entry, so a fixed size is
  data; `inv_slots_provider` is for sizes that change (a debug switch, gear that gives slots).
- A scrolled grid is drawn whole inside a scroll panel (below): pixel scrolling, a row per wheel notch, the normal
  scroll bar right of it (`<key>.scroll`, `auto_scroller` by default). Its viewport is `<key>.visible_rows` rows, or
  a height from `ui_invgrid_scroll_height(type, fn)` (a client mod stash uses the game's own viewport and switches
  the game's stash scrolling off); a height of 0 leaves the grid unscrolled that frame (the stash's tabs, while
  they're off, leave the game its own grid and scrolling). Cells outside the viewport are clipped and inert; clicks, drags and drops reach the
  real slots.
- A new size applies when a panel next shows the inventory; shrinking never drops an item, and loading a save keeps
  every saved item. macOS only.

## The stash view (`stash_view.c`)

The estate stash as tabs, one per group of items, each with its own sorts, in place of the game's single sorted grid.
Every item stays in the one storage; a tab only chooses what the grid shows and in which order, so a drop on the stash
lands in the item's own tab whichever tab is open. Register in `*_install`, before `stash_view_install`:

```c
static bool is_rune(const uint8_t *item, void *ud) {
    return *(const unsigned *)(item + ITEM_TYPE_HASH) == name_hash("my_rune");
}
static void by_power(const uint8_t *item, StashKey *k, void *ud) { k->k[0] = -my_rune_power(item); }   // highest first

stash_tab_register(&(StashTabDef){.id = "runes", .icon = "panels/my_mod/tab_runes.png", .tooltip = "str_my_tab_runes",
                                  .has = is_rune, .sorts = {{"panels/my_mod/sort_power.png", "str_my_sort_power", by_power}}});
stash_tab_register(&(StashTabDef){.id = "trinkets", .icon = "panels/my_mod/tab_trinkets.png", .tooltip = "str_my_tab_trinkets",
                                  .sorts = {{"…rarity.png", "…", stash_key_trinket_rarity},
                                            {"…class.png", "…", stash_key_trinket_class}}});   // no .has: the rest
stash_view_layout(my_layout, "tabs", my_layout, "stash_grid");
```

- An item belongs to the first tab that claims it; a tab without `has` takes everything no other tab claims (register
  one, or items no tab claims can't be seen).
- A sort fills a `StashKey`: items are ordered by `k[0]`, `k[1]`, `k[2]`, then `name` (the item id unless the key sets
  it), all ascending, so negate for "highest first". The tab's sort buttons are drawn in the game's sort row (only when
  it has two or more); `stash_key_trinket_rarity` and `stash_key_trinket_class` are the trinket sorts.
- Pictures: a tab icon and a sort icon may have `_hover` and `_on` variants (`tab.png`, `tab_hover.png`, `tab_on.png`);
  the tab backgrounds are the layout's `background` with the same variants. The tooltip is a string id, shown when the
  tab's icon is hovered (the game's button tooltip), so tabs stay narrow.
- Layout (under the key you pass): `pos` (the first tab's top left, panel coordinates), `size`, `spacing`,
  `icon_offset`, `open_offset`, `background`, `strip_background` / `strip_pos` / `strip_size` (a picture behind the
  tabs), `sort_button_size`, `sort_row_reserved` (places kept at
  the sort row's right end; 1 keeps the unequip button's), `drop_area` (`"view"`: the grid's viewport, where the
  items are shown; `"layout"`, the default: `drop_pos` and `drop_size`), the rect given to the game's own stash drop
  area: an item dropped anywhere in it goes to the first empty slot. Keep it clear of the tabs: a drop area under a
  tab takes the tab's hover, so the tab gets no clicks. Tabs above the panel
  (`pos.y` < 0) grow the panel's rect upward by as much (its drawing is moved back down), so the game's input reaches
  them: a widget only gets clicks, hover and tooltips inside its rect.
- **Unequip all:** each tab has one "Unequip all" button at the sort row's right end, in place of the game's
  "Unequip all trinkets". After a confirmation it sends every hero's equipped items that belong to the open tab to the
  stash: trinkets from the trinket slots, and items in your registered equipment slot kinds (`equip_slots_unequip`),
  then saves as the game does. `StashTabDef.unequip`: `off` (no button on that tab), `icon` (a picture with an optional
  `_hover` variant; NULL is the game's), `tooltip`, `question`, `yes` and `no` (string ids; NULL are the game's trinket strings).
  With no tabs the button is the game's.
- `stash_view_active(fn)` limits the tabs to when `fn` is true (a campaign with your mod). `stash_view_tab()` is the
  open tab's id. `stash_view_on_panel` and `stash_view_on_sell` are the panel's draw and a sale, for code that needs
  them (the view owns those functions).
- A registered item type is accepted on the stash only if it joins `ITEM_CHECK_STASH_DROP` (`item_types.c`).

## Equipment slots (`equip_slots.c`)

A hero's equipment drawn as slots that a layout file places, in place of the game's weapon and armour rows and
trinket grid: on the character sheet (`sheet`), the expedition hero panel (`expedition`) and the embark screen's quick
trinket select (`embark`, the Arena's party setup only). Every slot is a drag source and a drop target, as the game's
trinket slots are: an item leaves by being dragged to the stash, the party inventory or another slot. A placement
with no slots in the layout is left to the game.

A slot has a **kind**. `trinket` is built in: slot `index` is the hero's own trinket slot, so the game's rules apply
unchanged (class requirements, limits, the battle lock, swaps, sounds, trinket buffs). Other kinds are yours,
registered in `*_install` before `equip_slots_install`:

```c
static const uint8_t *rune_item(const void *hero, int index, void *ud) { return my_worn_rune(hero); }   // NULL: empty
static int rune_accepts(const void *hero, int index, const uint8_t *item, void *ud) {
    if (!is_rune(item)) return 0;                     // not a rune: no highlight, refused
    return my_can_wear(hero, item) ? 1 : 2;           // 2: the red highlight, refused
}
static bool rune_set(void *hero, int index, const uint8_t *item, void *ud) { return my_wear(hero, item); }   // NULL: empty it

equip_slot_kind_register(&(EquipSlotKind){.id = "my_rune", .item = rune_item, .accepts = rune_accepts,
                                          .set = rune_set, .movable = not_in_battle});
equip_slots_active(my_campaign);                      // optional: the layout only applies when this is true
equip_slots_layout(my_layout, "slots");               // the layout and the key its entries are under
```

- Dropped on, a slot asks `accepts`, then `set`s the new item and hands its old one back to where the drag came from
  (a swap; a cleared item when it was empty). Picked up and taken somewhere, it `set`s what the target handed back if
  it accepts that, else empties, and an item it can't take goes to `item_store` (the party inventory or the estate).
- `movable` (NULL: always) locks the slot: no pick up, no drop, the red highlight. `count` is how many slots of the kind
  a hero has (indices 0 .. count-1; 0 means 1).
- `equip_slots_items` lists what a hero has in your kinds' slots (item tooltips' `equipped` use it);
  `equip_slots_unequip` empties them into an inventory (the stash's Unequip all button uses it);
  `equip_slots_on_show(fn)` tells one client which hero's equipment is shown each frame.
- A registered item type in a slot is drawn with its type's compose (see "Item drawing" below); a trinket as the game's
  grid draws it (rarity frame under the icon). Hovering a slot shows the item's tooltip as the stash does.

**Layout** (under the key you pass), positions relative to where the game draws the trinkets:

```json
{
  "slots": {
    "sheet": {
      "slots": {
        "rune":     {"kind": "my_rune", "index": 0, "pos": [-48, 52], "size": [72, 144],
                     "empty": "panels/my_mod/empty_rune.png",
                     "highlight": {"valid": "panels/my_mod/rune_glow.png", "offset": [-10, -10], "grow": [20, 20]}},
        "trinket0": {"kind": "trinket", "index": 0, "pos": [32, 52]},
        "trinket1": {"kind": "trinket", "index": 1, "pos": [112, 52]}
      }
    },
    "tooltip_offset": [85, 0]
  }
}
```

- Per slot: `kind` (default `trinket`), `index` (0), `pos`, `size` (72 x 144, the game's trinket icon).
- **Settings**, each looked up on the slot, else its placement (`sheet.highlight`, ...), else the key (`slots.empty`,
  ...), else the game's: `empty` (the picture while the slot is empty or its item is dragged; default none),
  `highlight.valid` and `highlight.invalid` (default the game's `overlays/eqp_mouseover.png` and
  `eqp_unavailable_mouseover.png`), `highlight.offset` (from the slot's top left; default the game's, -24,-20) and
  `highlight.grow` (the picture's size over the slot's; default 46,43, the game's art over its icon), `tooltip_offset`
  (the item tooltip's anchor from the slot; default 85,0, before a client's `item_tips_place` moves it).
- Pictures are drawn in white (the game's rows end with the sheet title's gold set), so art shows its own colours.

## Item drawing (`item_draw.c`)

A registered item type (`item_type_register`, `item_types.c`) decides how its items look wherever they're drawn: the
inventory grids, the dragged item, equipment slots (`equip_slots.c`), and anything you draw with `item_draw`.

- `ItemTypeDef.icon(item, tex, ud)`: the item's base picture (a texture handle your code keeps). Return false for the
  class's own icon (`panels/icons_equip/<type>/inv_<type>+<id>.png`).
- `ItemTypeDef.compose(q, out, max, ud)`: fill `out` with up to `max` layers, bottom first, and return how many
  (-1: just the base picture). `q` has the item, the active hero (`items_active_hero`), where it's drawn
  (`ITEM_DRAW_GRID`, `_DRAG`, `_SLOT`, `_OTHER`), the base picture, its drawn size and scale.
- An `ItemLayer` is the base picture (`.base = true`), a texture (`.tex`) or a Spine sprite (`.sprite`, animated: its
  origin goes on the anchor point). `.anchor` (`UiAnchor`: the eight compass points and the centre) is the point of
  the layer that meets the same point of the item's rect; `.offset` moves it from there. `.size` {0, 0} is the
  picture's own size. Offsets and sizes are in the base picture's pixels and scale with the item.
- Leave the base layer out to hide the base picture (in a grid it still takes clicks and drags).
- `item_draw(item, hero, pos, size, place)` draws an item yourself; `item_draw_note(item, tex, place)` marks a picture
  you're about to draw with `GUI_Image` as that item's (up to 4; `item_draw_note(NULL, ...)` forgets them).

```c
static int compose(const ItemDrawQuery *q, ItemLayer *out, int max, void *ud) {
    int n = 0;
    if (max < 3) return -1;
    if (my_frame(q->item, out[n].tex)) out[n++].anchor = UI_ANCHOR_TOP_LEFT;        // under the art
    out[n++] = (ItemLayer){.base = true};                                           // the art
    if (my_badge(q->item, out[n].tex)) {                                            // a badge, top right
        out[n].anchor = UI_ANCHOR_TOP_RIGHT;
        out[n++].offset = (Vec2){-4, 4};
    }
    return n;
}
```

Layers are drawn in the game's current transform and GUI colour, as the item's own picture is (an unusable item's
darkening applies to them too).

## Item tooltips and descriptions (`item_tips.c`)

An item's type supplies what its tooltip and its description show, as layers, the way `compose` supplies its picture.

- `ItemTypeDef.tooltip(q, out, ud)`: the content of the item's tooltip wherever the game shows one
  (`Inventory::Display::CreateItemTooltip`: inventories, the stash, shops, equipment slots). You don't place the
  tooltip: the game still sizes, places, clamps and frames its box, around your content's size. Return false for the
  game's own tooltip.
- `ItemTypeDef.describe(q, out, ud)`: content for a panel to show. `item_describe(item, hero, width)` builds it
  (NULL: the type has none); `item_content_draw(c, regions, pos, width)` draws it in the current panel and returns its
  height.
- The game's own types take the same callbacks through `item_type_view("trinket", tooltip, describe, ud)`.
- `item_tips_place(fn, ud)` (before `item_tips_install`) decides where every item tooltip goes, instead of each
  screen's own offset from the item: `fn` gets an `ItemTipPlace` (`slot`: the item's position as the screen passed
  it; `game`: where the game would put the box; `mouse`; `screen_min` / `screen_max`, all in the tooltip's transform)
  and returns the box's top left; the game then keeps the box on the screen. a client mod: right of the cursor, level
  with the item's top.
- `ItemContentQuery`: the item; the hero it's shown for (the tooltip's own, else the active hero, with `hero_implied`
  set; NULL: none); the hero's equipment (`equipped`: trinkets, then the registered equipment slot kinds' items;
  `item_is_equipped(q, item)` tests one by identity); the panel's `width` (describe; 0 for a tooltip: as wide as you
  need); and, for a tooltip, the game's own lines (`game`, `ngame`) to keep or rework.
- `ItemContent` has two regions, `head` (the name) over `body` (the rest). Set each region's size: that's what the box
  or panel holds. A tooltip shows both, each as wide as the wider; a panel can put the head in a header of its own.
- Layers (`item_content_add(c, kind, region)`): text (`ITEM_LAYER_TEXT`: the content's lines `first` .. `first +
  count - 1`, wrapped at `width` (0: the region's), in a `column`: 0 left, 1 centred, 2 right) or a picture
  (`ITEM_LAYER_IMAGE`: `tex`, `size`, optional `label` text on it in a `font` and `rgba`). Each is placed by two points:
  its region's point `at` gets the layer's own point `anchor`, then `offset`. A layer may reach outside its region on
  purpose: a badge over the tooltip's corner.
- Lines: `item_content_line(c, font, colour, text)` copies the text into the content and returns its index;
  `item_content_string(c, i)` is the stored string, for line icons and inline badges (`ui_tooltip_line_icons`, ...),
  which work on content text too. `item_content_lines_width` / `_height` measure lines (icons counted).

```c
static bool tooltip(const ItemContentQuery *q, ItemContent *c, void *ud) {
    const void *font = q->ngame ? q->game[0].font : ui_font("tooltip");
    item_content_line(c, font, ui_colour_hvec4("notable"), my_name(q->item));
    item_content_line(c, font, ui_colour_hvec4("neutral"), item_is_equipped(q, q->item) ? "Worn" : "In the bag");
    ItemContentLayer *t = item_content_add(c, ITEM_LAYER_TEXT, ITEM_REGION_HEAD);
    t->count = 1; t->column = 1; t->at = t->anchor = UI_ANCHOR_TOP;               // the name, centred
    t = item_content_add(c, ITEM_LAYER_TEXT, ITEM_REGION_BODY);
    t->first = 1; t->count = 1; t->column = 1; t->at = t->anchor = UI_ANCHOR_TOP;
    c->head = (Vec2){item_content_lines_width(c, 0, 1), item_content_lines_height(c, 0, 1, 0)};
    c->body = (Vec2){item_content_lines_width(c, 1, 1), item_content_lines_height(c, 1, 1, 0)};
    ItemContentLayer *b = item_content_add(c, ITEM_LAYER_IMAGE, ITEM_REGION_HEAD);  // a badge on the box's corner
    if (b && ui_texture("my_mod/badge.png", b->tex)) { b->at = UI_ANCHOR_TOP_RIGHT; b->anchor = UI_ANCHOR_CENTRE; }
    return true;
}
```

## Item prices and salvage (`item_sale.c`)

Two more `ItemTypeDef` callbacks, for registered types (call `item_sale_install` after `item_types_install`, before
`stash_view_install`):

- **`price(item, &buy, &sell, ud)`**: the item's own prices in gold. They come filled with its class's (the data file's
  `purchase_gold_value` and `sell_gold_value`); change either. They apply wherever the game reads a price: shop stock
  and its tooltips, the Nomad Wagon's purchase, the stash's sale and its "Sell for:". The game still applies its own
  discounts and its sell-back share. (The game reads prices straight from the item's class, shared by every item of an
  id, so the library writes the type's prices into the class just around the code that looks at one item, and puts
  them back after.)
- **Salvage, `salvage_quote(item, &quote, ud)` and `salvage(item, &dont_ask, ud)`**: a second way to get rid of an item
  in the estate stash, besides the game's Shift-click sale. With Ctrl held over the item, the quote (a label string id,
  an icon and an amount, e.g. "Salvage for:" and a currency) shows where the game puts "Sell for:", in place of the hint
  under the grid; a Ctrl-click calls `salvage`, which does the work (and may ask first: `*dont_ask` is yours to set
  for "don't ask again", and goes back to false when the stash closes). Return false from the quote when it can't be
  salvaged now. The prompt's look comes from `item_sale_layout(layout, key)`: `text_offset`, `price_offset`,
  `icon_size`, `icon_gap`, `font`, `colour`, `amount_font`, `click_slop`.

```c
static void my_price(const uint8_t *item, int *buy, int *sell, void *ud) { *buy = *sell = my_rune_value(item); }
static bool my_quote(const uint8_t *item, ItemSalvageQuote *q, void *ud) {
    q->label = "str_my_shatter_for"; q->icon = "shared/estate/currency.my_dust.icon.png"; q->amount = my_dust(item);
    return true;
}
static void my_salvage(const uint8_t *item, bool *dont_ask, void *ud) { my_shatter(itemdata_id(item)); }
```

## Dialogs (`ui_confirm.c`)
```c
static void answered(const char *key, const uint8_t *data) { if (!strcmp(key, "yes")) do_it(); }
ui_confirm_yes_always_no(text, answered, NULL, 0);           // Yes / Always / No, keys "yes" / "always" / "no"

static const UiConfirmOption opts[] = {{"hire", "str_hire", on_hire}, {"cancel", "str_cancel", NULL}};
ui_confirm(text, opts, 2, &id, sizeof id);                    // any options (up to 8), data up to 16 bytes
```
The library draws every question dialog, the game's and yours (it owns `ConfirmDialog::System::ShowEntry`): a frame
sized to the question, with the options as a row of buttons along the bottom, the hovered one under the game's
highlight bar. An option is a key (handed to its callback), a label (a string id or text) and a callback (`NULL`: it
just closes). The default set's labels are `engine_confirm_yes`, `engine_confirm_always` and `engine_confirm_no` (the library
package's string table; yours may reword them); "Always" is yours to remember (skip the question while it's set). A lone option takes Enter or Esc, as
the game's does. The game keeps its queue, blur and input blocking. The waiting spinner and controller play still use
the game's own dialog. The look is `dialog.layout.json` "confirm" (sizes, fonts, button padding; `enabled: 0` hands
dialogs back to the game).

A question can hold pictures inline: `{currency|gold}450` puts the estate currency's icon
(`shared/estate/currency.<id>.icon.png`, a mod's own currencies too) before the amount, and `{icon|<path>}` puts any
texture. The dialog lays such a question out with `ui_rich_text(pos, s, font, colour, width, icon, gap, centred, draw)`,
which wraps words with the pictures among them, each line from `pos.x` or centred on it; use it for your own panels too. The icon size is
`confirm.icon` in the layout.

A dialog can have its own look: `ui_confirm_styled(section, text, opts, n, data, len)` draws it with
`dialog.layout.json` section `section` instead of "confirm". The section names only what it changes (any name it
lacks comes from "confirm"), and its `blocks` add headings and pictures above the question, in the file's order,
centred: `{"text": "<string id>", "font": "<font_ref>", "colour": "<colour id>", "width": 600, "gap": 12}` or
`{"image": "<path>", "size": [w, h], "gap": 12}`, or a list filled when the dialog draws: `{"lines": "<list>",
"heading": "<string id>", "bullet": "<string id>", "font", "colour", "line_gap", "gap"}`, where `ui_confirm_lines(name, fn)` registers the list
(`fn` fills `UiConfirmLine {text, font, colour}`, text localized, `NULL` font or colour: the block's; the heading shows
only when there are lines). The library's announcement ("announce", below) is one.
`ui_confirm_busy()` says whether any question dialog is up or queued, so a dialog of your own can wait its turn.

## The announcement (`announce.c`)
When a campaign first runs with the library, and again whenever a mod built on it is added (so the player sees it
loaded), the library shows a dialog saying Darkest Dungeon Extender is running and what it is, on the
first screen of the campaign the player can act on: an expedition (the Old Road too; exploring, or a hero's turn when
a save loads mid-battle) or the Hamlet once it's settled (not fading, no building, news or town event open), never
over another question dialog. The campaign remembers what it has announced (campaign field `announced`, and per registered mod `mod_<hash of its
name id>`; a mod the campaign hasn't seen is marked New in the list), see
[Campaign data](02-concepts.md)). Its look is the package's `dialog.layout.json` "announce", its text the package's
`engine_announce_*` strings and its colours `colours/engine.colours.darkest`; `"enabled": 0` turns it off, and
`"test": 1` shows it once per game start whatever the campaign has seen (for tuning the layout while the game runs).

Its list names the mods built on the library: each registers itself once, with a string id from its own table (so it's
translated, and may carry colour markup) and optionally a font_ref and a colour id for its line:
```c
announce_mod("str_my_mod_name", "my_title_font", "my_colour");   // a native client, in engine_client_init
```
```json
"announce": {"name": "str_my_mod_name", "font": "my_title_font", "colour": "my_colour"}
```
(the second in any config, as a global key). A font switch inside the text isn't possible (the game's text has colour
codes but no font codes), so a line has one font. Only mods applied to the campaign count: listed in the campaign's own mod-list
order, and only they bring the dialog back. The library matches a mod by its `project.xml` Title (a local mod) or its
folder's name (a Workshop item), so a mod registers from its own files (its client's init, or its own config). The
dialog never shows in a campaign without the library package applied.

## Layout files (`ui_layout.c`)
A layout file is JSON in your mod (a client mod keeps them in `engine/ui/`). Names are dotted paths through nested
objects, and a position or size is a two-number array:
```json
{
  "anvil": { "pos": [83, 456], "size": 64 },
  "fonts": { "text": "town_activity_description" }
}
```
```c
UiLayout *l = ui_layout_load("/absolute/path/my_panel.layout.json");   // once
Vec2 at = ui_layout_vec2(l, "anvil.pos", (Vec2){0, 0});
float size = ui_layout_num(l, "anvil.size", 64);
const char *font = ui_layout_str(l, "fonts.text", "town_activity_description");
```
- The file is read again whenever it changes (checked at most once a second), so you can edit a deployed layout and
  watch the panel move. A broken edit is logged and the last good version kept.
- A missing name gives the fallback and is logged once. A missing file gives every fallback (logged once).
- Look values up each frame rather than caching them, so edits show up. Strings from `ui_layout_str` are valid
  until the file next reloads.
- Comment your layouts with `"_name"` keys next to the values they explain.

**The library's own layouts** (`dialog.layout.json`) come from the library package
([Limits and distribution](10-limits-and-distribution.md#the-library-package)). `ui_layout_mod("<file>")` gives them
with the campaign's applied mods' `engine/ui/<file>` in front, chained per name: a name is looked up in each mod's
file (the mod highest in the campaign's mod list first), then the package's; the chain is rebuilt at each campaign
load, and outside a campaign only the package's applies. So a mod's file holds only what it changes. A client mod's own files chain the same way (an event-log client
`my_log.layout.json`: another mod's copy with only `combat_log.font` restyles its log). Use `ui_layout_mod` for
those files rather than loading your own copy. `ui_layout_chain(l, next)` chains any two layouts the same way.

## Shared game layout
`common.h` also has the offsets several files need (`Inventory::Item`'s `ITEM_TYPE_HASH`, `ITEM_ID`, ...;
`GameLocString`, the game's `LocalizedString`). Offsets only one file uses stay at the top of that file, with a
comment saying where they were found (see [Extending the engine](09-extending-the-engine.md)).
