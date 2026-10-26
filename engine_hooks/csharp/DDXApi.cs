using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Typed function pointers for the DDX client API. All strings are UTF-8.</summary>
public sealed unsafe class DDXApi
{
    private readonly nint _host;
    private DDXPopups? _popups;

    /// <summary>Gets the lazily created popup-description service that copies native popup metadata into managed text.</summary>
    public DDXPopups Popups => _popups ??= new DDXPopups(this);

    private GameObjects? _objects;

    /// <summary>Gets the lazily created factory and semantic-operation service for borrowed native game-object views.</summary>
    public GameObjects Objects => _objects ??= new GameObjects(this);

    /// <summary>Binds the native DDX host module for the current platform and verifies essential exports immediately. Construction fails if the process is not a compatible DDX host.</summary>
    /// <remarks>Uses the DDX native contract: <c>log_file</c>, <c>hook_on</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal DDXApi()
    {
        // On macOS, the main-program handle resolves globally visible DYLD-inserted DDX exports.
        // On Windows, DDX is a separately loaded DLL, so bind directly to that module.
        _host = OperatingSystem.IsWindows()
            ? NativeLibrary.Load("ddx.dll")
            : NativeLibrary.GetMainProgramHandle();
        _ = Export("log_file"); // fail fast if this isn't a DDX host
        _ = Export("hook_on");
    }

    private nint Export(string name)
    {
        if (!NativeLibrary.TryGetExport(_host, name, out nint address) || address == 0)
            throw new EntryPointNotFoundException($"DDX export not found: {name}");
        return address;
    }

    /// <summary>
    /// Address of any exported DDX function. This gives advanced clients access to the full C API;
    /// callers must use the exact signature and obey the lifetime and thread rules in common.h.
    /// </summary>
    public nint NativeExport(string name) => Export(name);

    /// <summary>Resolve a game symbol by its mangled name, using DDX's cross-platform symbol map.</summary>
    public nint GameSymbol(string mangledName)
    {
        byte* text = DDXClient.Utf8(mangledName);
        nint result = ((delegate* unmanaged[Cdecl]<byte*, nint>)Export("sym"))(text);
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Selects the native client&apos;s log destination using a UTF-8 filename.</summary>
    /// <param name="name">Identifier of the native service, field, or named value being addressed.</param>
    /// <remarks>Uses the DDX native contract: <c>log_file</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void LogFile(string name)
    {
        byte* text = DDXClient.Utf8(name);
        ((delegate* unmanaged[Cdecl]<byte*, void>)Export("log_file"))(text);
        NativeMemory.Free(text);
    }

    // logf_ is variadic in C; this fixed two-argument call uses its supported "%s" form.
    /// <summary>Writes a managed message through the native log service using its fixed percent-s format.</summary>
    /// <param name="message">The message used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>logf_</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Log(string message)
    {
        byte* fmt = DDXClient.Utf8("%s");
        byte* arg = DDXClient.Utf8(message);
        ((delegate* unmanaged[Cdecl]<byte*, byte*, void>)Export("logf_"))(fmt, arg);
        NativeMemory.Free(arg);
        NativeMemory.Free(fmt);
    }

    /// <summary>Returns the number of hook points advertised by the native DDX registry.</summary>
    /// <remarks>Uses the DDX native contract: <c>hook_point_count</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int HookPointCount() => ((delegate* unmanaged[Cdecl]<int>)Export("hook_point_count"))();

    /// <summary>Copies the registered hook point&apos;s name, or returns null when the native registry supplies no name.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>hook_point_name</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public string? HookPointName(int index)
    {
        byte* value = ((delegate* unmanaged[Cdecl]<int, byte*>)Export("hook_point_name"))(index);
        return value == null ? null : Marshal.PtrToStringUTF8((nint)value);
    }

    /// <summary>Copies the registered hook point&apos;s descriptive text, or returns null when unavailable.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>hook_point_about</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public string? HookPointAbout(int index)
    {
        byte* value = ((delegate* unmanaged[Cdecl]<int, byte*>)Export("hook_point_about"))(index);
        return value == null ? null : Marshal.PtrToStringUTF8((nint)value);
    }

    /// <summary>Tests whether the indexed native hook point supports the requested before or after phase.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    /// <param name="phase">The phase used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>hook_point_has_phase</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool HookPointHasPhase(int index, HookPhase phase) =>
        ((delegate* unmanaged[Cdecl]<int, int, byte>)Export("hook_point_has_phase"))(
            index,
            (int)phase
        ) != 0;

    /// <summary>Tests whether the native registry contains the specified hook point name.</summary>
    /// <param name="name">Identifier of the native service, field, or named value being addressed.</param>
    /// <remarks>Uses the DDX native contract: <c>hook_point_exists</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool HookPointExists(string name)
    {
        byte* text = DDXClient.Utf8(name);
        bool result =
            ((delegate* unmanaged[Cdecl]<byte*, byte>)Export("hook_point_exists"))(text) != 0;
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Returns the number of mods known to the native mod registry.</summary>
    /// <remarks>Uses the DDX native contract: <c>mods_count</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int ModsCount() => ((delegate* unmanaged[Cdecl]<int>)Export("mods_count"))();

    /// <summary>Tests whether the native registry currently represents an active campaign mod list.</summary>
    /// <remarks>Uses the DDX native contract: <c>mods_campaign</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ModsCampaign() =>
        ((delegate* unmanaged[Cdecl]<byte>)Export("mods_campaign"))() != 0;

    /// <summary>Gets the campaign position of a mod; zero has highest priority, -1 is inactive.</summary>
    public int ModsPosition(int mod) =>
        ((delegate* unmanaged[Cdecl]<int, int>)Export("mods_pos"))(mod);

    /// <summary>Tests whether the indexed mod is applied to the current campaign.</summary>
    /// <param name="mod">The mod used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>mods_active</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ModsActive(int mod) =>
        ((delegate* unmanaged[Cdecl]<int, byte>)Export("mods_active"))(mod) != 0;

    /// <summary>Folder containing the mod at this index; the returned UTF-8 pointer is owned by DDX.</summary>
    public string? ModsFolder(int mod)
    {
        byte* value = ((delegate* unmanaged[Cdecl]<int, byte*>)Export("mods_folder"))(mod);
        return value == null ? null : Marshal.PtrToStringUTF8((nint)value);
    }

    /// <summary>Engine directory for a mod; the returned UTF-8 pointer is owned by DDX.</summary>
    public string? ModsEngineDirectory(int mod)
    {
        byte* value = ((delegate* unmanaged[Cdecl]<int, byte*>)Export("mods_engine_dir"))(mod);
        return value == null ? null : Marshal.PtrToStringUTF8((nint)value);
    }

    /// <summary>Returns the native mod-list generation counter for invalidating campaign-dependent caches.</summary>
    public uint ModsGeneration() => ((delegate* unmanaged[Cdecl]<uint>)Export("mods_generation"))();

    /// <summary>Registers a process-lifetime callback when the active campaign mod list changes.</summary>
    internal void ModsOnCampaign(delegate* unmanaged[Cdecl]<void> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<void>, void>)Export(
                "mods_on_campaign"
            )
        )(callback);

    /// <summary>Registers a callback after custom hero fields have been restored from saved data.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>herodata_on_restore</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal void HeroDataOnRestore(delegate* unmanaged[Cdecl]<nint, void> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, void>, void>)Export(
                "herodata_on_restore"
            )
        )(callback);

    /// <summary>Registers a callback immediately before custom item fields are serialized.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>itemdata_on_save</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal void ItemDataOnSave(delegate* unmanaged[Cdecl]<nint, void> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, void>, void>)Export(
                "itemdata_on_save"
            )
        )(callback);

    /// <summary>Registers callbacks bracketing the native hero derived-stat calculation.</summary>
    /// <param name="before">The before used by this ddx api operation.</param>
    /// <param name="after">The after used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>stats_on_calc</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal void StatsOnCalc(
        delegate* unmanaged[Cdecl]<nint, void> before,
        delegate* unmanaged[Cdecl]<nint, void> after
    ) =>
        (
            (delegate* unmanaged[Cdecl]<
                delegate* unmanaged[Cdecl]<nint, void>,
                delegate* unmanaged[Cdecl]<nint, void>,
                void>)Export("stats_on_calc")
        )(before, after);

    /// <summary>Registers a provider for native buff source descriptions. Returned text must remain valid for native consumption.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>buff_origin_on</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal void BuffOriginOn(delegate* unmanaged[Cdecl]<nint, nint, byte*> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, nint, byte*>, void>)Export(
                "buff_origin_on"
            )
        )(callback);

    /// <summary>Registers a provider for native stat labels. Returned UTF-8 labels must remain valid for native consumption.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>stat_label_on</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal void StatLabelOn(delegate* unmanaged[Cdecl]<nint, byte*, byte*> callback) =>
        (
            (delegate* unmanaged[Cdecl]<
                delegate* unmanaged[Cdecl]<nint, byte*, byte*>,
                void>)Export("stat_label_on")
        )(callback);

    /// <summary>Tests whether the native localization registry contains the supplied identifier.</summary>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <remarks>Uses the DDX native contract: <c>loc_exists</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool LocalizationExists(string id)
    {
        byte* text = DDXClient.Utf8(id);
        bool result = ((delegate* unmanaged[Cdecl]<byte*, byte>)Export("loc_exists"))(text) != 0;
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Gets a borrowed UTF-8 pointer from the native localization registry. Copy the text before retaining it beyond its native lifetime.</summary>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_loc</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public byte* Localization(string id)
    {
        byte* text = DDXClient.Utf8(id);
        byte* result = ((delegate* unmanaged[Cdecl]<byte*, byte*>)Export("ui_loc"))(text);
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Enables native custom-item field persistence before registering item save callbacks or writing item data.</summary>
    /// <remarks>Uses the DDX native contract: <c>itemdata_enable</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void ItemDataEnable() => ((delegate* unmanaged[Cdecl]<void>)Export("itemdata_enable"))();

    /// <summary>Reads an item&apos;s saved unique identifier without allocating a new identifier.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>itemdata_id</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int ItemDataId(nint item) =>
        ((delegate* unmanaged[Cdecl]<nint, int>)Export("itemdata_id"))(item);

    /// <summary>Reads the native hash of an item&apos;s type identifier.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>itemdata_type_hash</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint ItemDataTypeHash(nint item) =>
        ((delegate* unmanaged[Cdecl]<nint, uint>)Export("itemdata_type_hash"))(item);

    /// <summary>Reads the native hash of an item&apos;s concrete item identifier.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>itemdata_item_hash</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint ItemDataItemHash(nint item) =>
        ((delegate* unmanaged[Cdecl]<nint, uint>)Export("itemdata_item_hash"))(item);

    /// <summary>Returns an item&apos;s unique saved identifier, allocating one through DDX if necessary.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>itemdata_ensure_id</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int ItemDataEnsureId(nint item) =>
        ((delegate* unmanaged[Cdecl]<nint, int>)Export("itemdata_ensure_id"))(item);

    /// <summary>Creates a native item owned by the caller so custom fields can be attached before inventory copying. Release it with ItemFree after use; inventories copy the source object.</summary>
    public nint ItemCreate(string type, string id, int amount = 1)
    {
        byte* t = DDXClient.Utf8(type),
            i = DDXClient.Utf8(id);
        try
        {
            return (
                (delegate* unmanaged[Cdecl]<byte*, byte*, int, nint>)Export("game_item_create")
            )(t, i, amount);
        }
        finally
        {
            NativeMemory.Free(t);
            NativeMemory.Free(i);
        }
    }

    /// <summary>Releases a caller-owned item returned by ItemCreate. Do not free a borrowed item that belongs to an inventory.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_item_free</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void ItemFree(nint item) =>
        ((delegate* unmanaged[Cdecl]<nint, void>)Export("game_item_free"))(item);

    /// <summary>Copies a native item into the destination inventory. The caller retains responsibility for any caller-owned source item.</summary>
    /// <param name="inventory">Borrowed native inventory address. The native owner must remain alive throughout the call.</param>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_inventory_copy</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool InventoryCopy(nint inventory, nint item) =>
        ((delegate* unmanaged[Cdecl]<nint, nint, byte>)Export("game_inventory_copy"))(
            inventory,
            item
        ) != 0;

    /// <summary>Stores the item through the native item destination rules and returns whether a destination accepted it.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>item_store</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ItemStore(nint item) =>
        ((delegate* unmanaged[Cdecl]<nint, nint>)Export("item_store"))(item) != 0;

    /// <summary>Gets the borrowed native item represented by the supplied drag object.</summary>
    /// <param name="drag">Borrowed native drag address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>item_drag_item</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint DragItem(nint drag) =>
        ((delegate* unmanaged[Cdecl]<nint, nint>)Export("item_drag_item"))(drag);

    /// <summary>Reads the native item-kind identifier of the supplied drag object.</summary>
    /// <param name="drag">Borrowed native drag address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>item_drag_kind</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint DragKind(nint drag) =>
        ((delegate* unmanaged[Cdecl]<nint, uint>)Export("item_drag_kind"))(drag);

    /// <summary>Converts the supplied native item-kind selection to its drag acceptance mask.</summary>
    /// <param name="kinds">The kinds used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>item_drag_mask</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint DragMask(uint kinds) =>
        ((delegate* unmanaged[Cdecl]<uint, uint>)Export("item_drag_mask"))(kinds);

    /// <summary>Gets the borrowed current native item drag object, or zero when no drag is active.</summary>
    /// <remarks>Uses the DDX native contract: <c>item_drag_current</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint CurrentDrag => ((delegate* unmanaged[Cdecl]<nint>)Export("item_drag_current"))();

    /// <summary>Resolves a low-level native call site&apos;s return address using a function, instruction offset, and optional target. Keep platform-specific inspection in the shared adapter layer.</summary>
    /// <param name="function">Borrowed native function address. The native owner must remain alive throughout the call.</param>
    /// <param name="offset">Relative displacement in the consuming native operation&apos;s coordinate system.</param>
    /// <param name="target">Borrowed native target address. The native owner must remain alive throughout the call. Defaults to 0.</param>
    /// <remarks>Uses the DDX native contract: <c>call_site_return</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint CallSite(nint function, uint offset, nint target = 0) =>
        ((delegate* unmanaged[Cdecl]<nint, uint, nint, nint>)Export("call_site_return"))(
            function,
            offset,
            target
        );

    /// <summary>Gets a borrowed floating-point register address for the current low-level site callback. Valid only during that callback.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_site_xmm</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float* SiteXmm(int index) =>
        ((delegate* unmanaged[Cdecl]<int, float*>)Export("game_site_xmm"))(index);

    /// <summary>Gets the borrowed register snapshot for the current low-level native site callback. Valid only during that callback.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_site_regs</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public GameRegisters* SiteRegisters =>
        ((delegate* unmanaged[Cdecl]<GameRegisters*>)Export("game_site_regs"))();

    /// <summary>Registers a custom native equipment slot kind and its callbacks. Keep the complete definition and referenced data alive for the registration lifetime.</summary>
    /// <param name="kind">Pointer to kind data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <remarks>Uses the DDX native contract: <c>equip_slot_kind_register</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool EquipmentKindRegister(EquipSlotKind* kind) =>
        ((delegate* unmanaged[Cdecl]<EquipSlotKind*, byte>)Export("equip_slot_kind_register"))(kind)
        != 0;

    /// <summary>Registers a predicate that enables custom native equipment slot handling.</summary>
    /// <param name="active">The active used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>equip_slots_active</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void EquipmentActive(delegate* unmanaged[Cdecl]<byte> active) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<byte>, void>)Export(
                "equip_slots_active"
            )
        )(active);

    /// <summary>Finds the current borrowed native hero address for a persistent hero GUID, or zero when absent.</summary>
    /// <param name="guid">Persistent hero identifier used to reacquire the current native hero or its saved fields.</param>
    /// <remarks>Uses the DDX native contract: <c>game_roster_hero</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint RosterHero(uint guid) =>
        ((delegate* unmanaged[Cdecl]<uint, nint>)Export("game_roster_hero"))(guid);

    /// <summary>Copies current borrowed roster hero addresses into a managed array, up to the supplied capacity.</summary>
    /// <param name="max">Maximum number of entries or native objects the operation may produce. Defaults to 64.</param>
    /// <remarks>Uses the DDX native contract: <c>game_roster_heroes</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint[] RosterHeroes(int max = 64)
    {
        var heroes = new nint[Math.Clamp(max, 1, 4096)];
        int count;
        fixed (nint* output = heroes)
            count = ((delegate* unmanaged[Cdecl]<nint*, int, int>)Export("game_roster_heroes"))(
                output,
                heroes.Length
            );
        return heroes[..count];
    }

    /// <summary>Gets the borrowed native estate storage inventory.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_estate_storage</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint EstateStorage() =>
        ((delegate* unmanaged[Cdecl]<nint>)Export("game_estate_storage"))();

    /// <summary>Copies current borrowed party hero addresses into a managed array, optionally excluding dead heroes.</summary>
    /// <param name="livingOnly">The living only used by this ddx api operation. Defaults to true.</param>
    /// <remarks>Uses the DDX native contract: <c>game_party_heroes</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint[] PartyHeroes(bool livingOnly = true)
    {
        nint* heroes = stackalloc nint[4];
        int count = (
            (delegate* unmanaged[Cdecl]<nint*, int, byte, int>)Export("game_party_heroes")
        )(heroes, 4, (byte)(livingOnly ? 1 : 0));
        return new ReadOnlySpan<nint>(heroes, count).ToArray();
    }

    /// <summary>Counts the native inventory amount for the specified item type hash using a 64-bit result.</summary>
    /// <param name="inventory">Borrowed native inventory address. The native owner must remain alive throughout the call.</param>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_inventory_amount</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public long InventoryAmount(nint inventory, uint type) =>
        ((delegate* unmanaged[Cdecl]<nint, uint, long>)Export("game_inventory_amount"))(
            inventory,
            type
        );

    /// <summary>Adds an item stack to the supplied native inventory using the game&apos;s stacking rules.</summary>
    /// <param name="inventory">Borrowed native inventory address. The native owner must remain alive throughout the call.</param>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <param name="amount">Signed quantity or effect magnitude in the consuming native operation&apos;s units.</param>
    /// <remarks>Uses the DDX native contract: <c>game_inventory_add</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool InventoryAdd(nint inventory, string type, string id, int amount)
    {
        byte* t = DDXClient.Utf8(type),
            i = DDXClient.Utf8(id);
        try
        {
            return (
                    (delegate* unmanaged[Cdecl]<nint, byte*, byte*, int, byte>)Export(
                        "game_inventory_add"
                    )
                )(inventory, t, i, amount) != 0;
        }
        finally
        {
            NativeMemory.Free(t);
            NativeMemory.Free(i);
        }
    }

    /// <summary>Queues the specified loot table type and code for the current expedition.</summary>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="code">Native loot-table code used by generation rules.</param>
    /// <remarks>Uses the DDX native contract: <c>game_queue_loot</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool QueueLoot(string type, string code)
    {
        byte* t = DDXClient.Utf8(type),
            c = DDXClient.Utf8(code);
        try
        {
            return ((delegate* unmanaged[Cdecl]<byte*, byte*, byte>)Export("game_queue_loot"))(t, c)
                != 0;
        }
        finally
        {
            NativeMemory.Free(t);
            NativeMemory.Free(c);
        }
    }

    /// <summary>Sets the native stacking limit for buffs with the specified identifier.</summary>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <param name="max">Maximum number of entries or native objects the operation may produce.</param>
    /// <remarks>Uses the DDX native contract: <c>buff_stack_limit</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void BuffStackLimit(string id, int max)
    {
        byte* text = DDXClient.Utf8(id);
        try
        {
            ((delegate* unmanaged[Cdecl]<byte*, int, void>)Export("buff_stack_limit"))(text, max);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Registers the mod&apos;s native announcement presentation, including optional icon and font identifiers.</summary>
    /// <param name="name">Identifier of the native service, field, or named value being addressed.</param>
    /// <param name="icon">The icon used by this ddx api operation.</param>
    /// <param name="font">Native font identifier used for text drawing or measurement.</param>
    /// <remarks>Uses the DDX native contract: <c>announce_mod</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void AnnounceMod(string name, string? icon, string? font)
    {
        byte* n = DDXClient.Utf8(name),
            i = icon is null ? null : DDXClient.Utf8(icon),
            f = font is null ? null : DDXClient.Utf8(font);
        try
        {
            ((delegate* unmanaged[Cdecl]<byte*, byte*, byte*, void>)Export("announce_mod"))(
                n,
                i,
                f
            );
        }
        finally
        {
            NativeMemory.Free(n);
            NativeMemory.Free(i);
            NativeMemory.Free(f);
        }
    }

    /// <summary>Reads the estate balance for the specified currency hash.</summary>
    /// <param name="currency">The currency used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_estate_amount</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int EstateAmount(uint currency) =>
        ((delegate* unmanaged[Cdecl]<uint, int>)Export("game_estate_amount"))(currency);

    /// <summary>Applies a signed estate currency adjustment and saves the result, including zero-amount adjustments.</summary>
    /// <param name="currency">The currency used by this ddx api operation.</param>
    /// <param name="amount">Signed quantity or effect magnitude in the consuming native operation&apos;s units.</param>
    /// <remarks>Uses the DDX native contract: <c>game_estate_add</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool EstateAdd(uint currency, int amount) =>
        ((delegate* unmanaged[Cdecl]<uint, int, byte>)Export("game_estate_add"))(currency, amount)
        != 0;

    /// <summary>Checks all nonnegative costs before deducting any currency and saves once on success. Currency identifiers must be distinct; returns false when payment cannot be made.</summary>
    /// <param name="costs">The costs used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_estate_pay</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool EstatePay(ReadOnlySpan<CurrencyCost> costs)
    {
        fixed (CurrencyCost* values = costs)
            return (
                    (delegate* unmanaged[Cdecl]<CurrencyCost*, int, byte>)Export("game_estate_pay")
                )(values, costs.Length) != 0;
    }

    /// <summary>Reads an integer custom field from the supplied native item using its registered field index.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <param name="field">The field used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>itemdata_get</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int ItemDataGet(nint item, int field) =>
        ((delegate* unmanaged[Cdecl]<nint, int, int>)Export("itemdata_get"))(item, field);

    /// <summary>Tests whether the supplied native item has a saved value for the registered custom field.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <param name="field">The field used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>itemdata_has</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ItemDataHas(nint item, int field) =>
        ((delegate* unmanaged[Cdecl]<nint, int, byte>)Export("itemdata_has"))(item, field) != 0;

    /// <summary>Writes an integer custom field on the native item and reports whether persistence accepts the update.</summary>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <param name="field">The field used by this ddx api operation.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>itemdata_set</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ItemDataSet(nint item, int field, int value) =>
        ((delegate* unmanaged[Cdecl]<nint, int, int, byte>)Export("itemdata_set"))(
            item,
            field,
            value
        ) != 0;

    /// <summary>Registers a native custom item type and its unmanaged behavior callbacks. Keep referenced strings and user data valid for the registration lifetime.</summary>
    /// <param name="definition">Pointer to definition data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <remarks>Uses the DDX native contract: <c>item_type_register</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal bool ItemTypeRegister(ItemTypeDef* definition) =>
        ((delegate* unmanaged[Cdecl]<ItemTypeDef*, byte>)Export("item_type_register"))(definition)
        != 0;

    /// <summary>Hashes an identifier with the game&apos;s native identifier hash function so managed catalog lookups match game data.</summary>
    /// <param name="name">Identifier of the native service, field, or named value being addressed.</param>
    /// <remarks>Uses the DDX native contract: <c>name_hash</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint NameHash(string name)
    {
        byte* text = DDXClient.Utf8(name);
        uint result = ((delegate* unmanaged[Cdecl]<byte*, uint>)Export("name_hash"))(text);
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Tests whether the borrowed native actor is a hero.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>is_hero</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool IsHero(nint actor) =>
        ((delegate* unmanaged[Cdecl]<nint, byte>)Export("is_hero"))(actor) != 0;

    /// <summary>Tests whether the borrowed native actor is a monster.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>is_monster</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool IsMonster(nint actor) =>
        ((delegate* unmanaged[Cdecl]<nint, byte>)Export("is_monster"))(actor) != 0;

    /// <summary>Reads the persistent native hero identifier used for roster lookups and saved fields.</summary>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>hero_guid</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint HeroGuid(nint hero) =>
        ((delegate* unmanaged[Cdecl]<nint, uint>)Export("hero_guid"))(hero);

    /// <summary>Reads the native identifier hash of the hero&apos;s class.</summary>
    public uint HeroClassHash(nint hero) =>
        ((delegate* unmanaged[Cdecl]<nint, uint>)Export("hero_class_hash"))(hero);

    /// <summary>Reads the native monster identifier used by custom monster data.</summary>
    /// <param name="monster">Borrowed native monster address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>monster_guid</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint MonsterGuid(nint monster) =>
        ((delegate* unmanaged[Cdecl]<nint, uint>)Export("monster_guid"))(monster);

    /// <summary>Reads a named native actor stat using the game&apos;s stat lookup service.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="stat">The stat used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>actor_stat</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float ActorStat(nint actor, string stat)
    {
        byte* text = DDXClient.Utf8(stat);
        float result = ((delegate* unmanaged[Cdecl]<nint, byte*, float>)Export("actor_stat"))(
            actor,
            text
        );
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Tests the actor&apos;s native buffs against the supplied identifier pattern.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="pattern">The pattern used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>actor_has_buff</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ActorHasBuff(nint actor, string pattern)
    {
        byte* text = DDXClient.Utf8(pattern);
        bool result =
            ((delegate* unmanaged[Cdecl]<nint, byte*, byte>)Export("actor_has_buff"))(actor, text)
            != 0;
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Tests the hero&apos;s native quirks against the supplied identifier pattern.</summary>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <param name="pattern">The pattern used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>hero_has_quirk</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool HeroHasQuirk(nint hero, string pattern)
    {
        byte* text = DDXClient.Utf8(pattern);
        bool result =
            ((delegate* unmanaged[Cdecl]<nint, byte*, byte>)Export("hero_has_quirk"))(hero, text)
            != 0;
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Tests the hero&apos;s native diseases against the supplied optional identifier pattern.</summary>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <param name="pattern">The pattern used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>hero_has_disease</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool HeroHasDisease(nint hero, string? pattern)
    {
        byte* text = pattern is null ? null : DDXClient.Utf8(pattern);
        bool result =
            ((delegate* unmanaged[Cdecl]<nint, byte*, byte>)Export("hero_has_disease"))(hero, text)
            != 0;
        if (text != null)
            NativeMemory.Free(text);
        return result;
    }

    /// <summary>Tests the hero&apos;s equipped native items against the supplied identifier pattern.</summary>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <param name="pattern">The pattern used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>hero_has_item</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool HeroHasItem(nint hero, string pattern)
    {
        byte* text = DDXClient.Utf8(pattern);
        bool result =
            ((delegate* unmanaged[Cdecl]<nint, byte*, byte>)Export("hero_has_item"))(hero, text)
            != 0;
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Removes native actor buffs matching any supplied identifier pattern and returns the number removed.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="patterns">The patterns used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>actor_remove_buffs</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int ActorRemoveBuffs(nint actor, params string[] patterns)
    {
        byte** values = stackalloc byte*[patterns.Length];
        for (int i = 0; i < patterns.Length; i++)
            values[i] = DDXClient.Utf8(patterns[i]);
        int result = (
            (delegate* unmanaged[Cdecl]<nint, byte**, int, int>)Export("actor_remove_buffs")
        )(actor, values, patterns.Length);
        for (int i = 0; i < patterns.Length; i++)
            NativeMemory.Free(values[i]);
        return result;
    }

    /// <summary>Registers a named integer persistence field and returns the native field index used by hero, item, and monster data accessors.</summary>
    /// <param name="name">Identifier of the native service, field, or named value being addressed.</param>
    /// <remarks>Uses the DDX native contract: <c>field_register</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int FieldRegister(string name)
    {
        byte* text = DDXClient.Utf8(name);
        int result = ((delegate* unmanaged[Cdecl]<byte*, int>)Export("field_register"))(text);
        NativeMemory.Free(text);
        return result;
    }

    /// <summary>Reads a registered integer hero field by persistent GUID, avoiding retained hero pointers.</summary>
    /// <param name="guid">Persistent hero identifier used to reacquire the current native hero or its saved fields.</param>
    /// <param name="field">The field used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>herodata_get</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int HeroDataGet(uint guid, int field) =>
        ((delegate* unmanaged[Cdecl]<uint, int, int>)Export("herodata_get"))(guid, field);

    /// <summary>Writes a registered integer hero field by persistent GUID for native save persistence.</summary>
    /// <param name="guid">Persistent hero identifier used to reacquire the current native hero or its saved fields.</param>
    /// <param name="field">The field used by this ddx api operation.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>herodata_set</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void HeroDataSet(uint guid, int field, int value) =>
        ((delegate* unmanaged[Cdecl]<uint, int, int, void>)Export("herodata_set"))(
            guid,
            field,
            value
        );

    /// <summary>Tests whether the persistent hero record contains the registered custom field.</summary>
    /// <param name="guid">Persistent hero identifier used to reacquire the current native hero or its saved fields.</param>
    /// <param name="field">The field used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>herodata_has</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool HeroDataHas(uint guid, int field) =>
        ((delegate* unmanaged[Cdecl]<uint, int, byte>)Export("herodata_has"))(guid, field) != 0;

    /// <summary>Reads a registered integer field from a borrowed native monster.</summary>
    /// <param name="monster">Borrowed native monster address. The native owner must remain alive throughout the call.</param>
    /// <param name="field">The field used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>monsterdata_get</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int MonsterDataGet(nint monster, int field) =>
        ((delegate* unmanaged[Cdecl]<nint, int, int>)Export("monsterdata_get"))(monster, field);

    /// <summary>Writes a registered integer field on a borrowed native monster.</summary>
    /// <param name="monster">Borrowed native monster address. The native owner must remain alive throughout the call.</param>
    /// <param name="field">The field used by this ddx api operation.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>monsterdata_set</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void MonsterDataSet(nint monster, int field, int value) =>
        ((delegate* unmanaged[Cdecl]<nint, int, int, void>)Export("monsterdata_set"))(
            monster,
            field,
            value
        );

    /// <summary>Applies native damage with explicit damage type, source, and attacker context so normal game handling can run.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="amount">Signed quantity or effect magnitude in the consuming native operation&apos;s units.</param>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="source">Source identifier used to attribute the event or select its configured rule.</param>
    /// <param name="attacker">Borrowed native attacker address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>game_damage</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool GameDamage(nint actor, float amount, string? type, string? source, nint attacker)
    {
        byte* t = type is null ? null : DDXClient.Utf8(type);
        byte* s = source is null ? null : DDXClient.Utf8(source);
        bool result =
            (
                (delegate* unmanaged[Cdecl]<nint, float, byte*, byte*, nint, byte>)Export(
                    "game_damage"
                )
            )(actor, amount, t, s, attacker) != 0;
        if (s != null)
            NativeMemory.Free(s);
        if (t != null)
            NativeMemory.Free(t);
        return result;
    }

    /// <summary>Applies native health healing with optional healer and source context.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="amount">Signed quantity or effect magnitude in the consuming native operation&apos;s units.</param>
    /// <param name="healer">Borrowed native healer address. The native owner must remain alive throughout the call.</param>
    /// <param name="source">Source identifier used to attribute the event or select its configured rule.</param>
    /// <remarks>Uses the DDX native contract: <c>game_heal</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool GameHeal(nint actor, float amount, nint healer, string? source)
    {
        byte* s = source is null ? null : DDXClient.Utf8(source);
        bool result =
            ((delegate* unmanaged[Cdecl]<nint, float, nint, byte*, byte>)Export("game_heal"))(
                actor,
                amount,
                healer,
                s
            ) != 0;
        if (s != null)
            NativeMemory.Free(s);
        return result;
    }

    /// <summary>Applies a signed native stress change with actor and source context.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="amount">Signed quantity or effect magnitude in the consuming native operation&apos;s units.</param>
    /// <param name="by">Borrowed native by address. The native owner must remain alive throughout the call.</param>
    /// <param name="source">Source identifier used to attribute the event or select its configured rule.</param>
    /// <remarks>Uses the DDX native contract: <c>game_stress</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool GameStress(nint actor, float amount, nint by, string? source)
    {
        byte* s = source is null ? null : DDXClient.Utf8(source);
        bool result =
            ((delegate* unmanaged[Cdecl]<nint, float, nint, byte*, byte>)Export("game_stress"))(
                actor,
                amount,
                by,
                s
            ) != 0;
        if (s != null)
            NativeMemory.Free(s);
        return result;
    }

    /// <summary>Applies a signed change to the expedition torch through native game handling.</summary>
    /// <param name="change">The change used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_torch</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool GameTorch(float change) =>
        ((delegate* unmanaged[Cdecl]<float, byte>)Export("game_torch"))(change) != 0;

    /// <summary>Applies a named native effect with explicit performer and target context.</summary>
    /// <param name="performer">Borrowed native performer address. The native owner must remain alive throughout the call.</param>
    /// <param name="target">Borrowed native target address. The native owner must remain alive throughout the call.</param>
    /// <param name="effect">The effect used by this ddx api operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_apply_effect</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool GameApplyEffect(nint performer, nint target, string effect)
    {
        byte* name = DDXClient.Utf8(effect);
        bool result =
            ((delegate* unmanaged[Cdecl]<nint, nint, byte*, byte>)Export("game_apply_effect"))(
                performer,
                target,
                name
            ) != 0;
        NativeMemory.Free(name);
        return result;
    }

    /// <summary>Draws a floating-point value from the game-owned global random stream in the range zero inclusive to one exclusive.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_random</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float GameRandom() => ((delegate* unmanaged[Cdecl]<float>)Export("game_random"))();

    /// <summary>Gets the unmanaged entry point for registering named native hook listeners with their phase and user data.</summary>
    /// <remarks>Uses the DDX native contract: <c>hook_on</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal delegate* unmanaged[Cdecl]<
        byte*,
        int,
        delegate* unmanaged[Cdecl]<HookContext*, void*, void>,
        void*,
        byte> HookOn =>
        (delegate* unmanaged[Cdecl]<
            byte*,
            int,
            delegate* unmanaged[Cdecl]<HookContext*, void*, void>,
            void*,
            byte>)Export("hook_on");

    /// <summary>Gets the unmanaged entry point for adding a wrapper to a native game-function hook chain.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_hook_wrap</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal delegate* unmanaged[Cdecl]<byte*, nint, nint*, byte> GameHookWrap =>
        (delegate* unmanaged[Cdecl]<byte*, nint, nint*, byte>)Export("game_hook_wrap");

    /// <summary>Gets the unmanaged entry point for observing a registered native instruction site.</summary>
    /// <remarks>Uses the DDX native contract: <c>game_hook_site</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    internal delegate* unmanaged[Cdecl]<
        byte*,
        delegate* unmanaged[Cdecl]<void*, void>,
        byte> GameHookSite =>
        (delegate* unmanaged[Cdecl]<byte*, delegate* unmanaged[Cdecl]<void*, void>, byte>)Export(
            "game_hook_site"
        );
}
