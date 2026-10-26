using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>A borrowed game object. Views do not keep native objects alive; reacquire heroes by guid across lifecycle events.</summary>
public abstract class GameObject
{
    /// <summary>Gets the shared DDX service used by derived views to access semantic native operations.</summary>
    protected DDXApi Api { get; }

    /// <summary>Gets the borrowed native address represented by this view. A nonzero address does not keep the native object alive or guarantee it has not been invalidated.</summary>
    public nint Handle { get; }

    /// <summary>Creates a GameObject using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected GameObject(DDXApi api, nint handle)
    {
        Api = api;
        Handle = handle;
    }

    /// <summary>Gets whether this view contains a nonzero native handle. This is a null check rather than a native lifetime or validity probe.</summary>
    public bool Exists => Handle != 0;
}
