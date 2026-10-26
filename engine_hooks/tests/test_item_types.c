// Offline tests for the item type registry (item_types.c), without the game hooks.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define ITEM_TYPES_TEST
#include "../src/item_types.c"
#include "mods_stub.h"

bool g_debug;
void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}
unsigned name_hash(const char *s) {
    unsigned h = 2166136261u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static int g_failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                                         \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

// Mods follow the campaign: a mod's type is known only while the campaign applies the mod; two mods may each register
// a name, the top one's is found.
static void test_mods(void) {
    g_mod_current = 0;
    CHECK(item_type_register(&(ItemTypeDef){.type = "mod_gear", .home = ITEM_HOME_STASH}));
    CHECK(!item_type_register(&(ItemTypeDef){.type = "mod_gear"})); // twice by one mod
    g_mod_current = 1;
    CHECK(item_type_register(&(ItemTypeDef){.type = "mod_gear", .home = ITEM_HOME_CURRENCY}));
    g_mod_current = -1;
    unsigned h = name_hash("mod_gear");
    CHECK(!item_type_find(h)); // no campaign
    campaign(0, -1, -1, -1);
    CHECK(item_type_find(h) && item_type_find(h)->home == ITEM_HOME_STASH);
    campaign(1, 0, -1, -1); // mod 1 on top
    CHECK(item_type_find(h) && item_type_find(h)->home == ITEM_HOME_CURRENCY);
    campaign(-1, -1, 0, -1); // neither applied
    CHECK(!item_type_find(h));
    CHECK(item_type_find(name_hash("example_weapon"))); // the library's own always
    campaign(-1, -1, -1, -1);
}

int main(void) {
    CHECK(!item_type_find(name_hash("example_weapon")));
    char name[] = "example_weapon";
    CHECK(item_type_register(&(ItemTypeDef){.type = name, .home = ITEM_HOME_STASH}));
    strcpy(name, "changed"); // the registry keeps its own copy of the name
    const ItemTypeDef *d = item_type_find(name_hash("example_weapon"));
    CHECK(d && !strcmp(d->type, "example_weapon") && d->home == ITEM_HOME_STASH);
    CHECK(!d->sound);
    CHECK(!item_type_register(&(ItemTypeDef){.type = "example_weapon"})); // twice
    char sound[] = "trinket";
    CHECK(item_type_register(&(ItemTypeDef){.type = "example_sound", .sound = sound}));
    strcpy(sound, "x"); // and of the sound name
    CHECK(!strcmp(item_type_find(name_hash("example_sound"))->sound, "trinket"));
    CHECK(!item_type_register(&(ItemTypeDef){
        .type = "example_loud",
        .sound =
            "audio/"
            "a_path_of_128_characters_"
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.wav"}));
    CHECK(!item_type_register(&(ItemTypeDef){.type = ""}) && !item_type_register(NULL));
    CHECK(item_type_register(&(ItemTypeDef){.type = "example_armour"}));
    CHECK(item_type_find(name_hash("example_armour"))->home == ITEM_HOME_NONE);
    int n = 3;
    char many[8];
    while (snprintf(many, sizeof many, "t%d", n), item_type_register(&(ItemTypeDef){.type = many}))
        n++;
    CHECK(n == MAX_ITEM_TYPES);
    CHECK(!item_type_find(name_hash("trinket")));
    g_ntypes = 3;
    g_types_mods.n = 3;
    g_types_mods.gen = 0; // room again for the mods' types
    test_mods();
    printf(g_failures ? "%d failure(s)\n" : "all passed\n", g_failures);
    return g_failures != 0;
}
