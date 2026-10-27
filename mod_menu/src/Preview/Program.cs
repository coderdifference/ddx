using System.Text.Json.Nodes;
using DDXModMenu;

var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls(
    Environment.GetEnvironmentVariable("DDX_PREVIEW_URL") ?? "http://127.0.0.1:5187"
);
var app = builder.Build();
var gate = new object();
var session = new MenuSession();
var campaigns = new Dictionary<string, JsonObject>();
session.Commit = (token, data) =>
{
    campaigns[token] = JsonNode.Parse(data)!.AsObject();
    return true;
};
var root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../"));
app.MapGet("/", () => Results.File(Path.Combine(root, "preview/index.html"), "text/html"));
app.MapPost(
    "/event",
    async (HttpRequest request) =>
    {
        if (request.ContentLength is > 2097152)
            return Results.BadRequest();
        var message = (await JsonNode.ParseAsync(request.Body))!.AsObject();
        lock (gate)
        {
            if (message["event"]?.GetValue<string>() == "load")
            {
                string token = message["campaign"]!.GetValue<string>();
                var schema = JsonNode
                    .Parse(File.ReadAllText(Path.Combine(root, "examples/options.json")))!
                    .AsObject();
                var second = (JsonObject)schema.DeepClone();
                second["id"] = "example.interface";
                second["title"] = "Interface Settings";
                message["schemas"] = new JsonArray(schema, second);
                if (Environment.GetEnvironmentVariable("DDX_PREVIEW_FORM") is string clientForm)
                    message["schemas"]!
                        .AsArray()
                        .Add((JsonNode)JsonNode.Parse(File.ReadAllText(clientForm))!);
                message["saved"] = campaigns.TryGetValue(token, out var data)
                    ? data.DeepClone()
                    : null;
            }
            return Results.Text(session.Dispatch(message).ToJsonString(), "application/json");
        }
    }
);
app.Run();
