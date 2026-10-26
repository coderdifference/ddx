using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;

namespace DDX.Managed;

public abstract unsafe partial class DDXClient
{
    /// <summary>
    /// Override to construct dependent fields (build), reject or normalize drafts (validate),
    /// or apply restored/committed values (loaded/changed). Unloaded clears campaign caches.
    /// Never apply gameplay changes during build/validate; edits may be discarded.
    /// Exceptions reject the operation and never cross the native boundary.
    /// </summary>
    protected virtual void OnModOptions(ModOptionsContext context) { }

    /// <summary>
    /// Registers this mod's JSON form and override callback. Call from OnInitialize.
    /// The default path is mod_options/options.json alongside engine/. Returns false when
    /// DDX lacks support or registration is rejected. The file must belong to this mod.
    /// </summary>
    protected bool RegisterModOptions(string? path = null)
    {
        path ??= Path.Combine(EngineDirectory, "..", "mod_options", "options.json");
        return RegisterModOptionsJson(JsonNode.Parse(File.ReadAllText(path))!.AsObject());
    }

    /// <summary>Registers a form constructed from merged mod data. Call once from OnInitialize; build callbacks can supply campaign-specific defaults.</summary>
    /// <param name="form">Version 1 form with this mod's stable ID and numeric or selection fields. The registration takes a copy.</param>
    /// <returns>False when the host does not support forms or rejects the schema.</returns>
    protected bool RegisterModOptionsJson(JsonObject form)
    {
        byte* text = Utf8(form.ToJsonString());
        try
        {
            var register = (delegate* unmanaged[Cdecl]<
                byte*,
                delegate* unmanaged[Cdecl]<byte*, nuint, nint>,
                delegate* unmanaged[Cdecl]<nint, void>,
                int>)
                Api.NativeExport("modmenu_form_register");
            return register(text, &DispatchModOptions, &ReleaseModOptions) == 1;
        }
        catch (EntryPointNotFoundException)
        {
            return false;
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Copies this registered mod's committed campaign values, or null outside a campaign.</summary>
    protected JsonObject? ReadModOptions(string modId)
    {
        byte* id = Utf8(modId);
        try
        {
            var get = (delegate* unmanaged[Cdecl]<byte*, byte*>)Api.NativeExport("modmenu_values");
            byte* value = get(id);
            return value == null
                ? null
                : JsonNode.Parse(Marshal.PtrToStringUTF8((nint)value)!) as JsonObject;
        }
        finally
        {
            NativeMemory.Free(id);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static nint DispatchModOptions(byte* input, nuint length)
    {
        try
        {
            if (input == null || length > 2097152)
                return 0;
            var message = JsonNode
                .Parse(new ReadOnlySpan<byte>(input, checked((int)length)))!
                .AsObject();
            var context = new ModOptionsContext(message);
            _active?.OnModOptions(context);
            return Marshal.StringToCoTaskMemUTF8(context.Reply().ToJsonString());
        }
        catch (Exception error)
        {
            return Marshal.StringToCoTaskMemUTF8(
                new JsonObject { ["error"] = error.Message }.ToJsonString()
            );
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ReleaseModOptions(nint reply)
    {
        if (reply != 0)
            Marshal.FreeCoTaskMem(reply);
    }
}
