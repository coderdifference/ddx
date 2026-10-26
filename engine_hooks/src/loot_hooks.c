// Loot hook points (hooks.c): loot queued for generation, and generated.
// Windows: QueueGenerateElement takes its code by reference (MSVC: a copy the caller makes); QueueGenerateItem is
// inlined into its callers (an Item copy, then AddItem into the raid loot's item queue), so loot_item hooks
// Inventory::System::AddItem and acts only for that inventory (the raid's Loot::System; macOS sees any).
// Design: design/docs/13-expedition-hooks.md; offsets: docs/engine-design.md "Curio, trap, loot and scouting points".
//   loot_queue  Loot::System::QueueGenerateElement(Loot::Generate::Type const*, FixedLengthString<32>): a loot table
//               code queued (the vector overload queues each of its codes through this one). source: the generate
//               type's id ("battle", "curio", "camping", "quest", "threshold", "hero_death"...); type: the code. Before:
//               cancel drops it, or set type to another code (up to 31 characters). Loot::System::Restore re-queues
//               a saved queue through it too.
//   loot_item   Loot::System::QueueGenerateItem(Inventory::Item const&): a given item queued (battle and curio
//               rewards, quest items, camping). item: the Inventory::Item. Before: cancel drops it.
//   loot_generated  Loot::System::ProcessGenerateQueues() (this: Loot::System) empties the generate queue: a
//               std::deque at +0x08 (block map; start index +0x20, size +0x28; 128 elements of 0x20 per block) of
//               {Loot::Generate::Type const* (its id string at +0), std::vector of FixedLengthString<32> codes (0x20
//               each, text first) at +0x08 / +0x10}, generating each code's table into the result inventory
//               (Inventory::System at +0x100, GetGenerateResultInventorySystem), which the loot window shows. The
//               hook copies the queued codes, runs the original, then fires once per code (in queue order): source:
//               the generate type's id; type: the code; inventory: the result, which callbacks may add items to;
//               kind: the code's position in this batch (0 first), amount: the batch's code count. The game generates
//               every code into the one inventory before the first call, so what each code produced isn't known; a
//               client acting on the whole window (a gold bonus) does so once, at kind 0. Before: fired the same way
//               just before the original; codes a callback queues then (game_queue_loot) are generated into the same
//               window and fire after with the rest (not before). Cancel does nothing.
#include "common.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char s[32];
} LootCode; // FixedLengthString<32>
#ifdef _WIN32
typedef const LootCode *LootCodeArg; // by reference
#define CODE_TEXT(a)     ((a)->s)
#define CODE_PASS(local) (&(local))
#else
typedef LootCode LootCodeArg; // by value (on the stack)
#define CODE_TEXT(a)     ((a).s)
#define CODE_PASS(local) (local)
#endif

static void **g_raid; // &Raid::s_Instance
static void *raid(void) {
    return g_raid ? *g_raid : NULL;
}

static void (*g_queue_original)(void *loot, const void *type, LootCodeArg code);
static void queue_hook(void *loot, const void *type, LootCodeArg arg) {
    const char *id = type ? *(const char *const *)type : NULL; // Loot::Generate::Type: its id string first
    LootCode code;
    memcpy(code.s, CODE_TEXT(arg), sizeof code.s);
    char text[32];
    memcpy(text, code.s, sizeof text);
    text[31] = 0;
    HookContext c = {.raid = raid(), .kind = -1, .source = id, .type = text};
    if (!hook_emit("loot_queue", HOOK_BEFORE, &c))
        return;
    if (c.type != text) { // a callback chose another code
        memset(code.s, 0, sizeof code.s);
        if (c.type)
            strncpy(code.s, c.type, sizeof code.s - 1);
        memcpy(text, code.s, sizeof text);
        c.type = text;
    }
    g_queue_original(loot, type, CODE_PASS(code));
    hook_emit("loot_queue", HOOK_AFTER, &c);
}

#ifdef _WIN32
static uint64_t (*g_add_original)(void *inventory, void *item, bool stack);
static uint64_t add_hook(void *inventory, void *item, bool stack) { // Inventory::System::AddItem
    uint8_t *r = raid();
    if (!r || inventory != r + RAID_LOOT + LOOT_ITEMS)
        return g_add_original(inventory, item, stack);
    HookContext c = {.raid = r, .item = item, .kind = -1};
    if (!hook_emit("loot_item", HOOK_BEFORE, &c))
        return 0;
    uint64_t v = g_add_original(inventory, item, stack);
    hook_emit("loot_item", HOOK_AFTER, &c);
    return v;
}
#else
static void (*g_item_original)(void *loot, const void *item);
static void item_hook(void *loot, const void *item) {
    HookContext c = {.raid = raid(), .item = item, .kind = -1};
    if (!hook_emit("loot_item", HOOK_BEFORE, &c))
        return;
    g_item_original(loot, item);
    hook_emit("loot_item", HOOK_AFTER, &c);
}
#endif

#define MAX_CODES 64
typedef struct {
    char source[24];
    char code[32];
} Generated;

static int queued_codes(const void *loot, Generated *q) { // the generate queue's codes, in order
    int n = 0;
    const uint8_t *lb = loot;
    size_t start = *(const size_t *)(lb + QUEUE_START), size = *(const size_t *)(lb + QUEUE_SIZE);
    void *const *map = *(void *const *const *)(lb + QUEUE_MAP);
    for (size_t i = 0; map && i < size && n < MAX_CODES; i++) {
        size_t at = start + i;
#ifdef _WIN32
        at &= *(const size_t *)(lb + QUEUE_MAP_SIZE) - 1; // MSVC: the map is a ring
#endif
        const uint8_t *el = (const uint8_t *)map[at / QUEUE_BLOCK] + (at % QUEUE_BLOCK) * QUEUE_ELEMENT;
        const char *const *type = *(const char *const *const *)el;
        const char *source = type && *type ? *type : "";
        const uint8_t *b = *(const uint8_t *const *)(el + 0x08), *e = *(const uint8_t *const *)(el + 0x10);
        for (const uint8_t *code = b; b && code < e && n < MAX_CODES; code += sizeof(LootCode), n++) {
            snprintf(q[n].source, sizeof q[n].source, "%s", source);
            snprintf(q[n].code, sizeof q[n].code, "%.31s", (const char *)code);
        }
    }
    return n;
}

static void emit_codes(void *loot, const Generated *q, int n, HookPhase when) {
    for (int i = 0; i < n; i++) {
        HookContext c = {.raid = raid(),
                         .kind = i,
                         .amount = (float)n,
                         .source = q[i].source,
                         .type = q[i].code,
                         .inventory = (uint8_t *)loot + LOOT_RESULT};
        hook_emit("loot_generated", when, &c);
    }
}

static void (*g_process_original)(void *loot);
static void process_hook(void *loot) {
    Generated q[MAX_CODES];
    int n = queued_codes(loot, q);
    if (n) {
        emit_codes(loot, q, n, HOOK_BEFORE);
        n = queued_codes(loot, q); // with any codes the callbacks queued
    }
    g_process_original(loot);
    emit_codes(loot, q, n, HOOK_AFTER);
}

void loot_hooks_install(void) {
    g_raid = (void **)sym("_ZN4Raid10s_InstanceE");
    if (hook_wanted("loot_queue")) {
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
                                    0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x48}; // 5 pushes, sub rsp,0x48
        *(void **)&g_queue_original =
            install_hook("Loot::System::QueueGenerateElement (loot_queue)",
                         sym("_ZN4Loot6System20QueueGenerateElementEPKNS_8Generate4TypeE17FixedLengthStringILi32EE"),
                         (void *)queue_hook, p, sizeof p);
    }
    if (hook_wanted("loot_item")) {
#ifdef _WIN32
        *(void **)&g_add_original = install_hook("Inventory::System::AddItem (loot_item)",
                                                 sym("win:Inventory::System::AddItem"), (void *)add_hook, NULL, 0);
#else
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x48, 0x81, 0xec,
                                    0xb8, 0x05, 0x00, 0x00, 0x48, 0x89, 0xfb}; // push rbx; sub rsp,0x5b8; mov rbx,rdi
        *(void **)&g_item_original =
            install_hook("Loot::System::QueueGenerateItem (loot_item)",
                         sym("_ZN4Loot6System17QueueGenerateItemERKN9Inventory4ItemE"), (void *)item_hook, p, sizeof p);
#endif
    }
    if (hook_wanted("loot_generated")) {
        static const uint8_t p[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41,
                                    0x54, 0x53, 0x48, 0x81, 0xec, 0xc8, 0x00, 0x00, 0x00}; // 5 pushes, sub rsp,0xc8
        *(void **)&g_process_original =
            install_hook("Loot::System::ProcessGenerateQueues (loot_generated)",
                         sym("_ZN4Loot6System21ProcessGenerateQueuesEv"), (void *)process_hook, p, sizeof p);
    }
}
