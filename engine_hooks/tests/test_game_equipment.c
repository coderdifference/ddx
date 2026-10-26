// Validate the real native transaction against shared class rows, including nested callbacks and two heroes.
#include "../src/game_equipment.c"
#include <assert.h>
#include <stdio.h>
void *sym(const char *name) {
    return NULL;
}
int main(void) {
    uint8_t hero[HERO_ARMOUR_LEVEL + 8] = {0}, other[HERO_ARMOUR_LEVEL + 8] = {0};
    uint8_t cls[CLASS_ARMOURS + 24] = {0}, weapons[2 * WEAPON_SIZE] = {0}, armour[ARMOUR_SIZE] = {0};
    *(void **)(hero + HERO_CLASS) = cls;
    *(void **)(other + HERO_CLASS) = cls;
    uint8_t **w = (uint8_t **)(cls + CLASS_WEAPONS), **a = (uint8_t **)(cls + CLASS_ARMOURS);
    w[0] = weapons;
    w[1] = weapons + sizeof weapons;
    a[0] = armour;
    a[1] = armour + sizeof armour;
    *(int *)(hero + HERO_WEAPON_LEVEL) = 7;
    *(int *)(other + HERO_WEAPON_LEVEL) = 3;
    *(int *)(weapons + WEAPON_SIZE + WEAPON_DMG_MIN) = 11;
    EquipmentRow *first = game_equipment_row(hero, 0, 999);
    assert(first && first->index == 1);
    EquipmentValues v;
    game_equipment_values(first, &v);
    assert(v.damage_min == 11);
    v.damage_min = 20;
    game_equipment_set_values(first, &v);
    assert(*(int *)(weapons + WEAPON_SIZE + WEAPON_DMG_MIN) == 11); // detached copy
    game_equipment_apply(first);
    game_equipment_apply(first); // applying one lease twice is harmless
    assert(*(int *)(weapons + WEAPON_SIZE + WEAPON_DMG_MIN) == 20);
    assert(*(int *)(hero + HERO_WEAPON_LEVEL) == 1);
    EquipmentRow *nested = game_equipment_row(other, 0, 2);
    game_equipment_values(nested, &v);
    assert(v.damage_min == 20);
    v.damage_min = 30;
    game_equipment_set_values(nested, &v);
    game_equipment_apply(nested);
    game_equipment_release(nested);
    assert(*(int *)(weapons + WEAPON_SIZE + WEAPON_DMG_MIN) == 20);
    assert(*(int *)(other + HERO_WEAPON_LEVEL) == 3);
    game_equipment_release(first);
    assert(*(int *)(weapons + WEAPON_SIZE + WEAPON_DMG_MIN) == 11);
    assert(*(int *)(hero + HERO_WEAPON_LEVEL) == 7);
    EquipmentRow *unapplied = game_equipment_row(hero, 1, 0);
    assert(unapplied && unapplied->index == 0);
    game_equipment_release(unapplied);
    game_equipment_release(NULL);
    assert(!game_equipment_row(NULL, 0, 1) && !game_equipment_row(hero, 2, 1));
    _Static_assert(sizeof(EquipmentValues) == 32, "Portable equipment values ABI");
    puts("Native equipment transaction checks passed");
    return 0;
}
