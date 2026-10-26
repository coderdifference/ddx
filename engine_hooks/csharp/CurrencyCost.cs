using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors a native transaction cost as a currency identifier hash and signed amount. Preserve sequential layout; native payment checks require distinct currency hashes and nonnegative costs.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct CurrencyCost
{
    /// <summary>Stores the native identifier hash of the currency to charge.</summary>
    public uint Currency;

    /// <summary>Stores the signed currency quantity. EstatePay requires a nonnegative charge.</summary>
    public int Amount;
}
