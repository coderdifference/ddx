using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors one output layer of a composed item icon. Fixed text buffers and field order must match the native ABI; callers provide the sprite, anchor, and geometry.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ItemLayer
{
    /// <summary>Stores the native byte flag indicating use of the base item layer.</summary>
    public byte Base;

    /// <summary>Stores an inline 16-byte native texture descriptor. Populate through DDX texture helpers and preserve the ABI capacity.</summary>
    public fixed byte Texture[16];

    /// <summary>Stores a borrowed native sprite handle used by this icon layer.</summary>
    public nint Sprite;

    /// <summary>Stores the alignment point used to position this icon layer.</summary>
    public UiAnchor Anchor;

    /// <summary>Stores this icon layer&apos;s positional offset and dimensions in native UI coordinates.</summary>
    public Vec2 Offset,
        Size;
}
