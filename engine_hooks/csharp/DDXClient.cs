using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>
/// Base class for a DDX NativeAOT client. It binds to DDX's exported C API from the host process,
/// registers hook-point callbacks, and keeps callback state alive for the lifetime of the client.
/// </summary>
public abstract unsafe partial class DDXClient
{
    /// <summary>The managed client&apos;s required DDX host ABI version. Initialization rejects hosts advertising a different version.</summary>
    public const int ApiVersion = 1;
    private static readonly List<GCHandle> CallbackRoots = [];
    private static readonly List<nint> GameHookSlots = [];
    private static DDXClient? _active;
    private static DDXApi? _api;

    /// <summary>Gets the client engine directory supplied by the native loader during successful initialization.</summary>
    public string EngineDirectory { get; private set; } = "";

    /// <summary>Gets the initialized shared native API. Throws if accessed before the DDX host has initialized this client.</summary>
    protected DDXApi Api =>
        _api ?? throw new InvalidOperationException("DDX API is not initialized");

    private DDXUi? _ui;

    /// <summary>Gets the lazily created shared UI service for this client.</summary>
    protected DDXUi Ui => _ui ??= new DDXUi(Api);

    private DDXCombat? _combat;

    /// <summary>Gets the lazily created shared combat service for this client.</summary>
    protected DDXCombat Combat => _combat ??= new DDXCombat(Api);

    /// <summary>Gets the native log filename selected by the derived client during initialization.</summary>
    protected abstract string LogName { get; }

    /// <summary>Register client APIs and callbacks here, before DDX installs its feature hooks.</summary>
    protected abstract void OnInitialize();

    /// <summary>Called synchronously on the game thread for a registered hook point.</summary>
    protected virtual void OnHook(HookContext* context) { }

    /// <summary>Called after DDX has read a campaign's mod list, and again when that campaign closes.</summary>
    protected virtual void OnCampaignChanged() { }

    /// <summary>Called after DDX restores the registered custom fields for a hero.</summary>
    protected virtual void OnHeroDataRestored(nint hero) { }

    /// <summary>Called immediately before DDX saves an inventory item carrying registered custom fields.</summary>
    protected virtual void OnItemSaved(nint item) { }

    /// <summary>Runs immediately before native hero derived-stat calculation. Derived clients may install temporary equipment state that must be restored in OnStatsCalcAfter.</summary>
    protected virtual void OnStatsCalcBefore(nint hero) { }

    /// <summary>Runs immediately after native hero derived-stat calculation. Derived clients should restore temporary equipment state installed by OnStatsCalcBefore.</summary>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    protected virtual void OnStatsCalcAfter(nint hero) { }

    /// <summary>Initializes a derived client from its exported native entry point.</summary>
    public static int InitializeClient(DDXClient client, EngineClientInfo* info)
    {
        if (info == null || info->Api != ApiVersion || info->EngineDir == 0)
            return 1;
        try
        {
            _api ??= new DDXApi();
            _active = client;
            client.EngineDirectory = Marshal.PtrToStringUTF8(info->EngineDir) ?? "";
            client.Api.LogFile(client.LogName);
            client.OnInitialize();
            client.Api.Log(
                $"C# DDX client initialized; hook points available: {client.Api.HookPointCount()}"
            );
            return 0;
        }
        catch (Exception error)
        {
            try
            {
                client.Api.Log($"C# DDX client initialization failed: {error}");
            }
            catch { }

            return 1;
        }
    }

    /// <summary>Registers hook with DDX for this component. Native callback registrations require their managed state to remain alive.</summary>
    /// <param name="point">The point used by this ddx client operation.</param>
    /// <param name="phase">The phase used by this ddx client operation.</param>
    protected bool RegisterHook(string point, HookPhase phase)
    {
        var handle = GCHandle.Alloc(this);
        CallbackRoots.Add(handle); // DDX has no unregister API; registrations live for the process lifetime.
        byte* name = Utf8(point);
        bool registered =
            Api.HookOn(name, (int)phase, &DispatchHook, (void*)GCHandle.ToIntPtr(handle)) != 0;
        NativeMemory.Free(name);
        if (!registered)
        {
            CallbackRoots.Remove(handle);
            handle.Free();
        }

        return registered;
    }

    /// <summary>
    /// Register for campaign load/close notifications. Use this to invalidate caches of campaign-dependent
    /// registrations or data. DDX has no unregister API, so the callback remains rooted for the process lifetime.
    /// </summary>
    protected void RegisterCampaignChanged()
    {
        Api.ModsOnCampaign(&DispatchCampaignChanged);
    }

    /// <summary>Register a callback after the game's hero fields and DDX custom hero fields have been restored.</summary>
    protected void RegisterHeroDataRestore()
    {
        Api.HeroDataOnRestore(&DispatchHeroDataRestore);
    }

    /// <summary>Enable DDX item fields and register a callback immediately before item save.</summary>
    protected void RegisterItemSave()
    {
        Api.ItemDataEnable();
        Api.ItemDataOnSave(&DispatchItemSave);
    }

    /// <summary>Register before/after callbacks around the game's derived-stat calculation.</summary>
    protected void RegisterStatsCalc()
    {
        Api.StatsOnCalc(&DispatchStatsCalcBefore, &DispatchStatsCalcAfter);
    }

    /// <summary>Register a DDX-managed inventory item type during client initialization.</summary>
    protected bool RegisterItemType(ItemTypeDef* definition) => Api.ItemTypeRegister(definition);

    /// <summary>
    /// Wrap a catalogued DDX game hook. The callback must use the exact native signature for that hook and
    /// call the returned next function when appropriate. Prefer hook points when they provide the event needed.
    /// </summary>
    protected bool WrapGameFunction(string id, nint wrapper, out GameHookNext next)
    {
        next = default;
        byte* name = Utf8(id);
        nint* slot = (nint*)NativeMemory.AllocZeroed((nuint)sizeof(nint));
        bool ok = Api.GameHookWrap(name, wrapper, slot) != 0;
        NativeMemory.Free(name);
        if (ok)
        {
            GameHookSlots.Add((nint)slot); // DDX updates this slot when the campaign's hook chain changes.
            next = new GameHookNext((nint)slot);
        }
        else
        {
            NativeMemory.Free(slot);
        }

        return ok;
    }

    /// <summary>Observe a catalogued mid-function site. The register block is borrowed for this callback.</summary>
    protected bool ObserveGameSite(string id, delegate* unmanaged[Cdecl]<void*, void> observer)
    {
        byte* name = Utf8(id);
        bool ok = Api.GameHookSite(name, observer) != 0;
        NativeMemory.Free(name);
        return ok;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void DispatchHook(HookContext* context, void* userdata)
    {
        try
        {
            var handle = GCHandle.FromIntPtr((nint)userdata);
            if (handle.Target is DDXClient client)
                client.OnHook(context);
        }
        catch
        {
            // Exceptions must never unwind through native game code.
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void DispatchCampaignChanged()
    {
        try
        {
            _active?.OnCampaignChanged();
        }
        catch
        {
            // Exceptions must never unwind through native game code.
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void DispatchHeroDataRestore(nint hero)
    {
        try
        {
            _active?.OnHeroDataRestored(hero);
        }
        catch
        { /* Never unwind through the game's save/restore code. */
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void DispatchItemSave(nint item)
    {
        try
        {
            _active?.OnItemSaved(item);
        }
        catch
        { /* Never unwind through the game's save code. */
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void DispatchStatsCalcBefore(nint hero)
    {
        try
        {
            _active?.OnStatsCalcBefore(hero);
        }
        catch
        { /* Never unwind through the game's stat calculation. */
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void DispatchStatsCalcAfter(nint hero)
    {
        try
        {
            _active?.OnStatsCalcAfter(hero);
        }
        catch
        { /* Never unwind through the game's stat calculation. */
        }
    }

    /// <summary>Allocates a null-terminated UTF-8 copy of managed text using NativeMemory. The caller owns the returned buffer and must release it with NativeMemory.Free.</summary>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    internal static byte* Utf8(string value)
    {
        int bytes = System.Text.Encoding.UTF8.GetByteCount(value);
        byte* result = (byte*)NativeMemory.Alloc((nuint)bytes + 1);
        System.Text.Encoding.UTF8.GetBytes(value, new Span<byte>(result, bytes));
        result[bytes] = 0;
        return result;
    }
}
