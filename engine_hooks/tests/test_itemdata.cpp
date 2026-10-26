// Offline test for itemdata.c against a real libc++ std::map, the game's Inventory::Item tag map
// (std::map<unsigned, unsigned> at Item+0x90), and the game's tag move. Build: engine_hooks/tests/run.sh.
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>

typedef std::map<unsigned, unsigned> Tags;
struct FakeItem { uint8_t bytes[0x90]; Tags tags; uint8_t rest[0x400]; };
static_assert(offsetof(FakeItem, tags) == 0x90, "tags at 0x90");

extern "C" {
int itemdata_id(const void *item);
int itemdata_ensure_id(void *item);
int itemdata_get(const void *item, int field);
bool itemdata_has(const void *item, int field);
bool itemdata_set(void *item, int field, int value);
void itemdata_test_init(void (*add)(void *, unsigned, int), void (*remove)(void *, unsigned));
bool g_debug;
void logf_(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n'); }
// stand-ins for what itemdata.c links against (install and save / restore aren't exercised)
void *sym(const char *) { return nullptr; }
void *install_hook(const char *, void *, void *, const uint8_t *, size_t) { return nullptr; }
struct ModsList;
int mods_list_order(ModsList *, bool) { return 0; }
int mods_list_add(ModsList *) { return 0; }
int field_count(void) { return 0; }
const char *field_name(int) { return ""; }
}

// Inventory::Item::AddTag (ignores amounts <= 0, adds to an existing tag) / RemoveTag
static void add_tag(void *item, unsigned tag, int amount) { if (amount > 0) ((FakeItem *)item)->tags[tag] += (unsigned)amount; }
static void remove_tag(void *item, unsigned tag) { ((FakeItem *)item)->tags.erase(tag); }
// Inventory::System::AddToItem's tag move: min(value, amount) of each tag goes from src to dest
static void move_tags(FakeItem *dest, FakeItem *src, unsigned amount) {
    for (auto &t : src->tags) { unsigned m = t.second < amount ? t.second : amount; dest->tags[t.first] += m; t.second -= m; }
}

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
    itemdata_test_init(add_tag, remove_tag);
    FakeItem *a = new FakeItem(), *b = new FakeItem(), *c = new FakeItem();

    // ids: none, then one assigned and kept
    a->tags[0x12345678u] = 1;                       // a game tag below the iid range
    CHECK(itemdata_id(a) == 0);
    int ia = itemdata_ensure_id(a);
    CHECK(ia > 0 && itemdata_id(a) == ia && itemdata_ensure_id(a) == ia);
    CHECK(a->tags.size() == 2 && a->tags[0xC0000000u + ia] == 1);
    int ib = itemdata_ensure_id(b);
    CHECK(ib > ia);

    // fields
    CHECK(itemdata_get(a, 3) == 0 && !itemdata_has(a, 3));
    CHECK(itemdata_set(a, 3, 42) && itemdata_get(a, 3) == 42 && itemdata_has(a, 3));
    CHECK(itemdata_get(b, 3) == 0);
    CHECK(itemdata_set(c, 1, -7) && itemdata_id(c) > ib && itemdata_get(c, 1) == -7);   // set assigns an id
    CHECK(!itemdata_set(a, -1, 1));

    // the game's move: the id (and so its fields) follows the item; the emptied source keeps a 0-count key
    FakeItem *slot = new FakeItem();
    move_tags(slot, a, 1);
    CHECK(itemdata_id(slot) == ia && itemdata_get(slot, 3) == 42);
    CHECK(itemdata_id(a) == 0 && a->tags.count(0xC0000000u + ia) == 1);
    // a copy (the game's copy constructor copies the map) shares the record
    FakeItem *copy = new FakeItem();
    copy->tags = slot->tags;
    CHECK(itemdata_id(copy) == ia && itemdata_get(copy, 3) == 42);

    // the walk: a 0-count key below a counted one, many keys, past the last node
    FakeItem *d = new FakeItem();
    d->tags[0xC0000000u + 5] = 0;
    d->tags[0xC0000000u + 9] = 1;
    CHECK(itemdata_id(d) == 9);
    FakeItem *e = new FakeItem();
    for (unsigned i = 1; i <= 40; i++) e->tags[0xC0000000u + i] = i == 40 ? 1 : 0;
    for (unsigned i = 1; i <= 40; i++) e->tags[i * 0x01000000u] = 1;
    CHECK(itemdata_id(e) == 40);
    e->tags[0xC0000000u + 40] = 0;
    CHECK(itemdata_id(e) == 0);
    e->tags[0xFFFFFFF0u] = 3;                       // a game tag in the range would read as an id (install checks)
    CHECK(itemdata_id(e) == 0x3FFFFFF0);

    printf(fails ? "%d failure(s)\n" : "all passed\n", fails);
    return fails != 0;
}
