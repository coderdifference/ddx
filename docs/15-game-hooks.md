# 15. Game hooks: the functions a mod can wrap

When no [hook point](12-hook-points.md) or service fits, a client **wraps** a game function. DDX owns the hook on each
function in its catalogue (`engine_hooks/src/game_hooks.c`); any number of mods wrap it, each wrapper calling on to
the next. A mod never patches the game itself.

```c
static void (*o_death)(void *hero);                       // the next wrapper, or the game
static void h_death(void *hero) {
    /* before the game: read the hero while it's still whole */
    o_death(hero);                                         // call on (always, unless you mean to replace the game's code)
    /* after the game */
}
// in engine_client_init:
if (!game_hook_wrap("Hero::OnDeathFinishInternal", (void *)h_death, (void **)&o_death))
    logf_("my mod: hero death hook off");                  // unknown id, or the game's bytes differ (another version)
```

How the chain works:
- A wrapper has **exactly the game function's signature** (C++ `this` as the first argument) and calls `*next`.
- The library hooks a function only once some mod wraps it; until then the game's code runs untouched.
- Wrappers are ordered by the campaign: the library's innermost, then the applied mods from the lowest in the
  campaign's mod list to the highest. The **top mod's wrapper runs first** and sees the final result last, so it
  wins. A mod not applied to the loaded campaign is left out of the chain.
- The chain is relinked at each campaign load and close: always call through your `next` variable, never a copy.
- Up to 8 wrappers per function (all mods together).
- A **site** (`game_hook_site`) is a point inside a function: your `fn(const Regs *r)` reads the game's registers there
  (it can't change them). All observers run, the library's first, the top mod's last. On Windows the stub also saves
  the SSE registers: `game_site_xmm(i)` gives `xmm<i>` (4 floats), which the observer may change (MSVC keeps values
  such as a draw cursor in xmm6-15 across a loop); NULL on macOS.

## The catalogue
Signatures are given as a client declares its `next` pointer. `uint64_t` returns pass through whatever the game
returned (return it unchanged unless you mean to change it).

### Actors and heroes
| Id | Signature | When it runs |
|---|---|---|
| `Actor::UpdateTrinketBuffs` | `uint64_t (void *actor)` | the actor's equipment buffs are rebuilt: add your own buffs after calling on |
| `Actor::GetCurrentResistance` | `float (const void *actor, int type)` | a resistance is read for a check or a tooltip (stun, blight, bleed, disease, move, debuff, deathblow, trap); macOS's deathblow roll doesn't call it, so change deathblow through `GetAdditionalResistance` |
| `Actor::GetAdditionalResistance` | `float (const void *actor, int type)` | a hero's resolve-level part of a resistance is read: every resistance read adds it, the deathblow roll and the deathblow chance shown included (monsters have their own) |
| `Hero::UpdateWeaponAndArmourRanks` | `uint64_t (void *hero)` | the hero's weapon and armour levels are recomputed |
| `Hero::~Hero` | `uint64_t (void *hero)` | a hero object is destroyed (dismissal, death, leaving a campaign): drop anything you keep for it |
| `Hero::AddResolveXpAndLevelEquipmentAndSkills` | `void (void *hero, unsigned xp)` | resolve XP is added and the hero's level, gear and skills follow (also the Stage Coach, town events and spawn data raising a recruit's level) |
| `Hero::OnDeathFinishInternal` | `void (void *hero)` | a hero's death is finalised, in an expedition: its death loot can still be queued |
| `Party::SetSelectedHero` | `void (void *party, void *hero)` | the selected hero changes |
| `Party::SetSelectedHeroByPartyPosition` | `void (void *party, int position)` | the same, by party position |

### Items, loot and quests
| Id | Signature | When it runs |
|---|---|---|
| `InventoryItemSlotInterface::OnDrop` | `uint64_t (void *slot, const void *pos, void *drag)` | something is dropped on an inventory slot (0: refuse) |
| `InventoryItemSlotInterface::OnDropAccepted` | `uint64_t (void *slot, const void *drag)` | a drop from that slot was taken elsewhere |
| `Building::Store::TrinketSupply::GenerateItems` | `void (void *supply)` | the Nomad Wagon restocks |
| `Quest::System::GenerateQuestRewards` | `void (const void *system, void *quests, const void *generation)` | the week's quests get their rewards (`quests`: a `std::vector<GeneratedQuest>`) |

### Town: roster, recruits, stash, estate bar
| Id | Signature | When it runs |
|---|---|---|
| `TownUI::Panel::RosterElement::OnPickedUp` | `void (void *element, const void *pos, void *object, void *data)` | a roster hero is picked up |
| `TownUI::Panel::RosterList::OnDrop` | `uint64_t (void *list, const Vec2 *pos, void *drag)` | something is dropped on the roster |
| `TownUI::Panel::RosterList::DismissHero` | `void (void *list, unsigned guid)` | a hero is dismissed |
| `TownUI::Panel::BaseHeroRecruitDisplay::AddHeroSlot` | `void (void *display, void *hero, bool flag)` | a recruit is shown at the Stage Coach |
| `RecruitHeroSlotInterface::OnPickedUp` | `void (void *slot, const Vec2 *pos, void *object, void *drag)` | a recruit is picked up |
| `UI::Display::NameFrame` | `void (Vec2 pos, const void *icon, const char *name, float *width, const char *sub, const void *icon2, void *fn, void *s1, void *s2, void *preview, uint64_t id, const void *elem)` | a panel's name frame is drawn (tell callers apart with `call_site_return`) |
| `EstateSummary::PreChildrenShowInternal` | `void (void *panel)` | the estate bar draws (town, every frame) |
| `EstateSummary currency entry` | site: `void (const Regs *r)`; `r->rax` the entry's currency class | each currency of the estate bar |

### Expedition screen
| Id | Signature | When it runs |
|---|---|---|
| `RaidDisplay::PrePanelShowInternal` | `void (void *display)` | every frame of an expedition: draw your own UI after calling on (check `display == *RaidDisplay::s_Instance`) |
| `DDTabPanel::RenderPartyReorderButton` | `void (void *tabpanel)` | the dungeon panel's party reorder button is drawn |

### Town: the Blacksmith
| Id | Signature | When it runs |
|---|---|---|
| `BlacksmithDisplay::OnActionEnterStartInternal` | `void (void *panel)` | the Blacksmith opens on a hero |
| `BlacksmithDisplay upgrade trees` | `void (const void *closure, int type)` | its per-equipment-type upgrade tree lambda |
| `BlacksmithDisplay unselect hero` | `void (void *func, unsigned *guid)` | its unselect-hero `std::function` |
| `BlacksmithDisplay::OnActionShowBodyInternal` | `void (void *panel)` | the Blacksmith's body draws |
| `BlacksmithDisplay::GetUpgradeDescriptionList` | `void *(void *out, void *self, uint64_t req)` | an upgrade's description lines (returned through `out`) |
| `BuildingDisplay::GetChildPanelOffset` | `Vec2 (const void *self, unsigned i)` | where a building's child panel goes |
| `HeroActionDisplay::PreChildrenShowBodyInternal` | `void (void *panel)` | a building's hero action panel draws |

### The character sheet
| Id | Signature | When it runs |
|---|---|---|
| `HeroDisplay::App::ShowBaseStats CRIT tooltip` | `void (void *closure)` | the CRIT stat's tooltip lambda |

## Adding a function to the catalogue
A function you need that isn't listed gets a catalogue entry, not a hook in your client (DDX is a shared library;
send the entry upstream). An entry is an id, the mangled symbol, and the first bytes of the function exactly as they
must be: whole instructions, at least 14 bytes, with any RIP-relative operand listed. The bytes are checked before
patching, so a different game version turns the hook off instead of corrupting it.

1. Find the function: `nm -gU <game binary> | c++filt | grep <Name>`, then its mangled name with `nm -gU` alone.
2. Disassemble its start (`otool -tV -p <mangled> <binary> | head`, or a disassembler) and copy the prologue bytes.
   Common prologues have macros: `PROLOGUE_SUB32(lo, hi)`, `PROLOGUE_SUB8(n)`.
3. Add `{"<Id>", "<mangled>", B({...})}` to `g_defs` in `engine_hooks/src/game_hooks.c` (with `.rip` / `.nrip` for
   RIP-relative operands, `.site = true, .offset = ...` for a mid-function site).
4. `engine_hooks/tests/run.sh` (the offline test runs the stubs on real code), then check `ddx.log` in game:
   `<Id>: hooked at 0x...`.

Windows: the catalogue's functions need entries in `syms_win.c` too (offsets in the MSVC build); see
[Extending](09-extending-the-engine.md).
