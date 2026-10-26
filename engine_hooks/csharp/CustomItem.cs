using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides a borrowed view of an item registered by a DDX client. Clients may derive from this view to expose their registered item fields.</summary>
public class CustomItem : Item
{
    /// <summary>Creates a CustomItem using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal CustomItem(DDXApi api, nint handle)
        : base(api, handle) { }
}
