using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Shared hero/monster behavior, independent of either platform's game layout.</summary>
public abstract class Actor : GameObject
{
    /// <summary>Creates a Actor using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected Actor(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Gets the actor&apos;s current native display name, copied into a managed string.</summary>
    public string Name => Api.Objects.Text("game_actor_name", Handle);

    /// <summary>Gets the actor&apos;s native party rank through the shared game-object adapter.</summary>
    public uint Rank => unchecked((uint)Api.Objects.Int("game_actor_rank", Handle));

    /// <summary>Gets or sets the actor&apos;s native stress value through DDX. Writing this value directly does not perform a stress-healing effect.</summary>
    public float Stress
    {
        get => Api.Objects.Float("game_actor_stress", Handle);
        set => Api.Objects.SetFloat("game_actor_set_stress", Handle, value);
    }

    /// <summary>Reads the selected native resistance statistic for this actor.</summary>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    public float Resistance(ResistanceType type) => Api.Objects.Resistance(Handle, (int)type);

    /// <summary>Removes the actor&apos;s current affliction through the native game adapter.</summary>
    public void RemoveAffliction() => Api.Objects.RemoveAffliction(Handle);

    /// <summary>Removes the actor&apos;s current virtue through the native game adapter.</summary>
    public void RemoveVirtue() => Api.Objects.RemoveVirtue(Handle);

    /// <summary>Requests native health healing with a battle and source context so the game can resolve its healing behavior and feedback.</summary>
    /// <param name="amount">Signed quantity or effect magnitude in the consuming native operation&apos;s units.</param>
    /// <param name="source">Source identifier used to attribute the event or select its configured rule.</param>
    /// <param name="battle">The battle used by this actor operation.</param>
    /// <param name="sourceId">The source id used by this actor operation. Defaults to &quot;hero_skill&quot;.</param>
    public bool Heal(float amount, Actor? source, Battle battle, string sourceId = "hero_skill") =>
        Api.Objects.Heal(Handle, battle.Handle, amount, source?.Handle ?? 0, sourceId);

    /// <summary>Requests native stress healing with a battle and source context so the game can resolve its healing behavior and feedback.</summary>
    /// <param name="amount">Signed quantity or effect magnitude in the consuming native operation&apos;s units.</param>
    /// <param name="source">Source identifier used to attribute the event or select its configured rule.</param>
    /// <param name="battle">The battle used by this actor operation.</param>
    /// <param name="sourceId">The source id used by this actor operation.</param>
    public bool HealStress(float amount, Actor? source, Battle battle, string sourceId) =>
        Api.Objects.Heal(
            Handle,
            battle.Handle,
            amount,
            source?.Handle ?? 0,
            sourceId,
            stress: true
        );

    /// <summary>Gets whether the native actor currently represents a corpse.</summary>
    public bool IsCorpse => Api.Objects.Bool("game_actor_is_corpse", Handle);

    /// <summary>Attaches the identified native equipment buff with the specified source identifier. Returns the native adapter&apos;s success result.</summary>
    /// <param name="buff">The buff used by this actor operation.</param>
    /// <param name="source">Source identifier used to attribute the event or select its configured rule.</param>
    public bool AddEquipmentBuff(uint buff, uint source) =>
        Api.Objects.AddEquipmentBuff(Handle, buff, source);
}
