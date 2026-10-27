using System.Text.Json.Nodes;

namespace DDXModMenu;

/// <summary>Presentation-only object navigation. Fields retain stable IDs and one complete draft across groups.</summary>
public static class OptionNavigation
{
    private static string[] PathOf(JsonObject field) =>
        field["group"] is JsonArray group ? group.Select(n => n!.GetValue<string>()).ToArray() : [];

    /// <summary>Accepts the root or a prefix that contains at least one field.</summary>
    public static bool Exists(OptionSchema schema, string[] path) =>
        path.Length == 0
        || path.Length <= 12
            && schema
                .Fields.OfType<JsonObject>()
                .Any(f =>
                    PathOf(f).Take(path.Length).SequenceEqual(path)
                    && PathOf(f).Length >= path.Length
                );

    /// <summary>Returns immediate child groups and direct scalar fields, without modifying draft values.</summary>
    public static JsonObject View(OptionSchema schema, string[] path)
    {
        var children = new Dictionary<string, int>(StringComparer.Ordinal);
        var fields = new JsonArray();
        foreach (var field in schema.Fields.OfType<JsonObject>())
        {
            var group = PathOf(field);
            if (group.Length < path.Length || !group.Take(path.Length).SequenceEqual(path))
                continue;
            if (group.Length == path.Length)
                fields.Add(field.DeepClone());
            else
                children[group[path.Length]] = children.GetValueOrDefault(group[path.Length]) + 1;
        }
        var groups = new JsonArray();
        foreach (var pair in children)
            groups.Add(
                (JsonNode)
                    new JsonObject
                    {
                        ["label"] = pair.Key,
                        ["count"] = pair.Value,
                        ["path"] = new JsonArray(
                            path.Append(pair.Key)
                                .Select(p => (JsonNode?)JsonValue.Create(p))
                                .ToArray()
                        ),
                    }
            );
        return new JsonObject
        {
            ["path"] = new JsonArray(path.Select(p => (JsonNode?)JsonValue.Create(p)).ToArray()),
            ["groups"] = groups,
            ["fields"] = fields,
        };
    }
}
