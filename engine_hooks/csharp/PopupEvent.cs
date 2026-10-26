using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors the borrowed native combat popup event, including actor, wording, numeric value, and buff duration. Preserve sequential layout and do not retain its pointers after the callback.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct PopupEvent
{
    /// <summary>Stores the borrowed native actor receiving the popup.</summary>
    public nint Actor;

    /// <summary>Stores the native popup style or event-type identifier.</summary>
    public int Type;

    /// <summary>Points to borrowed null-terminated UTF-8 popup wording for this callback.</summary>
    public byte* Text;

    /// <summary>Stores the unsigned numeric value associated with the popup.</summary>
    public uint Number;

    /// <summary>Stores the native byte boolean indicating named popup content.</summary>
    public byte Named;

    /// <summary>Stores the native duration or round count associated with the popup&apos;s effect.</summary>
    public int Rounds;

    /// <summary>Stores the borrowed native buff address associated with the popup, when present.</summary>
    public nint Buff;

    /// <summary>Stores the native duration mode used to interpret Rounds.</summary>
    public int DurationType;
}
