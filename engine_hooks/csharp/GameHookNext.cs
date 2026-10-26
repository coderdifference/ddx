using System.Threading;

namespace DDX.Managed;

/// <summary>The current inner wrapper/game function for a registered game-hook chain.</summary>
public readonly unsafe struct GameHookNext
{
    private readonly nint _slot;

    /// <summary>Creates a GameHookNext using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="slot">Borrowed native slot address. The native owner must remain alive throughout the call.</param>
    internal GameHookNext(nint slot) => _slot = slot;

    /// <summary>Gets the current inner function address from the shared hook-chain slot using a volatile read. A default or unregistered chain yields zero; read at call time because other wrappers can update the slot.</summary>
    public nint Address => _slot == 0 ? 0 : Volatile.Read(ref *(nint*)_slot);
}
