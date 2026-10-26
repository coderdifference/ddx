# DDX engine

Build with `./build.sh` on macOS or `./build_windows.sh` with a UCRT-capable mingw-w64 compiler. Run `./tests/run.sh` on macOS for the offline suite.

See the [repository README](../README.md), [architecture](../docs/architecture.md) and [modder's guide](../docs/README.md). `src/common.h` declares the C API; `src/game_objects.h` declares semantic game adapters. Resources in `mod/` are needed for DDX's shared UI.
