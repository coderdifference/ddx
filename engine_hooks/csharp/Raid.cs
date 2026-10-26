using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides a borrowed view of the current expedition, its actors, battle state, and deferred death loot. Reacquire the view when the raid changes.</summary>
public class Raid : GameObject
{
    /// <summary>Creates a Raid using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Raid(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Queues the supplied item for this raid&apos;s native death-loot handling and reports whether the operation succeeds.</summary>
    /// <param name="item">Item or portable gear record used by the operation; native item views remain borrowed.</param>
    public bool QueueDeathItem(Item item) => Api.Objects.QueueDeathItem(Handle, item.Handle);

    /// <summary>Gets a borrowed view of the battle associated with this raid. Check Exists before using it outside combat.</summary>
    public Battle Battle => Api.Objects.Battle(Api.Objects.Pointer("game_raid_battle", Handle));

    /// <summary>Gets the current native expedition difficulty used for loot and progression rules.</summary>
    public int Difficulty => Api.Objects.Int("game_raid_difficulty", Handle);

    /// <summary>Gets whether this raid currently reports an active battle.</summary>
    public bool InBattle => Api.Objects.Bool("game_raid_in_battle", Handle);

    /// <summary>Enumerates non-null borrowed actor views from the raid&apos;s current native actor list. Avoid retaining views across actor removal or raid changes.</summary>
    public IEnumerable<Actor> Actors
    {
        get
        {
            for (int i = 0; i < Api.Objects.Int("game_raid_actor_count", Handle); i++)
                if (Api.Objects.Actor(Api.Objects.At("game_raid_actor", Handle, i)) is { } actor)
                    yield return actor;
        }
    }
}
