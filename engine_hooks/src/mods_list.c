// A registry's entries by owner, in campaign order (see common.h, "mods_list.c"). Built on mods.c's mods_current,
// mods_by_priority and mods_generation only (tests use tests/mods_stub.h for those).
#include "common.h"

int mods_order(const int *owners, int n, bool top_first, int *out) {
    int mods[64], nm = mods_by_priority(mods, 64), k = 0;
    if (!top_first)
        for (int i = 0; i < n; i++)
            if (owners[i] < 0)
                out[k++] = i;
    for (int j = 0; j < nm; j++) {
        int m = mods[top_first ? j : nm - 1 - j];
        for (int i = 0; i < n; i++)
            if (owners[i] == m)
                out[k++] = i;
    }
    if (top_first)
        for (int i = 0; i < n; i++)
            if (owners[i] < 0)
                out[k++] = i;
    return k;
}

int mods_list_add(ModsList *l) {
    if (l->n >= MODS_LIST_MAX)
        return -1;
    l->owner[l->n] = mods_current();
    __atomic_store_n(&l->gen, 0, __ATOMIC_RELEASE);
    return l->n++;
}

int mods_list_order(ModsList *l, bool top_first) {
    unsigned gen = mods_generation();
    if (__atomic_load_n(&l->gen, __ATOMIC_ACQUIRE) != gen || l->top_first != top_first) {
        int order[MODS_LIST_MAX], n = mods_order(l->owner, l->n, top_first, order);
        __atomic_store_n(&l->norder, 0, __ATOMIC_RELEASE);
        for (int k = 0; k < n; k++)
            l->order[k] = order[k];
        l->top_first = top_first;
        __atomic_store_n(&l->norder, n, __ATOMIC_RELEASE);
        __atomic_store_n(&l->gen, gen, __ATOMIC_RELEASE);
    }
    return __atomic_load_n(&l->norder, __ATOMIC_ACQUIRE);
}

bool mods_list_active(const ModsList *l, int i) {
    if (i < 0 || i >= l->n)
        return false;
    if (l->owner[i] < 0)
        return true;
    int mods[64], nm = mods_by_priority(mods, 64);
    for (int j = 0; j < nm; j++)
        if (mods[j] == l->owner[i])
            return true;
    return false;
}

int mods_list_first(ModsList *l) {
    return mods_list_order(l, true) > 0 ? l->order[0] : -1;
}
