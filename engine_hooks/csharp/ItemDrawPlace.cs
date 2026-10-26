using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Identifies the native UI context in which an item icon is being drawn. Used by composition callbacks to choose appropriate layers.</summary>
public enum ItemDrawPlace
{
    /// <summary>Selects grid for ItemDrawPlace.</summary>
    Grid,

    /// <summary>Selects drag for ItemDrawPlace.</summary>
    Drag,

    /// <summary>Selects slot for ItemDrawPlace.</summary>
    Slot,

    /// <summary>Selects other for ItemDrawPlace.</summary>
    Other,
}
