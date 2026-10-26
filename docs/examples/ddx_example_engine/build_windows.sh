#!/usr/bin/env bash
# Build DDX Example's native client for Windows (64-bit, mingw-w64, UCRT as the library) into the mod:
# DDXExample/engine/ddx_example.dll, linked against the library's import library (engine_hooks/build_windows.sh writes it).
set -euo pipefail
cd "$(dirname "$0")"
DDX_SRC=${DDX_SRC:-../../../engine_hooks/src}
IMPLIB=${DDX_IMPLIB:-../../../engine_hooks/build/win/libddx.dll.a}
OUT=../DDXExample/engine/ddx_example.dll
x86_64-w64-mingw32-gcc -shared -O1 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  -mcrtdll=ucrt -static-libgcc -I "$DDX_SRC" -o "$OUT" src/*.c "$IMPLIB"
echo "Built $(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
