using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors the borrowed item-tooltip context, including equipped items and vanilla text lines. Fixed buffer capacities and field order must match the native ABI.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ItemContentQuery
{
    /// <summary>Stores borrowed native item and hero handles for the current tooltip callback.</summary>
    public nint Item,
        Hero;

    /// <summary>Stores the native byte flag indicating that the hero context was inferred by the tooltip system.</summary>
    public byte HeroImplied;

    /// <summary>Stores up to eight borrowed native equipped-item handles in the fixed ABI buffer.</summary>
    public fixed long Equipped[8];

    /// <summary>Stores the number of valid handles in Equipped, bounded by the eight-entry capacity.</summary>
    public int EquippedCount;

    /// <summary>Stores the available native tooltip content width.</summary>
    public float Width;

    /// <summary>Points to borrowed vanilla tooltip lines supplied by the game for this callback.</summary>
    public UiTextLine* Game;

    /// <summary>Stores the number of vanilla lines accessible through Game.</summary>
    public int GameCount;
}
