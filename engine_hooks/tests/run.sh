#!/usr/bin/env bash
# Offline tests for the config/JSON code (native build, stand-ins for the game).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p ../build
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_config test_config.c ../src/json.c ../src/expr.c
# example_class.engine.json is the reference test input (ENGINE_TEST_CONFIG overrides).
../build/test_config "${ENGINE_TEST_CONFIG:-example_class.engine.json}"

# Client-owned engine paths (mods.c)
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_mod_paths test_mod_paths.c
../build/test_mod_paths

# Semantic actor/item/inventory object views
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_game_objects test_game_objects.c
../build/test_game_objects

# Native equipment row ownership and nested restoration
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_game_equipment test_game_equipment.c
../build/test_game_equipment

# A registry's campaign order (mods_list.c)
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_mods_list test_mods_list.c ../src/mods_list.c
../build/test_mods_list

# Hook point registry (hooks.c)
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_hooks test_hooks.c ../src/mods_list.c
../build/test_hooks

# Item type registry (item_types.c)
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_item_types test_item_types.c ../src/mods_list.c
../build/test_item_types

# Hook installers' fix-up for a RIP-relative operand in moved code (hook_reloc.c)
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_hook_reloc test_hook_reloc.c ../src/hook_reloc.c
../build/test_hook_reloc

# Shared game hooks (game_hooks.c): stubs, the wrapper chain and site observers on real code (x86_64, Rosetta)
clang -arch x86_64 -Wall -Wextra -Wno-unused-parameter -o ../build/test_game_hooks test_game_hooks.c ../src/game_hooks.c ../src/mods_list.c
../build/test_game_hooks | tail -1

# Compare sites (hook_cmp.c, hook.c): x86_64, run under Rosetta on Apple silicon; patches and calls real code
clang -arch x86_64 -Wall -Wextra -Wno-unused-parameter -o ../build/test_cmp_site test_cmp_site.c ../src/hook_cmp.c \
    ../src/hook.c ../src/hook_reloc.c
../build/test_cmp_site | tail -1

# Layout files (ui_layout.c)
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_ui_layout test_ui_layout.c ../src/ui_layout.c ../src/json.c
../build/test_ui_layout

# Item data: the iid tag walk against a real libc++ std::map (the game's tag map), the game's tag move, the fields
clang -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -DITEMDATA_TEST -c -o ../build/itemdata_test.o ../src/itemdata.c
clang++ -std=c++17 -Wall -o ../build/test_itemdata test_itemdata.cpp ../build/itemdata_test.o
../build/test_itemdata

# Item content (item_tips.c): building content, placing its layers
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_item_tips test_item_tips.c ../src/mods_list.c
../build/test_item_tips

# Act-out roll weights (act_outs.c, fake game data)
clang -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -o ../build/test_act_outs test_act_outs.c ../src/expr.c ../src/mods_list.c ../src/glob.c
../build/test_act_outs

# Config hook rules (hook_rules.c): parsing, filters, before changes, actions, the loop guard.
clang -Wall -Wextra -Wno-unused-parameter -o ../build/test_hook_rules test_hook_rules.c ../src/json.c ../src/expr.c ../src/glob.c
../build/test_hook_rules

# Popup source fallback and tray origin selection (unused game installers are stripped).
clang -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections -Wl,-dead_strip \
    -o ../build/test_popup_sources test_popup_sources.c
../build/test_popup_sources

# Effect registrations must own temporary managed UTF-8 prefix buffers.
clang -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections -Wl,-dead_strip \
    -fsanitize=address -o ../build/test_effect_prefix test_effect_prefix.c
../build/test_effect_prefix
