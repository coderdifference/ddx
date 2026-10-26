#!/usr/bin/env bash
# Launch Darkest Dungeon with ddx.dylib injected (macOS). Steam must be running.
# Steam's launch options on macOS don't accept "VAR=value %command%", so we start the game directly.
# By default the intro (logos, splash) and the videos are skipped, so the game opens on the campaign selection
# (-skipfeflowintro sets the front end's first state, Darkest::ParseArguments; -disablevideo turns off videos).
# Set DD_INTRO=1 to keep them. Extra arguments are passed to the game too, e.g.:
#   engine_hooks/run_game.sh -enabledebuginput -skiptutorial
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="$HERE/build/ddx.dylib"
GAME="${DD_GAME:-$HOME/Library/Application Support/Steam/steamapps/common/DarkestDungeon/_osx/Darkest.app/Contents/MacOS/Darkest}"
[[ -f "$LIB" ]] || "$HERE/build.sh"
[[ -x "$GAME" ]] || { echo "Game not found: $GAME" >&2; exit 1; }
# The game must start in its data folder (it looks for its 'pin' file there). Started outside Steam,
# the Steam API needs the app id, otherwise it relaunches the game through Steam (without the
# library): pass it as SteamAppId/SteamGameId instead of writing a steam_appid.txt into the game.
cd "$(dirname "$GAME")/../Resources/data"
rm -f ddx.log
rm -f ddx_combat.log


ARGS=()
[[ "${DD_INTRO:-0}" == 1 ]] || ARGS=(-skipfeflowintro -disablevideo)
SteamAppId=262060 SteamGameId=262060 DYLD_INSERT_LIBRARIES="$LIB" exec "$GAME" ${ARGS[@]+"${ARGS[@]}"} "$@"
