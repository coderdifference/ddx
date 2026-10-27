#include "../src/game_upgrade_cost.c"
#include <assert.h>
#include <stdio.h>
static struct {
    int amount;
    char type[64];
    int padding;
} rows[2];
static struct {
    uint64_t header;
    void *begin, *end, *cap;
} requirement;
static const uint8_t *lookup(const CostPurchaseId *id) {
    return id->tree == 7 && id->code == 'b' && id->guid == 0 ? (const uint8_t *)&requirement : NULL;
}
void *sym(const char *name) {
    return !strcmp(name, "_ZN7Upgrade19GetRequirementClassERKNS_18PurchaseIdentifierE") ? (void *)lookup : NULL;
}
unsigned name_hash(const char *name) {
    return !strcmp(name, "gold") ? 10 : !strcmp(name, "deed") ? 20 : 30;
}
int main(void) {
    _Static_assert(sizeof(rows[0]) == 0x48, "Currency layout");
    strcpy(rows[0].type, "gold");
    rows[0].amount = 100;
    strcpy(rows[1].type, "deed");
    rows[1].amount = 4;
    requirement.begin = rows;
    requirement.end = rows + 2;
    requirement.cap = rows + 2;
    int value = -1;
    assert(game_upgrade_cost(7, 1, 10, &value) && value == 100);
    assert(game_upgrade_set_cost(7, 1, 20, 17) && rows[1].amount == 17 && rows[0].amount == 100);
    assert(!game_upgrade_set_cost(7, 1, 20, -1) && rows[1].amount == 17);
    assert(!game_upgrade_set_cost(7, 1, 30, 1));
    assert(!game_upgrade_cost(7, 0, 10, &value) && !game_upgrade_cost(8, 1, 10, &value));
    assert(!game_upgrade_cost(7, -1, 10, &value) && !game_upgrade_cost(7, 26, 10, &value));
    assert(!game_upgrade_cost(7, 1, 10, NULL));
    requirement.end = (char *)rows + 1;
    assert(!game_upgrade_set_cost(7, 1, 10, 200));
    requirement.end = rows;
    assert(!game_upgrade_cost(7, 1, 10, &value));
    puts("Upgrade currency requirements passed");
}
