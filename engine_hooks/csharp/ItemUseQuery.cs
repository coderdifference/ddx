using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Borrowed item usability query from DDX.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct ItemUseQuery
{
    /// <summary>Stores the borrowed native hero and item handles being checked for usability.</summary>
    public nint Hero,
        Item;

    /// <summary>Stores the native item type hash and concrete item identifier hash in ABI order.</summary>
    public uint TypeHash,
        IdHash;

    /// <summary>Stores native byte booleans describing the current town and battle contexts.</summary>
    public byte InTown,
        InBattle;
}
