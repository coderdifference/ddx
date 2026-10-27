#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p artifacts/tests
clang -arch x86_64 -Wl,-export_dynamic -Wall -Wextra tests/test_service_host.c -o artifacts/tests/service_host
artifacts/tests/service_host "$(pwd)/artifacts/osx-x64/ddx_mod_menu.dylib"
