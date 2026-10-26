using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Configures the native stash unequip control and its localized confirmation text. Preserve sequential layout and keep referenced UTF-8 strings valid while the stash uses them.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct StashUnequipDef
{
    /// <summary>Stores the native byte flag that disables this tab&apos;s unequip control.</summary>
    public byte Off;

    /// <summary>Native icon pointer in the StashUnequipDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public byte* Icon;

    /// <summary>Native tooltip pointer in the StashUnequipDef ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
    public byte* Tooltip;

    /// <summary>Points to the null-terminated UTF-8 confirmation question identifier for unequipping items.</summary>
    public byte* Question;

    /// <summary>Points to the null-terminated UTF-8 label identifier for accepting unequip confirmation.</summary>
    public byte* Yes;

    /// <summary>Points to the null-terminated UTF-8 label identifier for declining unequip confirmation.</summary>
    public byte* No;
}
