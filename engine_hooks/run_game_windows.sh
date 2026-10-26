#!/usr/bin/env bash
# Launch the Windows Darkest Dungeon in a CrossOver bottle with ddx.dll injected (ddxlaunch.exe; the Windows
# counterpart of run_game.sh). Steam must be running in the bottle. Extra arguments are passed to the game.
#   CROSSOVER_APP   the CrossOver app (default: the first of /Applications/CrossOver.app, ~/Applications/CrossOver*.app,
#                   ~/Downloads/CrossOver*.app that has wine)
#   CX_BOTTLE       the bottle (default Steam)
#   CX_BOTTLE_PATH  the bottles folder (default ~/CXPBottles if it has the bottle, else CrossOver's own)
#   DD_WIN_GAME     the game folder, Windows path (default: Steam's install folder, from the registry)
# On Windows itself, run build/win/ddxlaunch.exe (ddx.dll next to it) the same way.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if [[ -z "${CROSSOVER_APP:-}" ]]; then
  for app in /Applications/CrossOver.app "$HOME"/Applications/CrossOver*.app "$HOME"/Downloads/CrossOver*.app; do
    if [[ -x "$app/Contents/SharedSupport/CrossOver/bin/wine" ]]; then CROSSOVER_APP="$app"; break; fi
  done
fi

CX_BOTTLE="${CX_BOTTLE:-Steam}"
if [[ -z "${CX_BOTTLE_PATH:-}" && -d "$HOME/CXPBottles/$CX_BOTTLE" ]]; then export CX_BOTTLE_PATH="$HOME/CXPBottles"; fi
WINE="${CROSSOVER_APP:-}/Contents/SharedSupport/CrossOver/bin/wine"
[[ -f "$HERE/build/win/ddx.dll" && -f "$HERE/build/win/ddxlaunch.exe" ]] || "$HERE/build_windows.sh"
[[ -x "$WINE" ]] || { echo "CrossOver's wine not found: $WINE (set CROSSOVER_APP)" >&2; exit 1; }
args=()
[[ -n "${DD_WIN_GAME:-}" ]] && args=(--game "$DD_WIN_GAME")
[[ "${DD_INTRO:-0}" == 1 ]] || args+=(-skipfeflowintro -disablevideo)
# 1. Force DX9 DXVK Overrides
export WINEDLLOVERRIDES="d3d9=n,b"

# 2. Fix Shader Compilation Stutter (Crucial for DXVK)
export DXVK_ASYNC=1                  # Compiles shaders asynchronously in the background
export DXVK_STATE_CACHE=1            # Caches compiled pipelines to prevent future stutters

# 3. Synchronisation Tweaks for M4 Silicon
export WINE_MSYNC=1                  # Low-overhead Mac synchronization
export WINE_D3D_NO_ASYNC=1           # Prevents thread collision if DXVK falls back

# 3. Optional: Turn on the DXVK Heads-Up Display for mod testing performance
#export DXVK_HUD="fps,devinfo"

echo "Launching Windows Darkest Dungeon in bottle $CX_BOTTLE with ddx.dll injected..."
echo "  $WINE --bottle $CX_BOTTLE $HERE/build/win/ddxlaunch.exe ${args[@]+"${args[@]}"} $*"
exec "$WINE" --bottle "$CX_BOTTLE" "$HERE/build/win/ddxlaunch.exe" "${args[@]+"${args[@]}"}" "$@"
