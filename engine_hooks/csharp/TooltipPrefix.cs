using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>A scoped prefix of an existing tooltip; DDX preserves its original lines and native string ownership.</summary>
public sealed unsafe class TooltipPrefix : IDisposable
{
    private readonly DDXApi api;
    private nint handle;

    /// <summary>Creates a TooltipPrefix using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="tooltip">Borrowed native tooltip address. The native owner must remain alive throughout the call.</param>
    /// <param name="reserve">Initial capacity reserved by the native tooltip prefix scope.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_tooltip_prefix_begin</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal TooltipPrefix(DDXApi api, nint tooltip, uint reserve)
    {
        this.api = api;
        handle = (
            (delegate* unmanaged[Cdecl]<nint, uint, nint>)
                api.NativeExport("ui_tooltip_prefix_begin")
        )(tooltip, reserve);
    }

    /// <summary>Gets whether native tooltip-prefix creation returned a usable scope handle.</summary>
    public bool Available => handle != 0;

    /// <summary>Appends managed text to this temporary tooltip prefix with an optional packed colour. Returns a borrowed native line pointer owned by the tooltip scope.</summary>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <param name="colour">Native text or drawing colour value expected by the receiving UI operation. Defaults to null.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_tooltip_prefix_line</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public byte* Add(string value, ulong? colour = null)
    {
        byte* text = DDXClient.Utf8(value);
        try
        {
            return (
                (delegate* unmanaged[Cdecl]<nint, byte*, ulong, byte, byte*>)
                    api.NativeExport("ui_tooltip_prefix_line")
            )(handle, text, colour ?? 0, (byte)(colour.HasValue ? 1 : 0));
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Ensures that the native tooltip prefix reserves at least the requested width and returns the resulting width.</summary>
    /// <param name="width">Available or requested width in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_tooltip_prefix_width</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float EnsureWidth(float width) =>
        (
            (delegate* unmanaged[Cdecl]<nint, float, float>)
                api.NativeExport("ui_tooltip_prefix_width")
        )(handle, width);

    /// <summary>Releases this object&apos;s owned native resources and restores any temporary native state. Dispose scopes in reverse creation order on the game thread.</summary>
    /// <remarks>Uses the DDX native contract: <c>ui_tooltip_prefix_end</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Dispose()
    {
        if (handle == 0)
            return;
        ((delegate* unmanaged[Cdecl]<nint, void>)api.NativeExport("ui_tooltip_prefix_end"))(handle);
        handle = 0;
    }
}
