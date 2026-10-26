namespace DDX.Managed;

/// <summary>Identifies the native origin category of an actor buff.</summary>
/// <remarks>
/// Values match the 34 entries in Buff::k_SourceData used by Buff::GetSourceId and the DDX native adapter.
/// Equipment retains its managed name for the native bsrc_trinket category. These values are categories, not flags.
/// </remarks>
public enum BuffSource
{
    /// <summary>Native source <c>bsrc_notspecified</c>.</summary>
    NotSpecified = 0,

    /// <summary>Native source <c>bsrc_skill</c>.</summary>
    Skill = 1,

    /// <summary>Native source <c>bsrc_instantSkill</c>.</summary>
    InstantSkill = 2,

    /// <summary>Native source <c>bsrc_affliction</c>.</summary>
    Affliction = 3,

    /// <summary>Native source <c>bsrc_virtue</c>.</summary>
    Virtue = 4,

    /// <summary>Native source <c>bsrc_quirk</c>.</summary>
    Quirk = 5,

    /// <summary>Native source <c>bsrc_trinket</c>.</summary>
    Equipment = 6,

    /// <summary>Native source <c>bsrc_trinket_set</c>.</summary>
    TrinketSet = 7,

    /// <summary>Native source <c>bsrc_campingskill</c>.</summary>
    CampingSkill = 8,

    /// <summary>Native source <c>bsrc_deathsdoor</c>.</summary>
    DeathsDoor = 9,

    /// <summary>Native source <c>bsrc_town</c>.</summary>
    Town = 10,

    /// <summary>Native source <c>bsrc_curio</c>.</summary>
    Curio = 11,

    /// <summary>Native source <c>bsrc_stun</c>.</summary>
    Stun = 12,

    /// <summary>Native source <c>bsrc_guard</c>.</summary>
    Guard = 13,

    /// <summary>Native source <c>bsrc_riposte</c>.</summary>
    Riposte = 14,

    /// <summary>Native source <c>bsrc_item</c>.</summary>
    Item = 15,

    /// <summary>Native source <c>bsrc_disease</c>.</summary>
    Disease = 16,

    /// <summary>Native source <c>bsrc_companion</c>.</summary>
    Companion = 17,

    /// <summary>Native source <c>bsrc_deathsdoor_recovery</c>.</summary>
    DeathsDoorRecovery = 18,

    /// <summary>Native source <c>bsrc_deathsdoor_recovery_heart_attack</c>.</summary>
    DeathsDoorRecoveryHeartAttack = 19,

    /// <summary>Native source <c>bsrc_quest_failure</c>.</summary>
    QuestFailure = 20,

    /// <summary>Native source <c>bsrc_completed_darkest_dungeon_quest_party_hero</c>.</summary>
    CompletedDarkestDungeonQuestPartyHero = 21,

    /// <summary>Native source <c>bsrc_town_event</c>.</summary>
    TownEvent = 22,

    /// <summary>Native source <c>bsrc_vampire</c>.</summary>
    Vampire = 23,

    /// <summary>Native source <c>bsrc_never_again</c>.</summary>
    NeverAgain = 24,

    /// <summary>Native source <c>bsrc_battle_modifier</c>.</summary>
    BattleModifier = 25,

    /// <summary>Native source <c>bsrc_district</c>.</summary>
    District = 26,

    /// <summary>Native source <c>bsrc_torchsettings</c>.</summary>
    TorchSettings = 27,

    /// <summary>Native source <c>bsrc_flashback_start</c>.</summary>
    FlashbackStart = 28,

    /// <summary>Native source <c>bsrc_flashback_result</c>.</summary>
    FlashbackResult = 29,

    /// <summary>Native source <c>bsrc_crit</c>.</summary>
    Crit = 30,

    /// <summary>Native source <c>bsrc_trinket_additional_effect</c>.</summary>
    TrinketAdditionalEffect = 31,

    /// <summary>Native source <c>bsrc_quest_modifier</c>.</summary>
    QuestModifier = 32,

    /// <summary>Native source <c>bsrc_last_hero</c>.</summary>
    LastHero = 33,
}
