using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>DDX UI resources and layouts. Native handles are borrowed for the host lifetime.</summary>
/// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
public sealed unsafe partial class DDXUi(DDXApi api)
{
    private readonly nint push = api.GameSymbol("_Z14Transform_Pushv"),
        pop = api.GameSymbol("_Z13Transform_Popv"),
        translate = api.GameSymbol("_Z19Transform_TranslateRK4vec2"),
        depth = api.GameSymbol("_Z19Transform_Translatef"),
        scale = api.GameSymbol("_Z15Transform_Scalef");

    /// <summary>Saves the current native UI transform state. Pair every call with Pop after drawing the scoped content.</summary>
    public void Push()
    {
        if (push != 0)
            ((delegate* unmanaged[Cdecl]<void>)push)();
    }

    /// <summary>Restores the most recently saved native UI transform state. Balance this with Push to avoid affecting subsequent game rendering.</summary>
    public void Pop()
    {
        if (pop != 0)
            ((delegate* unmanaged[Cdecl]<void>)pop)();
    }

    /// <summary>Offsets subsequent native drawing within the current transform scope.</summary>
    /// <param name="offset">Relative displacement in the consuming native operation&apos;s coordinate system.</param>
    public void Translate(Vec2 offset)
    {
        if (translate != 0)
            ((delegate* unmanaged[Cdecl]<Vec2*, void>)translate)(&offset);
    }

    /// <summary>Sets the drawing depth for subsequent native UI content.</summary>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    public void Depth(float value)
    {
        if (depth != 0)
            ((delegate* unmanaged[Cdecl]<float, void>)depth)(value);
    }

    /// <summary>Applies a uniform scale to subsequent drawing within the current UI transform scope.</summary>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    public void Scale(float value)
    {
        if (scale != 0)
            ((delegate* unmanaged[Cdecl]<float, void>)scale)(value);
    }

    /// <summary>Requests a split name frame for the identified native drawing caller. Supplies alternate titles, texture, and a callback that resolves placement.</summary>
    /// <param name="caller">Borrowed native caller address. The native owner must remain alive throughout the call.</param>
    /// <param name="title">The title used by this ddx ui operation.</param>
    /// <param name="blankTitle">The blank title used by this ddx ui operation.</param>
    /// <param name="texture">The texture used by this ddx ui operation.</param>
    /// <param name="placement">The placement used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_name_frame_split</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool SplitNameFrame(
        nint caller,
        string title,
        string blankTitle,
        string texture,
        delegate* unmanaged[Cdecl]<Vec2*, byte> placement
    )
    {
        byte* a = DDXClient.Utf8(title),
            b = DDXClient.Utf8(blankTitle),
            c = DDXClient.Utf8(texture);
        try
        {
            return (
                    (delegate* unmanaged[Cdecl]<
                        nint,
                        byte*,
                        byte*,
                        byte*,
                        delegate* unmanaged[Cdecl]<Vec2*, byte>,
                        byte>)
                        api.NativeExport("ui_name_frame_split")
                )(caller, a, b, c, placement) != 0;
        }
        finally
        {
            NativeMemory.Free(a);
            NativeMemory.Free(b);
            NativeMemory.Free(c);
        }
    }

    /// <summary>Configures the split stash header with populated and blank titles plus a placement callback.</summary>
    /// <param name="title">The title used by this ddx ui operation.</param>
    /// <param name="blankTitle">The blank title used by this ddx ui operation.</param>
    /// <param name="texture">The texture used by this ddx ui operation.</param>
    /// <param name="placement">The placement used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_realm_header_split</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool SplitStashHeader(
        string title,
        string blankTitle,
        string texture,
        delegate* unmanaged[Cdecl]<Vec2*, byte> placement
    )
    {
        byte* a = DDXClient.Utf8(title),
            b = DDXClient.Utf8(blankTitle),
            c = DDXClient.Utf8(texture);
        try
        {
            return (
                    (delegate* unmanaged[Cdecl]<
                        byte*,
                        byte*,
                        byte*,
                        delegate* unmanaged[Cdecl]<Vec2*, byte>,
                        byte>)
                        api.NativeExport("ui_realm_header_split")
                )(a, b, c, placement) != 0;
        }
        finally
        {
            NativeMemory.Free(a);
            NativeMemory.Free(b);
            NativeMemory.Free(c);
        }
    }

    /// <summary>Registers a callback for native recruitment tooltips. The callback receives borrowed native objects and must remain valid while registered.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_recruit_tooltip_on</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool OnRecruitTooltip(delegate* unmanaged[Cdecl]<nint, nint, void> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, nint, void>, byte>)
                api.NativeExport("ui_recruit_tooltip_on")
        )(callback) != 0;

    /// <summary>Creates an owned prefix scope that inserts lines before native tooltip content. Dispose the scope to finish the prefix and restore native state.</summary>
    /// <param name="tooltip">Borrowed native tooltip address. The native owner must remain alive throughout the call.</param>
    /// <param name="reserve">Initial capacity reserved by the native tooltip prefix scope.</param>
    public TooltipPrefix PrependTooltip(nint tooltip, uint reserve) => new(api, tooltip, reserve);

    /// <summary>Registers a provider for extra building upgrade description lines. Return the native handled status after populating the supplied output.</summary>
    /// <param name="provider">The provider used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_building_descriptions</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool BuildingDescriptions(delegate* unmanaged[Cdecl]<uint, int, nint, byte> provider) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<uint, int, nint, byte>, byte>)
                api.NativeExport("ui_building_descriptions")
        )(provider) != 0;

    /// <summary>Appends managed text to a native building-description output collection using UTF-8 encoding.</summary>
    /// <param name="output">Borrowed native output address. The native owner must remain alive throughout the call.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_building_description_add</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void AddBuildingDescription(nint output, string value)
    {
        byte* text = DDXClient.Utf8(value);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<nint, byte*, byte>)
                    api.NativeExport("ui_building_description_add")
            )(output, text);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Registers a provider that adjusts the native building upgrade tree position and size.</summary>
    /// <param name="provider">The provider used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_building_tree_offsets</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool BuildingTreeOffsets(
        delegate* unmanaged[Cdecl]<nint, Vec2*, Vec2*, byte> provider
    ) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, Vec2*, Vec2*, byte>, byte>)
                api.NativeExport("ui_building_tree_offsets")
        )(provider) != 0;

    /// <summary>Selects the native inventory highlight mode for subsequent UI rendering.</summary>
    /// <param name="kind">The kind used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_inventory_highlight</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void InventoryHighlight(int kind) =>
        ((delegate* unmanaged[Cdecl]<int, void>)api.NativeExport("ui_inventory_highlight"))(kind);

    /// <summary>Sets the native drawing tint from a packed RGBA value.</summary>
    /// <param name="rgba">The rgba used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_colour</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Tint(uint rgba) =>
        ((delegate* unmanaged[Cdecl]<uint, void>)api.NativeExport("ui_colour"))(rgba);

    /// <summary>Draws a loaded native texture at the supplied UI position and size.</summary>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="texture">Pointer to texture data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="size">Dimensions in the consuming native UI coordinate system. Defaults to default.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_image_sized</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Image(Vec2 pos, byte* texture, Vec2 size = default) =>
        ((delegate* unmanaged[Cdecl]<Vec2, byte*, Vec2, void>)api.NativeExport("ui_image_sized"))(
            pos,
            texture,
            size
        );

    /// <summary>Loads a texture by path and draws it at the supplied position. Returns false when the texture cannot be resolved.</summary>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="path">Texture, layout, or filesystem path recognized by the consuming native service.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_image_path</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ImagePath(Vec2 pos, string path)
    {
        byte* text = DDXClient.Utf8(path);
        try
        {
            return (
                    (delegate* unmanaged[Cdecl]<Vec2, byte*, byte>)api.NativeExport("ui_image_path")
                )(pos, text) != 0;
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Draws text with the specified native font, colour, alignment, and optional wrapping width.</summary>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <param name="font">Native font identifier used for text drawing or measurement.</param>
    /// <param name="colour">Native text or drawing colour value expected by the receiving UI operation.</param>
    /// <param name="anchor">Native alignment point used to position the content. Defaults to 0.</param>
    /// <param name="width">Available or requested width in the consuming native UI coordinate system. Defaults to 0.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_text_anchored</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Text(
        Vec2 pos,
        string value,
        string font,
        string colour,
        int anchor = 0,
        float width = 0
    )
    {
        byte* t = Encoded(value),
            f = DDXClient.Utf8(font),
            c = DDXClient.Utf8(colour);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<Vec2, byte*, byte*, byte*, int, float, void>)
                    api.NativeExport("ui_text_anchored")
            )(pos, t, f, c, anchor, width);
        }
        finally
        {
            NativeMemory.Free(t);
            NativeMemory.Free(f);
            NativeMemory.Free(c);
        }
    }

    /// <summary>Measures the native rendered height of text using the supplied font and wrapping width.</summary>
    /// <param name="font">Native font identifier used for text drawing or measurement.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <param name="width">Available or requested width in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_text_height</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float TextHeight(string font, string value, float width)
    {
        byte* f = DDXClient.Utf8(font),
            t = Encoded(value);
        try
        {
            return (
                (delegate* unmanaged[Cdecl]<byte*, byte*, float, float>)
                    api.NativeExport("ui_text_height")
            )(f, t, width);
        }
        finally
        {
            NativeMemory.Free(f);
            NativeMemory.Free(t);
        }
    }

    /// <summary>Measures the native rendered width of text using the supplied font.</summary>
    /// <param name="font">Native font identifier used for text drawing or measurement.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_text_width</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float TextWidth(string font, string value)
    {
        byte* f = DDXClient.Utf8(font),
            t = Encoded(value);
        try
        {
            return (
                (delegate* unmanaged[Cdecl]<byte*, byte*, float>)api.NativeExport("ui_text_width")
            )(f, t);
        }
        finally
        {
            NativeMemory.Free(f);
            NativeMemory.Free(t);
        }
    }

    /// <summary>Draws and processes a native textured button with a stable widget ID and enabled state. Clicks invoke the unmanaged callback with the supplied payload.</summary>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="texture">Pointer to texture data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <param name="enabled">Whether the native widget or feature should accept interaction.</param>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <param name="data">Loaded gear catalog supplying definitions and stable indices for this operation. Defaults to default.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_button</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool Button(
        Vec2 pos,
        byte* texture,
        uint id,
        bool enabled,
        delegate* unmanaged[Cdecl]<byte*, void> callback,
        ReadOnlySpan<byte> data = default
    )
    {
        fixed (byte* state = data)
            return (
                    (delegate* unmanaged[Cdecl]<
                        Vec2,
                        byte*,
                        uint,
                        byte,
                        delegate* unmanaged[Cdecl]<byte*, void>,
                        byte*,
                        nuint,
                        byte>)
                        api.NativeExport("ui_button")
                )(pos, texture, id, (byte)(enabled ? 1 : 0), callback, state, (nuint)data.Length)
                != 0;
    }

    /// <summary>Tests whether the current native mouse position lies inside the supplied UI rectangle.</summary>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="size">Dimensions in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_mouse_in</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool MouseIn(Vec2 pos, Vec2 size) =>
        ((delegate* unmanaged[Cdecl]<Vec2, Vec2, byte>)api.NativeExport("ui_mouse_in"))(pos, size)
        != 0;

    /// <summary>Draws a native dialog frame using a style from the supplied layout.</summary>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="style">The style used by this ddx ui operation.</param>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="size">Dimensions in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_dialog_frame</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void DialogFrame(nint layout, string style, Vec2 pos, Vec2 size)
    {
        byte* text = DDXClient.Utf8(style);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<nint, byte*, Vec2, Vec2, void>)
                    api.NativeExport("ui_dialog_frame")
            )(layout, text, pos, size);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Draws a native item panel using persistent opaque panel state and returns its resulting content rectangle.</summary>
    /// <param name="state">Pointer to state data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <param name="hero">Borrowed native hero address. The native owner must remain alive throughout the call.</param>
    /// <param name="resolve">Hero resolve progression level used for eligibility or starter selection.</param>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="size">Dimensions in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_item_panel</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public UiRect ItemPanel(
        UiPanelBox* state,
        nint layout,
        string key,
        nint item,
        nint hero,
        int resolve,
        Vec2 pos,
        Vec2 size
    )
    {
        byte* text = DDXClient.Utf8(key);
        try
        {
            return (
                (delegate* unmanaged[Cdecl]<
                    UiPanelBox*,
                    nint,
                    byte*,
                    nint,
                    nint,
                    int,
                    Vec2,
                    Vec2,
                    UiRect>)
                    api.NativeExport("ui_item_panel")
            )(state, layout, text, item, hero, resolve, pos, size);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Resets opaque item-panel state before reusing it for another item or layout.</summary>
    /// <param name="state">Pointer to state data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_item_panel_reset</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void ResetItemPanel(UiPanelBox* state) =>
        ((delegate* unmanaged[Cdecl]<UiPanelBox*, void>)api.NativeExport("ui_item_panel_reset"))(
            state
        );

    /// <summary>Initializes opaque native drop-target state and its item-drop callback.</summary>
    /// <param name="state">Pointer to state data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_drop_target_init</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void DropInit(
        UiDropTarget* state,
        delegate* unmanaged[Cdecl]<UiDropTarget*, nint, nint, ulong> callback
    ) =>
        (
            (delegate* unmanaged[Cdecl]<
                UiDropTarget*,
                delegate* unmanaged[Cdecl]<UiDropTarget*, nint, nint, ulong>,
                void*,
                void>)
                api.NativeExport("ui_drop_target_init")
        )(state, callback, null);

    /// <summary>Places and processes a native drop target for the accepted item-kind mask.</summary>
    /// <param name="state">Pointer to state data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="accepts">The accepts used by this ddx ui operation.</param>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="size">Dimensions in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_drop_target</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Drop(UiDropTarget* state, uint accepts, Vec2 pos, Vec2 size) =>
        (
            (delegate* unmanaged[Cdecl]<UiDropTarget*, uint, Vec2, Vec2, void>)
                api.NativeExport("ui_drop_target")
        )(state, accepts, pos, size);

    /// <summary>Gets whether the native hotspot input service is ready to process interactions.</summary>
    /// <remarks>Uses the DDX native contract: <c>ui_hotspot_ready</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool HotspotReady =>
        ((delegate* unmanaged[Cdecl]<byte>)api.NativeExport("ui_hotspot_ready"))() != 0;

    /// <summary>Registers callbacks before and after native hotspot processing. The before callback can influence whether the native interaction proceeds.</summary>
    /// <param name="before">The before used by this ddx ui operation.</param>
    /// <param name="after">The after used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_on_hotspot</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void OnHotspot(
        delegate* unmanaged[Cdecl]<byte*, byte> before,
        delegate* unmanaged[Cdecl]<byte*, void> after
    ) =>
        (
            (delegate* unmanaged[Cdecl]<
                delegate* unmanaged[Cdecl]<byte*, byte>,
                delegate* unmanaged[Cdecl]<byte*, void>,
                void>)
                api.NativeExport("ui_on_hotspot")
        )(before, after);

    /// <summary>Registers a native text filter that may replace UTF-8 text during UI rendering. Replacement pointers must remain valid for native consumption.</summary>
    /// <param name="filter">The filter used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_on_text</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void OnText(delegate* unmanaged[Cdecl]<nint, byte*, byte**, int> filter) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, byte*, byte**, int>, void>)
                api.NativeExport("ui_on_text")
        )(filter);

    /// <summary>Resolves a dialog placement rule against an anchor point and dialog size.</summary>
    /// <param name="place">The place used by this ddx ui operation.</param>
    /// <param name="point">The point used by this ddx ui operation.</param>
    /// <param name="size">Dimensions in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_dialog_place</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public Vec2 DialogPlace(string place, Vec2 point, Vec2 size)
    {
        int kind =
            place == "centre" ? 1
            : place == "top_left" ? 2
            : 0;
        return (
            (delegate* unmanaged[Cdecl]<int, Vec2, Vec2, Vec2>)api.NativeExport("ui_dialog_place")
        )(kind, point, size);
    }

    /// <summary>Draws wrapped native rich text with inline icon spacing and returns its rendered height.</summary>
    /// <param name="pos">Position in the consuming native UI coordinate system.</param>
    /// <param name="value">Value to read, format, or apply using the representation expected by this operation.</param>
    /// <param name="font">Native font identifier used for text drawing or measurement.</param>
    /// <param name="colour">Native text or drawing colour value expected by the receiving UI operation.</param>
    /// <param name="width">Available or requested width in the consuming native UI coordinate system.</param>
    /// <param name="iconSize">The icon size used by this ddx ui operation.</param>
    /// <param name="gap">The gap used by this ddx ui operation.</param>
    /// <param name="centred">The centred used by this ddx ui operation. Defaults to false.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_rich_text</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float RichText(
        Vec2 pos,
        string value,
        string font,
        string colour,
        float width,
        float iconSize,
        float gap,
        bool centred = false
    )
    {
        byte* t = DDXClient.Utf8(value),
            f = DDXClient.Utf8(font),
            c = DDXClient.Utf8(colour);
        try
        {
            return (
                (delegate* unmanaged[Cdecl]<
                    Vec2,
                    byte*,
                    byte*,
                    byte*,
                    float,
                    float,
                    float,
                    byte,
                    byte,
                    float>)
                    api.NativeExport("ui_rich_text")
            )(pos, t, f, c, width, iconSize, gap, (byte)(centred ? 1 : 0), 1);
        }
        finally
        {
            NativeMemory.Free(t);
            NativeMemory.Free(f);
            NativeMemory.Free(c);
        }
    }

    /// <summary>Draws a level badge using the specified layout entry, centre, size, and level.</summary>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <param name="centre">The centre used by this ddx ui operation.</param>
    /// <param name="size">Dimensions in the consuming native UI coordinate system.</param>
    /// <param name="level">The level used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_level_badge</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void LevelBadge(nint layout, string key, Vec2 centre, Vec2 size, int level)
    {
        byte* text = DDXClient.Utf8(key);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<nint, byte*, Vec2, Vec2, int, void>)
                    api.NativeExport("ui_level_badge")
            )(layout, text, centre, size, level);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Looks up a localization identifier and copies the result into a managed string. Falls back to the identifier when the native lookup returns no text.</summary>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    public string Localize(string id) => Marshal.PtrToStringUTF8((nint)api.Localization(id)) ?? id;

    private byte* Encoded(string value)
    {
        int cap = Math.Max(4096, System.Text.Encoding.UTF8.GetByteCount(value) + 1024);
        byte* buffer = (byte*)NativeMemory.Alloc((nuint)cap);
        int length = System.Text.Encoding.UTF8.GetBytes(value, new Span<byte>(buffer, cap - 1));
        buffer[length] = 0;
        (
            (delegate* unmanaged[Cdecl]<byte*, nuint, void>)
                api.NativeExport("tooltip_encode_colours")
        )(buffer, (nuint)cap);
        return buffer;
    }

    /// <summary>Localizes a template and replaces each supplied keyed placeholder with its managed value.</summary>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <param name="values">The values used by this ddx ui operation.</param>
    public string Format(string id, params (string Key, string Value)[] values)
    {
        string result = Localize(id);
        foreach (var value in values)
            result = result.Replace("{" + value.Key + "}", value.Value, StringComparison.Ordinal);
        return result;
    }

    /// <summary>Reads a string from a native layout, returning the supplied fallback when the key is absent.</summary>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <param name="fallback">Value returned when the requested configuration or layout value is absent or incompatible.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_layout_str</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public string String(nint layout, string key, string fallback)
    {
        byte* k = DDXClient.Utf8(key),
            f = DDXClient.Utf8(fallback);
        try
        {
            return Marshal.PtrToStringUTF8(
                    (nint)
                        (
                            (delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte*>)
                                api.NativeExport("ui_layout_str")
                        )(layout, k, f)
                ) ?? fallback;
        }
        finally
        {
            NativeMemory.Free(k);
            NativeMemory.Free(f);
        }
    }

    /// <summary>Resolves a native font identifier to a borrowed font handle for drawing and measurement.</summary>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_font</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint Font(string id)
    {
        byte* text = DDXClient.Utf8(id);
        try
        {
            return ((delegate* unmanaged[Cdecl]<byte*, nint>)api.NativeExport("ui_font"))(text);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Tests whether the resolved native colour has nonzero RGB components in its packed representation.</summary>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    public bool ColourHasRgb(string id) => (Colour(id) & 0xffffffffffffUL) != 0;

    /// <summary>Resolves a native colour identifier to its packed text-colour representation.</summary>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_colour_hvec4</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public ulong Colour(string id)
    {
        byte* text = DDXClient.Utf8(id);
        try
        {
            return ((delegate* unmanaged[Cdecl]<byte*, ulong>)api.NativeExport("ui_colour_hvec4"))(
                text
            );
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Appends a managed string as a native tooltip content line and returns its line index.</summary>
    /// <param name="content">Borrowed native content address. The native owner must remain alive throughout the call.</param>
    /// <param name="font">Borrowed native font address. The native owner must remain alive throughout the call.</param>
    /// <param name="colour">Native text or drawing colour value expected by the receiving UI operation.</param>
    /// <param name="line">The line used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>tooltip_encode_colours</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int ContentLine(nint content, nint font, ulong colour, string line)
    {
        int cap = Math.Max(4096, System.Text.Encoding.UTF8.GetByteCount(line) + 1024);
        byte* text = (byte*)NativeMemory.Alloc((nuint)cap);
        try
        {
            int length = System.Text.Encoding.UTF8.GetBytes(line, new Span<byte>(text, cap - 1));
            text[length] = 0;
            (
                (delegate* unmanaged[Cdecl]<byte*, nuint, void>)
                    api.NativeExport("tooltip_encode_colours")
            )(text, (nuint)cap);
            return ContentRawLine(content, font, colour, text);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Appends a borrowed UTF-8 string as a native tooltip content line and returns its line index.</summary>
    /// <param name="content">Borrowed native content address. The native owner must remain alive throughout the call.</param>
    /// <param name="font">Borrowed native font address. The native owner must remain alive throughout the call.</param>
    /// <param name="colour">Native text or drawing colour value expected by the receiving UI operation.</param>
    /// <param name="text">Pointer to text data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <remarks>Uses the DDX native contract: <c>item_content_line</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int ContentRawLine(nint content, nint font, ulong colour, byte* text) =>
        (
            (delegate* unmanaged[Cdecl]<nint, nint, ulong, byte*, int>)
                api.NativeExport("item_content_line")
        )(content, font, colour, text);

    /// <summary>Gets the borrowed UTF-8 text pointer of an existing native tooltip content line.</summary>
    /// <param name="content">Borrowed native content address. The native owner must remain alive throughout the call.</param>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>item_content_string</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public byte* ContentString(nint content, int index) =>
        ((delegate* unmanaged[Cdecl]<nint, int, byte*>)api.NativeExport("item_content_string"))(
            content,
            index
        );

    /// <summary>Allocates a native tooltip content layer of the requested kind and region. Populate it only while the owning content object remains valid.</summary>
    /// <param name="content">Borrowed native content address. The native owner must remain alive throughout the call.</param>
    /// <param name="kind">The kind used by this ddx ui operation.</param>
    /// <param name="region">The region used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>item_content_add</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public ItemContentLayer* ContentLayer(nint content, int kind, int region) =>
        (
            (delegate* unmanaged[Cdecl]<nint, int, int, ItemContentLayer*>)
                api.NativeExport("item_content_add")
        )(content, kind, region);

    /// <summary>Measures the native width of a contiguous range of tooltip content lines.</summary>
    /// <param name="content">Borrowed native content address. The native owner must remain alive throughout the call.</param>
    /// <param name="first">The first used by this ddx ui operation.</param>
    /// <param name="count">The count used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>item_content_lines_width</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float ContentWidth(nint content, int first, int count) =>
        (
            (delegate* unmanaged[Cdecl]<nint, int, int, float>)
                api.NativeExport("item_content_lines_width")
        )(content, first, count);

    /// <summary>Measures the wrapped native height of a contiguous range of tooltip content lines at the supplied width.</summary>
    /// <param name="content">Borrowed native content address. The native owner must remain alive throughout the call.</param>
    /// <param name="first">The first used by this ddx ui operation.</param>
    /// <param name="count">The count used by this ddx ui operation.</param>
    /// <param name="width">Available or requested width in the consuming native UI coordinate system.</param>
    /// <remarks>Uses the DDX native contract: <c>item_content_lines_height</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float ContentHeight(nint content, int first, int count, float width) =>
        (
            (delegate* unmanaged[Cdecl]<nint, int, int, float, float>)
                api.NativeExport("item_content_lines_height")
        )(content, first, count, width);

    /// <summary>Tests whether the item appears in the equipped-item list of the current tooltip query.</summary>
    /// <param name="query">Pointer to query data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="item">Borrowed native item address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>item_is_equipped</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool IsEquipped(ItemContentQuery* query, nint item) =>
        (
            (delegate* unmanaged[Cdecl]<ItemContentQuery*, nint, byte>)
                api.NativeExport("item_is_equipped")
        )(query, item) != 0;

    /// <summary>Registers tooltip and description callbacks for a custom native item type. Keep the unmanaged callbacks valid for the registration lifetime.</summary>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="tooltip">The tooltip used by this ddx ui operation.</param>
    /// <param name="describe">The describe used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>item_type_view</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void ItemView(
        string type,
        delegate* unmanaged[Cdecl]<void*, void*, void*, byte> tooltip,
        delegate* unmanaged[Cdecl]<void*, void*, void*, byte> describe
    )
    {
        byte* text = DDXClient.Utf8(type);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<
                    byte*,
                    delegate* unmanaged[Cdecl]<void*, void*, void*, byte>,
                    delegate* unmanaged[Cdecl]<void*, void*, void*, byte>,
                    void*,
                    byte>)
                    api.NativeExport("item_type_view")
            )(text, tooltip, describe, null);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Registers a provider that chooses item tooltip placement from the native placement context.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>item_tips_place</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void ItemTipPlace(delegate* unmanaged[Cdecl]<ItemTipPlace*, void*, Vec2> callback) =>
        (
            (delegate* unmanaged[Cdecl]<
                delegate* unmanaged[Cdecl]<ItemTipPlace*, void*, Vec2>,
                void*,
                void>)
                api.NativeExport("item_tips_place")
        )(callback, null);

    /// <summary>Registers a provider that writes item display names and colour identifiers into native output buffers.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>items_on_display_name</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void DisplayName(
        delegate* unmanaged[Cdecl]<nint, byte*, nuint, byte*, nuint, byte> callback
    ) =>
        (
            (delegate* unmanaged[Cdecl]<
                delegate* unmanaged[Cdecl]<nint, byte*, nuint, byte*, nuint, byte>,
                void>)
                api.NativeExport("items_on_display_name")
        )(callback);

    /// <summary>Configures inline icons and spacing on a native tooltip line.</summary>
    /// <param name="line">Pointer to line data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="path">Texture, layout, or filesystem path recognized by the consuming native service.</param>
    /// <param name="gap">The gap used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_tooltip_line_icons</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void LineIcons(byte* line, string path, float gap)
    {
        byte* text = DDXClient.Utf8(path);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<byte*, byte*, float, void>)
                    api.NativeExport("ui_tooltip_line_icons")
            )(line, text, gap);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Inserts a textured badge and optional label at a byte position in a native tooltip line.</summary>
    /// <param name="line">Pointer to line data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="at">The at used by this ddx ui operation.</param>
    /// <param name="path">Texture, layout, or filesystem path recognized by the consuming native service.</param>
    /// <param name="size">Dimensions in the consuming native UI coordinate system.</param>
    /// <param name="offset">Relative displacement in the consuming native operation&apos;s coordinate system.</param>
    /// <param name="gap">The gap used by this ddx ui operation.</param>
    /// <param name="label">The label used by this ddx ui operation.</param>
    /// <param name="font">Native font identifier used for text drawing or measurement.</param>
    /// <param name="rgba">The rgba used by this ddx ui operation.</param>
    /// <param name="labelOffset">The label offset used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_tooltip_inline_badge</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void InlineBadge(
        byte* line,
        nuint at,
        string path,
        Vec2 size,
        Vec2 offset,
        float gap,
        string label,
        string font,
        uint rgba,
        Vec2 labelOffset
    )
    {
        byte* p = DDXClient.Utf8(path),
            l = DDXClient.Utf8(label),
            f = DDXClient.Utf8(font);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<
                    byte*,
                    nuint,
                    byte*,
                    Vec2,
                    Vec2,
                    float,
                    byte*,
                    byte*,
                    uint,
                    Vec2,
                    void>)
                    api.NativeExport("ui_tooltip_inline_badge")
            )(line, at, p, size, offset, gap, l, f, rgba, labelOffset);
        }
        finally
        {
            NativeMemory.Free(p);
            NativeMemory.Free(l);
            NativeMemory.Free(f);
        }
    }

    /// <summary>Opens a native confirmation dialog with Yes, Always, and No choices. The callback receives the selected option key.</summary>
    /// <param name="question">Localized confirmation text presented by the native dialog.</param>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_confirm_yes_always_no</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void ConfirmYesAlwaysNo(
        string question,
        delegate* unmanaged[Cdecl]<byte*, byte*, void> callback
    )
    {
        byte* text = Encoded(question);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<
                    byte*,
                    delegate* unmanaged[Cdecl]<byte*, byte*, void>,
                    void*,
                    nuint,
                    void>)
                    api.NativeExport("ui_confirm_yes_always_no")
            )(text, callback, null, 0);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Opens a native confirmation dialog with the supplied keyed choices. Keep the unmanaged selection callback valid until the dialog resolves.</summary>
    /// <param name="question">Localized confirmation text presented by the native dialog.</param>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <param name="options">Stable option keys paired with the labels shown by the confirmation dialog.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_confirm</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Confirm(
        string question,
        delegate* unmanaged[Cdecl]<byte*, byte*, void> callback,
        params (string Key, string Label)[] options
    )
    {
        if (options.Length is < 1 or > 8)
            throw new ArgumentOutOfRangeException(nameof(options));
        ConfirmOption* native = stackalloc ConfirmOption[options.Length];
        NativeMemory.Clear(native, (nuint)(sizeof(ConfirmOption) * options.Length));
        byte* text = Encoded(question);
        try
        {
            for (int i = 0; i < options.Length; i++)
                native[i] = new ConfirmOption
                {
                    Key = DDXClient.Utf8(options[i].Key),
                    Label = DDXClient.Utf8(options[i].Label),
                    Callback = callback,
                };
            (
                (delegate* unmanaged[Cdecl]<byte*, ConfirmOption*, int, void*, nuint, void>)
                    api.NativeExport("ui_confirm")
            )(text, native, options.Length, null, 0);
        }
        finally
        {
            NativeMemory.Free(text);
            for (int i = 0; i < options.Length; i++)
            {
                NativeMemory.Free(native[i].Key);
                NativeMemory.Free(native[i].Label);
            }
        }
    }

    /// <summary>Registers a callback when a native tooltip is shown, allowing callers to augment its content.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_tooltip_on_show</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void OnTooltip(delegate* unmanaged[Cdecl]<nint, nint, nint, void> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, nint, nint, void>, void>)
                api.NativeExport("ui_tooltip_on_show")
        )(callback);

    /// <summary>Loads a native layout from an explicit filesystem path and returns its borrowed layout handle.</summary>
    public nint LayoutFile(string path)
    {
        byte* text = DDXClient.Utf8(path);
        try
        {
            return ((delegate* unmanaged[Cdecl]<byte*, nint>)api.NativeExport("ui_layout_load"))(
                text
            );
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Resolves a native layout by its game layout filename and returns its borrowed handle.</summary>
    /// <param name="file">Layout filename recognized by the native game or client layout directory.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_layout_mod</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint Layout(string file)
    {
        byte* text = DDXClient.Utf8(file);
        try
        {
            return ((delegate* unmanaged[Cdecl]<byte*, nint>)api.NativeExport("ui_layout_mod"))(
                text
            );
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Reads a numeric native layout field, returning the supplied fallback when absent.</summary>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <param name="fallback">Value returned when the requested configuration or layout value is absent or incompatible.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_layout_num</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float Number(nint layout, string key, float fallback)
    {
        byte* text = DDXClient.Utf8(key);
        try
        {
            return (
                (delegate* unmanaged[Cdecl]<nint, byte*, float, float>)
                    api.NativeExport("ui_layout_num")
            )(layout, text, fallback);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Reads a two-component native layout field, returning the supplied fallback when absent.</summary>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <param name="fallback">Value returned when the requested configuration or layout value is absent or incompatible.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_layout_vec2</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public Vec2 Vector(nint layout, string key, Vec2 fallback)
    {
        byte* text = DDXClient.Utf8(key);
        try
        {
            return (
                (delegate* unmanaged[Cdecl]<nint, byte*, Vec2, Vec2>)
                    api.NativeExport("ui_layout_vec2")
            )(layout, text, fallback);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Loads a native texture descriptor into caller-supplied ABI storage. Returns whether the texture resolves successfully.</summary>
    /// <param name="path">Texture, layout, or filesystem path recognized by the consuming native service.</param>
    /// <param name="output">Pointer to output data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_texture</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool Texture(string path, byte* output)
    {
        byte* text = DDXClient.Utf8(path);
        try
        {
            return ((delegate* unmanaged[Cdecl]<byte*, byte*, byte>)api.NativeExport("ui_texture"))(
                    text,
                    output
                ) != 0;
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Gets a borrowed native sparkle sprite for the requested trinket display kind and index.</summary>
    /// <param name="kind">The kind used by this ddx ui operation.</param>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_trinket_sparkle</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint TrinketSparkle(int kind, uint index) =>
        ((delegate* unmanaged[Cdecl]<int, uint, nint>)api.NativeExport("ui_trinket_sparkle"))(
            kind,
            index
        );

    /// <summary>Configures the layout entry used to position native custom equipment slots.</summary>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <remarks>Uses the DDX native contract: <c>equip_slots_layout</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void EquipmentLayout(nint layout, string key)
    {
        byte* text = DDXClient.Utf8(key);
        try
        {
            ((delegate* unmanaged[Cdecl]<nint, byte*, void>)api.NativeExport("equip_slots_layout"))(
                layout,
                text
            );
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Registers a callback when native equipment slots are shown so client state can be refreshed.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>equip_slots_on_show</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void EquipmentOnShow(delegate* unmanaged[Cdecl]<nint, void> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, void>, void>)
                api.NativeExport("equip_slots_on_show")
        )(callback);

    /// <summary>Registers a provider for the native hero currently used by town item interfaces.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>items_town_hero</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void TownHero(delegate* unmanaged[Cdecl]<nint> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint>, void>)
                api.NativeExport("items_town_hero")
        )(callback);

    /// <summary>Registers a native texture-path redirection callback. Respect the output buffer capacity and the returned pointer lifetime.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_texture_redirect</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void RedirectTextures(delegate* unmanaged[Cdecl]<byte*, byte*, nuint, byte*> callback) =>
        (
            (delegate* unmanaged[Cdecl]<
                delegate* unmanaged[Cdecl]<byte*, byte*, nuint, byte*>,
                void>)
                api.NativeExport("ui_texture_redirect")
        )(callback);

    /// <summary>Configures a custom item type&apos;s native inventory grid from a layout entry.</summary>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <remarks>Uses the DDX native contract: <c>ui_invgrid_scroll</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void InventoryGrid(string type, nint layout, string key)
    {
        byte* t = DDXClient.Utf8(type),
            k = DDXClient.Utf8(key);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<byte*, nint, byte*, void>)
                    api.NativeExport("ui_invgrid_scroll")
            )(t, layout, k);
        }
        finally
        {
            NativeMemory.Free(t);
            NativeMemory.Free(k);
        }
    }

    /// <summary>Registers a provider for additional native inventory slots for a custom item type.</summary>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="provider">The provider used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>inv_slots_provider</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void InventoryExtraSlots(string type, delegate* unmanaged[Cdecl]<int> provider)
    {
        byte* text = DDXClient.Utf8(type);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<byte*, delegate* unmanaged[Cdecl]<int>, void>)
                    api.NativeExport("inv_slots_provider")
            )(text, provider);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Registers a native stash tab definition. Strings, callbacks, and user data must remain valid while registered.</summary>
    /// <param name="definition">Pointer to definition data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <remarks>Uses the DDX native contract: <c>stash_tab_register</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool StashTab(StashTabDef* definition) =>
        ((delegate* unmanaged[Cdecl]<StashTabDef*, byte>)api.NativeExport("stash_tab_register"))(
            definition
        ) != 0;

    /// <summary>Registers a predicate determining whether the custom native stash view is active.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>stash_view_active</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void StashActive(delegate* unmanaged[Cdecl]<byte> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<byte>, void>)
                api.NativeExport("stash_view_active")
        )(callback);

    /// <summary>Configures native stash tabs and scrolling using the supplied layouts and keys.</summary>
    /// <param name="tabs">Borrowed native tabs address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <param name="scroll">Borrowed native scroll address. The native owner must remain alive throughout the call.</param>
    /// <param name="scrollKey">The scroll key used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>stash_view_layout</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void StashLayout(nint tabs, string key, nint scroll, string scrollKey)
    {
        byte* k = DDXClient.Utf8(key),
            s = DDXClient.Utf8(scrollKey);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<nint, byte*, nint, byte*, void>)
                    api.NativeExport("stash_view_layout")
            )(tabs, k, scroll, s);
        }
        finally
        {
            NativeMemory.Free(k);
            NativeMemory.Free(s);
        }
    }

    /// <summary>Configures the native sale panel from a layout entry.</summary>
    /// <param name="layout">Borrowed native layout address. The native owner must remain alive throughout the call.</param>
    /// <param name="key">Named configuration field or layout entry to read.</param>
    /// <remarks>Uses the DDX native contract: <c>item_sale_layout</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void SaleLayout(nint layout, string key)
    {
        byte* text = DDXClient.Utf8(key);
        try
        {
            ((delegate* unmanaged[Cdecl]<nint, byte*, void>)api.NativeExport("item_sale_layout"))(
                layout,
                text
            );
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Registers callbacks around native stash panel drawing and interaction.</summary>
    /// <param name="before">The before used by this ddx ui operation.</param>
    /// <param name="after">The after used by this ddx ui operation.</param>
    /// <remarks>Uses the DDX native contract: <c>stash_view_on_panel</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void StashOnPanel(
        delegate* unmanaged[Cdecl]<nint, void> before,
        delegate* unmanaged[Cdecl]<nint, void> after
    ) =>
        (
            (delegate* unmanaged[Cdecl]<
                delegate* unmanaged[Cdecl]<nint, void>,
                delegate* unmanaged[Cdecl]<nint, void>,
                void>)
                api.NativeExport("stash_view_on_panel")
        )(before, after);
}
