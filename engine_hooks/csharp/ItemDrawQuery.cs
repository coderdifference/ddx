using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors the borrowed context passed to a native item composition callback. Pointers and handles are valid only for the current callback; preserve sequential field layout.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ItemDrawQuery
{
    /// <summary>Stores borrowed native item and hero handles for the current icon composition callback.</summary>
    public nint Item,
        Hero;

    /// <summary>Stores whether the icon is drawn in a grid, drag, equipment slot, or other native UI context.</summary>
    public ItemDrawPlace Place;

    /// <summary>Points to the borrowed native base texture descriptor for the current item drawing callback.</summary>
    public byte* Base;

    /// <summary>Stores the requested icon dimensions in native UI coordinates.</summary>
    public Vec2 Size;

    /// <summary>Stores the native scale factor applied to the composed item icon.</summary>
    public float Scale;
}
