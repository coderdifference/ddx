# 5. UI features

Three UI changes let a class have more skills than vanilla's layout holds: the combat skill bar pages, the
character sheet pages, and the Guild shows one icon per skill in a grid.

The combat bar and character sheet features apply to **configured classes only**. The Guild grid and the
`Name (rank)` tooltip titles apply to **every class**, vanilla included, whenever the library is loaded
([global side effects](10-limits-and-distribution.md#shared-behavior)).

## Grouping skills: `guild_row` and `sheet_page`
Two per-skill keys organise all three screens:

```json
"example_strike": {"guild_row": 1, "sheet_page": 1},
"example_guard":  {"guild_row": 1, "sheet_page": 1},
"example_burst":  {"guild_row": 2, "sheet_page": 2}
```

- `guild_row`: the Guild row (1 = top).
- `sheet_page`: the character sheet page, and the group the combat bar pages by. Without it, a skill's
  `guild_row` is used.
- Within a row, page or group, skills keep the order of the config's `skills` section.

## Combat skill bar paging
```json
"skill_pages": {
    "size": 4,
    "icons": [
        "panels/icons_ability/ex_page_prev.png",
        "panels/icons_ability/ex_page_next.png",
        "panels/icons_ability/ex_page_prev_disabled.png",
        "panels/icons_ability/ex_page_next_disabled.png"
    ]
}
```

In battle, a hero whose valid skills (after [conditions](02-concepts.md#skill-conditions-which-skills-a-hero-can-use))
outnumber `size` gets pages:
- Skills are ordered by `sheet_page` (else `guild_row`; skills with neither go last), then config order.
- Groups are packed into pages: a group goes on the current page if it fits, otherwise it starts the next. A
  group bigger than a page continues over several. Skills in the same group stay together where space allows, and small groups can share a page.
- Two tabs page through: **next** right of the Pass tab, **previous** left of the first skill slot. Both wrap.
  With one page they're drawn disabled (the disabled icons) and don't respond.
- The page goes back to the first at the start and end of each battle. It isn't saved.
- The bar, the hotkeys and the skill actually used all see the same page, because paging trims the same list
  the conditions filter.

**Fixed slots.** For a paging class the bar always has `size` skill slots, then Move, then Pass and the tabs:
Move is always slot `size + 1` (hotkey 5 at size 4) and nothing jumps around as pages change. Slots a page
doesn't fill show the game's empty-slot frame (`panels/icons_ability/ability_none.png`). Out of battle, in a
raid, the bar keeps the same fixed slots while the hero's skills fit in `size`. The game's skill hotkeys are 1-5,
so `size` 4 is the practical maximum.

**Tab art.** 20 × 72 px, like the vanilla Pass tab. `icons` is `[prev, next, prev disabled, next disabled]`;
the disabled pair is optional (the enabled icons are used instead). The game doesn't dim a disabled button
itself, so draw the disabled icons dimmed.

**Banner layout.** The previous tab sits just left of the first skill, where the hero's name and class are
drawn. If they overlap, supply your own `scripts/layout/panel.banner.darkest` override and adjust the name
layout. This file affects every hero's banner, so check vanilla classes and other UI mods too.

## Character sheet pages
For configured classes the character sheet's combat skill grid shows one row at a time, in pages:
- one page per `sheet_page` (else `guild_row`), in order; a page with more skills than a row (7) continues on
  the next;
- then the class's other skills, a row at a time;
- `hidden` skills and skills the hero doesn't own yet are left out (the Guild shows those), and empty pages
  with them.

Tabs left and right of the row page through, wrapping; they use the `skill_pages` icons. A class without
`skill_pages` icons still gets pages but no tabs to turn them, so only the first page can be seen: give every
configured class the icons. Selecting skills (the four "equipped" ones) works as normal on every
page. The pre-embark (provisioning) character sheet is the same screen and pages the same way.

## The Guild grid
With the library loaded, the Guild's combat skill page is rebuilt for **every** class:
- **One icon per skill**, no rank shields. The skill's rank is drawn on the icon over a small badge
  (`panels/engine_guild_rank.png`).
- **Click to buy the next rank.** The first rank is bought as in vanilla; afterwards a click buys the next rank,
  running the game's own purchase code (gold, prerequisites, sounds). A rank still locked (by resolve or a
  prerequisite) is refused.
- **Tooltip**: the current rank and the next one (separated by a blank line), the next rank's prerequisites
  and resolve level, and its cost as the last line (`Cost: 250 Gold`, red if the estate can't afford it). Costs
  aren't drawn on the page.
- **Overlays**: the vanilla lock on skills whose first purchase is locked; a coin bag
  (`panels/engine_guild_buy.png`) on unlearned skills that can be bought now.
- **Grid**: skills with `guild_row` go in that row; other skills follow, 4 per row. Icons are 80 × 100 px apart.
  Only a row's first icon draws the row divider.
- **Centred rows**: rows listed in `guild_centred_rows` are centred on the widest row and drawn without a
  divider, so they read as headers for the row below. A config row with no skills in the Guild takes no space.

Tooltips in the Guild are [base tooltips](04-tooltips.md#base-tooltips-outside-a-raid).

### Placeholder levels and `guild_first_rank`
DD1 gives every hero level 0 of every skill in the class. To make a skill that must be **bought before it can
be used**, give it a placeholder level 0 with no effects and
`"guild_first_rank": 2` in the config:

```
combat_skill: .id "example_burst" .level 0 … .generation_guaranteed true          (placeholder: no effects)
combat_skill: .id "example_burst" .level 1 … .effect "ex_burst"  (rank 1)
…
```
```json
"example_burst": {"guild_first_rank": 2, "guild_row": 2}
```

- The placeholder level is kept off the skill list (never usable) and off the character sheet.
- In the Guild, while only the placeholder is owned, the icon shows disabled if level 1 is locked, or as
  unlearned (buyable) if it isn't; a click buys level 1. Ranks count from level 1, in the badge and in `[[rank]]`.
- Gate the first real rank in the upgrade tree like any prerequisite. For example:
  ```json
  {"code": "1", "currency_cost": [{"type": "gold", "amount": 250}],
   "prerequisite_requirements": [
       {"tree_id": "example_class.example_burst", "requirement_code": "0"},
       {"tree_id": "guild.skill_levels", "requirement_code": "a"}],
   "prerequisite_resolve_level": 1}
  ```
  Note the game's purchase code checks gold and prerequisite ranks but **not** resolve; the library checks the
  lock first, so resolve requirements hold.

## Files the UI features need
The library loads these from the game's data (mods included). The library package ships the Guild's
([Limits and distribution](build-and-release.md#ddx-package)); your tab icons are yours.

| File | Size | Used by | Made by |
|---|---|---|---|
| `panels/engine_guild_rank.png` | 26 × 26 | the Guild's rank badge (every class) | DDX resource package |
| `panels/engine_guild_buy.png` | 72 × 72 | the Guild's "can be bought" overlay (every class) | DDX resource package |
| string `engine_guild_cost` | | the Guild's cost line; `%s` = the amounts (`Cost: %s`) | the package's string table |
| your tab icons | 20 × 72 each | `skill_pages.icons` | your mod |
| `scripts/layout/panel.banner.darkest` | | room for the previous tab | your mod, if needed |
