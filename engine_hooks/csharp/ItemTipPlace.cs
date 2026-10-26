using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors native tooltip placement coordinates for slot, game, mouse, and screen bounds. Values use the coordinate space provided by the tooltip layout callback.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct ItemTipPlace
{
    /// <summary>Stores slot position, default game placement, mouse position, and screen bounds in the ABI-defined order for tooltip placement decisions.</summary>
    public Vec2 Slot,
        Game,
        Mouse,
        ScreenMin,
        ScreenMax;
}
