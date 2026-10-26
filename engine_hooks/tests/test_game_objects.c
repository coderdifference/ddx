// Check semantic views against the native layout and item-class lookup, including unpopulated item caches.
#include "../src/game_objects.c"
#include <assert.h>
static uint8_t item_class[0x100], trinket_class[0x400];
static void *library_value = item_class;
static void *lookup(const void *library, unsigned type, unsigned id) {
    assert(library == item_class && type == 10 && id == 20);
    return item_class;
}
static void *trinket(unsigned id) {
    assert(id == 30);
    return trinket_class;
}
static float resolve_level(const void *hero) {
    return 4.5f;
}
void *sym(const char *name) {
    if (!strcmp(name, "_ZN9Inventory11SystemClass10s_InstanceE"))
        return &library_value;
    if (!strcmp(name, "_ZNK9Inventory11SystemClass12GetItemClassEjj"))
        return (void *)lookup;
    if (!strcmp(name, "_ZN7Trinket7Library15GetTrinketClassEj"))
        return (void *)trinket;
    if (!strcmp(name, "_ZNK4Hero15GetResolveLevelEv"))
        return (void *)resolve_level;
    return NULL;
}
int game_random_int(void *rng, int min, int max) {
    return min;
}
float *game_resist_clamp(int type, bool flag) {
    return NULL;
}
int main(void) {
    uint8_t quirk_hero[HERO_QUIRKS + 24] = {0};
    uint8_t quirks[3 * QUIRK_SIZE] = {0}, classes[3][0x60] = {{0}};
    const uint8_t **qv = (const uint8_t **)(quirk_hero + HERO_QUIRKS);
    qv[0] = quirks; qv[1] = quirks + sizeof quirks;
    for (int i = 0; i < 3; i++)
        *(const uint8_t **)(quirks + i * QUIRK_SIZE) = classes[i];
    classes[0][QUIRKCLASS_POSITIVE] = 1;
    quirks[QUIRK_SIZE + QUIRK_LOCKED] = 1;
    classes[2][QUIRKCLASS_DISEASE] = 1;
    assert(game_hero_negative_quirk(quirk_hero, 0) == 1); // locked negative
    assert(game_hero_negative_quirk(quirk_hero, 1) == 2); // disease
    assert(game_hero_negative_quirk(quirk_hero, 2) == -1);
    assert(game_hero_negative_quirk(NULL, 0) == -1);
    uint8_t actor[HERO_CLASS + 8] = {0};
    strcpy((char *)actor + ACTOR_NAME, "Test Hero");
    *(unsigned *)(actor + ACTOR_RANK) = 4;
    assert(!strcmp(game_actor_name(actor), "Test Hero") && game_actor_rank(actor) == 4);
    game_actor_set_stress(actor, 42);
    assert(game_actor_stress(actor) == 42 && game_hero_resolve(actor) == 4.5f);
    uint8_t items[2 * ITEM_SIZE] = {0}, inventory[SYSTEM_ITEMS + 24] = {0};
    // Model the native vector with begin/end pointers; no game allocation is needed for these views.
    uint8_t **v = (uint8_t **)(inventory + SYSTEM_ITEMS);
    v[0] = items;
    v[1] = items + sizeof items;
    *(int *)(items + ITEM_AMOUNT) = 3;
    strcpy((char *)items + ITEM_TYPE_STRING, "trinket");
    strcpy((char *)items + ITEM_ID, "test");
    *(unsigned *)(items + ITEM_TYPE_HASH) = 10;
    *(unsigned *)(items + ITEM_ID_HASH) = 20;
    *(unsigned *)(item_class + ITEMCLASS_TRINKET_ID) = 30;
    *(unsigned *)(trinket_class + TRINKETCLASS_RARITY) = 99;
    assert(game_inventory_count(inventory) == 2 && game_inventory_item(inventory, 1) == items + ITEM_SIZE);
    assert(!game_inventory_item(inventory, -1) && !game_inventory_item(inventory, 2));
    assert(game_item_amount(items) == 3 && !strcmp(game_item_id(items), "test"));
    assert(game_item_rarity(items) ==
           99); // ITEM_CLASS is still null: use the library lookup rather than the lazy cache
    uint8_t buff[BUFF_ID + 64] = {0};
    strcpy((char *)buff + BUFF_ID, "example");
    *(unsigned *)(buff + BUFF_SOURCE) = 6;
    *(unsigned *)(buff + BUFF_SOURCE_ID) = 123;
    assert(game_buff_source(buff) == 6 && game_buff_source_id(buff) == 123 && !strcmp(game_buff_id(buff), "example"));
    // Missing borrowed handles must produce neutral values rather than dereference game memory.
    assert(!game_actor_rank(NULL) && !game_inventory_count(NULL) && !game_item_amount(NULL));
    puts("Semantic game object checks passed");
    return 0;
}
