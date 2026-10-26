// Upgrade currency requirements are owned by the game. Only edit existing amounts;
// never resize the game's currency vector or retain borrowed requirement pointers.
#include "common.h"
#include <string.h>

typedef struct {
    unsigned tree;
    uint8_t code, pad[3];
    unsigned guid;
} CostPurchaseId;

static int *upgrade_amount(unsigned tree, int level, unsigned currency) {
    static const uint8_t *(*get)(const CostPurchaseId *);
    if (level < 0 || level >= 26)
        return NULL;
    if (!get)
        *(void **)&get = sym("_ZN7Upgrade19GetRequirementClassERKNS_18PurchaseIdentifierE");
    CostPurchaseId id = {.tree = tree, .code = (uint8_t)('a' + level)};
    const uint8_t *req = get ? get(&id) : NULL;
    if (!req)
        return NULL;
    // Upgrade::RequirementClass costs: begin +8, end +16 on both supported x64 builds.
    // Currency is 0x48 bytes: amount at +0, FixedLengthString<64> type at +4.
    uint8_t *begin, *end;
    memcpy(&begin, req + 8, sizeof(begin));
    memcpy(&end, req + 16, sizeof(end));
    uintptr_t first = (uintptr_t)begin, last = (uintptr_t)end;
    if (!begin || last < first || (last - first) % 0x48 || (last - first) / 0x48 > 64)
        return NULL;
    for (uint8_t *row = begin; row < end; row += 0x48) {
        char type[65];
        memcpy(type, row + 4, 64);
        type[64] = 0;
        if (name_hash(type) == currency)
            return (int *)row;
    }
    return NULL;
}

bool game_upgrade_cost(unsigned tree, int level, unsigned currency, int *out) {
    int *amount = upgrade_amount(tree, level, currency);
    if (!amount || !out)
        return false;
    *out = *amount;
    return true;
}

bool game_upgrade_set_cost(unsigned tree, int level, unsigned currency, int value) {
    if (value < 0)
        return false;
    int *amount = upgrade_amount(tree, level, currency);
    if (!amount)
        return false;
    *amount = value;
    return true;
}
