using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides a borrowed view of native skill-effect metadata used by managed combat integrations. The native skill owns the effect lifetime.</summary>
public class Effect : GameObject
{
    /// <summary>Creates a Effect using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Effect(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Gets the stress-heal amount configured on this native skill effect.</summary>
    public float StressHeal => Api.Objects.Float("game_effect_stress_heal", Handle);

    /// <summary>Gets whether the native adapter classifies this effect as damage over time.</summary>
    public bool IsDamageOverTime => Api.Objects.Bool("game_effect_is_dot", Handle);
}
