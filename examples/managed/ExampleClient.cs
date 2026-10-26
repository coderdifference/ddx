using DDX.Managed;

namespace DDXManagedExample;

/// <summary>A minimal client that logs each hero's battle-start notification.</summary>
public sealed unsafe class ExampleClient : DDXClient
{
    /// <summary>Uses a separate log in the game's working directory.</summary>
    protected override string LogName => "ddx_managed_example";

    /// <summary>Registers before DDX installs the requested game hooks.</summary>
    protected override void OnInitialize()
    {
        if (!RegisterHook("battle_start", HookPhase.After))
            throw new InvalidOperationException("battle_start registration failed");
    }

    /// <summary>Observes the single hook registered by this example.</summary>
    protected override void OnHook(HookContext* context)
    {
        Api.Log("Battle started: hero callback received.");
    }
}
