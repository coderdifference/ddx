using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides opaque storage for a native UI drop target. Its 80-byte capacity is ABI-defined; use DDX helpers to operate on it.</summary>
[StructLayout(LayoutKind.Sequential, Size = 80)]
public unsafe struct UiDropTarget
{
    /// <summary>Stores the opaque 80-byte native drop-target object. Initialize and mutate through DDX UI operations rather than interpreting the bytes in clients.</summary>
    public fixed byte Storage[80];
}
