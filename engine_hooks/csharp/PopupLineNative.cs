using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides fixed UTF-8 output buffers for native popup-description generation. Buffer capacities and the 608-byte structure size must match the native adapter.</summary>
[StructLayout(LayoutKind.Sequential, Size = 608)]
internal unsafe struct PopupLineNative
{
    /// <summary>Stores the native popup wording as inline UTF-8 text within the 192-byte ABI buffer.</summary>
    public fixed byte Wording[192];

    /// <summary>Stores the native popup source as inline UTF-8 text within the 96-byte ABI buffer.</summary>
    public fixed byte Source[96];

    /// <summary>Stores the native popup detail as inline UTF-8 text within the 320-byte ABI buffer.</summary>
    public fixed byte Detail[320];
}
