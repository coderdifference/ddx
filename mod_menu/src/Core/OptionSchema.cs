using System.Text.Json.Nodes;

namespace DDXModMenu;

/// <summary>One validated author-supplied form. IDs are stable persistence keys.</summary>
public sealed class OptionSchema
{
    public string Id { get; }
    public string Title { get; }
    public JsonObject Document { get; }
    public JsonArray Fields => (JsonArray)Document["fields"]!;

    public OptionSchema(JsonObject document)
    {
        Document = (JsonObject)document.DeepClone();
        Id = Required(Document, "id");
        Title = Required(Document, "title");
        if (
            Id.Length > 128
            || Id.Any(c => !char.IsAsciiLetterOrDigit(c) && c is not '.' and not '_' and not '-')
        )
            throw new ArgumentException(
                "Mod ID must use ASCII letters, digits, dots, underscores or hyphens."
            );
        if (Document["version"]?.GetValue<int>() != 1)
            throw new ArgumentException("Unsupported schema version.");
        if (Document["fields"] is not JsonArray fields || fields.Count > 256)
            throw new ArgumentException("Expected at most 256 fields.");
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var node in fields)
        {
            if (node is not JsonObject field || !seen.Add(Required(field, "id")))
                throw new ArgumentException("Missing or duplicate field ID.");
            _ = Required(field, "label");
            if (Required(field, "id").Length > 128 || Required(field, "label").Length > 512)
                throw new ArgumentException("Field IDs or labels exceed their length limits.");
            var kind = Required(field, "type");
            if (kind is not ("text" or "integer" or "float" or "single" or "multi" or "enum"))
                throw new ArgumentException($"Unsupported field type: {kind}");
            if (
                field["min"] is not null
                && field["max"] is not null
                && Number(field["min"]) > Number(field["max"])
            )
                throw new ArgumentException("Minimum exceeds maximum.");
            if (kind is "single" or "multi" or "enum")
            {
                if (field["choices"] is not JsonArray choices || choices.Count is 0 or > 256)
                    throw new ArgumentException("Selection fields require 1 to 256 choices.");
                var ids = new HashSet<string>(StringComparer.Ordinal);
                foreach (var choice in choices)
                    if (
                        choice is not JsonObject item
                        || !ids.Add(Required(item, "id"))
                        || Required(item, "label").Length > 512
                    )
                        throw new ArgumentException("Invalid or duplicate choice.");
            }
            Validate(field, field["default"]);
        }
    }

    internal static string Required(JsonObject item, string key) =>
        item[key]?.GetValue<string>() is { Length: > 0 } value
            ? value
            : throw new ArgumentException($"Missing {key}.");

    private static double Number(JsonNode? node)
    {
        if (
            node is null
            || !double.TryParse(
                node.ToJsonString(),
                System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture,
                out var value
            )
            || !double.IsFinite(value)
        )
            throw new ArgumentException("Expected a finite number.");
        return value;
    }

    /// <summary>Checks JSON type, range, length and choice membership without coercion.</summary>
    public static void Validate(JsonObject field, JsonNode? value)
    {
        if (value is null)
            throw new ArgumentException("A value is required.");
        var kind = Required(field, "type");
        if (kind == "text")
        {
            var text = value.GetValue<string>();
            int max = field["maxLength"]?.GetValue<int>() ?? 4096;
            if (max < 0 || max > 65536 || text.Length > max || text.Contains((char)0))
                throw new ArgumentException("Text is too long.");
        }
        else if (kind is "integer" or "float")
        {
            double number = Number(value);
            if (
                kind == "integer"
                && (
                    number != Math.Truncate(number)
                    || number < int.MinValue
                    || number > int.MaxValue
                )
            )
                throw new ArgumentException("Expected a 32-bit integer.");
            if (
                field["min"] is not null && number < Number(field["min"])
                || field["max"] is not null && number > Number(field["max"])
            )
                throw new ArgumentException("Value is outside the allowed range.");
        }
        else
        {
            var choices = ((JsonArray)field["choices"]!)
                .Select(c => c!["id"]!.GetValue<string>())
                .ToHashSet(StringComparer.Ordinal);
            if (kind == "multi")
            {
                if (value is not JsonArray values)
                    throw new ArgumentException("Expected a list.");
                var selected = new HashSet<string>(StringComparer.Ordinal);
                foreach (var item in values)
                    if (
                        item is null
                        || !choices.Contains(item.GetValue<string>())
                        || !selected.Add(item.GetValue<string>())
                    )
                        throw new ArgumentException("Unknown or duplicate selection.");
            }
            else if (!choices.Contains(value.GetValue<string>()))
                throw new ArgumentException("Unknown selection.");
        }
    }

    /// <summary>Restores valid saved values; obsolete or invalid values use schema defaults.</summary>
    public JsonObject Restore(JsonObject? saved)
    {
        var result = new JsonObject();
        foreach (var node in Fields)
        {
            var field = node!.AsObject();
            string id = Required(field, "id");
            JsonNode? value = saved?[id];
            try
            {
                Validate(field, value);
            }
            catch
            {
                value = field["default"];
            }
            result[id] = value!.DeepClone();
        }
        return result;
    }
}
