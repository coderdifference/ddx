using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Managed ABI mirror of ItemTypeDef in engine_hooks/src/common.h. Query structs remain opaque pointers.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ItemTypeDef
{
    /// <summary>Points to the stable null-terminated UTF-8 custom item type identifier. Keep it valid for the registration lifetime.</summary>
    public byte* Type;

    /// <summary>Stores the native destination category used when items of this type are stored.</summary>
    public ItemHome Home;

    /// <summary>Points to the null-terminated UTF-8 currency identifier associated with this item type, when applicable.</summary>
    public byte* Currency;

    /// <summary>Stores the native ItemTypeChecks bitmask selecting vanilla trinket-style validation checks for this custom type.</summary>
    public uint Joins;

    /// <summary>Points to the null-terminated UTF-8 sound identifier used for this custom item type.</summary>
    public byte* Sound;

    /// <summary>Checks whether the queried native item is usable in the supplied context. Receives the vanilla decision and registered user data, returning a native byte decision.</summary>
    public delegate* unmanaged[Cdecl]<void*, byte, void*, byte> CanUse;

    /// <summary>Writes the queried item&apos;s icon texture descriptor into caller-supplied native storage and returns whether it handled the item.</summary>
    public delegate* unmanaged[Cdecl]<void*, byte*, void*, byte> Icon;

    /// <summary>Writes item icon layers into the native output buffer up to its capacity and returns the layer count. Query and user-data pointers are borrowed.</summary>
    public delegate* unmanaged[Cdecl]<void*, void*, int, void*, int> Compose;

    /// <summary>Composes native tooltip content for an item query and returns whether the callback handled it. All context pointers are borrowed for the callback.</summary>
    public delegate* unmanaged[Cdecl]<void*, void*, void*, byte> Tooltip;

    /// <summary>Composes native description content for an item query and returns whether the callback handled it. All context pointers are borrowed for the callback.</summary>
    public delegate* unmanaged[Cdecl]<void*, void*, void*, byte> Describe;

    /// <summary>Writes native buy and sell prices into caller-supplied integer outputs for the queried item.</summary>
    public delegate* unmanaged[Cdecl]<void*, int*, int*, void*, void> Price;

    /// <summary>Writes a native salvage quote for the queried item and returns whether salvage presentation is available.</summary>
    public delegate* unmanaged[Cdecl]<void*, void*, void*, byte> SalvageQuote;

    /// <summary>Handles the native salvage action for the queried item, with its selected action text and registered user data.</summary>
    public delegate* unmanaged[Cdecl]<void*, byte*, void*, void> Salvage;

    /// <summary>Stores opaque client context passed unchanged to item-type callbacks. Keep it valid for the registration lifetime.</summary>
    public void* UserData;
}
