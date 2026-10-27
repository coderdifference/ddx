// Semantic game-object API. All native object handles are borrowed unless an operation explicitly returns an owned scope.
#pragma once

typedef struct EquipmentRow EquipmentRow;
typedef struct {
    float accuracy;
    int damage_min, damage_max;
    float critical, speed, dodge, protection, hp;
} EquipmentValues;
typedef struct TipPrefix TipPrefix;
typedef struct BuildingDescriptions BuildingDescriptions;
typedef bool (*CurrencyPlacement)(unsigned *, float *);

// game_objects.c
void *game_current_raid(void);
const char *game_actor_name(const void *a);
unsigned game_actor_rank(const void *a);
float game_actor_stress(const void *a);
void game_actor_set_stress(void *a, float v);
bool game_actor_is_corpse(const void *a);
float game_hero_resolve(const void *a);
bool game_hero_suppresses_death_loot(const void *a);
int game_item_amount(const void *item);
const char *game_item_type(const void *item);
const char *game_item_id(const void *item);
unsigned game_item_rarity(const void *item);
int game_inventory_count(const void *inv);
void *game_inventory_item(void *inv, int i);
void game_inventory_remove(void *inv, const void *item);
void *game_raid_random(void *battle);
int game_raid_difficulty(const void *raid);
bool game_raid_in_battle(const void *raid);
void *game_raid_actor(void *raid, int i);
int game_raid_actor_count(const void *raid);
void *game_quest_reward(void *vector, int i);
void *game_party_selected(void *party);
void *game_roster_element_hero(void *element);
unsigned game_buff_source(const void *b);
unsigned game_buff_source_id(const void *b);
const char *game_buff_id(const void *b);
bool game_actor_add_equipment_buff(void *hero, unsigned buff, unsigned source);
float game_effect_stress_heal(const void *e);
bool game_effect_is_dot(const void *e);
unsigned game_effect_stress_heal_field(void);
void *game_skill_effect(void *skill, int i);
void *game_supply_inventory(void *supply);
bool game_raid_queue_death_item(void *raid, const void *item);
bool game_buff_description(unsigned hash, const void *hero, char *out, size_t cap);
void *game_current_raid_display(void);
int game_attack_target_count(const void *attack);
int game_hero_negative_quirk(const void *hero, int selected);
int game_raid_tab(const void *display);
void game_raid_set_tab(void *display, int tab);
bool game_raid_reorder_available(void *panel);
void game_raid_reorder(void *display);
bool ui_hotspot_is_party_reorder(const void *p);
void game_actor_remove_affliction(void *actor);
void game_actor_remove_virtue(void *actor);
bool game_hero_remove_quirk(void *hero, unsigned index);
int game_inventory_slot_count(const void *inv);
void *game_inventory_slot(void *inv, int i);
void game_inventory_remove_slot(void *inv, unsigned i);
void game_roster_save(void);
bool game_upgrade_purchased(unsigned tree, int level, unsigned guid);
void ui_inventory_highlight(int kind);
float game_actor_resistance(const void *actor, int type);
float game_resistance_maximum(int type, bool flag);
void game_resistance_set_maximum(int type, bool flag, float value);
bool game_actor_health_heal(void *target, void *battle, float amount, const void *source, const char *id);
bool game_actor_stress_heal(void *target, void *battle, float amount, const void *source, const char *id);
unsigned game_actor_capabilities(void);
void *game_raid_battle(void *raid);
int game_battle_random_int(void *battle, int low, int high);

// game_equipment.c
// A row owns a copy of a class upgrade (slot 0: weapon, 1: armour; level is one-based and clamped).
// apply temporarily replaces shared native data; release every row, unwinding applied rows in reverse order.
// Description callback text is borrowed only for the duration of the callback.
void *game_equipment_row(void *hero, int slot, int level);
void game_equipment_values(const EquipmentRow *r, EquipmentValues *out);
void game_equipment_set_values(EquipmentRow *r, const EquipmentValues *v);
void game_equipment_apply(EquipmentRow *r);
void game_equipment_release(EquipmentRow *r);
int game_equipment_description(EquipmentRow *r, uint64_t colour, void (*line)(const char *, uint64_t, void *),
                               void *ud);

// game_inventory_ui.c
bool game_inventory_full_swap(bool (*predicate)(const void *));
void *game_inventory_cell_inventory(void *cell);
void *game_inventory_cell_item(void *cell);
void *game_stash_empty_cell(void *realm);
bool ui_realm_header_split(const char *title, const char *blank, const char *texture, bool (*placement)(Vec2 *));

// game_recruit.c
// A successful prefix_begin must be paired with prefix_end to restore saved tooltip lines.
// Prefix text returned by prefix_line belongs to the native tooltip.
void *game_hero_slot_hero(void *slot);
bool game_hero_town_event(const void *hero);
int game_trinket_discount(int price);
void *game_recruit_drag_hero(void *drag);
bool game_roster_is_full(void);
bool game_recruit_hire(void *list, const Vec2 *pos, void *slot, void *hero, void *drop, const CurrencyCost *costs,
                       int ncosts);
void *ui_tooltip_prefix_begin(void *tip, unsigned reserve);
const char *ui_tooltip_prefix_line(TipPrefix *p, const char *text, uint64_t colour, bool replace_colour);
float ui_tooltip_prefix_width(TipPrefix *p, float minimum);
void ui_tooltip_prefix_end(TipPrefix *p);
bool ui_recruit_tooltip_on(void (*callback)(void *, void *));

// game_building.c
bool game_building_upgrades_open(void *panel);
void game_building_move_hero_slot(void *panel, Vec2 delta);
void *game_building_closure_hero(void *closure);
bool ui_hero_action_text_site(const void *caller);
bool ui_building_description_add(BuildingDescriptions *out, const char *text);
bool ui_building_descriptions(bool (*provider)(unsigned, int, BuildingDescriptions *));
bool ui_building_tree_offsets(bool (*provider)(void *, Vec2 *, Vec2 *));

// ui_estate_currency.c
bool ui_estate_currency_place(CurrencyPlacement fn);
bool game_upgrades_available(void);
int game_skill_effect_count(void *skill);
int game_inventory_item_count(const void *inventory);

// Existing game-owned upgrade currency amounts. Game thread only, after campaign load.
// Level 0 is code 'a'; no vector allocation or new currency entries.
bool game_upgrade_cost(unsigned tree, int level, unsigned currency, int *out);
bool game_upgrade_set_cost(unsigned tree, int level, unsigned currency, int value);
