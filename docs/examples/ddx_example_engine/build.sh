#!/usr/bin/env bash
# Build DDX Example's native client (macOS, x86_64 like the game) into the mod: DDXExample/engine/ddx_example.dylib.
# DDX_SRC: the library's headers (engine_hooks/src). Its functions stay unresolved here and are found in the injected
# library when the client loads (-undefined dynamic_lookup).
set -euo pipefail
cd "$(dirname "$0")"
DDX_SRC=${DDX_SRC:-../../../engine_hooks/src}
OUT=../DDXExample/engine/ddx_example.dylib
clang -arch x86_64 -dynamiclib -O1 -Wall -Wextra -Wno-unused-parameter -I "$DDX_SRC" \
  -undefined dynamic_lookup -o "$OUT" src/*.c
echo "Built $(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
