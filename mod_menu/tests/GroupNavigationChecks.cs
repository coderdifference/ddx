using System.Text.Json.Nodes;
using DDXModMenu;

internal static class GroupNavigationChecks
{
    public static void Run()
    {
        static void Check(bool value, string message)
        {
            if (!value)
                throw new Exception(message);
        }
        var form = JsonNode
            .Parse(
                """{"version":1,"id":"grouped","title":"Grouped","layout":"table","fields":[{"id":"gold","label":"Gold","group":["Blacksmith","Reforge"],"type":"integer","min":0,"default":100},{"id":"fragments","label":"Fragments","group":["Blacksmith","Reforge"],"type":"integer","min":0,"default":5},{"id":"chance","label":"Battle","group":["Drops"],"type":"float","min":0,"max":1,"default":0.25}]}"""
            )!
            .AsObject();
        string? saved = null;
        var menu = new MenuSession
        {
            Commit = (_, payload) =>
            {
                saved = payload;
                return true;
            },
        };
        JsonObject Send(string action, JsonArray? path = null) =>
            menu.Dispatch(
                new JsonObject
                {
                    ["event"] = action,
                    ["campaign"] = "A",
                    ["mod"] = "grouped",
                    ["paused"] = true,
                    ["path"] = path,
                }
            );
        menu.Dispatch(
            new JsonObject
            {
                ["event"] = "load",
                ["campaign"] = "A",
                ["schemas"] = new JsonArray(form),
            }
        );
        Send("open");
        var root = Send("select");
        Check(
            root["navigation"]!["groups"]!.AsArray().Count == 2
                && root["navigation"]!["fields"]!.AsArray().Count == 0,
            "Root should contain sections rather than 3 flattened fields"
        );
        var nested = Send("navigate", new JsonArray("Blacksmith", "Reforge"));
        Check(
            nested["navigation"]!["fields"]!.AsArray().Count == 2
                && nested["values"]!.AsObject().Count == 3,
            "Navigation must retain full draft"
        );
        menu.Dispatch(
            new JsonObject
            {
                ["event"] = "edit",
                ["campaign"] = "A",
                ["field"] = "gold",
                ["value"] = 321,
            }
        );
        var drops = Send("navigate", new JsonArray("Drops"));
        Check(
            drops["values"]!["gold"]!.GetValue<int>() == 321
                && drops["navigation"]!["fields"]!.AsArray().Count == 1,
            "Moving between objects must preserve edits"
        );
        Check(
            Send("navigate", new JsonArray("Missing"))["error"] is not null,
            "Unknown group must be rejected"
        );
        Check(
            Send("navigate", new JsonArray())["navigation"]!["groups"]!.AsArray().Count == 2,
            "Up/root navigation"
        );
        Check(
            Send("save")["error"] is null
                && JsonNode.Parse(saved!)!["grouped"]!["gold"]!.GetValue<int>() == 321
                && JsonNode.Parse(saved!)!["grouped"]!.AsObject().Count == 3,
            "Save must include values from all groups"
        );
        Send("close");
        Send("open");
        Check(
            Send("select")["navigation"]!["path"]!.AsArray().Count == 0,
            "Reopening should start at sections"
        );
        var invalid = (JsonObject)form.DeepClone();
        invalid["fields"]![0]!["group"] = new JsonArray(123);
        bool rejected = false;
        try
        {
            _ = new OptionSchema(invalid);
        }
        catch (ArgumentException)
        {
            rejected = true;
        }
        Check(rejected, "Group names must be strings");
        foreach (
            JsonNode badDescription in new JsonNode[]
            {
                JsonValue.Create(123)!,
                JsonValue.Create(new string('x', 2049))!,
            }
        )
        {
            var badHelp = (JsonObject)form.DeepClone();
            badHelp["fields"]![0]!["description"] = badDescription.DeepClone();
            rejected = false;
            try
            {
                _ = new OptionSchema(badHelp);
            }
            catch (ArgumentException)
            {
                rejected = true;
            }
            Check(rejected, "Invalid descriptions must be rejected");
        }
        Console.WriteLine(
            "PASS: nested groups, draft preservation, complete saves, invalid paths and metadata"
        );
    }
}
