using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors a temporary native effect-field override as a byte offset and floating-point value. The offset must come from the matching native adapter; preserve sequential field layout.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct EffectPatch
{
    /// <summary>Stores the byte offset of the float field in the native effect object. Resolve this through the matching adapter rather than embedding platform offsets in clients.</summary>
    public uint Offset;

    /// <summary>Stores the temporary floating-point value to apply at Offset during effect resolution.</summary>
    public float Value;
}
