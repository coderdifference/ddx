using System.Text.Json.Nodes;

namespace DDX.Managed;

/// <summary>One synchronous options event on the game thread. Values and form are detached JSON copies.</summary>
public sealed class ModOptionsContext
{
    /// <summary>build, validate, loaded, changed or unloaded. Only changed means a Save committed.</summary>
    public string Event { get; }

    /// <summary>Editable form for build events. Keep stable IDs and valid defaults.</summary>
    public JsonObject? Form { get; set; }

    /// <summary>Draft on build/validate, committed values on loaded/changed; empty on unload.</summary>
    public JsonObject Values { get; set; }

    /// <summary>Set a user-facing validation error to reject an edit or Save. No side effects in validate.</summary>
    public string? Error { get; set; }

    internal ModOptionsContext(JsonObject message)
    {
        Event = message["event"]?.GetValue<string>() ?? "";
        Form = message["schema"]?.DeepClone() as JsonObject;
        Values = message["values"]?.DeepClone() as JsonObject ?? new();
    }

    internal JsonObject Reply() =>
        new()
        {
            ["schema"] = Form?.DeepClone(),
            ["values"] = Values.DeepClone(),
            ["error"] = Error,
        };
}
