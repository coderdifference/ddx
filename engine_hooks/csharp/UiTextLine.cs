using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors a native tooltip text line with font, colour, and borrowed UTF-8 text. Preserve layout and keep referenced data valid during native consumption.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct UiTextLine
{
    /// <summary>Stores the borrowed native font handle used to render this line.</summary>
    public nint Font;

    /// <summary>Stores the native packed text colour for this line.</summary>
    public ulong Colour;

    /// <summary>Points to borrowed null-terminated UTF-8 text rendered by this line.</summary>
    public byte* Text;
}
