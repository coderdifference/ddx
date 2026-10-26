# 9. Extending the engine

For engineers adding a feature to `engine_hooks` itself: a new hook, a new config key, or support for a game
of how each existing piece was found, with the offsets and what was tested; read the entry for anything near
what you're changing.

## Architecture
The library is plain C (no C++ runtime), one file per feature, all sharing `src/common.h`:

| File | Role |
|---|---|
| `main.c` | Constructor run at injection: opens the log, finds core symbols, loads configs, installs every feature in order. |
| `game_hooks.c` | Shared game hooks: the catalogue of game functions clients wrap (`game_hook_wrap`, `game_hook_site`). |
| `clients.c` | Native clients: loads every mod's `engine/*.dylib` (`*.dll`) and calls its `engine_client_init` (below). |
| `hook.c` | `install_hook`, `install_hook_with_trampoline`, `sym` (symbol lookup). |
| `game.c` | Core game functions and object layout: `is_hero`, `is_monster`, `hero_guid`, `monster_guid`, `hero_class_hash`, `skill_id_hash`, `name_hash`, `find_effect`, `actor_remove_buffs`. |
| `log.c` | `logf_` (always) and `dbgf_` (only with `"debug": true`). |
| `json.c`, `expr.c` | JSON reader; formula evaluator. |
| `config.c` | Loads `*.engine.json`; every query the features make (`config_skill_allowed`, `config_apply_action`, …). |
| `herodata.c`, `monsterdata.c` | Per-hero and per-monster fields; `Hero::Save` / `Hero::Restore` and `Monster::Save` / `Monster::Restore` hooks. |
| `itemdata.c` | Per-item fields for items that never stack (trinkets), keyed by an item id kept in the item's tags; `Inventory::Item::Save` / `Restore` hooks (macOS). |
| `ugc.c` | Local mod list fixes: `project.xml` titles and descriptions (the game's XML reader drops them, so every local mod shared the id ""), with old campaigns' `""` ids still resolving. |
| `effects.c` | `EffectApplyInternal`: actions, `ddx_set_* / ddx_add_*`, `remove_quirk_*`, client effect prefixes (`effects_on_prefix`), effect variants and heal formulas. `Buff::Group::AddBuff`: `buff_stack_limit(id, max)`, at most `max` instances of a buff id from one kind of source (one more replaces the oldest, refreshing the stack; gear buffs never count). |
| `skills.c` | `Hero::GetModeSelectedCombatSkills`: conditions, placeholders, paging window. |
| `stats.c` | Damage (`GetDamageModifier`) and heal (`Actor_DoSkillFriendly`) formulas. |
| `anim.c` | `Actor::SetSpriteAnimation`: animation variants. |
| `hooks.c` | The hook point registry: every point's name and phases, `hook_on`, `hook_emit`, `hook_wanted` ([chapter 12](12-hook-points.md)). |
| `events.c` | The per-hero points (`Hero::OnRaidStart` …); config `events` as listeners. |
| `combat_hooks.c` | The combat points (`Battle::ApplyHit`, `Hero::OnDeathDoorDeathAvoided`) and the follow-up hit and side effect helpers. |
| `cooldown.c` | `Battle::Actor_DoSkill`, `Battle::GetIsSkillBattleValid`: cooldowns, skill-use ids and the active performer/skill/target context. |
| `items.c` | Who may use an item: `DungeonController::CanHeroUseItem` (expedition) and the realm inventory's validity lambda plus `TownDisplay::GetCharacterPanelHero` (town marks). Order: the game, a registered type's `can_use`, `items_on_use` rules, then config `items` rules (expedition). `items_town_hero` sets the hero town marks are for with no sheet open. |
| `tooltip.c` | Skill tooltips, live and base. |
| `campaigndata.c`, `announce.c` | Campaign-wide fields (`Options::System::SaveProfileOptions` / `RestoreProfileOptions` / `ResetProfileOptions`); the once-per-campaign announcement (the estate bar through the catalogue, `raid_ready`, the frame-end callback). |
| `paging.c`, `sheet.c`, `guild.c`, `ui.c` | Combat bar paging, character sheet paging, Guild grid, shared `GUI_ImageButton` hook and UI helpers. |
| `ui_draw.c`, `ui_text.c`, `ui_tooltip.c`, `ui_input.c`, `ui_confirm.c`, `ui_dialog.c`, `ui_layout.c`, `callsite.c` | The UI library ([chapter 11](11-ui-library.md)): textures, images, buttons, drop targets, localized text; the one owner of `GUI_Text` (filters, substitutions); tooltips in panels, tooltip line icons and the frame-end callback (owner of `GUI_RenderTooltips` and the tooltip text drawing); mouse buttons and modifier keys from SDL (press and release positions and modifiers); the question dialog; dialog frames at any size and placing them (on the screen's centre); layout files; checked call sites. |

Features that nothing uses skip their hooks (`events_install` and `combat_hooks_install` install only for points
something listens to (`hook_wanted`), `events_install` also for cooldowns,
`stats_install` without formulas), so the library touches as little as possible.

## How a hook works
```c
static const uint8_t prologue[] = PROLOGUE_SUB8(0x38);          // sub rsp,0x38
g_orig = (get_skills_fn)install_hook("Hero::GetModeSelectedCombatSkills",
                                     sym("_ZNK4Hero27GetModeSelectedCombatSkillsEv"),
                                     (void *)hook, prologue, sizeof prologue);
```
- `sym(mangled)` finds a game function by its mangled C++ name, **without** the leading underscore. Most are
  exported (`dlsym`). Private ones ("was a private external") are found in the executable's full symbol table
  (`LC_SYMTAB`), which `sym` reads as a fallback. No hard-coded addresses anywhere.
- `install_hook` compares the function's first bytes with `expected`. **If they differ, it logs and refuses**:
  a game update can switch a feature off, never crash it. On a match it writes an absolute
  `jmp [rip+0]` to your detour over those bytes and returns a trampoline (the saved bytes, then a jump back)
  that calls the original.
- `expected` must be **whole instructions, at least 14 bytes, with no relative branches or calls** (they'd point
  somewhere else once copied). `PROLOGUE_SUB32(a, b)` / `PROLOGUE_SUB8(a)` are the common
  `push rbp … sub rsp, imm` prologues.
- When those bytes hold **RIP-relative operands** (`mov rax,[rip+disp32]`, often the stack guard load right after
  the prologue, `lea` of a vtable or global, or a `call` / `jmp` / `jcc` rel32, which are relative the same way), use
  `install_hook_rip(name, target, detour, expected, len, rip, nrip)`: one `HookRip {disp, next}` per operand, `disp`
  the disp32's offset in `expected`, `next` the end of that instruction (they differ when an immediate follows).
  The trampoline is placed within 2 GB of the game's code and each operand recomputed for it; a short (rel8) jump
  is one too (`next == disp + 1`), rewritten there as a rel32 one. `expedition_hooks.c` hooks `Raid::ApplyBurnDot` this way:
  ```c
  // push rbp; mov rbp,rsp; push r14; push rbx; sub rsp,0x50; mov rax,[rip+disp32] (disp32 at 14, ends at 18)
  static const uint8_t b[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x50,
                              0x48, 0x8b, 0x05, 0xe6, 0x97, 0x96, 0x00};
  install_hook_rip("Raid::ApplyBurnDot", sym("_ZN4Raid12ApplyBurnDotEP5Actor"), (void *)burn_hook, b, sizeof b,
                   (const HookRip[]){{14, 18}}, 1);
  ```
- For anything else (a branch back into the replaced bytes), build the trampoline yourself and use
  `install_hook_with_trampoline`. `stats.c` does this for `ActorCombatSkill::GetDamageModifier`, whose short
  `je` is rewritten as a `jne` over an absolute jump.
- Detours call the original through the trampoline and **return what it returned**, even for functions you
  believe are void (the event hooks return `uint64_t` for that reason).

## Finding a function
1. List symbols: `nm -gU Darkest | c++filt | grep -i <word>` on the game binary
   (`…/Darkest.app/Contents/MacOS/Darkest`). Private symbols: `nm Darkest` (without `-gU`).
2. Disassemble it: `otool -tV Darkest -p <mangled name>` or `objdump -d --disassemble-symbols=<mangled> Darkest`,
   or a disassembler such as Hopper or Ghidra for reading call graphs.
3. Work out the signature from the prologue's register use and the callers (SysV x86_64: `rdi`, `rsi`, `rdx`,
   `rcx`, `r8`, `r9`; floats and packed `vec2` in `xmm0…`; `this` first).
4. **Hidden return pointers.** A function that returns a struct takes a hidden first argument (`rdi`), shifting
   the others. `Behaviors::Combat_StartTurn(prng&, Actor*)` is really `(result, rng, actor)`; a hook with the
   visible signature crashed at the first hero turn. Check the prologue before trusting a signature.
5. Copy the first 14+ bytes of whole instructions as `expected`.

## Object layout
Offsets live as `#define`s at the top of the file that uses them, with a comment saying where they were found.
The main ones:

| Object | Offset | Field |
|---|---|---|
| `Hero` | `0x1298` | `HeroClass*` (class id hash at `HeroClass+0xd0`) |
| `Hero` | vtable symbol `_ZTV4Hero` + 16 | how `is_hero` recognises a hero |
| `Actor` | `0x460` | buff group (`std::vector<Buff::GroupEntry>`, entries `0x1d8`, id string at `+8+0x61`) |
| `Actor` | `0x1058` | `SpineSprite*` |
| `ActorCombatSkill` | `0x4c` | id hash; `0x48` level (`HeroCombatSkill`) |
| `ActorCombatSkill` | `0x364` / `0x368` | `.heal` min / max (int) |
| `ActorCombatSkill` | `0x380` / `0x384` | `.atk` / `.crit` (float) |
| `ActorCombatSkill` | `0x43c` | `.dmg` base (float) |
| `Effect` | `0` / `0xc8` / `0x360` | name string / `.heal` (float) / swap-source-and-target flag |

Hashes are the game's own `string_hash(s, 0)` (`name_hash`), so a config id and the game's id match.

## Adding a config key
1. Add storage: a member of `ClassConfig` (class-wide) or `SkillRule` (per skill) in `config.c`.
2. Parse it in `parse_class`. **For a skill key, add an explicit `if (!strcmp(key, "<name>")) { …; continue; }`
   before the final `parse_cond` call**, or it will be read as a condition on a field of that name.
3. Add an accessor (`config_<thing>(const ClassConfig *c, …)`) and declare it in `common.h`.
4. Document the key in the comment block at the top of `config.c` and in
   [Config reference](03-config-reference.md).
5. Add checks to `tests/test_config.c` and run `tests/run.sh`.

## Adding a feature
1. Find the game function and confirm, in the disassembly, that hooking it does what you think (who calls it,
   when, with what).
2. Write `src/<feature>.c` with a `<feature>_install()` that finds symbols and installs hooks, skipping them
   when no config needs the feature.
3. Declare it in `common.h` and call it from `main.c`. Order matters only for shared hooks (`ui_install` goes
   last, after its clients register with `ui_on_image_button`).
4. In the detour, act only for configured classes (`config_for_hero(hero)` returns NULL otherwise) unless the
   feature is meant to be global.
5. Log what matters with `logf_`, detail with `dbgf_`.
   tested, offsets found).

## Patterns worth reusing
- **Temporary override**: to make the game use a per-hero value inside one call, write it into the game object,
  call the original, and restore it (the skill heal formula writes `.heal` min/max around
  `Actor_DoSkillFriendly`; the effect heal formula writes `Effect+0xc8` around `EffectApplyInternal`).
- **Context from an outer call**: hook an outer function to remember "which hero", read it in an inner hook
  (`stats.c` notes the hero in `CalculateDerivedStatsInternal` and uses it in `GetDamageModifier`).
- **Filter in place**: `skills.c` compacts the game's returned `std::vector` of skill pointers by moving `end`.
- **Per-hero state** that must persist goes in fields (`field_register` + `herodata_get`/`set`); UI-only state
  (the current page) can be a small table keyed by hero guid.
- **Per-monster state** that must persist goes in `monsterdata_get` / `monsterdata_set`; serialize it in
  `Monster::Save` / `Monster::Restore`, keyed at runtime by `Monster::GetGuid`.
- **Per-item state** goes in `itemdata_get` / `itemdata_set` (call `itemdata_enable` before `itemdata_install`). An
  inventory item has no guid and is copied between slots, so the library keeps an item id in its tags map, as the
  tag's key (the game treats tag values as stack counts). Only items with a stack limit of 1 can have one.
- **Skill resolution context** is scoped around the original `Battle::Actor_DoSkill` call. Use the
  `skill_resolution_*` accessors to read the performer, skill and complete target vector from another hook.
- **UI**: `ui_on_image_button(before, after)` lets a feature change or add image buttons as the game draws them;
  `ui_function` builds a libc++ `std::function<void()>` from a C callback; `ui_text` draws text with a font and
  colour id.
- **Call the game's own code** for anything with side effects (buying upgrades, removing buffs, localizing
  strings, building tooltips) rather than reimplementing it.

## Native clients (a mod's own C code)
*A full walkthrough with boilerplate is in [Native clients](13-native-clients.md); this section is the summary.*

A mod that needs code of its own (not only a config) ships it as a **native client**: a dynamic library in the mod's
`engine/` folder, built against the library's headers. At start-up the library loads every `<mod>/engine/*.dylib`
(Windows `*.dll`) from the game's mods folder and the Workshop's, after the configs and its own hook point listeners
and before its features install, and calls the client's exported

```c
int engine_client_init(const EngineClientInfo *info);   // info->api: ENGINE_CLIENT_API; info->engine_dir: the mod's engine/
```

A client checks `info->api`, then registers what it needs (hook point listeners, item types, slots, layouts with
`ui_layout_mod`, tooltip and stat callbacks) and returns 0; anything else is logged (`client <path>: init failed`)
and the client stays off. `ddx.log` lists each client and `clients: N loaded, M failed`. Call `log_file("<name>")` first
to give the client's `logf_` / `combat_logf` lines their own `<name>.log` / `<name>_combat.log`
([Testing and debugging](08-testing-and-debugging.md#the-log)).

Building (macOS): `clang -arch x86_64 -dynamiclib -I <library>/src -undefined dynamic_lookup -o <mod>/engine/<name>.dylib
src/*.c`. The library's functions stay unresolved in the client and are found in the injected library when the client
loads. Windows: link against the library's import library, `libddx.dll.a` (`build_windows.sh` writes it).

A client never calls `install_hook` itself: a function carries one hook, so two mods hooking the same one couldn't both
work. It uses the library's hook points and client APIs, or wraps a game function the library owns (below).

## Shared game hooks (wrapping a game function)
`game_hooks.c` keeps a catalogue of the game functions clients may change: each entry's id, symbol and the bytes its
hook replaces (checked first, so another game version turns the hook off). A client wraps one with a function of the
same signature that calls on through `next`:

```c
static void (*o_update)(void *actor);
static void h_update(void *actor) {
    o_update(actor);                    // the wrapper registered before this one, or the game
    /* ... add this mod's buffs ... */
}
// in engine_client_init:
game_hook_wrap("Actor::UpdateTrinketBuffs", (void *)h_update, (void **)&o_update);
```

The library hooks the function with its first wrapper (until then the game runs untouched) and jumps through a stub
to the outermost wrapper; each wrapper's `next` is the one inside it, so any number of mods can wrap the same
function. A wrapper belongs to the mod that registered it and is linked only while the loaded campaign applies that
mod: the library's innermost, then the campaign's mods from the lowest in its mod list to the highest, so the top
mod's wrapper runs first and has the last word on the result. The chain is relinked at each campaign load, so read
`next` at each call (a function pointer variable does). `game_hook_site(id, fn)` is the same for a catalogued
mid-function site: each `fn(const Regs *)` reads the game's registers there, in the same order (the library's first,
the top mod's last; macOS only). A function that
isn't in the catalogue yet gets an entry there (with its bytes from the disassembly), not a hook in the client. The
offline test `tests/test_game_hooks.c` runs the stubs and the chain on real code.

## One hook per function
A function can carry only one hook: a second `install_hook` on it finds the first patch and logs `already hooked (one
hook per function: share the first one), hook off`. For clients, the catalogue above is the one owner of each
function they change. When two features need the same function, keep one hook in the
module that owns it and give the others a way in: a hook point (`hook_emit`) for game events, or a small client API
for queries (as `items_on_use` for `CanHeroUseItem`, `effects_on_patch` for `EffectApplyInternal`).

## When the game updates
Hooks whose bytes changed log `unexpected prologue (other game version?), hook off` and stay off; the rest keep
working. For each: re-disassemble, check the function still does the same thing, update `expected`, and re-check
any offsets that file uses.

## Platform work

Windows already has a native build and launcher. Its address table is in `syms_win.c`; `hook_win.c` owns patching and executable checks. Follow the [architecture guide](architecture.md) for ABI and relocation rules. Linux needs a separate loader, symbol mapping and game-layout validation.
