using System.Collections;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Factories and semantic operations. Derived clients can construct their own GameObject subclasses.</summary>
/// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
public sealed unsafe class GameObjects(DDXApi api)
{
    /// <summary>Calls a semantic native adapter that returns a borrowed pointer for one object handle. The export must match the declared ABI signature.</summary>
    /// <param name="export">The export used by this game objects operation.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    internal nint Pointer(string export, nint handle) =>
        ((delegate* unmanaged[Cdecl]<nint, nint>)api.NativeExport(export))(handle);

    /// <summary>Calls a semantic native adapter that indexes a collection on the supplied object. Returned pointers remain borrowed from the native collection.</summary>
    /// <param name="export">The export used by this game objects operation.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    internal nint At(string export, nint handle, int index) =>
        ((delegate* unmanaged[Cdecl]<nint, int, nint>)api.NativeExport(export))(handle, index);

    /// <summary>Reads a signed integer from a semantic native object adapter with the declared ABI signature.</summary>
    /// <param name="export">The export used by this game objects operation.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    internal int Int(string export, nint handle) =>
        ((delegate* unmanaged[Cdecl]<nint, int>)api.NativeExport(export))(handle);

    /// <summary>Reads a native byte boolean from a semantic object adapter and converts it to a managed boolean.</summary>
    /// <param name="export">The export used by this game objects operation.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    internal bool Bool(string export, nint handle) =>
        ((delegate* unmanaged[Cdecl]<nint, byte>)api.NativeExport(export))(handle) != 0;

    /// <summary>Reads a floating-point value from a semantic native object adapter.</summary>
    /// <param name="export">The export used by this game objects operation.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    internal float Float(string export, nint handle) =>
        ((delegate* unmanaged[Cdecl]<nint, float>)api.NativeExport(export))(handle);

    /// <summary>Writes a floating-point value through a semantic native object adapter.</summary>
    /// <param name="export">The export used by this game objects operation.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    internal void SetFloat(string export, nint handle, float value) =>
        ((delegate* unmanaged[Cdecl]<nint, float, void>)api.NativeExport(export))(handle, value);

    /// <summary>Copies UTF-8 text returned by a semantic native object adapter into a managed string. Null native text becomes an empty string.</summary>
    /// <param name="export">The export used by this game objects operation.</param>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    internal string Text(string export, nint handle) =>
        Marshal.PtrToStringUTF8(Pointer(export, handle)) ?? "";

    /// <summary>Adds a native equipment buff to the specified actor with its source identifier and reports native success.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="buff">The buff used by this game objects operation.</param>
    /// <param name="source">Source identifier used to attribute the event or select its configured rule.</param>
    /// <remarks>Uses the DDX native contract: <c>game_actor_add_equipment_buff</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal bool AddEquipmentBuff(nint actor, uint buff, uint source) =>
        (
            (delegate* unmanaged[Cdecl]<nint, uint, uint, byte>)
                api.NativeExport("game_actor_add_equipment_buff")
        )(actor, buff, source) != 0;

    /// <summary>Removes an item from a native inventory. Existing borrowed item views may become invalid after this mutation.</summary>
    /// <param name="inventory">Borrowed native inventory address. The native owner must remain alive throughout the call.</param>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_inventory_remove</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal void Remove(nint inventory, nint item) =>
        ((delegate* unmanaged[Cdecl]<nint, nint, void>)api.NativeExport("game_inventory_remove"))(
            inventory,
            item
        );

    /// <summary>Queues an item in the raid&apos;s deferred death-loot handling and reports native success.</summary>
    /// <param name="raid">Borrowed native raid address. The native owner must remain alive throughout the call.</param>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_raid_queue_death_item</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal bool QueueDeathItem(nint raid, nint item) =>
        (
            (delegate* unmanaged[Cdecl]<nint, nint, byte>)
                api.NativeExport("game_raid_queue_death_item")
        )(raid, item) != 0;

    /// <summary>Wraps the native inventory associated with a supply UI object in a borrowed inventory view.</summary>
    /// <param name="supply">Borrowed native supply address. The native owner must remain alive throughout the call.</param>
    public Inventory SupplyInventory(nint supply) =>
        Inventory(Pointer("game_supply_inventory", supply));

    /// <summary>Copies a localized native buff description for the supplied hero context, or returns null when no description is available.</summary>
    /// <param name="hash">The hash used by this game objects operation.</param>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_buff_description</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public string? BuffDescription(uint hash, nint hero)
    {
        byte* text = stackalloc byte[512];
        return
            (
                (delegate* unmanaged[Cdecl]<uint, nint, byte*, nuint, byte>)
                    api.NativeExport("game_buff_description")
            )(hash, hero, text, 512) != 0
            ? Marshal.PtrToStringUTF8((nint)text)
            : null;
    }

    /// <summary>Copies a native buff description, including its terminating zero and binary colour codes.
    /// Encoded colour payloads are not UTF-8; pass these bytes directly to ContentRawLine.</summary>
    public byte[]? BuffDescriptionBytes(uint hash, nint hero)
    {
        byte* text = stackalloc byte[512];
        if (
            (
                (delegate* unmanaged[Cdecl]<uint, nint, byte*, nuint, byte>)
                    api.NativeExport("game_buff_description")
            )(hash, hero, text, 512) == 0
        )
            return null;
        ReadOnlySpan<byte> buffer = new(text, 512);
        int length = buffer.IndexOf((byte)0);
        return length >= 0 ? buffer[..(length + 1)].ToArray() : null;
    }

    /// <summary>Gets the selected inventory tab from the current native raid display.</summary>
    public int RaidTab => Int("game_raid_tab", CurrentRaidDisplay);

    /// <summary>Changes the selected tab on the current native raid display.</summary>
    /// <param name="tab">The tab used by this game objects operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_raid_set_tab</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void SetRaidTab(int tab) =>
        ((delegate* unmanaged[Cdecl]<nint, int, void>)api.NativeExport("game_raid_set_tab"))(
            CurrentRaidDisplay,
            tab
        );

    /// <summary>Tests whether the supplied native raid panel currently allows party reordering.</summary>
    /// <param name="panel">Borrowed native panel address. The native owner must remain alive throughout the call.</param>
    public bool CanReorderParty(nint panel) => Bool("game_raid_reorder_available", panel);

    /// <summary>Requests party reordering through the current native raid display.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_raid_reorder</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void ReorderParty() =>
        ((delegate* unmanaged[Cdecl]<nint, void>)api.NativeExport("game_raid_reorder"))(
            CurrentRaidDisplay
        );

    /// <summary>Tests whether native hotspot parameters identify the party reorder control.</summary>
    /// <param name="parameters">Borrowed native parameters address. The native owner must remain alive throughout the call.</param>
    public bool IsPartyReorderHotspot(nint parameters) =>
        Bool("ui_hotspot_is_party_reorder", parameters);

    /// <summary>Copies up to 64 native negative-quirk indices for the supplied hero, stopping at the native sentinel.</summary>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_hero_negative_quirk</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public IReadOnlyList<int> NegativeQuirks(nint hero)
    {
        var indices = new List<int>();
        for (int i = 0; i < 64; i++)
        {
            int index = (
                (delegate* unmanaged[Cdecl]<nint, int, int>)
                    api.NativeExport("game_hero_negative_quirk")
            )(hero, i);
            if (index < 0)
                break;
            indices.Add(index);
        }

        return indices;
    }

    /// <summary>Registers a predicate that requests complete native item swaps for eligible items. Keep the predicate valid while registered.</summary>
    /// <param name="predicate">The predicate used by this game objects operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_inventory_full_swap</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool RequireFullItemSwap(delegate* unmanaged[Cdecl]<nint, byte> predicate) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, byte>, byte>)
                api.NativeExport("game_inventory_full_swap")
        )(predicate) != 0;

    /// <summary>Finds a borrowed empty stash-cell address in the supplied native realm.</summary>
    /// <param name="realm">Borrowed native realm address. The native owner must remain alive throughout the call.</param>
    public nint EmptyStashCell(nint realm) => Pointer("game_stash_empty_cell", realm);

    /// <summary>Wraps the native inventory belonging to the supplied inventory cell.</summary>
    /// <param name="cell">Borrowed native cell address. The native owner must remain alive throughout the call.</param>
    public Inventory CellInventory(nint cell) =>
        Inventory(Pointer("game_inventory_cell_inventory", cell));

    /// <summary>Wraps the native item in the supplied cell, or returns null for an empty cell.</summary>
    /// <param name="cell">Borrowed native cell address. The native owner must remain alive throughout the call.</param>
    public Item? CellItem(nint cell) => Item(Pointer("game_inventory_cell_item", cell));

    /// <summary>Wraps the borrowed native hero currently held by the supplied hero slot.</summary>
    /// <param name="slot">Borrowed native slot address. The native owner must remain alive throughout the call.</param>
    public Hero HeroInSlot(nint slot) => Hero(Pointer("game_hero_slot_hero", slot));

    /// <summary>Tests whether the native hero originates from a town event.</summary>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    public bool IsTownEventHero(nint hero) => Bool("game_hero_town_event", hero);

    /// <summary>Applies the native trinket price discount calculation to the supplied price.</summary>
    /// <param name="price">The price used by this game objects operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_trinket_discount</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int TrinketDiscount(int price) =>
        ((delegate* unmanaged[Cdecl]<int, int>)api.NativeExport("game_trinket_discount"))(price);

    /// <summary>Wraps the borrowed native hero represented by a recruitment drag object.</summary>
    /// <param name="drag">Borrowed native drag address. The native owner must remain alive throughout the call.</param>
    public Hero RecruitDragHero(nint drag) => Hero(Pointer("game_recruit_drag_hero", drag));

    /// <summary>Gets whether the native hero roster has reached its current capacity.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_roster_is_full</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool RosterFull =>
        ((delegate* unmanaged[Cdecl]<byte>)api.NativeExport("game_roster_is_full"))() != 0;

    /// <summary>Attempts native recruitment with the supplied drag placement, original drop chain, and currency costs. Returns whether the native hire operation succeeds.</summary>
    /// <param name="list">Borrowed native list address. The native owner must remain alive throughout the call.</param>
    /// <param name="position">Drop or drawing position in the consuming native UI coordinate system.</param>
    /// <param name="slot">Borrowed native slot address. The native owner must remain alive throughout the call.</param>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <param name="drop">The drop used by this game objects operation.</param>
    /// <param name="costs">The costs used by this game objects operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_recruit_hire</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool Hire(
        nint list,
        Vec2 position,
        nint slot,
        nint hero,
        GameHookNext drop,
        ReadOnlySpan<CurrencyCost> costs
    )
    {
        fixed (CurrencyCost* c = costs)
            return (
                    (delegate* unmanaged[Cdecl]<
                        nint,
                        Vec2*,
                        nint,
                        nint,
                        nint,
                        CurrencyCost*,
                        int,
                        byte>)
                        api.NativeExport("game_recruit_hire")
                )(list, &position, slot, hero, drop.Address, c, costs.Length) != 0;
    }

    /// <summary>Tests whether the supplied native building panel is showing upgrades.</summary>
    /// <param name="panel">Borrowed native panel address. The native owner must remain alive throughout the call.</param>
    public bool BuildingUpgradesOpen(nint panel) => Bool("game_building_upgrades_open", panel);

    /// <summary>Offsets the native building panel&apos;s hero-slot position by the supplied vector.</summary>
    /// <param name="panel">Borrowed native panel address. The native owner must remain alive throughout the call.</param>
    /// <param name="delta">Change to apply in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>game_building_move_hero_slot</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void MoveBuildingHeroSlot(nint panel, Vec2 delta) =>
        (
            (delegate* unmanaged[Cdecl]<nint, Vec2, void>)
                api.NativeExport("game_building_move_hero_slot")
        )(panel, delta);

    /// <summary>Wraps the borrowed hero captured by a native building interaction closure.</summary>
    /// <param name="closure">Borrowed native closure address. The native owner must remain alive throughout the call.</param>
    public Hero BuildingClosureHero(nint closure) =>
        Hero(Pointer("game_building_closure_hero", closure));

    /// <summary>Tests whether the caller address identifies the supported native hero-action text site.</summary>
    /// <param name="caller">Borrowed native caller address. The native owner must remain alive throughout the call.</param>
    public bool IsHeroActionTextSite(nint caller) => Bool("ui_hero_action_text_site", caller);

    /// <summary>Removes a native inventory slot by its unsigned slot index.</summary>
    /// <param name="inventory">Borrowed native inventory address. The native owner must remain alive throughout the call.</param>
    /// <param name="slot">The slot used by this game objects operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_inventory_remove_slot</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal void RemoveSlot(nint inventory, uint slot) =>
        (
            (delegate* unmanaged[Cdecl]<nint, uint, void>)
                api.NativeExport("game_inventory_remove_slot")
        )(inventory, slot);

    /// <summary>Gets whether native building upgrades are currently available.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_upgrades_available</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool UpgradesAvailable =>
        ((delegate* unmanaged[Cdecl]<byte>)api.NativeExport("game_upgrades_available"))() != 0;

    /// <summary>Requests native roster persistence after managed changes to hero data.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_roster_save</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void SaveRoster() =>
        ((delegate* unmanaged[Cdecl]<void>)api.NativeExport("game_roster_save"))();

    /// <summary>Tests whether the specified native upgrade-tree level has been purchased, optionally for a particular hero GUID.</summary>
    /// <param name="tree">The tree used by this game objects operation.</param>
    /// <param name="level">The level used by this game objects operation.</param>
    /// <param name="guid">Persistent hero identifier used to reacquire the current native hero or its saved fields. Defaults to 0.</param>
    /// <remarks>Uses the DDX native contract: <c>game_upgrade_purchased</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool UpgradePurchased(uint tree, int level, uint guid = 0) =>
        (
            (delegate* unmanaged[Cdecl]<uint, int, uint, byte>)
                api.NativeExport("game_upgrade_purchased")
        )(tree, level, guid) != 0;

    /// <summary>Removes the supplied actor&apos;s current affliction through the semantic native adapter.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_actor_remove_affliction</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void RemoveAffliction(nint actor) =>
        ((delegate* unmanaged[Cdecl]<nint, void>)api.NativeExport("game_actor_remove_affliction"))(
            actor
        );

    /// <summary>Removes the supplied actor&apos;s current virtue through the semantic native adapter.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_actor_remove_virtue</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void RemoveVirtue(nint actor) =>
        ((delegate* unmanaged[Cdecl]<nint, void>)api.NativeExport("game_actor_remove_virtue"))(
            actor
        );

    /// <summary>Removes the native hero quirk at the supplied index and reports whether it succeeds.</summary>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_hero_remove_quirk</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool RemoveQuirk(nint hero, uint index) =>
        ((delegate* unmanaged[Cdecl]<nint, uint, byte>)api.NativeExport("game_hero_remove_quirk"))(
            hero,
            index
        ) != 0;

    /// <summary>Reads the selected native actor resistance value.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_actor_resistance</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float Resistance(nint actor, int type) =>
        ((delegate* unmanaged[Cdecl]<nint, int, float>)api.NativeExport("game_actor_resistance"))(
            actor,
            type
        );

    /// <summary>Reads the native maximum clamp for a resistance type and flag combination.</summary>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="flag">The flag used by this game objects operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_resistance_maximum</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float ResistanceMaximum(int type, bool flag) =>
        ((delegate* unmanaged[Cdecl]<int, byte, float>)api.NativeExport("game_resistance_maximum"))(
            type,
            (byte)(flag ? 1 : 0)
        );

    /// <summary>Updates the native maximum clamp for a resistance type and flag combination. Callers must restore temporary global changes at the appropriate lifecycle boundary.</summary>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="flag">The flag used by this game objects operation.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_resistance_set_maximum</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void SetResistanceMaximum(int type, bool flag, float value) =>
        (
            (delegate* unmanaged[Cdecl]<int, byte, float, void>)
                api.NativeExport("game_resistance_set_maximum")
        )(type, (byte)(flag ? 1 : 0), value);

    /// <summary>Resolves native health or stress healing with explicit target, battle, amount, source actor, and source identifier.</summary>
    /// <param name="target">Borrowed native target address. The native owner must remain alive throughout the call.</param>
    /// <param name="battle">Borrowed native battle address. The native owner must remain alive throughout the call.</param>
    /// <param name="amount">Signed quantity or effect magnitude in the consuming native operation&apos;s units.</param>
    /// <param name="source">Borrowed native source address. The native owner must remain alive throughout the call.</param>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <param name="stress">The stress used by this game objects operation. Defaults to false.</param>
    public bool Heal(
        nint target,
        nint battle,
        float amount,
        nint source,
        string id,
        bool stress = false
    )
    {
        byte* text = DDXClient.Utf8(id);
        try
        {
            return (
                    (delegate* unmanaged[Cdecl]<nint, nint, float, nint, byte*, byte>)
                        api.NativeExport(
                            stress ? "game_actor_stress_heal" : "game_actor_health_heal"
                        )
                )(target, battle, amount, source, text) != 0;
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Gets the bitmask of semantic actor operations supported by the current native adapter.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_actor_capabilities</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public ActorCapabilities Capabilities =>
        (ActorCapabilities)
            ((delegate* unmanaged[Cdecl]<uint>)api.NativeExport("game_actor_capabilities"))();

    /// <summary>Draws an integer from the native battle random stream using the game&apos;s bound semantics.</summary>
    /// <param name="battle">Borrowed native battle address. The native owner must remain alive throughout the call.</param>
    /// <param name="min">The min used by this game objects operation.</param>
    /// <param name="max">Maximum number of entries or native objects the operation may produce.</param>
    /// <remarks>Uses the DDX native contract: <c>game_battle_random_int</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal int RandomInBattle(nint battle, int min, int max) =>
        (
            (delegate* unmanaged[Cdecl]<nint, int, int, int>)
                api.NativeExport("game_battle_random_int")
        )(battle, min, max);

    /// <summary>Creates a borrowed Battle view for the supplied native address. The view does not validate or extend the native object&apos;s lifetime; a zero address produces a view whose Exists property is false.</summary>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    public Battle Battle(nint handle) => new(api, handle);

    /// <summary>Creates a borrowed Hero view for the supplied native address. The view does not validate or extend the native object&apos;s lifetime; a zero address produces a view whose Exists property is false.</summary>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    public Hero Hero(nint handle) => new(api, handle);

    /// <summary>Returns a borrowed hero or monster view according to the native actor type, or null for a zero handle.</summary>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    public Actor? Actor(nint handle) =>
        handle == 0 ? null
        : api.IsHero(handle) ? new Hero(api, handle)
        : new Monster(api, handle);

    /// <summary>Returns a borrowed trinket, provision, or custom-item view according to the native item type, or null for a zero handle.</summary>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    public Item? Item(nint handle) =>
        handle == 0
            ? null
            : Text("game_item_type", handle) switch
            {
                "trinket" => new Trinket(api, handle),
                "supply" or "provision" => new Provision(api, handle),
                _ => new CustomItem(api, handle),
            };

    /// <summary>Creates a borrowed Inventory view for the supplied native address. The view does not validate or extend the native object&apos;s lifetime; a zero address produces a view whose Exists property is false.</summary>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    public Inventory Inventory(nint handle) => new(api, handle);

    /// <summary>Gets a borrowed view of the raid currently exposed by the native adapter. Check Exists before using it when no expedition is active.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_current_raid</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public Raid CurrentRaid =>
        new(api, ((delegate* unmanaged[Cdecl]<nint>)api.NativeExport("game_current_raid"))());

    /// <summary>Creates a borrowed Raid view for the supplied native address. The view does not validate or extend the native object&apos;s lifetime; a zero address produces a view whose Exists property is false.</summary>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    public Raid Raid(nint handle) => new(api, handle);

    /// <summary>Creates a borrowed Buff view for the supplied native address. The view does not validate or extend the native object&apos;s lifetime; a zero address produces a view whose Exists property is false.</summary>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    public Buff Buff(nint handle) => new(api, handle);

    /// <summary>Creates a borrowed Effect view for the supplied native address. The view does not validate or extend the native object&apos;s lifetime; a zero address produces a view whose Exists property is false.</summary>
    /// <param name="handle">Borrowed native object address; the caller must ensure the object remains alive while the view is used.</param>
    public Effect Effect(nint handle) => new(api, handle);

    /// <summary>Gets the borrowed current native raid-display address for UI operations.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_current_raid_display</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint CurrentRaidDisplay =>
        ((delegate* unmanaged[Cdecl]<nint>)api.NativeExport("game_current_raid_display"))();

    /// <summary>Reads the number of targets attached to the supplied native attack.</summary>
    /// <param name="attack">Borrowed native attack address. The native owner must remain alive throughout the call.</param>
    public int AttackTargetCount(nint attack) => Int("game_attack_target_count", attack);

    /// <summary>Gets the borrowed native random-stream address associated with the supplied battle.</summary>
    /// <param name="battle">Borrowed native battle address. The native owner must remain alive throughout the call.</param>
    public nint BattleRandom(nint battle) => Pointer("game_raid_random", battle);

    /// <summary>Wraps the borrowed hero selected in the supplied native party object.</summary>
    /// <param name="party">Borrowed native party address. The native owner must remain alive throughout the call.</param>
    public Hero SelectedHero(nint party) => Hero(Pointer("game_party_selected", party));

    /// <summary>Wraps the borrowed hero represented by a native roster UI element.</summary>
    /// <param name="element">Borrowed native element address. The native owner must remain alive throughout the call.</param>
    public Hero RosterElementHero(nint element) =>
        Hero(Pointer("game_roster_element_hero", element));

    /// <summary>Enumerates borrowed reward inventories from generated native quests until the adapter reports no further inventory.</summary>
    /// <param name="generatedQuests">Borrowed native generated quests address. The native owner must remain alive throughout the call.</param>
    public IEnumerable<Inventory> QuestRewards(nint generatedQuests)
    {
        for (int i = 0; ; i++)
        {
            nint inv = At("game_quest_reward", generatedQuests, i);
            if (inv == 0)
                yield break;
            yield return Inventory(inv);
        }
    }

    /// <summary>Enumerates non-null borrowed effect views from a native skill&apos;s effect list.</summary>
    /// <param name="skill">Borrowed native skill address. The native owner must remain alive throughout the call.</param>
    public IEnumerable<Effect> SkillEffects(nint skill)
    {
        int count = Int("game_skill_effect_count", skill);
        for (int i = 0; i < count; i++)
        {
            nint effect = At("game_skill_effect", skill, i);
            if (effect != 0)
                yield return Effect(effect);
        }
    }

    /// <summary>Creates a portable patch for the native stress-heal field using an offset supplied by the matching native adapter.</summary>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_effect_stress_heal_field</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public EffectPatch StressHealPatch(float value) =>
        new()
        {
            Offset = (uint)
                (
                    (delegate* unmanaged[Cdecl]<uint>)
                        api.NativeExport("game_effect_stress_heal_field")
                )(),
            Value = value,
        };
}
