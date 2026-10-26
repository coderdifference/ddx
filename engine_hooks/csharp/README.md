# Managed DDX API

Compile these sources into a .NET 10 NativeAOT shared-library client. The [managed starter](../../examples/managed/README.md) provides a complete project; the [guide](../../docs/managed-clients.md) explains lifecycle and ownership.

`DDXClient` binds the host and roots its callbacks. `DDXApi`, `DDXUi` and `DDXCombat` expose native services. `Api.Objects` provides semantic game-object views through native adapters. Native layouts and pointer arithmetic belong in DDX's C implementation.

Each public type has its own file. Private nested helpers use `Owner.Helper.cs`. Preserve ABI field order and document ownership, sentinel values and mutation rules when changing interop types.
