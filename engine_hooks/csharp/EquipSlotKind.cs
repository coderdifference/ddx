using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Equipment kind callbacks and strings remain valid for the client lifetime.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct EquipSlotKind
{
    /// <summary>Points to the stable null-terminated UTF-8 equipment-kind identifier used for native registration.</summary>
    public byte* Id;

    /// <summary>Returns the borrowed native item for a hero and slot index, receiving registered user data.</summary>
    public delegate* unmanaged[Cdecl]<nint, int, void*, nint> Item;

    /// <summary>Checks whether a native item can enter the selected hero equipment slot and returns the native acceptance result.</summary>
    public delegate* unmanaged[Cdecl]<nint, int, nint, void*, int> Accepts;

    /// <summary>Updates the selected native hero equipment slot and returns a byte success flag.</summary>
    public delegate* unmanaged[Cdecl]<nint, int, nint, void*, byte> Set;

    /// <summary>Tests whether the selected native hero equipment slot permits moving its item.</summary>
    public delegate* unmanaged[Cdecl]<nint, int, void*, byte> Movable;

    /// <summary>Stores the number of slots registered for this equipment kind.</summary>
    public int Count;

    /// <summary>Stores opaque client context passed unchanged to equipment-kind callbacks. Keep it alive for the registration lifetime.</summary>
    public void* UserData;
}
