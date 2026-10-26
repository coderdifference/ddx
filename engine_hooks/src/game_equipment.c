// Owned copies and reversible overrides of vanilla equipment rows. Clients see only semantic values.
#include "common.h"
#include <stdlib.h>
#include <string.h>
struct EquipmentRow {
    uint8_t *hero, *target;
    int slot, index, old_level;
    // row is the editable copy; saved captures the shared class row when this scope is applied.
    uint8_t row[WEAPON_SIZE], saved[WEAPON_SIZE];
    bool applied;
};
static int row_size(int slot) {
    return slot ? ARMOUR_SIZE : WEAPON_SIZE;
}
static int level_offset(int slot) {
    return slot ? HERO_ARMOUR_LEVEL : HERO_WEAPON_LEVEL;
}
void *game_equipment_row(void *hero, int slot, int level) {
    if (!hero || (slot != 0 && slot != 1))
        return NULL;
    uint8_t *cls = *(uint8_t **)((uint8_t *)hero + HERO_CLASS);
    if (!cls)
        return NULL;
    uint8_t **v = (uint8_t **)(cls + (slot ? CLASS_ARMOURS : CLASS_WEAPONS));
    int count = v[0] && v[1] >= v[0] ? (int)((v[1] - v[0]) / row_size(slot)) : 0;
    if (!count)
        return NULL;
    int index = level <= 1 ? 0 : level > count ? count - 1 : level - 1;
    EquipmentRow *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    r->hero = hero;
    r->slot = slot;
    r->index = index;
    r->target = v[0] + index * row_size(slot);
    memcpy(r->row, r->target, row_size(slot));
    return r;
}
void game_equipment_values(const EquipmentRow *r, EquipmentValues *out) {
    if (!r || !out)
        return;
    memset(out, 0, sizeof *out);
    if (!r->slot) {
        out->accuracy = *(const float *)(r->row + 0x118);
        out->damage_min = *(const int *)(r->row + WEAPON_DMG_MIN);
        out->damage_max = *(const int *)(r->row + WEAPON_DMG_MAX);
        out->critical = *(const float *)(r->row + 0x124);
        out->speed = *(const float *)(r->row + 0x128);
    } else {
        out->dodge = *(const float *)(r->row + 0x118);
        out->protection = *(const float *)(r->row + 0x11c);
        out->hp = *(const float *)(r->row + 0x120);
        out->speed = *(const float *)(r->row + 0x124);
    }
}
void game_equipment_set_values(EquipmentRow *r, const EquipmentValues *v) {
    if (!r || !v)
        return;
    if (!r->slot) {
        *(float *)(r->row + 0x118) = v->accuracy;
        *(int *)(r->row + WEAPON_DMG_MIN) = v->damage_min;
        *(int *)(r->row + WEAPON_DMG_MAX) = v->damage_max;
        *(float *)(r->row + 0x124) = v->critical;
        *(float *)(r->row + 0x128) = v->speed;
    } else {
        *(float *)(r->row + 0x118) = v->dodge;
        *(float *)(r->row + 0x11c) = v->protection;
        *(float *)(r->row + 0x120) = v->hp;
        *(float *)(r->row + 0x124) = v->speed;
    }
}
void game_equipment_apply(EquipmentRow *r) {
    if (!r || r->applied)
        return;
    // Snapshot at apply time so nested scopes restore the outer override, not the original class data.
    memcpy(r->saved, r->target, row_size(r->slot));
    r->old_level = *(int *)(r->hero + level_offset(r->slot));
    memcpy(r->target, r->row, row_size(r->slot));
    *(int *)(r->hero + level_offset(r->slot)) = r->index;
    r->applied = true;
}
void game_equipment_release(EquipmentRow *r) {
    if (!r)
        return;
    if (r->applied) {
        memcpy(r->target, r->saved, row_size(r->slot));
        *(int *)(r->hero + level_offset(r->slot)) = r->old_level;
    }
    free(r);
}
int game_equipment_description(EquipmentRow *r, uint64_t colour, void (*line)(const char *, uint64_t, void *),
                               void *ud) {
    if (!r || !line)
        return 0;
    static void (*weapon)(void *, const void *, uint64_t), (*armour)(void *, const void *, uint64_t),
        (*destroy)(void *);
    if (!weapon)
        *(void **)&weapon =
            sym("_ZN11HeroDisplay3App28EquipmentAddWeaponStatsLinesERN20GuiTooltipParameters4TextEPK6Weapon5hvec4");
    if (!armour)
        *(void **)&armour =
            sym("_ZN11HeroDisplay3App28EquipmentAddArmourStatsLinesERN20GuiTooltipParameters4TextEPK6Armour5hvec4");
    if (!destroy)
        *(void **)&destroy = sym("_ZN20GuiTooltipParametersD1Ev");
    if (!destroy || (r->slot ? !armour : !weapon))
        return 0;
    // Native tooltip strings remain owned by tip; callbacks must consume them before destroy below.
    _Alignas(16) uint8_t tip[0x600] = {0};
    *(uint64_t *)(tip + TIP_HANDLE) = 0xffffffff00000000ULL;
    (r->slot ? armour : weapon)(tip + TIP_TEXT, r->row, colour);
    unsigned n = *(unsigned *)(tip + TIP_COUNT);
    if (n > 32)
        n = 32;
    for (unsigned i = 0; i < n; i++) {
        uint8_t *l = tip + TIP_LINES + i * LINE_SIZE;
        line(*(const char **)(l + LINE_STRING), *(uint64_t *)(l + LINE_COLOUR), ud);
    }
    destroy(tip);
    return (int)n;
}
