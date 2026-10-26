using System.Runtime.InteropServices;

namespace DDX.Managed;

// Mirrors HookContext. Pointers to game objects and strings are borrowed and valid for the callback only.
/// <summary>Mirrors the native hook callback context. Game-object and UTF-8 pointers are borrowed for the callback only; preserve field order, widths, and layout when extending the native ABI.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HookContext
{
    /// <summary>Points to the borrowed null-terminated UTF-8 hook name set by native dispatch.</summary>
    public byte* Point;

    /// <summary>Stores whether this callback runs before or after the native operation.</summary>
    public HookPhase Phase;

    /// <summary>Stores borrowed native event object handles in ABI order. Which handles are populated depends on the hook point; retain none beyond their native lifetime.</summary>
    public nint Raid,
        Battle,
        Area,
        Prop,
        Inventory,
        Actor,
        Attacker,
        Target;

    /// <summary>Stores borrowed native combat context handles in ABI order, valid only during the callback.</summary>
    public nint Skill,
        Item,
        Attack,
        TargetData;

    /// <summary>Stores the event input amount and resulting native change. Before callbacks may modify Amount; after callbacks inspect Result.</summary>
    public float Amount,
        Result;

    /// <summary>Stores the native damage, heal, or stress source kind, or -1 when no kind applies.</summary>
    public int Kind;

    /// <summary>Points to borrowed UTF-8 source, event type, and turn-skip reason identifiers in ABI order. Null values mean that the event has no corresponding identifier.</summary>
    public byte* Source,
        Type,
        Skip;

    /// <summary>Stores the act-out refusal mask. Before act-out-check callbacks may modify it; zero permits the action.</summary>
    public uint Refuse;

    /// <summary>Stores native byte booleans for hit, critical hit, nested synthetic dispatch, and cancellation. Before callbacks may set Cancel to stop the native action.</summary>
    public byte Hit,
        Crit,
        Synthetic,
        Cancel;

    /// <summary>Points to borrowed UTF-8 display text for events such as actor barks.</summary>
    public byte* Text;
}
