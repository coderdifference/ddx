using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Defines a native stash tab and its filtering behavior. Keep registered strings, callbacks, and user data valid while the native stash uses the tab.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct StashTabDef
{
    /// <summary>Native id pointer in the StashTabDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public byte* Id;

    /// <summary>Native icon pointer in the StashTabDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public byte* Icon;

    /// <summary>Native tooltip pointer in the StashTabDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public byte* Tooltip;

    /// <summary>Native instruction pointer in the StashTabDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public byte* Instruction;

    /// <summary>Tests whether a borrowed native item belongs to this stash tab, receiving the registered opaque user data.</summary>
    public delegate* unmanaged[Cdecl]<nint, void*, byte> Has;

    /// <summary>Stores up to four native sort-option definitions in their displayed order.</summary>
    public StashSortDef Sort0,
        Sort1,
        Sort2,
        Sort3;

    /// <summary>Native user data pointer in the StashTabDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public void* UserData;

    /// <summary>Stores this tab&apos;s native unequip-control and confirmation configuration.</summary>
    public StashUnequipDef Unequip;
}
