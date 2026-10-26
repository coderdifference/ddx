using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Identifies a vanilla weapon or armour equipment row for the shared DDX object API. Values must match the native equipment adapter.</summary>
public enum EquipmentSlot
{
    /// <summary>Selects weapon for EquipmentSlot.</summary>
    Weapon,

    /// <summary>Selects armour for EquipmentSlot.</summary>
    Armour,
}
