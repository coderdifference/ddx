using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides a borrowed native supply or provision view. Item metadata and persistence operations are inherited from Item.</summary>
public class Provision : Item
{
    /// <summary>Creates a Provision using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Provision(DDXApi api, nint handle)
        : base(api, handle) { }
}
