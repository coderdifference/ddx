using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors a generated tooltip content layer with region, alignment, text, and image placement. Fixed buffers and sequential layout are part of the native ABI.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ItemContentLayer
{
    /// <summary>Stores the native content layer kind and tooltip region identifiers in ABI order.</summary>
    public int Kind,
        Region;

    /// <summary>Stores the parent attachment point and the layer&apos;s own alignment point in ABI order.</summary>
    public UiAnchor At,
        Anchor;

    /// <summary>Stores the layer offset in native tooltip coordinates.</summary>
    public Vec2 Offset;

    /// <summary>Stores the first content-line index and number of contiguous lines rendered by this layer.</summary>
    public int First,
        Count;

    /// <summary>Stores the content wrapping width for this tooltip layer.</summary>
    public float Width;

    /// <summary>Stores the native tooltip column assigned to this layer.</summary>
    public int Column;

    /// <summary>Stores the inline 16-byte native texture descriptor used by image layers.</summary>
    public fixed byte Texture[16];

    /// <summary>Stores the layer dimensions in native tooltip coordinates.</summary>
    public Vec2 Size;

    /// <summary>Stores the inline null-terminated UTF-8 label within the 16-byte ABI capacity.</summary>
    public fixed byte Label[16];

    /// <summary>Stores the inline null-terminated UTF-8 font identifier within the 32-byte ABI capacity.</summary>
    public fixed byte Font[32];

    /// <summary>Stores the packed RGBA colour used to render the layer label.</summary>
    public uint Rgba;

    /// <summary>Stores the label&apos;s positional offset relative to this layer.</summary>
    public Vec2 LabelOffset;
}
