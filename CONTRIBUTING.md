# Working on DDX

Build from the repository root and run `engine_hooks/tests/run.sh` on macOS. Build Windows with `engine_hooks/build_windows.sh` when changing native code. Publish the managed starter when changing the C# API.

Keep raw game layouts, symbols and platform calling conventions in the native engine. Expose semantic operations to clients. A game function has one patch owner; add a shared event or catalogue entry when several features need it.

For each new game hook, document the exact signature, expected bytes, relative operands and the game build used to verify it. Unknown patch bytes must leave the hook off. A build pass is separate from in-game verification; report both.

Use `.clang-format` for C and CSharpier for C#. Keep one public C# type per file and document ownership, sentinels and mutation behavior. Add tests where they can check meaningful behavior without the game. Update the relevant guide when changing an API or package requirement.

Bug reports should include the platform, game build, reproduction steps, enabled client mods and the relevant section of `ddx.log`. Remove personal paths or save information before posting logs publicly.
