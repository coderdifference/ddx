// game_layout.h: where things are in the game's structures, for both builds (doc 18). Every offset, size and vtable
// slot the library and its clients read from the game lives here, once, as GL(macOS, Windows).
//
// macOS values come from the symbolled binary. The Windows (MSVC) layout differs wherever a std::string (32 bytes,
// libc++ 24), std::function (64, libc++ 48) or std::map (16, libc++ 24) comes earlier in the structure, so each Windows
// value is read from that build's disassembly:
//   a number          verified on Windows
//   WIN_AS_MAC(v)     live Windows code uses the macOS value; not verified there yet
//   WIN_UNKNOWN       not known: an undeclared name, so Windows code that uses it doesn't compile (a module still
//                     compiled out on Windows can use it; porting the module brings each one up)
// Offsets in a game function's code (a call site, a stack slot) stay with the code that patches it.
#ifndef GAME_LAYOUT_H
#define GAME_LAYOUT_H

#ifdef _WIN32
#define GL(mac, win) (win)
#else
#define GL(mac, win) (mac)
#endif
#define WIN_AS_MAC(v) (v)
#define WIN_UNKNOWN   windows_layout_value_not_verified

// ---------------------------------------------------------------- Actor (heroes and monsters)
#define ACTOR_NAME               GL(0x08, WIN_AS_MAC(0x08)) // display name (FixedLengthString, text first)
#define ACTOR_DISPLAY_DATA       GL(0x78, 0x78)             // ActorDisplayData (GetTrayIconData)
#define ACTOR_BUFF_GROUP         GL(0x460, 0x460)           // Buff::Group: vector<Buff::GroupEntry>
#define ACTOR_STATS              GL(0x490, 0x490)           // combat stats, ACTOR_STAT_SIZE each
#define ACTOR_STAT_SIZE          GL(0x68, 0x68)
#define ACTOR_RANK               GL(0x1018, 0x1018) // rank (party position)
#define ACTOR_HP                 GL(0x1038, 0x1038) // current HP (float)
#define ACTOR_SPRITE             GL(0x1058, 0x1050) // SpineSprite* (SetSpriteAnimation, SetStance)
#define ACTOR_EFFECT_SET         GL(0x11b0, 0x11d8) // a skill's effect list to use (GetSkillEffects)
#define ACTOR_TRAIT              GL(0x11b8, 0x11e0) // the trait's id hash: affliction, else 0
#define ACTOR_TRAIT_ALT          GL(0x11bc, 0x11e4) // ... virtue
#define ACTOR_PENDING_AFFLICTION GL(0x11c0, 0x11e8)
#define ACTOR_PENDING_VIRTUE     GL(0x11c4, 0x11ec)
#define ACTOR_TRINKETS           GL(0x1200, 0x1210) // Inventory::System of equipped trinkets
#define ACTOR_STRESS             GL(0x1274, 0x128c) // current stress (float)
// Actor vtable, byte offsets
#define ACTOR_GET_CLASS   GL(0x38, 0x30)   // GetClass()
#define ACTOR_IS_DEAD     GL(0x160, 0x158) // IsDead()
#define ACTOR_BARK_CURSED GL(0x168, 0x160) // vtable: true when its barks can show cursed
                                           // (GetBarkType)

// ---------------------------------------------------------------- Hero, HeroClass, quirks
#define HERO_NO_QUIRK_ACT_OUTS GL(0x127e, 0x1296) // bool: quirks' act-outs left out (and no
                                                  // death loot: client_example)
#define HERO_CLASS          GL(0x1298, 0x12b0)    // HeroClass* (Hero::Save's "heroClass")
#define HERO_WEAPON_LEVEL   GL(0x12a4, 0x12bc)
#define HERO_ARMOUR_LEVEL   GL(0x12a8, 0x12c0)
#define HERO_ROSTER_ID      GL(0x12f4, 0x130c)       // Camping::SkillLogEntryId's first member
#define HERO_QUIRKS         GL(0x12f8, 0x1310)       // std::vector<Hero::QuirkInstance>
#define HERO_RESOLVE_SLOT   GL(0x170 / 8, 0x168 / 8) // vtable slot: resolve level (float)
#define QUIRK_SIZE          GL(0x38, 0x38)           // Hero::QuirkInstance: Quirk::Class const* at 0
#define QUIRK_LOCKED        GL(0x2a, 0x2a)
#define QUIRKCLASS_HASH     GL(0x40, 0x40) // its id's hash (Hero::GetQuirkIndex)
#define QUIRKCLASS_POSITIVE GL(0x50, 0x50)
#define QUIRKCLASS_DISEASE  GL(0x51, 0x51)             // (Hero::AddQuirk 0x5c45a0)
#define QUIRKCLASS_BUFFS    GL(0xb0, 0xa8)             // std::vector<Buff::Instance>
#define QUIRKCLASS_ACT_OUTS GL(0x1c8, 0x1c0)           // ActOut::Data
#define CLASS_ID            GL(0x08, WIN_AS_MAC(0x08)) // HeroClass / ActorClass: id (char[])
#define CLASS_ID_HASH       GL(0xd0, WIN_AS_MAC(0xd0)) // HeroClass: id hash (Houndmaster check)
#define CLASS_BUY           GL(0x9c, 0x9c)             // Inventory::ItemClass: price to buy, to sell
#define CLASS_SELL          GL(0xa4, 0xa4)
#define CLASS_CRIT_EFFECTS  GL(0x568, 0x560) // ActorClass: std::vector<Effect const*> (Windows:
                                             // Actor::GetSkillEffects 0x474cd0)
#define CLASS_WEAPONS    GL(0xba8, 0xba0)    // HeroClass: weapon rows, WEAPON_SIZE each
#define CLASS_ARMOURS    GL(0xbc0, 0xbb8)    // HeroClass: armour rows, ARMOUR_SIZE each
#define CLASS_SKILLS     GL(0xbd8, 0xbd0)    // HeroClass: vector of vector<HeroCombatSkill>
#define WEAPON_SIZE      GL(0x130, WIN_AS_MAC(0x130))
#define WEAPON_DMG_MIN   GL(0x11c, WIN_AS_MAC(0x11c))
#define WEAPON_DMG_MAX   GL(0x120, WIN_AS_MAC(0x120))
#define ARMOUR_SIZE      GL(0x128, WIN_AS_MAC(0x128))
#define ROW_FIELDS       GL(0x118, WIN_AS_MAC(0x118)) // a weapon or armour row's first stat field
#define ROW_FIELDS_LEN   GL(0x14, WIN_AS_MAC(0x14))   // weapon: 0x118-0x12b; armour uses 0x118-0x127
#define MONSTER_CLASS    GL(0x13e0, 0x13f8)
#define TRAIT_HASH       GL(0x40, 0x40) // Trait::Type
#define TRAIT_OVERSTRESS GL(0x4c, 0x4c) // overstress_type's hash
#define TRAIT_ACT_OUTS   GL(0xd0, 0xd0) // ActOut::Data

// ---------------------------------------------------------------- skills
#define SKILL_ID           GL(0x08, WIN_AS_MAC(0x08)) // ActorCombatSkill: id (inline)
#define SKILL_ID_HASH      GL(0x4c, 0x4c)             // (battle-limited uses key)
#define SKILL_LEVEL        GL(0x48, WIN_AS_MAC(0x48)) // HeroCombatSkill::GetLevel (0-based)
#define SKILL_TYPE         GL(0x50, 0x50)             // .type (inline: "melee", "ranged")
#define SKILL_BATTLE_LIMIT GL(0x98, 0x98)             // .per_battle_limit (0: none)
#define SKILL_MOVE_BACK    GL(0x35c, 0x35c)           // int: .move <back> <forward>
#define SKILL_MOVE_FORWARD GL(0x360, 0x360)
#define SKILL_HEAL_MIN     GL(0x364, WIN_AS_MAC(0x364))
#define SKILL_HEAL_MAX     GL(0x368, WIN_AS_MAC(0x368))
#define SKILL_ACC          GL(0x380, WIN_AS_MAC(0x380)) // .atk (float, 0.85 = 85)
#define SKILL_CRIT         GL(0x384, WIN_AS_MAC(0x384)) // .crit (float)
#define SKILL_IS_RIPOSTE   GL(0x3ad, 0x3ad)             // GetIsRiposteSkill / SetIsRiposteSkill
#define SKILL_BASE_EFFECTS GL(0x3d0, 0x3c8)             // (ActorCombatSkill::LoadDefinition's ".effect")
#define SKILL_BASE_DMG     GL(0x43c, 0x41c)             // .dmg (GetBaseDamageModifier)
#define SKILL_LEVEL_SIZE   GL(0x530, 0x508)             // HeroCombatSkill

// ---------------------------------------------------------------- effects and buffs
#define EFFECT_HEAL          GL(0xc8, WIN_AS_MAC(0xc8)) // .heal (float; LoadSideEffects)
#define EFFECT_HEALSTRESS    GL(0xd4, 0xd4)             // (Windows: LoadSideEffects 0x4e724e)
#define EFFECT_DOT_POISON    GL(0x130, 0x130)
#define EFFECT_DOT_BLEED     GL(0x134, 0x134)
#define EFFECT_DURATION      GL(0x154, WIN_AS_MAC(0x154))
#define EFFECT_BUFF_HASHES   GL(0x348, WIN_AS_MAC(0x348)) // std::vector<unsigned> buff id hashes
#define EFFECT_SWAP          GL(0x360, WIN_AS_MAC(0x360)) // "swap source and target"; the name is at 0
#define EFFECT_DURATION_TYPE GL(0x3b0, WIN_AS_MAC(0x3b0)) // .buff_duration_type (DoT: before_turn)
#define BUFF_ENTRY_SIZE      GL(0x1d8, 0x1d8)             // Buff::GroupEntry: guid at 0, Buff::Instance
#define BUFF_ENTRY_INSTANCE  GL(8, WIN_AS_MAC(8))         // at 8
#define BUFF_ENTRY_SOURCE    GL(0x5c, WIN_AS_MAC(0x5c))   // = BUFF_ENTRY_INSTANCE + BUFF_SOURCE
#define BUFF_ENTRY_SOURCE_ID GL(0x60, 0x60)               // = BUFF_ENTRY_INSTANCE + BUFF_SOURCE_ID
#define BUFF_ENTRY_ID        GL(0x69, 0x69)               // = BUFF_ENTRY_INSTANCE + BUFF_ID
#define BUFF_INSTANCE_SIZE   GL(0x1d0, 0x1d0)
#define BUFF_STAT            GL(0x00, 0x00) // Buff::Instance: Buff::Stat::Types
#define BUFF_AMOUNT          GL(0x48, 0x48)
#define BUFF_DURATION        GL(0x4c, 0x4c) // amount left (rounds, battles; -1: none)
#define BUFF_DURATION_TYPE   GL(0x50, 0x50) // Buff::Duration
#define BUFF_SOURCE          GL(0x54, 0x54) // Buff::Source
#define BUFF_SOURCE_ID       GL(0x58, 0x58) // trinket id hash, quirk class hash...
#define BUFF_ID              GL(0x61, 0x61) // id (inline; saved as "id")

// ---------------------------------------------------------------- raid, party, battle
#define RAID_HEROES          GL(0x10, 0x08)     // vector<Hero*> (= the party's, which comes first)
#define RAID_PARTY           GL(0x10, 0x08)     // Party (embedded; selected hero at PARTY_SELECTED)
#define RAID_PARTY_INVENTORY GL(0x220, 0x218)   // Inventory::System (NextInitiative 0x5a73b7)
#define RAID_BATTLE          GL(0x4850, 0x4898) // Battle (embedded; Battle::IsReranking on
                                                // raid+0x4850; Windows 0x481100 passes raid+0x4898)
#define RAID_MONSTERS GL(0x4888, 0x48d0)
#define RAID_LOOT     GL(0x5100, 0x5140)       // Loot::System (Windows:
                                               // CurioInteractionQuestItemResult::Apply)
#define RAID_QUEST          GL(0x5280, 0x52c0) // Quest::RaidInstance* (Windows: the inlined ShowTrap)
#define QUEST_IS_PLOT       GL(0x104, 0x104)   // Quest::GeneratedQuest::is_plot_quest
#define RAID_AREA           GL(0x5590, 0x55d0) // Area* (Raid::GetArea; Windows: scouting's Apply)
#define RAID_TORCH          GL(0x5ec4, 0x5f14)
#define PARTY_LAST_ACT_OUT  GL(0x3d0, 0x428) // the last turn-start act-out (Battle::SetLastActOut)
#define PARTY_SELECTED      GL(0x3d8, WIN_AS_MAC(0x3d8))
#define BATTLE_ROUND        GL(0xa8, 0xa8)
#define BATTLE_RIPOSTES     GL(0x258, 0x270) // std::vector<Battle::Attack> RiposteAttempt queued
#define BATTLE_STATE        GL(0x270, 0x288) // BattleState (Battle::GetState; 0: none)
#define BATTLE_RNG          GL(0x2e8, 0x2f0) // prng_minstd_wrapped (RiposteAttempt's rolls)
#define RESIST_TYPES        GL(8, 8)         // ResistanceType::k_Data rows (stun .. trap)
#define RESIST_DATA_SIZE    GL(0xb8, 0xb8)
#define RESIST_CLAMP        GL(0x84, 0x84)     // {min, max}; flag true: +8 (ClampResistance)
#define RNG_RANDOM_INT_SLOT GL(6, 5)           // prng_minstd_wrapped vtable: random_int(int, int)
                                               // (MSVC puts it before random_int(unsigned))
#define ATTACK_TARGETS          GL(0x08, 0x08) // Battle::Attack: std::vector<TargetData>
#define ATTACK_PERFORMER        GL(0x20, 0x20)
#define ATTACK_SKILL            GL(0x28, 0x28)
#define ATTACK_SIZE             GL(0x58, 0x58)
#define TARGET_ACTOR            GL(0x00, 0x00) // Battle::Attack::TargetData
#define TARGET_HIT              GL(0x10, 0x10) // bool: riposte_on_hit (else on_miss) chance
#define TARGET_SIZE             GL(0x20, 0x20)
#define AREA_KIND               GL(0x1c, 0x1c)          // Area: kind (Area::GetKind), 0: room
#define RULES_AFFLICTION_LEVELS GL(0x220, 0x208)        // _Rules: RandomTable<unsigned> (_Rules starts with its
                                                        // rng)
#define RAID_DISPLAY_WAIT        GL(0xcfc, WIN_UNKNOWN) // RaidDisplay::T: the wait after the halo
#define RAIDDISPLAY_PRESENTATION GL(0x406c, 0x419c)     // RaidDisplay: the presentation override (kind; its trait
                                                        // +4), SetPresentationOverride's fields
#define RAIDDISPLAY_TAB             GL(0x16b8, 0x1768)  // the bottom-right panel shown (the tab hot spots' lambdas)
#define RAIDDISPLAY_BATTLE_FINISHED GL(0x45b8, 0x46f8)  // RaidDisplay::OnBattleFinish ran (its heroes' loop)
#define DISPLAY_POPUP_QUEUE         GL(0x30a0, 0x3168)  // RaidDisplay
#define DISPLAY_QUIRK_SCRIPT        GL(0x3108, 0x31c8)  // RaidDisplay: announcement_show.times (OnPostRaidStart)
#define RAID_T_PURGE_TIME           GL(0x518, 0x518)    // RaidDisplay::T: announcement_times.curio_purge (float)
#define TIMELINE_APPEND_WAIT        GL(0x38, 0x30)      // Timeline vtable: Append_Wait(float)
#define QUEUED_POPUP_TYPE           GL(0x58, 0x58)
#define QUEUED_POPUP_DONE           GL(0xb8, 0xb8)
#define QUEUED_POPUP_SIZE           GL(0xc8, 0xc8)
#define ACT_OUT_SOURCE              GL(0x00, WIN_UNKNOWN) // ActOut::Data::CombatStartTurnData: SourceIdentifier
#define ACT_OUT_SOURCE_ID           GL(0x04, WIN_UNKNOWN) // ... whose id (the trait's or quirk's) is a string
#define ACT_OUT_TYPE                GL(0x44, 0x44)
#define ACT_OUT_NUMBER              GL(0x48, WIN_UNKNOWN) // number_value
#define ACT_OUT_STRING              GL(0x4c, WIN_UNKNOWN) // string_value (an effect name for most types)
#define ACT_OUT_CHANCE              GL(0x8c, 0x8c)        // a weight
#define ACT_OUT_CLASSES             GL(0x90, 0x90)        // valid_hero_class_ids (vector<unsigned>, empty: any)
#define ACT_OUT_RAID_LIMIT          GL(0xa8, 0xa8)        // 0: none
#define ACT_OUT_SIZE                GL(0xb0, 0xb0)
#define CAMP_SKILL_ID               GL(0x08, 0x08) // Camping::SkillClass: id (char[]) (PerformCampingSkill
                                                   // 0x5bce30)
#define CAMP_SKILL_LOG_A GL(0x48, 0x48)            // the two members Camp::LogSkill records
#define CAMP_SKILL_LOG_B GL(0x4c, 0x4c)
#define CAMP_SKILL_COST  GL(0x50, 0x50)   // respite cost (int)
#define EVENT_ACTOR      GL(0x10, 0x10)   // HeroUsedItemEvent, TrinketTriggeredEvent: the hero
#define EVENT_ITEM       GL(0x18, 0x18)   // HeroUsedItemEvent: the item (HeroUseItem 0x425e6d)
#define PROP_TYPE        GL(0xc8, 0xc8)   // Prop: PropType* (GetPropTypeData)
#define PROP_NAME        GL(0x125, 0x125) // Prop::GetName(): its id (ShowObstacle)
#define PROP_TYPE_UI     GL(0x4c8, 0x4c8) // PropType::GetUIString() (ReadCurioPropCSVFile)
#define RESULT_DATA      GL(0x08, 0x08)   // a prop interaction result (the *Result::Apply)
#define RESULT_HERO      GL(0x10, 0x10)
#define RESULT_PROP      GL(0x18, 0x18)
#define SCOUTING_DEPTH   GL(0x04, 0x04) // PropInteractionResultData (scouting): an ASCII digit
#define QUEST_DIFFICULTY GL(0x118, 0x118)
#define QUEST_LENGTH     GL(0x11c, 0x11c)
#define QUEST_REWARDS    GL(0x170, 0x160)      // (Windows: GenerateQuestRewards 0x575000)
#define QUEST_SIZE       GL(0x300, 0x2f0)      // Quest::GeneratedQuest (Windows: its loop step)
#define LOOT_RESULT      GL(0x100, 0x100)      // Loot::System: the result inventory (the loot window)
#define LOOT_ITEMS       GL(0x30, 0x28)        // ... the item queue (QueueGenerateItem's inventory)
#define LOOT_OWNED       GL(0x90, 0x90)        // ... owned items (QueueOwnedItem; Windows: inlined in
                                               // Hero::OnDeathFinishInternal, AddItem(raid + 0x51d0))
#define LOOT_GENERATE_TYPE GL(0xf0, 0xf8)      // ... const Loot::Generate::Type* (SetGenerateType)
#define SUPPLY_INVENTORY   GL(0x70, 0x68)      // Building::Store::BaseTrinketSupply: its
                                               // Inventory::System
                                               // (Windows: TrinketSupply::GenerateItems' AddItem)
#define QUEUE_MAP               GL(0x08, 0x08) // the loot queue (std::deque): block map
#define QUEUE_MAP_SIZE          GL(0, 0x10)    // MSVC only: map entries (a power of two)
#define QUEUE_START             GL(0x20, 0x18)
#define QUEUE_SIZE              GL(0x28, 0x20)
#define QUEUE_ELEMENT           GL(0x20, 0x20)
#define QUEUE_BLOCK             GL(128, 1)       // elements per block
#define OVERLAY_TRAP_DISARMING  GL(0xd8c, 0xd84) // Overlay_Event: a disarm (not walked into)
#define RULES_TRAP_DISARM_BONUS GL(0, 0x3a0)     // Windows only (GetTrapDodgeChance inlined): _Rules'
#define RULES_TRAP_BASE         GL(0, 0x618)     // disarm bonus (float), difficulty bases (vector<float>)
#define HEAL_TYPE_SIZE          GL(0x68, 0x68)   // Heal::Source::k_TypeData entry; id first (char[64])

// ---------------------------------------------------------------- items and inventories
// Inventory::Item: amount int, type FixedLengthString<64> (its hash after it), id FixedLengthString<64> (its hash).
#define ITEM_AMOUNT      GL(0x00, 0x00)
#define ITEM_TYPE_STRING GL(0x04, 0x04) // Item::GetType: the type string (64 chars)
#define ITEM_TYPE_HASH   GL(0x44, 0x44)
#define ITEM_ID          GL(0x48, 0x48) // Inventory::Item::GetId
#define ITEM_ID_HASH     GL(0x88, 0x88)
#define ITEM_TAGS        GL(0x90, 0x90)                  // std::map<unsigned, unsigned> tags (libc++: begin, root,
                                                         // size; MSVC: head node, size)
#define ITEM_TAG_END               GL(0x98, WIN_UNKNOWN) // the tag map's end node (its "left" is the root)
#define ITEM_CLASS                 GL(0x5a0, 0x598)
#define ITEM_SIZE                  GL(0x5b0, 0x5a8) // Inventory::Item
#define ITEMCLASS_TRINKET_ID       GL(0x84, 0x84)
#define ITEMCLASS_STACK_LIMIT      GL(0x88, 0x88)
#define ITEMCLASS_STACK_MODIFIABLE GL(0x99, 0x99)
#define TRINKETCLASS_CLASSES       GL(0x278, WIN_AS_MAC(0x278)) // std::vector<unsigned> of hero class id hashes (empty:
                                                                // any)
#define TRINKETCLASS_RARITY       GL(0x324, 0x324)              // the rarity's hash
#define SYSTEM_ITEMS              GL(0x08, 0x08)                // Inventory::System: std::vector<Inventory::Item>
#define SYSTEM_CONFIG             GL(0x20, 0x20)                // its config type's hash
#define SYSTEM_FILLED             GL(0x24, 0x24)                // its filled slot count (RemoveSlotItem recounts it)
#define CONFIG_MAX_SLOTS          GL(0x44, WIN_AS_MAC(0x44))
#define CURRENCY_HASH             GL(0x40, 0x40) // Estate::CurrencyClass (Windows: EstateSummary's loop)
#define CURRENCY_HEIRLOOM         GL(0x44, 0x44)
#define CURRENCY_IN_SUMMARY       GL(0x45, 0x45)
#define CAMPAIGN_ESTATE           GL(0xe20, 0xe50)
#define ESTATE_CURRENCIES         GL(0x20, 0x20) // Estate::System: std::vector<Estate::Currency>
#define ESTATE_CURRENCY_SIZE      GL(0x48, 0x48) // Estate::Currency: amount +0, type (0x40) +4, hash +0x44
#define ESTATE_CURRENCY_TYPE_HASH GL(0x44, 0x44)
#define ROSTER_ENTRIES            GL(0x20, 0x20) // std::vector<Roster::Entry*> (Windows: stash_view.c's walk)
#define ROSTER_ENTRY_HERO         GL(0x10, 0x08)
#define ENTRY_HERO                GL(0x10, 0x08)     // a stash unequip entry
#define ENTRY_STATUS              GL(0x1520, 0x1540) // 3: skipped by UnequipAllTrinkets
#define ENTRY_TRINKETS_CHANGED    GL(0x159c, 0x15bc)
#define INPUT_DRAG                GL(0x1b0, 0x1b0)
#define IFACE_SYSTEM              GL(0x08, 0x08) // InventorySystemInterface: Inventory::System*
#define IFACE_SLOTS               GL(0x10, 0x10) // std::vector<InventorySlotInterface*>
#define SLOT_DROP_THIS            GL(0, 0x10)    // InventoryItemSlotInterface::OnDrop's this, from the
                                                 // slot:
                                                 // Windows passes the drop-target base (multiple inheritance)
#define SLOT_DROP          GL(0x10, 0x10)        // InventorySlotInterface: its DropContext
#define SLOT_ITEM          GL(0x30, 0x30)        // the slot's Item*
#define SLOT_HIGHLIGHT     GL(0xbc, 0xbc)        // InventorySlotInterface::GetHighlightType
#define SLOT_PICKED        GL(0xc0, 0xc0)        // IsPickedUp (bool)
#define ITEM_TRINKET_CLASS GL(0x5a8, 0x5a0)      // Inventory::Item: GetTrinketClass's cached TrinketClass*
#define EQUIP_DRAG_HERO    GL(0x600, 0x600)      // EquipmentDragData: the hero (SetHero)
#define ASSETS_RARITIES    GL(0x1bb0, 0x1bb0)    // UI::CampaignAssets: std::vector of trinket rarity
                                                 // entries (0x18 bytes: hash, texture at +8)
#define CELL_INVENTORY   GL(0x28, 0x18)          // (Windows: InventoryItemSlotInterface::OnDrop 0x7e22f0)
#define CELL_ITEM        GL(0x30, 0x20)
#define LAMBDA_INVENTORY GL(0x10, 0x10)   // the sell callback's Inventory::System*
#define WAGON_BUY_STOCK  GL(0x10, 0x08)   // the Nomad Wagon's buy lambda: its stock
#define REALM_INTERFACE  GL(0xb0, 0xc0)   // RealmInventory
#define REALM_AXIS_POS   GL(0x228, 0x238) // ContentAxisInfo at +0x218: position +0x10, target
                                          // +0x18
#define REALM_AXIS_TARGET   GL(0x230, 0x240)
#define REALM_BAR           GL(0x240, 0x250) // its ScrollBarWidget
#define REALM_ITEM_COUNT    GL(0x518, 0x528)
#define REALM_REFRESH       GL(0x528, 0x538) // ActiveUpdate rebuilds and sorts the panel's list
#define REALM_VIEW          GL(0x540, 0x550) // the grid's clip: x, y, w, h
#define GRID_COLUMNS        GL(0x00, 0x00)   // inventory grid layout
#define GRID_START          GL(0x04, 0x04)
#define GRID_STEP           GL(0x0c, 0x0c)
#define RARITY_TEXTURE      GL(0x08, 0x08) // CampaignAssets' trinket rarity entry: the frame
#define RM_ENTRIES          GL(0x68, 0x98) // the resource manager
#define RM_ENTRY_GENERATION GL(0x114, 0x114)
#define RM_ENTRY_REFS       GL(0x118, 0x118)
#define RM_ENTRY_SIZE       GL(0x120, 0x120)

// ---------------------------------------------------------------- town panels and assets
#define ASSETS_TREES_OFFSET     GL(0x324, 0x35c) // TownUI::Assets: upgrade_trees_offset (vec2)
#define ASSETS_TREES_SPACING    GL(0x32c, 0x364) // upgrade_trees_spacing (vec2) (Windows: GetChildPanelOffset 0x661580)
#define ASSETS_HIGHLIGHT_OFFSET GL(0xec8, 0xec8) // UI::CampaignAssets: the highlight's place by the icon
#define ASSETS_SELL_TEXT_POS    GL(0x1b58, 0x1b58) // TownUI::Assets: "Sell for:" (vec2)
#define ASSETS_SELL_PRICE_POS   GL(0x1b64, 0x1b64)
#define ASSETS_SORT_SPACING     GL(0x1bf0, 0x1bf0)
#define ASSETS_UNEQUIP_TEXTURE  GL(0x1cc8, 0x1cc8)  // the unequip button picture (texture handle)
#define ASSETS_HERO_SLOT_POS    GL(0x1e90, 0x1e90)  // TownUI::Assets: the hero slot's base pos (0x2b7d030)
#define ASSETS_GUILD_SKILL_POS  GL(0x1ee8, 0x1ee8)  // TownUI::Assets: guild_layout .skill_pos (vec2)
#define ASSETS_GUILD_TREE_LAYOUT GL(0x1ef8, 0x1ef8) // TownUI::Assets: guild_upgrade_tree_layout
#define ASSETS_DROP_POS          GL(0x23a0, 0x23a0) // the stash's drop area
                                                    // (RealmInventory::DrawTrinketArea)
#define ASSETS_DROP_SIZE    GL(0x23a8, 0x23a8)
#define CAMPAIGN_SKILL_GRID GL(0x1498, WIN_AS_MAC(0x1498)) // UI::CampaignAssets:
                                                           // character_combat_skill_grid_layout
#define CAMPAIGN_ICON_LOCK GL(0x15a8, WIN_AS_MAC(0x15a8))  // UI::CampaignAssets: the unlearned icon's lock
                                                           // overlay
#define SKILLGRID_START      GL(0x08, WIN_AS_MAC(0x08))    // .starting_pos (vec2)
#define SKILLGRID_SPACING    GL(0x10, WIN_AS_MAC(0x10))    // .spacing (vec2)
#define SKILLGRID_PER_ROW    GL(0x18, WIN_AS_MAC(0x18))    // .max_rows (skills per row)
#define PANEL_TREES          GL(0x128, 0x130) // BuildingDisplay: std::vector<UpgradeTreeDisplay*> (OnExitFinish)
#define PANEL_HERO_SLOT      GL(0x268, 0x288) // BlacksmithDisplay: its HeroSlot* (OnActionEnterStartInternal)
#define SLOT_INTERFACE       GL(0x120, 0x158) // TownUI::Panel::HeroSlot: its HeroSlotInterface*
#define DRAG_HERO            GL(0x08, 0x08)   // HeroDragData (Windows: Clone 0x7c2a40)
#define DRAG_ENTRY           GL(0x10, 0x10)
#define DRAG_SIZE            GL(0x20, 0x20)
#define ROSTER_ELEMENT_ENTRY GL(0x88, 0x18)            // RosterElement::OnPickedUp's this (Windows: its drag
                                                       // interface, 0x6a54b7)
#define GUILD_TREES         GL(0x250, 0x270)           // GuildDisplay: vector<UpgradeTreeDisplay*>
#define TREE_HASH           GL(0x90, WIN_AS_MAC(0x90)) // UpgradeTreeDisplay
#define TREE_GUID           GL(0x94, WIN_AS_MAC(0x94))
#define TREE_LAYOUT         GL(0x98, WIN_AS_MAC(0x98))
#define TREE_DIVIDER        GL(0x120, 0x138)           // ResourceHandle<Texture> (16 bytes)
#define TREE_REQ_FN_A       GL(0x150, 0x180)           // SharedDisplayParameters: tooltip functions' bases
#define TREE_REQ_FN_B       GL(0x180, 0x1c0)           // (PurchaseIdentifier, GuiTooltipParameters::Text&)
#define TREECLASS_INSTANCED GL(0x44, WIN_AS_MAC(0x44)) // TreeClass: "is_instanced" (prerequisites per hero)
#define TREECLASS_FIRST     GL(0x60, WIN_AS_MAC(0x60)) // TreeClass: requirement map's first node; code at +0x2c
#define REQ_PREREQS         GL(0x20, WIN_AS_MAC(0x20)) // RequirementClass: vector<RequirementIdentifier>
#define REQ_RESOLVE         GL(0x38, WIN_AS_MAC(0x38)) // prerequisite resolve level
#define LAYOUT_ICON         GL(0x0c, WIN_AS_MAC(0x0c)) // upgrade tree layout: .icon_offset
#define LAYOUT_LOCK         GL(0x14, WIN_AS_MAC(0x14)) // .icon_locked_offset
#define LAYOUT_COST_VISIBLE GL(0x1c, WIN_AS_MAC(0x1c)) // .icon_cost_visible
#define BTN_ID              GL(0x08, WIN_AS_MAC(0x08)) // ImageButtonParameters
#define BTN_TOOLTIP         GL(0x50, WIN_AS_MAC(0x50))
#define BTN_HOVER_SOUND     GL(0x1ec, 0x21c)
#define BTN_CLICK_SOUND     GL(0x22c, 0x25c)
#define FN_CALL_SLOT        GL(6, 2) // std::function's call: __base::operator(), _Do_call
#define RAIDSTAT_ADD        GL(0x30, WIN_AS_MAC(0x30))
#define RAIDSTAT_MUL        GL(0x48, WIN_AS_MAC(0x48))

// ---------------------------------------------------------------- tooltips and UI widgets
#define TIP_POS         GL(0x04, 0x04)   // GuiTooltipParameters: vec2, where the box goes
#define TIP_TEXT        GL(0x10, 0x10)   // its Text
#define TIP_WIDTH       GL(0x14, 0x14)   // the text's wrap width
#define TIP_AUTO_WIDTH  GL(0x18, 0x18)   // 1: as wide as the widest line (+ 10)
#define TIP_LINES       GL(0x20, 0x20)   // the Text's lines, LINE_SIZE each
#define TIP_COUNT       GL(0x520, 0x520) // the line count
#define TIP_HANDLE      GL(0x528, 0x528) // the background texture handle
#define TIP_PARAMS_SIZE GL(0x568, WIN_AS_MAC(0x568))
#define TEXT_COUNT      GL(0x510, WIN_AS_MAC(0x510)) // GuiTooltipParameters::Text: line count
#define LINE_SIZE       GL(0x28, 0x28)               // a line: font 0, colour (hvec4) 8, flags 0x10, string
                                                     // 0x18
#define LINE_COLOUR       GL(0x08, 0x08)
#define LINE_CENTRED      GL(0x11, 0x11)
#define LINE_STRING       GL(0x18, 0x18)
#define LINE_NEWLINE      GL(0x20, 0x20)
#define HOTSPOT_ID        GL(0x08, 0x08)             // HotSpotParameters
#define HOTSPOT_TOOLTIP   GL(0x50, 0x50)             // GuiTooltipParameters*
#define PARAMS_ID         GL(0x08, WIN_AS_MAC(0x08)) // ImageButtonParameters (paging.c)
#define PARAMS_POS        GL(0x18, WIN_AS_MAC(0x18))
#define PARAMS_ENABLED    GL(0x2ee, 0x31e)
#define TEXT_WRAP         GL(0x100, 0x130) // TextParameters
#define TEXT_MODE         GL(0x110, 0x140)
#define W_SIZE_V          GL(0x100, 0x130) // widget parameters (ui_draw.c)
#define W_ANCHOR          GL(0x110, 0x140)
#define W_TRANSFORM       GL(0x118, 0x148)
#define W_DROP_CONTEXT    GL(0x28, 0x28)
#define W_DRAG_CONTEXT    GL(0x30, 0x30)
#define W_ENABLED         GL(0x2ee, 0x31e)
#define W_CLIP            GL(0x188, 0x1b8)
#define W_SIZE            GL(0x330, 0x358)
#define P_POS             GL(0x18, 0x18) // ImageParameters (item_draw.c)
#define P_TEXTURE         GL(0x40, 0x40)
#define P_SIZE            GL(0x100, 0x130)
#define P_ANCHOR          GL(0x110, 0x140)
#define P_COLOUR          GL(0x158, 0x188)
#define SPRITE_FN_SIZE    GL(0x30, 0x40)
#define SPRITE_VECTOR     GL(0x310, 0x340)
#define TEXTURE_SIZE      GL(0x14, 0x14)
#define HD_MAP            GL(0x18, 0x10) // HeroDisplay::App: skill data map
#define HD_ROOT           GL(0x20, WIN_UNKNOWN)
#define NODE_VEC          GL(0x28, 0x28)
#define NODE_SIZE         GL(WIN_UNKNOWN, 0x40)
#define CSD_INFO          GL(0x400, 0x400)
#define CSD_MODE_MAP      GL(WIN_UNKNOWN, 0x600)
#define CSD_MODE_ROOT     GL(0x608, WIN_UNKNOWN)
#define PARAMS_CLASS      GL(0x18, WIN_AS_MAC(0x18)) // CombatSkillTooltipParams
#define PARAMS_SKILL      GL(0x30, WIN_AS_MAC(0x30))
#define CLOSURE_TOOLTIP   GL(0x08, WIN_AS_MAC(0x08)) // crit_info.c's lambda closure
#define CLOSURE_ACTOR     GL(0x18, WIN_AS_MAC(0x18))
#define DATA_LANGUAGE     GL(0x01, WIN_UNKNOWN) // the font library
#define DATA_FONTS        GL(0x108, 0x108)      // std::vector<Font*> (Windows: AddFontReference)
#define DATA_CURRENT      GL(0x120, 0x120)      // (GetFontFromId)
#define FONT_ID           GL(0x04, 0x04)
#define FONT_TEXTURE_FONT GL(0x108, 0x108)
#define FONT_LOADED       GL(0x110, 0x110) // (SetCurrentLanguage)
#define FONT_SIZE         GL(0x118, 0x118) // (the fonts file's operator new, 0x270968)
#define ENTRY_SIZE        GL(0x800, 0x310) // the confirmation dialog's EntryParameters (allocated
                                           // size)
#define ENTRY_ANSWERS GL(0x200, 0x200)
#define ENTRY_QUEUED  GL(0x330, 0x330) // a queued Entry: a header (0x10; Windows 8) and the
                                       // parameters
#define ANSWER_SIZE GL(0x140, 0x148)
#define ANSWER_FN   GL(0x80, 0x80)
#define ANSWER_FN_F GL(0xa0, 0xb8)
#define ANSWER_KEY  GL(0x130, 0x140)
#define E_TEXT      GL(0x10, 0x08) // in a queued Entry
#define E_ANSWERS   GL(0x210, 0x208)
#define E_BACKOUT_F GL(0x280, 0x298)
#ifdef _WIN32
#define E_TEXTURES                                                                                                     \
    0x2f0 // the entry's texture set (frame 0, back button 0x18);
          // macOS reads UI::AppAssets instead
#endif
#define SYS_ENTRIES            GL(0x00, 0x00) // the dialog system: std::vector<Entry>
#define SYS_CLOSING            GL(0x20, 0x20) // entries to pop at the next Update
#define VALID_RESULT_SIZE      GL(0x40, 0x40) // Battle::SkillValidResult (bools at 0, 8, 0x10, 0x18)
#define VALID_HP_OK            GL(0x18, WIN_AS_MAC(0x18))
#define VALID_RESULT_OK        GL(0x40, 0x40) // Inventory::Display::ItemValidResult: bytes 0 and 0x3f
#define TABPANEL_REORDER_TWEEN GL(0x10, 0x08) // DDTabPanel: the party order button's TweenFloat
#define LUT_FLAGS              GL(0x80, 0x80) // RaidDisplay::LoadAssets' flags for its grades

// ---------------------------------------------------------------- saves and mods
#define DARKEST_UGC_IDS GL(0x950, 0x950)                // Darkest: the campaign's mod list (ProfileGameFile at
                                                        // +0x748, its GetUGCIdList at +0x208), vector<UGCId>
#define UGC_ID_SIZE         GL(0xc0, 0xc0)              // UGCId: the id (title or folder) first
#define SAVEDATA_ADD_U32    GL(0x38, 0x98)              // SaveData vtable: add(c_string key, unsigned const&)
#define RESTOREDATA_GET_U32 GL(0x48, 0xa8)              // RestoreData vtable: get(key, unsigned&), false if
                                                        // missing
#define META_SHORT_DESC    GL(0x400, WIN_AS_MAC(0x400)) // a mod's metadata
#define META_MOD_DATA_PATH GL(0x600, WIN_AS_MAC(0x600))
#define META_IN_UGC        GL(0x6e0, WIN_UNKNOWN)
#define META_TITLE         GL(0x800, WIN_AS_MAC(0x800))
#define META_LANGUAGE      GL(0xa00, WIN_AS_MAC(0xa00))
#define META_DESC_PTR      GL(0x1048, WIN_AS_MAC(0x1048))
#define UGCID_SOURCE       GL(0x80, WIN_AS_MAC(0x80))

#endif
