using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides a borrowed view of native buff identity and source metadata. The native actor owns the buff lifetime.</summary>
public class Buff : GameObject
{
    /// <summary>Creates a Buff using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Buff(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Gets the native category describing how this buff was attached to its actor.</summary>
    public BuffSource Source => (BuffSource)Api.Objects.Int("game_buff_source", Handle);

    /// <summary>Gets the native source identifier that associates this buff with equipment or another origin.</summary>
    public uint SourceId => unchecked((uint)Api.Objects.Int("game_buff_source_id", Handle));

    /// <summary>Gets the buff&apos;s native identifier copied into a managed string.</summary>
    public string Id => Api.Objects.Text("game_buff_id", Handle);
}
