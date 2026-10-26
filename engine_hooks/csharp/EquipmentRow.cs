using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>An owned row copy. Apply temporarily overrides its hero's vanilla row; Dispose always restores it.</summary>
public sealed unsafe class EquipmentRow : IDisposable
{
    private readonly DDXApi api;
    private nint handle;

    /// <summary>Creates a EquipmentRow using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    internal EquipmentRow(DDXApi api, nint handle)
    {
        this.api = api;
        this.handle = handle;
    }

    /// <summary>Gets or sets the stats in this owned row copy. Setting values changes the copy; Apply installs it temporarily on the owning hero.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_equipment_values</c>, <c>game_equipment_set_values</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public EquipmentValues Values
    {
        get
        {
            EquipmentValues v = default;
            (
                (delegate* unmanaged[Cdecl]<nint, EquipmentValues*, void>)
                    api.NativeExport("game_equipment_values")
            )(handle, &v);
            return v;
        }
        set
        {
            (
                (delegate* unmanaged[Cdecl]<nint, EquipmentValues*, void>)
                    api.NativeExport("game_equipment_set_values")
            )(handle, &value);
        }
    }

    /// <summary>Temporarily installs this row copy on its hero. Throws after disposal; dispose the row on the game thread to restore the original row and level.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_equipment_apply</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Apply()
    {
        ObjectDisposedException.ThrowIf(handle == 0, this);
        ((delegate* unmanaged[Cdecl]<nint, void>)api.NativeExport("game_equipment_apply"))(handle);
    }

    /// <summary>Restores the hero&apos;s original row and level, releases the native row copy, and marks this wrapper disposed. Repeated disposal is harmless; nested overrides must be disposed in reverse order.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_equipment_release</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Dispose()
    {
        if (handle == 0)
            return;
        ((delegate* unmanaged[Cdecl]<nint, void>)api.NativeExport("game_equipment_release"))(
            handle
        );
        handle = 0;
    }

    /// <summary>Copies the native equipment description into managed text lines using the requested colour. The returned strings remain valid after the native callback completes.</summary>
    /// <param name="colour">Native text or drawing colour value expected by the receiving UI operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_equipment_description</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public IReadOnlyList<EquipmentText> Describe(ulong colour)
    {
        var lines = new List<EquipmentText>();
        var root = GCHandle.Alloc(lines);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<
                    nint,
                    ulong,
                    delegate* unmanaged[Cdecl]<byte*, ulong, void*, void>,
                    void*,
                    int>)
                    api.NativeExport("game_equipment_description")
            )(handle, colour, &Line, (void*)GCHandle.ToIntPtr(root));
        }
        finally
        {
            root.Free();
        }

        return lines;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Line(byte* text, ulong colour, void* ud)
    {
        try
        {
            ((List<EquipmentText>)GCHandle.FromIntPtr((nint)ud).Target!).Add(
                new(Marshal.PtrToStringUTF8((nint)text) ?? "", colour)
            );
        }
        catch { }
    }
}
