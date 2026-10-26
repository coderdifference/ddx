using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Mirrors a two-component native UI vector. Components use the coordinate system of the consuming DDX layout or drawing operation.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec2
{
    /// <summary>Stores horizontal and vertical coordinates in the consuming native UI operation&apos;s coordinate system.</summary>
    public float X,
        Y;

    /// <summary>Creates a Vec2 using the supplied dependencies and initial state. Native handles and services remain owned by their original providers.</summary>
    /// <param name="x">The x used by this vec2 operation.</param>
    /// <param name="y">The y used by this vec2 operation.</param>
    public Vec2(float x, float y)
    {
        X = x;
        Y = y;
    }
}
