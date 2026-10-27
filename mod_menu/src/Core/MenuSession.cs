using System.Text.Json.Nodes;

namespace DDXModMenu;

/// <summary>Game-thread controller. Host owns campaign identity, pause gating and durable writes.</summary>
public sealed class MenuSession
{
    private readonly Dictionary<string, OptionSchema> schemas = new(StringComparer.Ordinal);
    private JsonObject saved = new();
    private JsonObject? draft;
    private string? selected;
    private string? campaign;
    private bool open;
    private string[] groupPath = [];
    private string? error;

    /// <summary>Optional build/validate handler receiving phase, mod ID, copied schema and draft. Returned schema/values are revalidated; an error rejects the action.</summary>
    public Func<string, string, JsonObject, JsonObject, JsonObject?>? ClientEvent { get; set; }

    /// <summary>Called once per Save with campaign token and candidate JSON. False retains the draft and prior committed state.</summary>
    public Func<string, string, bool> Commit { get; set; } = (_, _) => false;

    /// <summary>Consumes trusted host events and returns a complete immutable render description.</summary>
    public JsonObject Dispatch(JsonObject message)
    {
        error = null;
        try
        {
            string action = OptionSchema.Required(message, "event");
            if (action == "unload")
            {
                Reset();
                return View();
            }
            if (action == "load")
            {
                Reset();
                var token = OptionSchema.Required(message, "campaign");
                var next = new Dictionary<string, OptionSchema>(StringComparer.Ordinal);
                foreach (var node in (JsonArray)message["schemas"]!)
                {
                    try
                    {
                        var parsed = new OptionSchema(
                            node as JsonObject
                                ?? throw new ArgumentException("Expected a schema object.")
                        );
                        if (!next.TryAdd(parsed.Id, parsed))
                            throw new ArgumentException("Duplicate mod ID.");
                    }
                    catch (Exception exception)
                    {
                        error = $"An invalid mod form was excluded: {exception.Message}";
                    }
                }
                if (message["saved"] is not null && message["saved"] is not JsonObject)
                    throw new ArgumentException("Invalid campaign settings.");
                saved = (JsonObject?)message["saved"]?.DeepClone() ?? new();
                foreach (var pair in next)
                    schemas.Add(pair.Key, pair.Value);
                campaign = token;
                return View();
            }
            if (campaign is null || message["campaign"]?.GetValue<string>() != campaign)
                throw new InvalidOperationException("No matching loaded campaign.");
            if (action == "open")
            {
                if (message["paused"]?.GetValue<bool>() != true)
                    throw new InvalidOperationException("Campaign pause menu required.");
                open = true;
                selected = null;
                draft = null;
                groupPath = [];
            }
            else if (action == "close")
            {
                open = false;
                selected = null;
                draft = null;
                groupPath = [];
            }
            else
            {
                if (!open)
                    throw new InvalidOperationException("Menu is closed.");
                if (action == "select")
                {
                    string id = OptionSchema.Required(message, "mod");
                    if (!schemas.TryGetValue(id, out var schema))
                        throw new ArgumentException("Unknown mod.");
                    selected = id;
                    groupPath = [];
                    draft = schema.Restore(saved[id] as JsonObject);
                    ApplyClient("build");
                }
                else if (action == "navigate")
                {
                    if (selected is null || draft is null)
                        throw new InvalidOperationException("Select a mod first.");
                    var path = (
                        message["path"] as JsonArray
                        ?? throw new ArgumentException("Expected a group path.")
                    )
                        .Select(n => n!.GetValue<string>())
                        .ToArray();
                    if (!OptionNavigation.Exists(schemas[selected], path))
                        throw new ArgumentException("Unknown settings group.");
                    groupPath = path;
                }
                else if (action == "edit")
                {
                    if (selected is null || draft is null)
                        throw new InvalidOperationException("Select a mod first.");
                    string id = OptionSchema.Required(message, "field");
                    var field =
                        schemas[selected]
                            .Fields.OfType<JsonObject>()
                            .SingleOrDefault(f => f["id"]!.GetValue<string>() == id)
                        ?? throw new ArgumentException("Unknown field.");
                    OptionSchema.Validate(field, message["value"]);
                    var before = (JsonObject)draft.DeepClone();
                    draft[id] = message["value"]!.DeepClone();
                    try
                    {
                        ApplyClient("validate");
                    }
                    catch
                    {
                        draft = before;
                        throw;
                    }
                }
                else if (action == "save")
                {
                    if (selected is null || draft is null)
                        throw new InvalidOperationException("Select a mod first.");
                    ApplyClient("validate");
                    var candidate = (JsonObject)saved.DeepClone();
                    // Preserve unknown keys for temporarily disabled mods and newer schema fields.
                    var values = candidate[selected] as JsonObject ?? new JsonObject();
                    foreach (var pair in draft)
                        values[pair.Key] = pair.Value?.DeepClone();
                    candidate[selected] = values.Parent is null ? values : values.DeepClone();
                    string payload = candidate.ToJsonString();
                    if (System.Text.Encoding.UTF8.GetByteCount(payload) > 1048576)
                        throw new ArgumentException("Campaign settings exceed 1 MiB.");
                    if (!Commit(campaign, payload))
                        throw new InvalidOperationException(
                            "Campaign save failed. Your changes are still available."
                        );
                    saved = candidate;
                }
                else
                    throw new ArgumentException("Unknown event.");
            }
        }
        catch (Exception exception)
        {
            error = exception.Message;
        }
        return View();
    }

    private void ApplyClient(string phase)
    {
        if (selected is null || draft is null || ClientEvent is null)
            return;
        var reply = ClientEvent(
            phase,
            selected,
            (JsonObject)schemas[selected].Document.DeepClone(),
            (JsonObject)draft.DeepClone()
        );
        if (reply is null)
            return;
        if (reply["error"]?.GetValue<string>() is { Length: > 0 } reason)
            throw new ArgumentException(reason);
        var schema =
            phase == "build" && reply["schema"] is JsonObject form
                ? new OptionSchema(form)
                : schemas[selected];
        if (schema.Id != selected)
            throw new ArgumentException("A callback cannot change the mod ID.");
        var values = reply["values"] as JsonObject ?? draft;
        foreach (var node in schema.Fields)
        {
            var field = node!.AsObject();
            OptionSchema.Validate(field, values[OptionSchema.Required(field, "id")]);
        }
        schemas[selected] = schema;
        draft = (JsonObject)values.DeepClone();
    }

    private void Reset()
    {
        campaign = null;
        open = false;
        selected = null;
        draft = null;
        groupPath = [];
        saved = new();
        schemas.Clear();
    }

    private JsonObject View()
    {
        var mods = new JsonArray();
        foreach (var schema in schemas.Values.OrderBy(s => s.Title, StringComparer.Ordinal))
            mods.Add((JsonNode)new JsonObject { ["id"] = schema.Id, ["title"] = schema.Title });
        return new JsonObject
        {
            ["version"] = 1,
            ["open"] = open,
            ["width"] = 960,
            ["height"] = 640,
            ["leftWidth"] = 260,
            ["mods"] = mods,
            ["selected"] = selected,
            ["schema"] = selected is null ? null : schemas[selected].Document.DeepClone(),
            ["values"] = draft?.DeepClone(),
            ["navigation"] = selected is null
                ? null
                : OptionNavigation.View(schemas[selected], groupPath),
            ["error"] = error,
            ["canSave"] = open && selected is not null,
        };
    }
}
