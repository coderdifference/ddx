using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors the portable native equipment stat row. Preserve field order and numeric types because DDX reads and writes this sequential ABI structure directly.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct EquipmentValues
{
    /// <summary>Stores the weapon accuracy modifier in the native equipment row&apos;s fractional representation.</summary>
    public float Accuracy;

    /// <summary>Stores the inclusive minimum and maximum base weapon damage values.</summary>
    public int DamageMin,
        DamageMax;

    /// <summary>Stores the remaining native equipment stats in ABI order: critical chance, speed, dodge, protection, and maximum health. Fractional stats retain the native row&apos;s units.</summary>
    public float Critical,
        Speed,
        Dodge,
        Protection,
        HitPoints;
}
