#!/usr/bin/env bash
# Build the optional engine hook library (x86_64: the game is Intel-only and runs under Rosetta).
set -euo pipefail
cd "$(dirname "$0")"
command -v clang >/dev/null 2>&1 || {
  echo "macOS build compiler not found: clang (install the Xcode command line tools)" >&2
  exit 1
}
mkdir -p build
clang -arch x86_64 -dynamiclib -O1 -Wall -Wextra -Wno-unused-parameter \
  -o build/ddx.dylib src/*.c
echo "Built $(pwd)/build/ddx.dylib"
