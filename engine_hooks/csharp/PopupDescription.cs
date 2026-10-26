using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Carries managed copies of a popup&apos;s wording, source, and detail. The strings remain valid after the native description buffers leave scope.</summary>
/// <param name="Wording">Copied managed popup wording independent of native buffer lifetime.</param>
/// <param name="Source">Copied managed popup source description independent of native buffer lifetime.</param>
/// <param name="Detail">Copied managed popup detail text independent of native buffer lifetime.</param>
public readonly record struct PopupDescription(string Wording, string Source, string Detail);
