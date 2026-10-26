using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides common identifiers, quantities, and registered custom fields for a borrowed native inventory item. It does not own the item or keep it alive across inventory changes.</summary>
public abstract class Item : GameObject
{
    /// <summary>Creates a Item using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected Item(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Gets the saved DDX unique identifier attached to this native item.</summary>
    public int UniqueId => Api.ItemDataId(Handle);

    /// <summary>Gets the native item stack quantity.</summary>
    public int Amount => Api.Objects.Int("game_item_amount", Handle);

    /// <summary>Gets the native item type identifier copied into a managed string.</summary>
    public string Type => Api.Objects.Text("game_item_type", Handle);

    /// <summary>Gets the concrete native item identifier copied into a managed string.</summary>
    public string Id => Api.Objects.Text("game_item_id", Handle);

    /// <summary>Gets the native hash of this item&apos;s type identifier.</summary>
    public uint TypeHash => Api.ItemDataTypeHash(Handle);

    /// <summary>Gets the native hash of this item&apos;s concrete identifier.</summary>
    public uint IdHash => Api.ItemDataItemHash(Handle);

    /// <summary>Reads an integer custom persistence field registered with DDX for this item.</summary>
    /// <param name="field">The field used by this item operation.</param>
    public int GetData(int field) => Api.ItemDataGet(Handle, field);

    /// <summary>Writes an integer custom persistence field registered with DDX and reports whether the update succeeds.</summary>
    /// <param name="field">The field used by this item operation.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    public bool SetData(int field, int value) => Api.ItemDataSet(Handle, field, value);
}
