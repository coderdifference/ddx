using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Flags the actor operations supported by the running native DDX adapter. Check support before enabling optional combat integrations.</summary>
[Flags]
public enum ActorCapabilities
{
    /// <summary>Selects health healing for ActorCapabilities.</summary>
    HealthHealing = 1,

    /// <summary>Selects stress healing for ActorCapabilities.</summary>
    StressHealing = 2,

    /// <summary>Selects resistance for ActorCapabilities.</summary>
    Resistance = 4,

    /// <summary>Selects raid for ActorCapabilities.</summary>
    Raid = 8,

    /// <summary>Selects equipment buffs for ActorCapabilities.</summary>
    EquipmentBuffs = 16,
}
