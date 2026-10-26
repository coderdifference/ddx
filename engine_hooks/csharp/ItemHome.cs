using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Selects the native destination category for a registered custom item type. Numeric values must match the native ItemHome enum.</summary>
public enum ItemHome
{
    /// <summary>Selects none for ItemHome.</summary>
    None,

    /// <summary>Selects stash for ItemHome.</summary>
    Stash,

    /// <summary>Selects currency for ItemHome.</summary>
    Currency,
}
