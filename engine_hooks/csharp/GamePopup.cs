using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Identifies native combat popup styles used for effect feedback. Explicit numeric values must match the native game adapter.</summary>
public enum GamePopup
{
    /// <summary>Selects health heal for GamePopup.</summary>
    HealthHeal = 2,

    /// <summary>Selects cleave for GamePopup.</summary>
    Cleave = 6,

    /// <summary>Selects buff for GamePopup.</summary>
    Buff = 21,

    /// <summary>Selects stress heal for GamePopup.</summary>
    StressHeal = 33,
}
