using System.Runtime.InteropServices;

namespace DDX.Managed;

public sealed unsafe partial class DDXUi
{
    /// <summary>Mirrors one native confirmation-dialog choice and its unmanaged callback. UTF-8 keys, labels, and callback pointers must remain valid while the native dialog consumes them.</summary>
    [StructLayout(LayoutKind.Sequential)]
    private struct ConfirmOption
    {
        /// <summary>Native key pointer in the ConfirmOption ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
        public byte* Key;

        /// <summary>Native label pointer in the ConfirmOption ABI. Referenced storage must remain valid while DDX consumes this structure.</summary>
        public byte* Label;

        /// <summary>Receives the selected native confirmation option through borrowed UTF-8 arguments. Keep the callback code valid until the dialog resolves.</summary>
        public delegate* unmanaged[Cdecl]<byte*, byte*, void> Callback;
    }
}
