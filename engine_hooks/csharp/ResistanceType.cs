using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Identifies a native actor resistance statistic. Explicit numeric values must match the native game adapter.</summary>
public enum ResistanceType
{
    /// <summary>Selects deathblow for ResistanceType.</summary>
    Deathblow = 6,
}
