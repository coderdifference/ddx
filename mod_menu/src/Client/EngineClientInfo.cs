using System.Runtime.InteropServices;

namespace DDXModMenu;

/// <summary>DDX loader ABI 1; engine directory is borrowed for initialization.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct EngineClientInfo
{
    public int Api;
    public nint EngineDir;
}
