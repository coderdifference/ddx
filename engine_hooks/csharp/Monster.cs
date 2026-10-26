using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides a borrowed, typed view of a native monster through the shared actor operations. Its handle is valid only while the native monster exists.</summary>
public class Monster : Actor
{
    /// <summary>Creates a Monster using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Monster(DDXApi api, nint handle)
        : base(api, handle) { }
}
