using System.Runtime.InteropServices;

namespace DDX.Managed;

/// <summary>Combat operations that preserve the game's effect queue, resistance checks, and follow-up resolver.</summary>
/// <param name="api">Shared DDX API used to access the native host. It must be initialized before this component is used.</param>
public sealed unsafe class DDXCombat(DDXApi api)
{
    /// <summary>Resolves a configured native effect by its identifier for later combat application.</summary>
    /// <param name="name">Identifier of the native service, field, or named value being addressed.</param>
    /// <remarks>Uses the DDX native contract: <c>find_effect</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint FindEffect(string name)
    {
        byte* text = DDXClient.Utf8(name);
        try
        {
            return ((delegate* unmanaged[Cdecl]<byte*, nint>)api.NativeExport("find_effect"))(text);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Applies a native effect with explicit performer and target while preserving the source hook&apos;s combat resolution context.</summary>
    /// <param name="source">Pointer to source data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="performer">Borrowed native performer address. The native owner must remain alive throughout the call.</param>
    /// <param name="target">Borrowed native target address. The native owner must remain alive throughout the call.</param>
    /// <param name="effect">Borrowed native effect address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>combat_apply_effect</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ApplyEffect(HookContext* source, nint performer, nint target, nint effect) =>
        (
            (delegate* unmanaged[Cdecl]<HookContext*, nint, nint, nint, byte>)
                api.NativeExport("combat_apply_effect")
        )(source, performer, target, effect) != 0;

    /// <summary>Applies an additional native effect within the supplied hook&apos;s side-effect context.</summary>
    /// <param name="source">Pointer to source data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="effect">Borrowed native effect address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>combat_apply_side_effect</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool ApplySideEffect(HookContext* source, nint effect) =>
        (
            (delegate* unmanaged[Cdecl]<HookContext*, nint, byte>)
                api.NativeExport("combat_apply_side_effect")
        )(source, effect) != 0;

    /// <summary>Requests a scaled follow-up hit through the native combat resolver. The filter controls eligible targets and receives the supplied opaque user data.</summary>
    /// <param name="source">Pointer to source data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation.</param>
    /// <param name="target">Borrowed native target address. The native owner must remain alive throughout the call.</param>
    /// <param name="scale">The scale used by this ddx combat operation.</param>
    /// <param name="crit">The crit used by this ddx combat operation.</param>
    /// <param name="filter">The filter used by this ddx combat operation.</param>
    /// <param name="userdata">Pointer to userdata data in the declared native ABI format. Keep the pointed-to storage valid for the duration required by the native operation. Defaults to null.</param>
    /// <remarks>Uses the DDX native contract: <c>combat_apply_followup_hit</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool FollowupHit(
        HookContext* source,
        nint target,
        float scale,
        bool crit,
        delegate* unmanaged[Cdecl]<nint, void*, byte> filter,
        void* userdata = null
    ) =>
        (
            (delegate* unmanaged[Cdecl]<
                HookContext*,
                nint,
                float,
                byte,
                delegate* unmanaged[Cdecl]<nint, void*, byte>,
                void*,
                byte>)
                api.NativeExport("combat_apply_followup_hit")
        )(source, target, scale, (byte)(crit ? 1 : 0), filter, userdata) != 0;

    /// <summary>Draws an integer between the supplied inclusive bounds from the native random stream. A zero stream address selects the native global stream.</summary>
    /// <param name="rng">Borrowed native rng address. The native owner must remain alive throughout the call.</param>
    /// <param name="min">The min used by this ddx combat operation.</param>
    /// <param name="max">Maximum number of entries or native objects the operation may produce.</param>
    /// <remarks>Uses the DDX native contract: <c>game_random_int</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int RandomInt(nint rng, int min, int max) =>
        ((delegate* unmanaged[Cdecl]<nint, int, int, int>)api.NativeExport("game_random_int"))(
            rng,
            min,
            max
        );

    /// <summary>Gets the writable native minimum/maximum pair for a resistance type, or null when unsupported. Callers changing it must restore the original values at the appropriate lifecycle boundary.</summary>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="flag">The flag used by this ddx combat operation.</param>
    /// <remarks>Uses the DDX native contract: <c>game_resist_clamp</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public float* ResistanceClamp(int type, bool flag) =>
        ((delegate* unmanaged[Cdecl]<int, byte, float*>)api.NativeExport("game_resist_clamp"))(
            type,
            (byte)(flag ? 1 : 0)
        );

    /// <summary>Gets the native identifier for the current skill use, allowing callbacks to deduplicate feedback within one resolution.</summary>
    /// <remarks>Uses the DDX native contract: <c>skill_use_id</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint SkillUseId =>
        ((delegate* unmanaged[Cdecl]<uint>)api.NativeExport("skill_use_id"))();

    /// <summary>Gets the number of targets in the current native skill resolution.</summary>
    /// <remarks>Uses the DDX native contract: <c>skill_resolution_target_count</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public int TargetCount =>
        ((delegate* unmanaged[Cdecl]<int>)api.NativeExport("skill_resolution_target_count"))();

    /// <summary>Gets the borrowed native actor address at a target index in the current skill resolution.</summary>
    /// <param name="index">Zero-based index into the collection addressed by this operation.</param>
    /// <remarks>Uses the DDX native contract: <c>skill_resolution_target</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint Target(int index) =>
        ((delegate* unmanaged[Cdecl]<int, nint>)api.NativeExport("skill_resolution_target"))(index);

    /// <summary>Gets the borrowed actor address performing the current native skill resolution.</summary>
    /// <remarks>Uses the DDX native contract: <c>skill_resolution_performer</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint SkillPerformer =>
        ((delegate* unmanaged[Cdecl]<nint>)api.NativeExport("skill_resolution_performer"))();

    /// <summary>Gets the borrowed skill address in the current native resolution.</summary>
    /// <remarks>Uses the DDX native contract: <c>skill_resolution_skill</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint Skill =>
        ((delegate* unmanaged[Cdecl]<nint>)api.NativeExport("skill_resolution_skill"))();

    /// <summary>Reads the native identifier hash of a borrowed skill.</summary>
    /// <param name="skill">Borrowed native skill address. The native owner must remain alive throughout the call.</param>
    /// <remarks>Uses the DDX native contract: <c>skill_id_hash</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public uint SkillHash(nint skill) =>
        ((delegate* unmanaged[Cdecl]<nint, uint>)api.NativeExport("skill_id_hash"))(skill);

    /// <summary>Gets the actor&apos;s last-used native skill and its use identifier through the output parameter.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="use">Receives the use produced by the operation. Check the return value before consuming this output.</param>
    /// <remarks>Uses the DDX native contract: <c>skill_last_used</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public nint LastSkill(nint actor, out uint use)
    {
        uint value = 0;
        nint skill = (
            (delegate* unmanaged[Cdecl]<nint, uint*, nint>)api.NativeExport("skill_last_used")
        )(actor, &value);
        use = value;
        return skill;
    }

    /// <summary>Registers an unmanaged callback that supplies temporary effect-field patches during native resolution. Keep callback storage valid for the registration lifetime.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>effects_on_patch</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void OnPatch(
        delegate* unmanaged[Cdecl]<nint, nint, nint, EffectPatch*, int, int> callback
    ) =>
        (
            (delegate* unmanaged[Cdecl]<
                delegate* unmanaged[Cdecl]<nint, nint, nint, EffectPatch*, int, int>,
                void>)
                api.NativeExport("effects_on_patch")
        )(callback);

    /// <summary>Registers an unmanaged callback that chooses replacement effect identifiers during native resolution. Returned UTF-8 text must remain valid while DDX consumes it.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>effects_on_swap</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void OnSwap(delegate* unmanaged[Cdecl]<nint, nint, nint, byte*> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, nint, nint, byte*>, void>)
                api.NativeExport("effects_on_swap")
        )(callback);

    /// <summary>Registers an unmanaged callback that supplies the minimum friendly-heal amount during native stat calculation.</summary>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>stats_on_friendly_heal_min</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void OnFriendlyHealMin(delegate* unmanaged[Cdecl]<nint, nint, int> callback) =>
        (
            (delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<nint, nint, int>, void>)
                api.NativeExport("stats_on_friendly_heal_min")
        )(callback);

    /// <summary>Registers an unmanaged handler for effect identifiers beginning with the supplied prefix. The callback remains available for native effect dispatch.</summary>
    /// <param name="prefix">The prefix used by this ddx combat operation.</param>
    /// <param name="callback">Unmanaged callback with the declared calling convention. Keep its code and captured context alive for the native registration lifetime.</param>
    /// <remarks>Uses the DDX native contract: <c>effects_on_prefix</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void OnEffectPrefix(
        string prefix,
        delegate* unmanaged[Cdecl]<byte*, nint, nint, void> callback
    )
    {
        byte* text = DDXClient.Utf8(prefix);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<
                    byte*,
                    delegate* unmanaged[Cdecl]<byte*, nint, nint, void>,
                    void>)
                    api.NativeExport("effects_on_prefix")
            )(text, callback);
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Queues the specified native bark for the actor and reports whether it is accepted.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="id">Stable identifier recognized by the consuming native registry or catalog.</param>
    /// <remarks>Uses the DDX native contract: <c>bark_queue</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public bool Bark(nint actor, string id)
    {
        byte* text = DDXClient.Utf8(id);
        try
        {
            return ((delegate* unmanaged[Cdecl]<nint, byte*, byte>)api.NativeExport("bark_queue"))(
                    actor,
                    text
                ) != 0;
        }
        finally
        {
            NativeMemory.Free(text);
        }
    }

    /// <summary>Shows native combat popup text for the supplied actor and popup type.</summary>
    /// <param name="actor">Borrowed native actor address. The native owner must remain alive throughout the call.</param>
    /// <param name="type">Native type identifier interpreted by the receiving operation.</param>
    /// <param name="text">Managed text to pass to the operation; native calls encode it as UTF-8.</param>
    /// <remarks>Uses the DDX native contract: <c>popup_text</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void Popup(nint actor, int type, string text)
    {
        byte* value = DDXClient.Utf8(text);
        try
        {
            ((delegate* unmanaged[Cdecl]<nint, int, byte*, void>)api.NativeExport("popup_text"))(
                actor,
                type,
                value
            );
        }
        finally
        {
            NativeMemory.Free(value);
        }
    }

    /// <summary>Associates a named effect with optional native popup and source-label text for subsequent buff feedback.</summary>
    /// <param name="target">Borrowed native target address. The native owner must remain alive throughout the call.</param>
    /// <param name="effect">The effect used by this ddx combat operation.</param>
    /// <param name="popup">The popup used by this ddx combat operation.</param>
    /// <param name="label">The label used by this ddx combat operation.</param>
    /// <remarks>Uses the DDX native contract: <c>buff_source_effect</c>. Call on the game thread with live borrowed handles; native declarations define pointer ownership and callback lifetimes.</remarks>
    public void NameEffect(nint target, string effect, string? popup, string? label)
    {
        byte* e = DDXClient.Utf8(effect),
            p = popup is null ? null : DDXClient.Utf8(popup),
            l = label is null ? null : DDXClient.Utf8(label);
        try
        {
            (
                (delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte*, void>)
                    api.NativeExport("buff_source_effect")
            )(target, e, p, l);
        }
        finally
        {
            NativeMemory.Free(e);
            NativeMemory.Free(p);
            NativeMemory.Free(l);
        }
    }
}
