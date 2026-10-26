using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Converts borrowed native combat popups into managed descriptions through the DDX adapter. Native pointers are consumed synchronously and copied into managed strings.</summary>
/// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
public sealed unsafe class DDXPopups(DDXApi api)
{
    /// <summary>Copies a native popup&apos;s wording, source, and detail into managed strings. Returns whether the native adapter supplied a description.</summary>
    /// <param name="popup">Pointer to popup data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="description">Receives the description produced by the operation. Check the return value before consuming this output.</param>
    /// <remarks>Uses the DDX native contract: <c>popup_line</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool TryDescribe(PopupEvent* popup, out PopupDescription description)
    {
        PopupLineNative line = default;
        bool found =
            (
                (delegate* unmanaged[Cdecl]<PopupEvent*, PopupLineNative*, byte>)
                    api.NativeExport("popup_line")
            )(popup, &line) != 0;
        description = new(Text(line.Wording, 192), Text(line.Source, 96), Text(line.Detail, 320));
        return found;
    }

    private static string Text(byte* text, int capacity)
    {
        ReadOnlySpan<byte> data = new(text, capacity);
        int length = data.IndexOf((byte)0);
        return System.Text.Encoding.UTF8.GetString(length < 0 ? data : data[..length]);
    }
}
