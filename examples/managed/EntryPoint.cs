using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using DDX.Managed;

namespace DDXManagedExample;

/// <summary>Exports the entry point called by DDX's native client loader.</summary>
public static unsafe class EntryPoint
{
    /// <summary>Registers this client with the compatible DDX host.</summary>
    [UnmanagedCallersOnly(EntryPoint = "engine_client_init", CallConvs = [typeof(CallConvCdecl)])]
    public static int Initialize(EngineClientInfo* info) =>
        DDXClient.InitializeClient(new ExampleClient(), info);
}
