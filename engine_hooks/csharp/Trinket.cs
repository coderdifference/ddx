using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides a borrowed native trinket view with rarity information. Item metadata and persistence operations are inherited from Item.</summary>
public class Trinket : Item
{
    /// <summary>Creates a Trinket using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Trinket(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Gets the native trinket rarity identifier used by loot and reward conversion rules.</summary>
    public uint Rarity => unchecked((uint)Api.Objects.Int("game_item_rarity", Handle));
}
