using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Flags native item validation operations to apply to custom item types. Values are combined as a bitmask and must match the native ABI.</summary>
[Flags]
public enum ItemTypeChecks : uint
{
    /// <summary>Selects none for ItemTypeChecks.</summary>
    None = 0,

    /// <summary>Selects sell for ItemTypeChecks.</summary>
    Sell = 1,

    /// <summary>Selects stash drop for ItemTypeChecks.</summary>
    StashDrop = 2,
}
