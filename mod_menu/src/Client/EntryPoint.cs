using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Nodes;

namespace DDXModMenu;

/// <summary>NativeAOT entry points; requires the optional DDX mod-menu host contract v1.</summary>
public static unsafe class EntryPoint
{
    private static readonly MenuSession Session = new();
    private static delegate* unmanaged[Cdecl]<byte*, nuint, byte*, nuint, int> commit;
    private static bool initialized;
    private static delegate* unmanaged[Cdecl]<byte*, byte*, byte*> clientEvent;

    /// <summary>Registers a process-lifetime event handler only when all required exports exist.</summary>
    [UnmanagedCallersOnly(EntryPoint = "engine_client_init", CallConvs = [typeof(CallConvCdecl)])]
    public static int Initialize(EngineClientInfo* info)
    {
        try
        {
            if (initialized)
                return 0;
            if (info == null || info->Api != 1 || info->EngineDir == 0)
                return 1;
            nint host = OperatingSystem.IsWindows()
                ? NativeLibrary.Load("ddx.dll")
                : NativeLibrary.GetMainProgramHandle();
            if (
                !NativeLibrary.TryGetExport(host, "modmenu_host_version", out var version)
                || ((delegate* unmanaged[Cdecl]<int>)version)() != 1
                || !NativeLibrary.TryGetExport(host, "modmenu_register", out var register)
                || !NativeLibrary.TryGetExport(host, "modmenu_commit", out var write)
            )
                return 2;
            commit = (delegate* unmanaged[Cdecl]<byte*, nuint, byte*, nuint, int>)write;
            if (!NativeLibrary.TryGetExport(host, "modmenu_client_event", out var handler))
                return 2;
            clientEvent = (delegate* unmanaged[Cdecl]<byte*, byte*, byte*>)handler;
            Session.ClientEvent = ClientEvent;
            Session.Commit = Commit;
            byte[] id = Encoding.UTF8.GetBytes("ddx.modmenu\0");
            fixed (byte* name = id)
            {
                var subscribe = (delegate* unmanaged[Cdecl]<
                    byte*,
                    delegate* unmanaged[Cdecl]<byte*, nuint, nint>,
                    delegate* unmanaged[Cdecl]<nint, void>,
                    int>)register;
                if (subscribe(name, &Dispatch, &Release) != 1)
                    return 3;
            }
            initialized = true;
            return 0;
        }
        catch
        {
            return 1;
        }
    }

    private static JsonObject? ClientEvent(
        string phase,
        string mod,
        JsonObject schema,
        JsonObject values
    )
    {
        var request = new JsonObject
        {
            ["event"] = phase,
            ["schema"] = schema,
            ["values"] = values,
        };
        byte[] id = Encoding.UTF8.GetBytes(mod + "\0"),
            json = Encoding.UTF8.GetBytes(request.ToJsonString() + "\0");
        fixed (
            byte* i = id,
                j = json
        )
        {
            byte* response = clientEvent(i, j);
            return response == null
                ? null
                : JsonNode.Parse(Marshal.PtrToStringUTF8((nint)response)!)?.AsObject();
        }
    }

    private static bool Commit(string campaign, string json)
    {
        byte[] token = Encoding.UTF8.GetBytes(campaign),
            payload = Encoding.UTF8.GetBytes(json);
        fixed (
            byte* t = token,
                p = payload
        )
            return commit(t, (nuint)token.Length, p, (nuint)payload.Length) == 1;
    }

    /// <summary>Returns owned, nul-terminated UTF-8; native host calls Release exactly once.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static nint Dispatch(byte* input, nuint length)
    {
        try
        {
            if (input == null || length > 2097152)
                return 0;
            var message = JsonNode
                .Parse(new ReadOnlySpan<byte>(input, checked((int)length)))
                ?.AsObject();
            if (message is null)
                return 0;
            return Marshal.StringToCoTaskMemUTF8(Session.Dispatch(message).ToJsonString());
        }
        catch
        {
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Release(nint output)
    {
        if (output != 0)
            Marshal.FreeCoTaskMem(output);
    }
}
