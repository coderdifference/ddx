using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors a native stash sort key containing three numeric comparison values and a UTF-8 name tie-breaker. Preserve field order and the inline buffer capacities.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct StashKey
{
    /// <summary>Stores the three native numeric sort components in comparison priority order.</summary>
    public fixed long Values[3];

    /// <summary>Stores the inline UTF-8 name used for sort tie-breaking. Keep it null-terminated within the 72-byte ABI capacity.</summary>
    public fixed byte Name[72];
}
