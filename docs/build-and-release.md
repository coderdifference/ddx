# Building, packaging and checking a release

## Native engine

`engine_hooks/build.sh` builds the Intel macOS library using clang and the system SDK. `engine_hooks/build_windows.sh` uses `x86_64-w64-mingw32-gcc`, links against UCRT and emits a DLL, launcher and import library. Use a mingw-w64 version that accepts `-mcrtdll=ucrt`.

```sh
./engine_hooks/build.sh
./engine_hooks/build_windows.sh
./engine_hooks/tests/run.sh
```

The offline suite is designed for macOS: several tests use Mach-O linking and Intel executable fixtures. It is not a Windows gameplay test suite.

## DDX package

```sh
python3 tools/stage.py --platform macos --output /tmp/ddx-release/DarkestDungeonExtender
python3 tools/stage.py --platform windows --output /tmp/ddx-win-release/DarkestDungeonExtender
```

Staging requires the requested platform's built artifacts. It copies DDX resources, adds the binaries, compiles localization and creates `DarkestDungeonExtender_modfiles.txt`. It refuses a nonempty output directory so a previous package cannot silently leave stale files behind. It does not upload anything.

The resource marker `engine/library.json` must remain in the DDX package. Client configs and binaries belong in the client's own `engine/` directory. The library binary belongs in DDX's `bin/` directory.

## Client package

Build against this checkout's API. On macOS, native clients resolve DDX exports at load time with dynamic lookup. Windows native clients link against `engine_hooks/build/win/libddx.dll.a`. C# clients compile the shared API sources and publish with NativeAOT on the target operating system.

Ship the client library and its configs, localization, textures and layouts. Compile string tables using `tools/localization.py`; it uses Python's standard library. Write a manifest listing each shipped mod resource with paths relative to the mod root. The staging tool's `--manifest-only` mode can generate one for a client folder.

Do not ship debug logs, build intermediates, local game paths or game binaries. Workshop metadata should describe the dependency and launch requirements. Publishing or licensing decisions remain separate from building the package; this source tree does not add a blanket license grant.

## Runtime release check

Launch from the staged package and confirm its binary is the one loaded. Check `ddx.log` for the executable build, rejected hooks and client errors. Test a clean campaign with only DDX, then your client, then another client. Verify save/reload, campaign deactivation, screen transitions and required resources on macOS and Windows. Record the game build and platform with the results. Do not label a feature verified solely because the build and offline suite passed.
