using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Defines one native stash sort option with an icon, tooltip, and key-building callback. Keep referenced UTF-8 strings and callback code valid for the registration lifetime.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct StashSortDef
{
    /// <summary>Native icon pointer in the StashSortDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public byte* Icon;

    /// <summary>Native tooltip pointer in the StashSortDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public byte* Tooltip;

    /// <summary>Builds the item&apos;s native sort key into caller-supplied StashKey storage. The item handle and user data are borrowed.</summary>
    public delegate* unmanaged[Cdecl]<nint, StashKey*, void*, void> Key;
}
