using System.Runtime.InteropServices;

namespace DDX.Managed;

// Mirrors EngineClientInfo in engine_hooks/src/common.h. This layout is part of DDX's client ABI.
/// <summary>Mirrors the initialization descriptor passed by the native DDX loader. Validate its ABI version before using the borrowed engine-directory pointer.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct EngineClientInfo
{
    /// <summary>Stores the native loader&apos;s ABI version. The managed client rejects values different from DDXClient.ApiVersion.</summary>
    public int Api;

    /// <summary>Stores a borrowed pointer to the loader&apos;s null-terminated UTF-8 client engine directory. Initialization copies it into managed text.</summary>
    public nint EngineDir;
}
