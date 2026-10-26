using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides opaque storage for a native panel-box object. Its 264-byte capacity is ABI-defined; use DDX helpers to initialize and operate on it.</summary>
[StructLayout(LayoutKind.Sequential, Size = 264)]
public unsafe struct UiPanelBox
{
    /// <summary>Stores the opaque 264-byte native panel object. Initialize and mutate through DDX UI operations rather than interpreting the bytes in clients.</summary>
    public fixed byte Storage[264];
}
