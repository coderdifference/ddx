using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Selects the alignment point used when positioning native UI content. Values follow the native layout anchor ordering.</summary>
public enum UiAnchor
{
    /// <summary>Selects top left for UiAnchor.</summary>
    TopLeft,

    /// <summary>Selects top for UiAnchor.</summary>
    Top,

    /// <summary>Selects top right for UiAnchor.</summary>
    TopRight,

    /// <summary>Selects left for UiAnchor.</summary>
    Left,

    /// <summary>Selects centre for UiAnchor.</summary>
    Centre,

    /// <summary>Selects right for UiAnchor.</summary>
    Right,

    /// <summary>Selects bottom left for UiAnchor.</summary>
    BottomLeft,

    /// <summary>Selects bottom for UiAnchor.</summary>
    Bottom,

    /// <summary>Selects bottom right for UiAnchor.</summary>
    BottomRight,
}
