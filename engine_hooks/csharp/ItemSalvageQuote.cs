using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors the label, icon, and currency yield returned by a native salvage quote callback. Referenced UTF-8 strings must outlive their use by DDX.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ItemSalvageQuote
{
    /// <summary>Points to the null-terminated UTF-8 label used to present the salvage quote.</summary>
    public byte* Label;

    /// <summary>Points to the null-terminated UTF-8 currency icon identifier used by the salvage quote.</summary>
    public byte* Icon;

    /// <summary>Stores the quoted currency yield for salvaging the item.</summary>
    public int Amount;
}
