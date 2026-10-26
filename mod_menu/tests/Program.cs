using System.Text.Json.Nodes;
using DDXModMenu;

var schema = JsonNode.Parse(File.ReadAllText(args[0]))!.AsObject();
var menu = new MenuSession();
int count = 0;
void Check(bool ok, string name)
{
    if (!ok)
        throw new Exception(name);
    count++;
}
JsonObject Send(string json) => menu.Dispatch(JsonNode.Parse(json)!.AsObject());
JsonObject Load(string token, JsonObject? saved = null) =>
    menu.Dispatch(
        new JsonObject
        {
            ["event"] = "load",
            ["campaign"] = token,
            ["schemas"] = new JsonArray(schema.DeepClone()),
            ["saved"] = saved,
        }
    );
Check(
    Send("{\"event\":\"open\",\"paused\":true}")["open"]!.GetValue<bool>() == false,
    "front end blocked"
);
Load("A");
Check(
    Send("{\"event\":\"open\",\"campaign\":\"A\",\"paused\":false}")["error"] is not null,
    "unpaused blocked"
);
Check(
    Send("{\"event\":\"open\",\"campaign\":\"A\",\"paused\":true}")["schema"] is null,
    "initial form blank"
);
Send("{\"event\":\"select\",\"campaign\":\"A\",\"mod\":\"example.expedition\"}");
foreach (
    var (field, value) in new[]
    {
        ("name", "\"New title\""),
        ("limit", "42"),
        ("scale", "2.75"),
        ("detail", "\"full\""),
        ("sound", "\"loud\""),
        ("events", "[\"heroes\",\"loot\"]"),
    }
)
    Check(
        Send($"{{\"event\":\"edit\",\"campaign\":\"A\",\"field\":\"{field}\",\"value\":{value}}}")[
            "error"
        ]
            is null,
        "valid " + field
    );
foreach (
    var (field, value) in new[]
    {
        ("limit", "1.5"),
        ("limit", "1001"),
        ("scale", "\"2\""),
        ("detail", "\"unknown\""),
        ("events", "[\"loot\",\"loot\"]"),
        ("events", "[\"missing\"]"),
    }
)
    Check(
        Send($"{{\"event\":\"edit\",\"campaign\":\"A\",\"field\":\"{field}\",\"value\":{value}}}")[
            "error"
        ]
            is not null,
        "invalid " + field
    );
Check(
    Send("{\"event\":\"save\",\"campaign\":\"A\"}")["error"] is not null,
    "failed commit reported"
);
string? persisted = null;
menu.Commit = (token, data) =>
{
    Check(token == "A", "commit scoped");
    persisted = data;
    return true;
};
Check(Send("{\"event\":\"save\",\"campaign\":\"A\"}")["error"] is null, "commit success");
Load("A", JsonNode.Parse(persisted!)!.AsObject());
Send("{\"event\":\"open\",\"campaign\":\"A\",\"paused\":true}");
var restored = Send("{\"event\":\"select\",\"campaign\":\"A\",\"mod\":\"example.expedition\"}");
Check(restored["values"]!["limit"]!.GetValue<int>() == 42, "round trip");
Load("B");
Send("{\"event\":\"open\",\"campaign\":\"B\",\"paused\":true}");
Check(
    Send("{\"event\":\"select\",\"campaign\":\"B\",\"mod\":\"example.expedition\"}")["values"]![
        "limit"
    ]!.GetValue<int>() == 20,
    "campaign isolation"
);
Check(
    Send("{\"event\":\"save\",\"campaign\":\"A\"}")["error"] is not null,
    "stale callback blocked"
);
Send("{\"event\":\"unload\"}");
Check(
    Send("{\"event\":\"select\",\"campaign\":\"B\",\"mod\":\"example.expedition\"}")["error"]
        is not null,
    "unload invalidates"
);
Console.WriteLine($"PASS: {count} assertions");

// Complex C# handlers can veto invalid combinations and normalize accepted drafts.
Load("C");
Send("{\"event\":\"open\",\"campaign\":\"C\",\"paused\":true}");
Send("{\"event\":\"select\",\"campaign\":\"C\",\"mod\":\"example.expedition\"}");
menu.ClientEvent = (phase, id, form, values) =>
    values["limit"]!.GetValue<int>() > 50
        ? new JsonObject { ["error"] = "Custom limit exceeded." }
        : new JsonObject { ["values"] = values };
var rejected = Send("{\"event\":\"edit\",\"campaign\":\"C\",\"field\":\"limit\",\"value\":60}");
Check(rejected["error"]!.GetValue<string>() == "Custom limit exceeded.", "custom validation error");
Check(rejected["values"]!["limit"]!.GetValue<int>() == 20, "custom rejection restores draft");
menu.ClientEvent = (phase, id, form, values) =>
{
    values["name"] = "Normalized";
    return new JsonObject { ["values"] = values };
};
Check(
    Send("{\"event\":\"edit\",\"campaign\":\"C\",\"field\":\"limit\",\"value\":30}")["values"]![
        "name"
    ]!.GetValue<string>() == "Normalized",
    "custom normalization"
);
Console.WriteLine($"PASS: {count} total assertions including extension hooks");

menu.ClientEvent = (phase, id, form, values) =>
{
    if (
        phase == "build"
        && !form["fields"]!
            .AsArray()
            .OfType<JsonObject>()
            .Any(f => f["id"]!.GetValue<string>() == "quota")
    )
    {
        form["fields"]!
            .AsArray()
            .Add(
                (JsonNode)
                    new JsonObject
                    {
                        ["id"] = "quota",
                        ["label"] = "Dynamic quota",
                        ["type"] = "integer",
                        ["default"] = 2,
                    }
            );
        values["quota"] = 2;
    }
    return new JsonObject { ["schema"] = form, ["values"] = values };
};
var built = Send("{\"event\":\"select\",\"campaign\":\"C\",\"mod\":\"example.expedition\"}");
Check(
    built["error"] is null && built["values"]!["quota"]!.GetValue<int>() == 2,
    "dynamic form field"
);
Check(
    Send("{\"event\":\"edit\",\"campaign\":\"C\",\"field\":\"quota\",\"value\":3}")["error"]
        is null,
    "dynamic field validates"
);
menu.ClientEvent = null;
var stored = new JsonObject
{
    ["example.expedition"] = new JsonObject { ["future"] = "preserve me", ["limit"] = 33 },
    ["disabled.mod"] = new JsonObject { ["hidden"] = 7 },
};
Load("D", stored);
Send("{\"event\":\"open\",\"campaign\":\"D\",\"paused\":true}");
Send("{\"event\":\"select\",\"campaign\":\"D\",\"mod\":\"example.expedition\"}");
menu.Commit = (token, json) =>
{
    persisted = json;
    return true;
};
Check(Send("{\"event\":\"save\",\"campaign\":\"D\"}")["error"] is null, "save with unknown keys");
var kept = JsonNode.Parse(persisted!)!;
Check(
    kept["example.expedition"]!["future"]!.GetValue<string>() == "preserve me"
        && kept["disabled.mod"]!["hidden"]!.GetValue<int>() == 7,
    "unknown fields and disabled mods preserved"
);
Send("{\"event\":\"edit\",\"campaign\":\"D\",\"field\":\"limit\",\"value\":77}");
Send("{\"event\":\"close\",\"campaign\":\"D\"}");
Check(
    Send("{\"event\":\"open\",\"campaign\":\"D\",\"paused\":true}")["schema"] is null,
    "reopen clears selection"
);
Check(
    Send("{\"event\":\"select\",\"campaign\":\"D\",\"mod\":\"example.expedition\"}")["values"]![
        "limit"
    ]!.GetValue<int>() == 33,
    "close discards unsaved draft"
);
var invalid = (JsonObject)schema.DeepClone();
invalid["id"] = "bad.form";
invalid["fields"]![0]!["default"] = 42;
var isolated = menu.Dispatch(
    new JsonObject
    {
        ["event"] = "load",
        ["campaign"] = "E",
        ["schemas"] = new JsonArray(invalid, schema.DeepClone()),
    }
);
Check(
    isolated["mods"]!.AsArray().Count == 1 && isolated["error"] is not null,
    "bad schema isolated"
);
Console.WriteLine($"PASS: {count} final assertions");
