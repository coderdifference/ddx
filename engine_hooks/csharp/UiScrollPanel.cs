using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors persistent native scrolling-panel state and layout information. Its explicit size and fixed key buffer must match the native ABI.</summary>
[StructLayout(LayoutKind.Sequential, Size = 120)]
public unsafe struct UiScrollPanel
{
    /// <summary>Stores persistent native scrollbar position and dragging state.</summary>
    public UiScroll Scroll;

    /// <summary>Stores native scroll-step size, shown extent, and total content extent in ABI order.</summary>
    public float Step,
        Shown,
        Content;

    /// <summary>Stores native scroll timing state used while processing input and animation.</summary>
    public double Time;

    /// <summary>Stores the borrowed native layout handle used to render this scroll panel.</summary>
    public nint Layout;

    /// <summary>Stores the inline null-terminated UTF-8 layout key. Keep text within the 48-byte ABI capacity.</summary>
    public fixed byte Key[48];

    /// <summary>Stores the native scroll-panel behavior mode identifier.</summary>
    public int Type;

    /// <summary>Stores the panel&apos;s position and viewport dimensions in native UI coordinates.</summary>
    public Vec2 Pos,
        Size;

    /// <summary>Stores the native scrollbar or panel width used by scrolling layout.</summary>
    public float Width;

    /// <summary>Stores native byte booleans controlling scrollbar visibility and panel open state.</summary>
    public byte Bar,
        Open;
}
