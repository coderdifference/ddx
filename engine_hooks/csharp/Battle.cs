using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides access to a borrowed native battle, including its game-owned random stream. Using this stream participates in native battle randomness.</summary>
public class Battle : GameObject
{
    /// <summary>Creates a Battle using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Battle(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Draws an integer using the native battle random stream and its bound semantics. This advances game-owned battle randomness.</summary>
    /// <param name="minimum">The minimum used by this battle operation.</param>
    /// <param name="maximum">The maximum used by this battle operation.</param>
    public int Random(int minimum, int maximum) =>
        Api.Objects.RandomInBattle(Handle, minimum, maximum);
}
