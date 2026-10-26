#!/usr/bin/env bash
# Build the Windows engine hook library and its launcher (64-bit; needs mingw-w64: brew install mingw-w64):
#   build/win/ddx.dll   the library (UCRT, as the game: memory the game frees can be ours and the other way); all its
#                           symbols exported, with build/win/libddx.dll.a to link native clients against
#   build/win/ddxlaunch.exe  starts the game with the library loaded
set -euo pipefail
cd "$(dirname "$0")"
CC=x86_64-w64-mingw32-gcc
command -v "$CC" >/dev/null 2>&1 || {
  echo "Windows build compiler not found: $CC (install mingw-w64, for example: brew install mingw-w64)" >&2
  exit 1
}
mkdir -p build/win
$CC -shared -O1 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -mcrtdll=ucrt -static \
  -Wl,--export-all-symbols -Wl,--out-implib,build/win/libddx.dll.a -o build/win/ddx.dll src/*.c
$CC -O1 -Wall -Wextra -municode -mcrtdll=ucrt -static \
  -o build/win/ddxlaunch.exe win/ddxlaunch.c
echo "Built $(pwd)/build/win/ddx.dll and ddxlaunch.exe"
