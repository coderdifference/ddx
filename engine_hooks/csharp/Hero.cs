using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Provides a borrowed, typed view of a native hero and its roster, resolve, quirk, and equipment data. Reacquire by GUID after lifecycle events; this object does not own the hero.</summary>
public class Hero : Actor
{
    /// <summary>Creates a Hero using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    protected internal Hero(DDXApi api, nint handle)
        : base(api, handle) { }

    /// <summary>Gets the persistent native hero GUID, or zero for a null view. Use the GUID to reacquire a hero after lifecycle changes.</summary>
    public uint Guid => Exists ? Api.HeroGuid(Handle) : 0;

    /// <summary>Gets the hero class identifier hash, or zero for a null view.</summary>
    public uint ClassHash => Exists ? Api.HeroClassHash(Handle) : 0;

    /// <summary>Gets the hero&apos;s native resolve value used for progression checks.</summary>
    public float Resolve => Api.Objects.Float("game_hero_resolve", Handle);

    /// <summary>Gets whether this hero&apos;s native state suppresses item drops on death.</summary>
    public bool SuppressesDeathLoot => Api.Objects.Bool("game_hero_suppresses_death_loot", Handle);

    /// <summary>Gets whether this hero is supplied by a town event, which may affect recruitment rules.</summary>
    public bool IsFromTownEvent => Api.Objects.IsTownEventHero(Handle);

    /// <summary>Gets the native indices of the hero&apos;s current negative quirks. Reacquire indices after removing or replacing quirks.</summary>
    public IReadOnlyList<int> NegativeQuirks => Api.Objects.NegativeQuirks(Handle);

    /// <summary>Removes the quirk at the supplied native index. Returns whether the native operation succeeds.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    public bool RemoveQuirk(uint index) => Api.Objects.RemoveQuirk(Handle, index);

    /// <summary>Gets whether the current roster still maps this hero&apos;s GUID to this exact borrowed native handle.</summary>
    public bool IsInRoster => Exists && Api.RosterHero(Guid) == Handle;

    /// <summary>Creates an owned copy of the hero&apos;s vanilla equipment row for the selected slot and level, or null when unavailable. Dispose the returned row on the game thread; applying it temporarily overrides the native row.</summary>
    /// <param name="slot">Weapon or armour slot to read, update, or generate.</param>
    /// <param name="level">The level used by this hero operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_equipment_row</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public unsafe EquipmentRow? Equipment(EquipmentSlot slot, int level)
    {
        nint row = (
            (delegate* unmanaged[Cdecl]<nint, int, int, nint>)Api.NativeExport("game_equipment_row")
        )(Handle, (int)slot, level);
        return row == 0 ? null : new EquipmentRow(Api, row);
    }

    /// <summary>Reads a registered DDX custom hero field by field index, using this hero&apos;s persistent GUID.</summary>
    /// <param name="field">The field used by this hero operation.</param>
    public int GetData(int field) => Api.HeroDataGet(Guid, field);

    /// <summary>Writes a registered DDX custom hero field by field index, using this hero&apos;s persistent GUID.</summary>
    /// <param name="field">The field used by this hero operation.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    public void SetData(int field, int value) => Api.HeroDataSet(Guid, field, value);
}
