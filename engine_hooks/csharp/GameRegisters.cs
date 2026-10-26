using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors the CPU register snapshot passed to low-level native hook callbacks. Field order and widths are ABI-defined; prefer semantic game-object adapters in client code.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct GameRegisters
{
    /// <summary>Stores the ABI-ordered integer register snapshot for a low-level native site callback. Valid only within that callback.</summary>
    public ulong Rax,
        Rcx,
        Rdx,
        Rbx,
        Rbp,
        Rsi,
        Rdi,
        R8,
        R9,
        R10,
        R11,
        R12,
        R13,
        R14,
        R15;
}
