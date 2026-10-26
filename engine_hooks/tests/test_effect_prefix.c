#include "../src/effects.c"
#include <assert.h>
int mods_list_add(ModsList *l) { return l->n++; }
int mods_list_order(ModsList *l, bool top) {
    for (int i = 0; i < l->n; i++) l->order[i] = i;
    return l->n;
}
static int calls;
static void callback(const char *name, void *target, void *performer) {
    assert(!strcmp(name, "client_example"));
    assert(target == (void *)1 && performer == (void *)2);
    calls++;
}
int main(void) {
    effects_on_prefix(NULL, callback);
    effects_on_prefix("", callback);
    effects_on_prefix("client_example", NULL);
    assert(g_nclients == 0);
    char *temporary = strdup("client_example");
    effects_on_prefix(temporary, callback);
    memset(temporary, 'x', strlen(temporary));
    free(temporary);
    assert(run_client("client_example", (void *)1, (void *)2));
    assert(calls == 1);
    assert(!run_client("other_effect", (void *)1, (void *)2));
    free((void *)g_clients[0].prefix);
    puts("effect prefix lifetime: all passed");
}
