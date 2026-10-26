using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors a native UI rectangle represented by position and size vectors. Preserve sequential layout for native drawing and hit-testing calls.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct UiRect
{
    /// <summary>Stores the rectangle&apos;s position and dimensions in native UI coordinates.</summary>
    public Vec2 Pos,
        Size;
}
