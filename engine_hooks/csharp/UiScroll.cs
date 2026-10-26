using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors native scrollbar position and drag state, including ABI padding. Embed in native scrolling structures without changing field order or widths.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct UiScroll
{
    /// <summary>Stores the current native scroll position in the scroll operation&apos;s coordinate units.</summary>
    public float Pos;

    /// <summary>Stores the native byte boolean indicating an active scrollbar drag.</summary>
    public byte Dragging;
    private byte _pad0,
        _pad1,
        _pad2;
}
