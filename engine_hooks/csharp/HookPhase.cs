namespace DDX.Managed;

/// <summary>Selects whether a hook callback runs before or after the native operation. Numeric values must match the DDX hook ABI.</summary>
public enum HookPhase
{
    /// <summary>Selects before for HookPhase.</summary>
    Before = 0,

    /// <summary>Selects after for HookPhase.</summary>
    After = 1,
}
