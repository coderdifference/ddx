using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Carries a copied equipment-description line and its native text colour. Managed text remains usable after the native description callback returns.</summary>
/// <param name="Text">Managed display text copied or formatted for the consuming UI operation.</param>
/// <param name="Colour">Optional packed native text colour used when rendering this line.</param>
public readonly record struct EquipmentText(string Text, ulong Colour);
