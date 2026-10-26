// Windows: the game's addresses for one Darkest.exe build (win64, TimeDateStamp g_win_timestamp), by the macOS
// symbol names the features use, plus "win:" names for Windows-only hook points. The MSVC build has no
// symbols: each address was found by matching the symbolled macOS binary (strings, vtables, call order) and
// checked in the disassembly; see ../README.md. Functions MSVC inlined everywhere have stand-ins here (native),
// written from the inlined copies. After a game update the byte checks fail and the features stay off.
#include "common.h"
#ifdef _WIN32
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

const uint32_t g_win_timestamp = 0x6ab13309;

// ---------------------------------------------------------------- stand-ins for inlined functions
static uint8_t *game(const char *name) {
    return sym(name);
}

// string_hash(s, seed): h = h * 53 + c, as the inlined loops do
static unsigned string_hash(const char *s, unsigned h) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        h = h * 0x35 + *p;
    return h;
}

// Hero::RemoveQuirkWithClassId: the quirk list (hero+0x1310, 0x38 per quirk: Quirk::Class* at 0, its id hash at
// +0x40), then Hero::RemoveQuirk(index, b)
static bool remove_quirk_with_class_id(void *hero, unsigned class_hash, bool b) {
    static bool (*remove)(void *hero, unsigned index, bool b);
    if (!remove)
        remove = (bool (*)(void *, unsigned, bool))game("_ZN4Hero11RemoveQuirkEjb");
    uint8_t *qb = *(uint8_t **)((uint8_t *)hero + 0x1310), *qe = *(uint8_t **)((uint8_t *)hero + 0x1318);
    for (unsigned i = 0; remove && qb + (i + 1) * 0x38 <= qe; i++) {
        const uint8_t *cls = *(uint8_t *const *)(qb + i * 0x38);
        if (cls && *(const unsigned *)(cls + 0x40) == class_hash)
            return remove(hero, i, b);
    }
    return false;
}

// Raid::IsInBattle: the battle state (Raid+0x4b20) is not 0
static bool raid_in_battle(const void *raid) {
    return *(const uint32_t *)((const uint8_t *)raid + 0x4b20) != 0;
}

// Actor::GetWasDeathStartCalled: the flag at Actor+0x8a0, as macOS (Battle::UnloadEnemy 0x5a7cca and ApplyHit read it)
static bool actor_death_started(const void *actor) {
    return ((const uint8_t *)actor)[0x8a0] != 0;
}

// Battle::GetCurrentActor: the initiative index (+0xa0) into the actor list (+0x20, 0x18-byte entries, the actor
// first), as macOS (RaidDisplay::UpdateBattleState reads them at Raid+0x4938 / +0x48b8, the Battle at +0x4898)
static void *battle_current_actor(const void *battle) {
    const uint8_t *b = battle;
    const uint8_t *begin = *(const uint8_t *const *)(b + 0x20), *end = *(const uint8_t *const *)(b + 0x28);
    uint32_t i = *(const uint32_t *)(b + 0xa0);
    return begin && end > begin && i < (uint64_t)(end - begin) / 0x18 ? *(void *const *)(begin + i * 0x18) : NULL;
}

// EffectApplyResult::operator bool: any of the flags at +1..+3, or results in the vector at +8 (as the inlined copy
// after EffectApply in PerformActOutReaction's lambda 0x772783)
static bool effect_result_bool(const void *result) {
    const uint8_t *r = result;
    return r[1] || r[2] || r[3] || *(void *const *)(r + 8) != *(void *const *)(r + 0x10);
}

// Actor::UpdateBuffsForTarget(a, b): UpdateBuffsInternal(a, b, the current actor's skill in a battle), then the
// cached target guids (Actor+0x1010, +0x1014) from a and b; only called with a = b = NULL here
static void update_buffs_for_target(void *actor, const void *a, const void *b) {
    static void (*internal)(void *actor, const void *a, const void *b, const void *skill);
    if (!internal)
        internal = (void (*)(void *, const void *, const void *, const void *))game("win:Actor::UpdateBuffsInternal");
    uint8_t *raid = *(uint8_t **)game("_ZN4Raid10s_InstanceE");
    const uint8_t *current = raid ? *(const uint8_t *const *)(raid + 0x4968) : NULL; // the battle's current actor
    const void *skill = current ? *(const void *const *)(current + 0x8e0) : NULL;    // Actor::GetCurrentCombatSkill
    if (internal)
        internal(actor, a, b, skill);
    *(uint64_t *)((uint8_t *)actor + 0x1010) = 0;
}

// SpineSprite::HasAnimation: any of the sprite's SpineData (handles at +0x20..+0x28, 0x30 each) has it
static bool sprite_has_animation(const void *sprite, const char *name) {
    static void *(*spine_data)(const void *handle);
    static void *(*find_animation)(const void *skeleton_data, const char *name);
    if (!spine_data)
        spine_data = (void *(*)(const void *))game("win:SpineData handle");
    if (!find_animation)
        find_animation = (void *(*)(const void *, const char *))game("win:spSkeletonData_findAnimation");
    if (!sprite || !spine_data || !find_animation)
        return false;
    const uint8_t *h = *(const uint8_t *const *)((const uint8_t *)sprite + 0x20),
                  *e = *(const uint8_t *const *)((const uint8_t *)sprite + 0x28);
    for (; h && h < e; h += 0x30) {
        const uint8_t *data = spine_data(h);
        if (data && find_animation(*(const void *const *)(data + 0x10), name))
            return true;
    }
    return false;
}

// Inventory::Item's tags (ITEM_TAGS): an MSVC std::map<unsigned, unsigned>, {head node, size}; nodes: left 0, parent 8,
// right 0x10, is-nil 0x19, key 0x1c, value 0x20. Its operator[] (key by reference) returns the value's address.
static const uint8_t *item_tag_node(const void *item, unsigned tag) {
    const uint8_t *head = *(const uint8_t *const *)((const uint8_t *)item + ITEM_TAGS);
    const uint8_t *n = head ? *(const uint8_t *const *)(head + 8) : NULL;
    while (n && !n[0x19]) {
        unsigned key = *(const unsigned *)(n + 0x1c);
        if (key == tag)
            return n;
        n = *(const uint8_t *const *)(n + (tag < key ? 0 : 0x10));
    }
    return NULL;
}
static int *item_tag_value(void *item, unsigned tag) {
    static int *(*index)(void *map, const unsigned *key);
    if (!index)
        index = (int *(*)(void *, const unsigned *))game("win:Item tags operator[]");
    return index ? index((uint8_t *)item + ITEM_TAGS, &tag) : NULL;
}
// Inventory::Item::AddTag(tag, amount): amount < 1 sets the tag's count to the item's amount, else adds amount
static void item_add_tag(void *item, unsigned tag, int amount) {
    int *v = item_tag_value(item, tag);
    if (v)
        *v = amount < 1 ? *(const int *)((const uint8_t *)item + ITEM_AMOUNT) : *v + amount;
}
// Inventory::Item::RemoveTag(tag): one less of a tag the item has (count > 0); the key stays
static void item_remove_tag(void *item, unsigned tag) {
    const uint8_t *n = item_tag_node(item, tag);
    if (n && *(const int *)(n + 0x20) > 0)
        (*item_tag_value(item, tag))--;
}
// Inventory::SystemClass::GetItemClass(type hash, id hash): its vector of classes, matched on +0x40 and +0x84
static const uint8_t *get_item_class(const void *self, unsigned type_hash, unsigned id_hash) {
    const uint8_t *const *b = *(const uint8_t *const *const *)self, *const *e =
                                                                        ((const uint8_t *const *const *)self)[1];
    for (; b && b < e; b++)
        if (*(const unsigned *)(*b + 0x40) == type_hash && *(const unsigned *)(*b + ITEMCLASS_TRINKET_ID) == id_hash)
            return *b;
    return NULL;
}

// Inventory::SystemClass::GetSystemConfigClass(type) (inlined; Inventory::System::Init 0x5d3990): an MSVC
// std::map<unsigned, SystemConfigClass> at +0x18 (nodes as the item tags'), the config at the node's +0x20
static uint8_t *get_system_config(const void *self, unsigned type) {
    uint8_t *head = *(uint8_t *const *)((const uint8_t *)self + 0x18);
    uint8_t *n = head ? *(uint8_t *const *)(head + 8) : NULL;
    while (n && !n[0x19]) {
        unsigned key = *(const unsigned *)(n + 0x1c);
        if (key == type)
            return n + 0x20;
        n = *(uint8_t *const *)(n + (type < key ? 0 : 0x10));
    }
    return NULL;
}
// std::vector<Inventory::Item> (begin, end, capacity end; ITEM_SIZE each): resize is inlined (System::Init), so
// its two halves are rebuilt from the functions it calls. Growing: default-construct n at the end (0x5d6800), or
// reallocate as MSVC does (1.5x the capacity, at least the new size): std::_Allocate, construct the new items, copy
// the old ones over (0x453e40), then 0x454140 destroys and frees the old storage and sets the three pointers.
#define ITEM_VECTOR_MAX 0x2d4279a2a6e520ull
static void item_vector_append(void *vec, unsigned long n) {
    static uint8_t *(*construct)(uint8_t *at, size_t n, void *vec);
    static uint8_t *(*copy)(uint8_t *first, uint8_t *last, uint8_t *dest);
    static void (*change)(void *vec, uint8_t *buf, size_t size, size_t cap);
    static uint8_t *(*alloc)(size_t bytes);
    if (!construct) {
        construct = (uint8_t *(*)(uint8_t *, size_t, void *))game("win:vector<Item> construct n");
        copy = (uint8_t *(*)(uint8_t *, uint8_t *, uint8_t *))game("win:vector<Item> copy range");
        change = (void (*)(void *, uint8_t *, size_t, size_t))game("win:vector<Item> change array");
        alloc = (uint8_t *(*)(size_t))game("_Znwm");
    }
    uint8_t **v = (uint8_t **)vec;
    if (!construct || !copy || !change || !alloc || !n)
        return;
    size_t size = (size_t)(v[1] - v[0]) / ITEM_SIZE, cap = (size_t)(v[2] - v[0]) / ITEM_SIZE;
    if (cap - size >= n) {
        v[1] = construct(v[1], n, vec);
        return;
    }
    size_t want = size + n, grown = cap + cap / 2;
    if (want > ITEM_VECTOR_MAX)
        return;
    size_t new_cap = cap > ITEM_VECTOR_MAX - cap / 2 || grown < want ? want : grown;
    uint8_t *buf = alloc(new_cap * ITEM_SIZE);
    if (!buf)
        return;
    construct(buf + size * ITEM_SIZE, n, vec);
    copy(v[0], v[1], buf);
    change(vec, buf, want, new_cap);
}
// its shrinking half: destroy the items from new_end on, then end = new_end
static void item_vector_destruct_at_end(void *vec, uint8_t *new_end) {
    static void (*dtor)(void *item);
    if (!dtor)
        dtor = (void (*)(void *))game("_ZN9Inventory4ItemD1Ev");
    uint8_t **v = (uint8_t **)vec;
    if (!dtor)
        return;
    for (uint8_t *p = new_end; p < v[1]; p += ITEM_SIZE)
        dtor(p);
    v[1] = new_end;
}

// operator delete: the game allocates from the UCRT heap, as this library does (built against ucrtbase)
static void op_delete(void *p) {
    free(p);
}

// ---------------------------------------------------------------- UI stand-ins (ui.c, paging.c, sheet.c, tooltip.c)
#define GUI_FONT        0x2612558 // the current font (GUI_SetFont; GUI_Text reads it)
#define TRANSFORM_STACK 0x1174b10 // mat4 levels at +0x23c (0x40 each), depth at +0x64, current matrix at +0x68
#define CAMPAIGN_ASSETS                                                                                                \
    0x2bedad0 // UI::CampaignAssets: the skill grid layout (+0x1498) is 0x142beef68, as
              // CharacterDisplay::ShowSkillGrid gets it, and the lock icon 0x110 after it
static uint8_t *image(void) {
    return (uint8_t *)GetModuleHandleA(NULL);
}

static void gui_set_font(unsigned id) {
    static void *(*font)(unsigned id);
    if (!font)
        font = (void *(*)(unsigned))game("_ZN11FontLibrary13GetFontFromIdEj");
    void *f = font ? font(id) : NULL;
    if (f)
        *(void **)(image() + GUI_FONT) = f;
}

// A texture handle copied into parameters takes a reference, as the game's constructors do (the destructor,
// TextParameters::~ 0x271090, releases it): ResourceManager::s_Instance's entries (RM_ENTRIES), the generation must
// match. The base constructor leaves the parameters' own handle empty, so there is nothing to release first.
static void handle_add_ref(const void *texture) {
    const int *h = texture;
    void **rm = (void **)game("_ZN15ResourceManager10s_InstanceE");
    if (!h || !rm || !*rm || (h[0] | h[1]) < 0)
        return;
    uint8_t *e = *(uint8_t **)((uint8_t *)*rm + RM_ENTRIES) + (size_t)h[0] * RM_ENTRY_SIZE;
    if (*(const int *)(e + RM_ENTRY_GENERATION) == h[1])
        InterlockedIncrement((volatile LONG *)(e + RM_ENTRY_REFS));
}

// ImageButtonParameters' constructors (inlined): the base constructor's defaults, then the fields
static void button_params(uint8_t *p, Vec2 pos, const void *texture, uint64_t id, bool enabled, const void *tooltip) {
    static void (*base)(void *p);
    if (!base)
        base = (void (*)(void *))game("win:ImageButtonParameters base");
    if (!base)
        return;
    base(p);
    *(uint64_t *)(p + 0x08) = id;
    memcpy(p + 0x18, &pos, sizeof pos);
    handle_add_ref(texture);
    memcpy(p + 0x40, texture, 16);
    *(const void **)(p + 0x50) = tooltip;
    p[0xa1] = enabled; // false: drawn, no hover or click
    p[0x31e] = 1;
}
static void button_params32(uint8_t *p, Vec2 pos, const void *texture, unsigned id, const void *tooltip) {
    button_params(p, pos, texture, id, true, tooltip);
}

static void *party_selected_hero(const void *party) {
    return *(void *const *)((const uint8_t *)party + 0x430);
}
static void panel_set_base_pos(uint8_t *panel, Vec2 pos) {
    memcpy(panel + 0x64, &pos, sizeof pos);
} // UI::Panel::Base

static void transform_push(void) {
    uint8_t *t = image() + TRANSFORM_STACK;
    uint32_t *depth = (uint32_t *)(t + 0x64);
    memcpy(t + 0x23c + (*depth + 1) * 0x40, t + 0x23c + *depth * 0x40, 0x40);
    (*depth)++;
}
static void transform_pop(void) {
    uint8_t *t = image() + TRANSFORM_STACK;
    uint32_t *depth = (uint32_t *)(t + 0x64);
    (*depth)--;
    memcpy(t + 0x68, t + 0x23c + *depth * 0x40, 0x40);
}
static void transform_translate(const Vec2 *v) {
    static void (*multiply)(const float *m);
    if (!multiply)
        multiply = (void (*)(const float *))game("win:Transform_Multiply");
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    m[12] = v->x;
    m[13] = v->y;
    if (multiply)
        multiply(m);
}

static void *campaign_assets(void) {
    return image() + CAMPAIGN_ASSETS;
}
// Inventory::Display::ShowInventoryItemHighlight(type): the slot highlight (1 valid, 2 invalid) at CampaignAssets
// +0xec8, at its own size, in the current transform (the textures CampaignAssets loads from these paths)
static void inventory_item_highlight(int type) {
    if (type != 1 && type != 2)
        return;
    uint8_t tex[16];
    if (ui_texture(type == 1 ? "overlays/eqp_mouseover.png" : "overlays/eqp_unavailable_mouseover.png", tex))
        ui_image(*(const Vec2 *)((const uint8_t *)campaign_assets() + ASSETS_HIGHLIGHT_OFFSET), tex);
}
// UI::Panel::Base::GetIsActive: the state at +0x58 is not 0 (macOS +0x50; BuildingDisplay::GetChildPanelOffset's copy)
static bool panel_is_active(const void *panel) {
    return *(const int *)((const uint8_t *)panel + 0x58) != 0;
}

// Hero: skill levels (vector<int>, -1: not owned) at +0x1370, selected skill indices (vector<unsigned>) at +0x1358
static int hero_skill_level(const void *hero, unsigned i) {
    const int *b = *(const int *const *)((const uint8_t *)hero + 0x1370),
              *e = *(const int *const *)((const uint8_t *)hero + 0x1378);
    return b && i < (unsigned)(e - b) ? b[i] : -1;
}
static bool hero_skill_selected(const void *hero, unsigned i) {
    const unsigned *b = *(const unsigned *const *)((const uint8_t *)hero + 0x1358),
                   *e = *(const unsigned *const *)((const uint8_t *)hero + 0x1360);
    for (const unsigned *p = b; p && p < e; p++)
        if (*p == i)
            return true;
    return false;
}

// GuiTooltipParameters::Text: lines (0x28 each) at +0x10, count at +0x510; Line: string +0x18, flag +0x20,
// length +0x24; SetString copies into the frame's widget string pool (AllocateWidgetString)
static void line_set_string(uint8_t *line, const char *s) {
    static char *(*alloc)(const char *s, unsigned len);
    if (!alloc)
        alloc = (char *(*)(const char *, unsigned))game("win:AllocateWidgetString");
    unsigned len = (unsigned)strlen(s);
    *(uint32_t *)(line + 0x24) = len;
    *(char **)(line + 0x18) = alloc ? alloc(s, len) : NULL;
    line[0x20] = 1;
}
static void text_add_line(uint8_t *text, const void *line) {
    uint32_t *count = (uint32_t *)(text + 0x510);
    memcpy(text + 0x10 + *count * 0x28, line, 0x28);
    (*count)++;
}
static void text_clear(uint8_t *text) {
    *(uint32_t *)(text + 0x510) = 0;
}
static const char *loc_string(const void *ls) {
    return *(const char *const *)ls;
} // LocalizedString: the text first

// HeroDisplay::App::CombatSkillName / CombatSkillInfo: the shared assets' HeroData for the class (a map by the class's
// hash at +0xd4; two maps, the game picks one by a flag), its skill map (+0x10 in HeroData, +0x38 in the node) by the
// skill's id hash, one CombatSkillData (0x610) per level: pips 0, name 0x200, info 0x400
static const char *skill_text(const uint8_t *cls, const uint8_t *skill, size_t field) {
    static void *(*insert)(void *map, void *result, const unsigned *key);
    static uint8_t **(*index)(void *map, const unsigned *key);
    if (!insert)
        insert = (void *(*)(void *, void *, const unsigned *))game("win:HeroData map insert");
    if (!index)
        index = (uint8_t **(*)(void *, const unsigned *))game("win:SkillData map index");
    if (!insert || !index || !cls || !skill)
        return NULL;
    uint8_t *map = image() + (image()[0x2a5c08b] ? 0x2bfae80 : 0x2bfae70);
    uint8_t result[16];
    insert(map, result, (const unsigned *)(cls + 0xd4));
    uint8_t *node = *(uint8_t **)result;
    uint8_t **vec = index(node + 0x38, (const unsigned *)(skill + 0x4c));
    unsigned level = *(const unsigned *)(skill + 0x48);
    if (!vec || !vec[0] || (size_t)(vec[1] - vec[0]) / 0x610 <= level)
        return NULL;
    return (const char *)(vec[0] + level * 0x610 + field);
}
static const char *combat_skill_name(const void *cls, const void *skill) {
    return skill_text(cls, skill, 0x200);
}
static const char *combat_skill_info(const void *cls, const void *skill) {
    return skill_text(cls, skill, 0x400);
}

// ActorDisplay::App::GetCombatSkillName(class, skill, with level): the skill's name format (its vtable slot 0; slot 1:
// whether the format takes the class id, at +0x48, before the skill id at +8), localized. The level suffix
// (with level) isn't made: tooltip.c asks without it.
typedef struct {
    uint64_t a;
    uint32_t b;
    uint8_t c;
} LocStr;
static char *win_actor_skill_name(char *out, const uint8_t *cls, const uint8_t *skill, bool with_level) {
    static LocStr (*localize)(const char *id);
    if (!localize)
        localize = (LocStr (*)(const char *))game("_ZN12Localization20LocalizeStringFromIdEPKc");
    out[0] = 0;
    void *const *vt = *(void *const *const *)skill;
    const char *fmt = ((const char *(*)(const void *))vt[0])(skill);
    if (!fmt || !localize)
        return out;
    char id[64];
    if (((bool (*)(const void *))vt[1])(skill))
        snprintf(id, sizeof id, fmt, cls + 0x48, skill + 8);
    else
        snprintf(id, sizeof id, fmt, skill + 8);
    LocStr ls = localize(id);
    const char *s = *(const char *const *)&ls;
    snprintf(out, 64, "%s", s ? s : "");
    (void)with_level;
    return out;
}

// Actor::GetCombatStat: RaidStats (0x68 each) from Actor+0x490, as on macOS
static const uint8_t *actor_combat_stat(const void *actor, int type) {
    return (const uint8_t *)actor + 0x490 + type * 0x68;
}

// TextureFont_CatColourStart / End: "<c>" + the colour name's hash in six letters ('A' + 6 bits each), "</c>"
static void colour_start(char *fls512, const char *colour) {
    unsigned h = string_hash(colour, 0);
    char enc[7];
    for (int k = 0; k < 5; k++)
        enc[k] = (char)('A' + ((h >> (6 * k)) & 0x3f));
    enc[5] = (char)('A' + (h >> 30));
    enc[6] = 0;
    size_t l = strnlen(fls512, 512);
    if (l < 512)
        snprintf(fls512 + l, 512 - l, "<c>%s", enc);
}
static void colour_end(char *fls512) {
    size_t l = strnlen(fls512, 512);
    if (l < 512)
        snprintf(fls512 + l, 512 - l, "</c>");
}

// ColourLibrary::GetEntry(id) takes a "found" flag on Windows
static void *colour_entry(void *out, unsigned id) {
    static void *(*get)(void *out, unsigned id, bool *found);
    if (!get)
        get = (void *(*)(void *, unsigned, bool *))game("win:ColourLibrary::GetEntry");
    bool found = false;
    return get ? get(out, id, &found) : out;
}
static uint64_t colour_entry_hvec4(const void *entry) {
    return *(const uint64_t *)((const uint8_t *)entry + 0x44);
}

// ---------------------------------------------------------------- Guild stand-ins (guild.c)
#define ALT_ROSTER_FLAG    0x2a5c08b // the flag macOS reads at Darkest+0x54b: the campaign's other roster (+0x728)
#define ALL_PURCHASED_FLAG 0x2b54690 // Upgrade: every purchase counts as made
#define PURCHASES          0x2b54640 // Upgrade: map<PurchaseIdentifier, Purchase>, purchased flag at node+0x28
#define TREE_CLASSES       0x2c29fa0 // Upgrade: vector<TreeClass>, 0x70 each: id hash +0x40, per hero +0x44, tags +0x48

// Campaign::CurrentRoster: the campaign (its roster first), or its other roster under the flag
static uint8_t *current_roster(void) {
    static uint8_t *(*current)(void);
    if (!current)
        current = (uint8_t *(*)(void))game("win:Campaign::Current");
    uint8_t *c = current ? current() : NULL;
    return c && image()[ALT_ROSTER_FLAG] ? c + 0x728 : c;
}
// Roster::System::GetHero: entries (guid first, the Hero at +8) at roster+0x20
static void *roster_hero(const uint8_t *roster, unsigned guid) {
    uint8_t *const *b = roster ? *(uint8_t *const *const *)(roster + 0x20) : NULL,
                   *const *e = roster ? *(uint8_t *const *const *)(roster + 0x28) : NULL;
    for (uint8_t *const *p = b; p && p < e; p++)
        if (*p && *(const unsigned *)*p == guid)
            return *p + 8;
    return NULL;
}

// Upgrade::GetIsPurchased(PurchaseIdentifier {tree, code, guid at +8}): the purchases map's entry (made if missing,
// as the game does)
static bool upgrade_is_purchased(const uint8_t *id) {
    static void *(*insert)(void *map, void *result, const void *key);
    if (!insert)
        insert = (void *(*)(void *, void *, const void *))game("win:Upgrade purchases insert");
    if (image()[ALL_PURCHASED_FLAG])
        return true;
    if (!insert)
        return false;
    uint8_t result[16];
    insert(image() + PURCHASES, result, id);
    return (*(uint8_t **)result)[0x28] != 0;
}
static const uint8_t *upgrade_req_class(const uint8_t *id) {
    static const uint8_t *(*get)(unsigned tree, char code);
    if (!get)
        get = (const uint8_t *(*)(unsigned, char))game("win:Upgrade::GetRequirementClass");
    return get ? get(*(const unsigned *)id, (char)id[4]) : NULL;
}
static const uint8_t *upgrade_tree_class(unsigned hash) {
    const uint8_t *b = *(const uint8_t *const *)(image() + TREE_CLASSES),
                  *e = *(const uint8_t *const *)(image() + TREE_CLASSES + 8);
    for (const uint8_t *tc = b; tc && tc < e; tc += 0x70)
        if (*(const unsigned *)(tc + 0x40) == hash)
            return tc;
    return NULL;
}
static bool upgrade_tree_has_tag(const uint8_t *tc, unsigned tag) {
    const unsigned *b = *(const unsigned *const *)(tc + 0x48), *e = *(const unsigned *const *)(tc + 0x50);
    for (const unsigned *p = b; p && p < e; p++)
        if (*p == tag)
            return true;
    return false;
}
// Upgrade::GetIsLocked: a prerequisite (RequirementClass +0x20: {tree, code} 8 bytes each; per hero if its tree is)
// not bought, or the hero's resolve level below the requirement's (+0x38)
static bool upgrade_is_locked(const uint8_t *id) {
    const uint8_t *req = upgrade_req_class(id);
    if (!req)
        return false;
    const uint8_t *b = *(const uint8_t *const *)(req + 0x20), *e = *(const uint8_t *const *)(req + 0x28);
    unsigned guid = *(const unsigned *)(id + 8);
    for (const uint8_t *p = b; p && p < e; p += 8) {
        const uint8_t *tc = upgrade_tree_class(*(const unsigned *)p);
        uint8_t key[12] = {0};
        memcpy(key, p, 5);
        unsigned g = tc && tc[0x44] ? guid : 0;
        memcpy(key + 8, &g, 4);
        if (!upgrade_is_purchased(key))
            return true;
    }
    unsigned resolve = *(const unsigned *)(req + 0x38);
    if (!resolve)
        return false;
    void *hero = roster_hero(current_roster(), guid);
    if (!hero)
        return true;
    float level = ((float (*)(const void *))(*(void *const *const *)hero)[0x168 / 8])(hero); // resolve level
    return (float)resolve > level;
}
// Upgrade::GetCanAffordPurchase is inlined in AttemptPurchase with the estate's checks (free upgrade events,
// discounts): not reproduced; the Guild's cost line isn't shown in red on Windows
static bool upgrade_can_afford(const void *id) {
    (void)id;
    return true;
}
static void currency_dtor(void *c) {
    (void)c;
} // Estate::Currency's destructor is empty
static void fls64_assign(char *fls, const char *s) {
    snprintf(fls, 64, "%s", s ? s : "");
}

// ImageParameters(vec2, ResourceHandle<Texture>) and GUI_Image (inlined): the base constructor, position and texture;
// drawing resolves the texture handle and adds an image widget (type 5)
static void image_params(uint8_t *p, Vec2 pos, const void *texture) {
    static void (*base)(void *p);
    if (!base)
        base = (void (*)(void *))game("win:ImageButtonParameters base");
    if (!base)
        return;
    base(p);
    memcpy(p + 0x18, &pos, sizeof pos);
    handle_add_ref(texture);
    memcpy(p + 0x40, texture, 16);
}
static void gui_image(uint8_t *p) {
    static void *(*resolve)(void *handle);
    static void (*append)(int type, const void *params);
    if (!resolve)
        resolve = (void *(*)(void *))game("win:ResourceHandle<Texture> resolve");
    if (!append)
        append = (void (*)(int, const void *))game("win:GUI_AppendWidget");
    if (!resolve || !append)
        return;
    resolve(p + 0x40);
    append(5, p);
}

// ---------------------------------------------------------------- the table
// ---------------------------------------------------------------- items.c (item drags, the stash's validity)
// InputManager::IsDragging: the flag at +0x1bc (as macOS)
static bool input_dragging(const uint8_t *im) {
    return im[0x1bc] != 0;
}
// InventoryItemDragData (as macOS): the item at +8, its slot +0x5f0, the amount +0x5f4
static const uint8_t *drag_get_item(const uint8_t *drag) {
    return drag + 8;
}
static unsigned drag_slot(const uint8_t *drag) {
    return *(const unsigned *)(drag + 0x5f0);
}
static void drag_set_item(uint8_t *drag, const void *item, unsigned slot) {
    static void (*assign)(void *dst, const void *src); // Inventory::Item::operator=
    if (!assign)
        assign = (void (*)(void *, const void *))game("win:Inventory::Item::operator=");
    if (item && assign)
        assign(drag + 8, item);
    *(int *)(drag + 0x5f4) = *(const int *)(drag + 8);
    *(unsigned *)(drag + 0x5f0) = slot;
}
// equip_slots.c. InventorySlotInterface's getters (the grid reads +0x30 and +0xbc inline; OnPickedUp sets +0xc0);
// Item::IsValid (the grid's inline test: the first int > 0); EquipmentDragData::SetHero (OnPickedUp writes +0x600).
static const uint8_t *slot_item(const uint8_t *slot) {
    return *(const uint8_t *const *)(slot + SLOT_ITEM);
}
static bool slot_picked_up(const uint8_t *slot) {
    return slot[SLOT_PICKED] != 0;
}
static int slot_highlight_type(const uint8_t *slot) {
    return *(const int *)(slot + SLOT_HIGHLIGHT);
}
static bool item_valid(const uint8_t *item) {
    return *(const int *)item > 0;
}
static void equip_drag_set_hero(uint8_t *data, void *hero) {
    *(void **)(data + EQUIP_DRAG_HERO) = hero;
}
// Item::GetTrinketClass, as Actor::UpdateTrinketBuffs (0x47ca10) inlines it: the cached class at +0x5a0, else the first
// of Trinket::Library's classes (std::vector at 0x142c2aa20, 0x428 bytes each, the id hash at +0x40) with the item's id
#define TRINKET_LIBRARY 0x2c2aa20
// Trinket::Library::GetTrinketClass(id) (inlined on Windows): that search alone, as macOS's
static const uint8_t *trinket_library_class(unsigned id) {
    const uint8_t *const *v = (const uint8_t *const *)(image() + TRINKET_LIBRARY);
    for (const uint8_t *c = v[0]; c && c != v[1]; c += 0x428)
        if (*(const unsigned *)(c + 0x40) == id)
            return c;
    return NULL;
}
static const uint8_t *item_trinket_class(const uint8_t *item) {
    const uint8_t *cls = *(const uint8_t *const *)(item + ITEM_TRINKET_CLASS);
    return cls ? cls : trinket_library_class(*(const unsigned *)(item + ITEM_ID_HASH));
}
// CampaignAssets::Instance::GetTrinketRarity(hash): the last rarity entry with the hash (PrizeBoothDisplay::
// BuildTrinketRewardWidget 0x658980 inlines it on the vector at CampaignAssets + 0x1bb0, as macOS)
static const uint8_t *trinket_rarity(const uint8_t *assets, unsigned hash) {
    const uint8_t *const *v = (const uint8_t *const *)(assets + ASSETS_RARITIES), *found = NULL;
    for (const uint8_t *e = v[0]; e && e != v[1]; e += 0x18)
        if (*(const unsigned *)e == hash)
            found = e;
    return found;
}
// Estate::System::GetTrinketStorage(type): 1 the estate's +0x38, 2 its +0xa0, else by the campaign's other-roster flag
// (RealmInventory::ComputeAndSortTrinkets: Campaign::Current() + 0xe88 or + 0xef0, the estate being at + 0xe50)
static void *trinket_storage(uint8_t *estate, int type) {
    if (type == 1)
        return estate + 0x38;
    if (type == 2)
        return estate + 0xa0;
    return estate + (*(const uint8_t *)(image() + ALT_ROSTER_FLAG) ? 0xa0 : 0x38);
}
// Inventory::System::GetIsRoomForItem: the item's amount fits in GetSpaceForItem
static bool inventory_has_room(const void *system, const void *item) {
    static int (*space)(const void *system, const void *item);
    if (!space)
        space = (int (*)(const void *, const void *))game("win:Inventory::System::GetSpaceForItem");
    return space && *(const int *)item <= space(system, item);
}
// Inventory::System::AddCopyOfItem: AddItem a copy
static unsigned inventory_add_copy(void *system, const void *item) {
    static void (*copy)(void *dst, const void *src);
    static void (*destroy)(void *item);
    static unsigned (*add)(void *system, void *item, bool b);
    if (!copy)
        copy = (void (*)(void *, const void *))game("_ZN9Inventory4ItemC2ERKS0_");
    if (!destroy)
        destroy = (void (*)(void *))game("_ZN9Inventory4ItemD1Ev");
    if (!add)
        add = (unsigned (*)(void *, void *, bool))game("win:Inventory::System::AddItem");
    if (!copy || !destroy || !add)
        return 0;
    _Alignas(16) uint8_t tmp[ITEM_SIZE];
    memset(tmp, 0, sizeof tmp);
    copy(tmp, item);
    unsigned r = add(system, tmp, true);
    destroy(tmp);
    return r;
}
// Loot::System::QueueOwnedItem(item): AddCopyOfItem into the owned items (inlined in Hero::OnDeathFinishInternal)
static void loot_queue_owned(void *loot, const void *item) {
    inventory_add_copy((uint8_t *)loot + LOOT_OWNED, item);
}
// Loot::System::SetGenerateType(type)
static void loot_set_generate_type(void *loot, const void *type) {
    *(const void **)((uint8_t *)loot + LOOT_GENERATE_TYPE) = type;
}
// Quest::RaidInstance::GetDifficulty
static int quest_difficulty(const void *quest) {
    return *(const int *)((const uint8_t *)quest + QUEST_DIFFICULTY);
}
// Building::Store::BaseTrinketSupply::GetInventorySystem
static void *supply_inventory(void *supply) {
    return (uint8_t *)supply + SUPPLY_INVENTORY;
}
// Estate::SystemClass::GetNumberOfCurrencyClasses / GetCurrencyClassFromIndex: its vector of class pointers at +0
// (both inlined in EstateSummary::PreChildrenShowInternal)
static unsigned estate_class_count(const void *sc) {
    void *const *b = *(void *const *const *)sc, *const *e = *((void *const *const *)sc + 1);
    return (unsigned)(e - b);
}
static void *estate_class_at(const void *sc, unsigned i) {
    return i < estate_class_count(sc) ? (*(void *const *const *)sc)[i] : NULL;
}
// Estate::System::GetAmount(hash): the currency of that type's amount (as macOS: the first entry's if none matches)
static int estate_amount(const void *estate, unsigned hash) {
    const uint8_t *b = *(const uint8_t *const *)((const uint8_t *)estate + ESTATE_CURRENCIES);
    const uint8_t *e = *(const uint8_t *const *)((const uint8_t *)estate + ESTATE_CURRENCIES + 8);
    if (b == e)
        return 0; // the game would read the empty vector's begin
    for (const uint8_t *c = b; c < e; c += ESTATE_CURRENCY_SIZE)
        if (*(const unsigned *)(c + ESTATE_CURRENCY_TYPE_HASH) == hash)
            return *(const int *)c;
    return *(const int *)b;
}
// Inventory::Display::GetTitle(item): "str_inventory_title_<type><id>" from its class (no id for a journal page), localized
static GameLocString item_title(const uint8_t *item) {
    static GameLocString (*localize)(const char *id);
    if (!localize)
        localize = (GameLocString (*)(const char *))game("_ZN12Localization20LocalizeStringFromIdEPKc");
    void **classes = (void **)game("_ZN9Inventory11SystemClass10s_InstanceE");
    const uint8_t *cls = classes && *classes ? get_item_class(*classes, *(const unsigned *)(item + ITEM_TYPE_HASH),
                                                              *(const unsigned *)(item + ITEM_ID_HASH))
                                             : NULL;
    if (!cls || !localize)
        return (GameLocString){NULL, 0};
    char id[256];
    snprintf(id, sizeof id, "str_inventory_title_%s%s", (const char *)cls,
             strcmp((const char *)cls, "journal_page") ? (const char *)cls + 0x44 : "");
    return localize(id);
}
// TownUI::Screen::TownDisplay::GetCharacterPanelHero: the character panel's hero while it's active (town +0x3918, its
// hero +0x3a00; the stash's validity check reads them inline)
static const void *town_panel_hero(const uint8_t *town) {
    return town && *(const int *)(town + 0x3918) ? *(const void *const *)(town + 0x3a00) : NULL;
}
// announce.c: TownDisplay's panel checks, inlined on Windows. A panel is active while its state (UI::Panel::Base
// +0x58, macOS +0x50) isn't 0. The town's panels (GetIsVisibleById 0x6cc4a0; RTTI of the panels the sub-object ctor
// 0x6ce970 builds): TownEventDisplay at +0x4670, NewsPopup at +0x4910. Buildings: the vector at +0x5548 / +0x5550
// (IsAnyHamletDisplayActive 0x6d3920 loops it).
static bool town_news_visible(const uint8_t *town) {
    return *(const int *)(town + 0x4910 + 0x58) != 0;
}
static bool town_event_visible(const uint8_t *town) {
    return *(const int *)(town + 0x4670 + 0x58) != 0;
}
static bool town_any_building(const uint8_t *town) {
    const uint8_t *const *b = *(const uint8_t *const *const *)(town + 0x5548),
                         *const *e = *(const uint8_t *const *const *)(town + 0x5550);
    for (; b && b < e; b++)
        if (*b && *(const int *)(*b + 0x58))
            return true;
    return false;
}

// ---------------------------------------------------------------- ui_draw.c (images, drop and drag targets, sprites)
// Transform_Top: the stack's current level
static const uint8_t *transform_top(void) {
    uint8_t *t = image() + TRANSFORM_STACK;
    return t + 0x23c + (size_t)*(const uint32_t *)(t + 0x64) * 0x40;
}
// GUI_Image_DrawNow: GUI_Draw_Image (macOS: a jump to it)
static void gui_image_now(const void *p) {
    static void (*draw)(const void *p);
    if (!draw)
        draw = (void (*)(const void *))game("_Z14GUI_Draw_ImageRK16WidgetParameters");
    if (draw)
        draw(p);
}
// GUI_Sprite: GUI_AppendWidget(9, params)
static void gui_sprite(const void *p) {
    static void (*append)(int type, const void *params);
    if (!append)
        append = (void (*)(int, const void *))game("win:GUI_AppendWidget");
    if (append)
        append(9, p);
}
// GUI_HandleDropContext / DragContext(WidgetParameters const&), inlined in GUI_DrawWidget (types 10 / 11): the
// rect's corner (pos + OffsetFromAnchor(anchor, size), z 0) and size to the vec3 / vec2 overloads
static void handle_context(const uint8_t *w, const char *overload) {
    static void (*offset)(Vec2 *out, int anchor, const Vec2 *size);
    if (!offset)
        offset = (void (*)(Vec2 *, int, const Vec2 *))game("win:OffsetFromAnchor");
    void (*handle)(const void *w, const float *pos, const Vec2 *size) =
        (void (*)(const void *, const float *, const Vec2 *))game(overload);
    if (!offset || !handle)
        return;
    Vec2 size = *(const Vec2 *)(w + W_SIZE_V), off;
    offset(&off, *(const int *)(w + W_ANCHOR), &size);
    float pos[3] = {*(const float *)(w + 0x18) + off.x, *(const float *)(w + 0x1c) + off.y, 0};
    handle(w, pos, &size);
}
static void handle_drop(const uint8_t *w) {
    handle_context(w, "_Z21GUI_HandleDropContextRK16WidgetParametersRK4vec3RK4vec2");
}
static void handle_drag(const uint8_t *w) {
    handle_context(w, "_Z21GUI_HandleDragContextRK16WidgetParametersRK4vec3RK4vec2");
}

// ---------------------------------------------------------------- ui_tooltip.c (tooltip lines, panel callbacks)
#define RENDER_STATE    0x1174ae0 // the renderer's state (a pointer): the draw colour (hvec4) at +0x44
#define ENCODED_COLOURS 0x2c27018 // g_EncodedColourEnabled, an MSVC vector<bool>: words, then the bit count +0x18
// Draw_Colour(hvec4)
static void draw_colour_hvec4(uint64_t c) {
    uint8_t *state = *(uint8_t **)(image() + RENDER_STATE);
    if (state)
        memcpy(state + 0x44, &c, 8);
}
// TextureFont_PushEncodedColourEnabled(on): push_back on the vector<bool>, as GUI_TooltipRender's inlined copy does
// (vector<bool>::insert(result, end, 1, &on)); Pop: its pop_back
static void encoded_push(bool on) {
    static void *(*insert)(void *v, void *result, const void *where, size_t count, const bool *value);
    if (!insert)
        insert = (void *(*)(void *, void *, const void *, size_t, const bool *))game("win:vector<bool>::insert");
    if (!insert)
        return;
    uint8_t *v = image() + ENCODED_COLOURS;
    size_t n = *(const size_t *)(v + 0x18);
    struct {
        uint32_t *word;
        size_t bit;
    } where = {*(uint32_t **)v + (n >> 5), n & 31};
    uint8_t result[16];
    insert(v, result, &where, 1, &on);
}
static void encoded_pop(void) {
    static void (*pop)(void *v);
    if (!pop)
        pop = (void (*)(void *))game("win:vector<bool>::pop_back");
    if (pop)
        pop(image() + ENCODED_COLOURS);
}
// GUI_Callback: GUI_AppendWidget(0xc, params)
static void gui_callback(const void *p) {
    static void (*append)(int type, const void *params);
    if (!append)
        append = (void (*)(int, const void *))game("win:GUI_AppendWidget");
    if (append)
        append(0xc, p);
}
// WidgetCallbackParameters(std::function<void(WidgetParameters const*)>): the base constructor, then the function moved
// to +0x60 (its _Func_base* at +0x98, as GUI_DrawWidget's type 0xc reads it). By value: the callee (here) empties the
// argument (_Move(where) into the new place, then _Delete_this(false) on the moved-from one in its own buffer).
static void callback_params(uint8_t *p, StdFunction *fn) {
    static void (*base)(void *p);
    if (!base)
        base = (void (*)(void *))game("win:ImageButtonParameters base");
    if (!base)
        return;
    base(p);
    StdFunction *dst = (StdFunction *)(p + 0x60);
    dst->f = NULL;
    void **impl = fn ? fn->f : NULL;
    if (!impl)
        return;
    if ((void *)impl == (void *)fn->buf) {
        void **vtbl = *(void ***)impl;
        dst->f = ((void *(*)(void *, void *))vtbl[1])(impl, dst->buf);
        ((void (*)(void *, bool))vtbl[4])(impl, false);
    } else
        dst->f = impl;
    fn->f = NULL;
}
// GuiTooltipParameters::~: releases its background texture (+0x528) and empties the handle
static void tip_params_dtor(uint8_t *tip) {
    static void (*release)(void *rm, const void *handle);
    if (!release)
        release = (void (*)(void *, const void *))game("_ZN15ResourceManager7ReleaseERK19ResourceEntryHandle");
    void **rm = (void **)game("_ZN15ResourceManager10s_InstanceE");
    const int *h = (const int *)(tip + 0x528);
    if (h[0] < 0 || h[1] < 0)
        return;
    if (rm && *rm && release)
        release(*rm, tip + 0x528);
    *(uint64_t *)(tip + 0x528) = 0xFFFFFFFF00000000ull;
    *(uint64_t *)(tip + 0x530) = 0;
}
// Texture_GetSize(handle): the texture's width and height (+0x14, u32s)
static Vec2 texture_get_size(const void *tex) {
    Vec2 v = {0, 0};
    ui_texture_size(tex, &v);
    return v;
}

// ---------------------------------------------------------------- item_types.c (types brought home, saved, sounds)
// Inventory::System (as macOS): the items' vector at +8 / +0x10 (ITEM_SIZE each), how many hold something at +0x24
static unsigned inventory_count(const uint8_t *system) {
    return *(const unsigned *)(system + 0x24);
}
// Inventory::System::GetItem(i) const: the i-th item whose amount is > 0 (Estate::System::AddInventorySystem's loop);
// NULL past the last (macOS asserts and returns the end)
static const uint8_t *inventory_get_item(const uint8_t *system, unsigned i) {
    const uint8_t *b = *(const uint8_t *const *)(system + 8), *e = *(const uint8_t *const *)(system + 0x10);
    for (const uint8_t *p = b; p && p + ITEM_SIZE <= e; p += ITEM_SIZE)
        if (*(const int *)p > 0 && !i--)
            return p;
    return NULL;
}
// Inventory::Item::GetHasTag(tag, amount): the tag's count (0 if absent) == amount; the tags an MSVC map at
// ITEM_TAGS (its head node: parent +8 the root, nil nodes byte 0x19 set; left 0, right 0x10, key 0x1c, count 0x20)
static bool item_has_tag(const uint8_t *item, unsigned tag, int amount) {
    const uint8_t *head = *(const uint8_t *const *)(item + ITEM_TAGS), *found = head;
    for (const uint8_t *n = head ? *(const uint8_t *const *)(head + 8) : NULL; n && !n[0x19];)
        if (*(const unsigned *)(n + 0x1c) < tag)
            n = *(const uint8_t *const *)(n + 0x10);
        else {
            found = n;
            n = *(const uint8_t *const *)n;
        }
    int count = found && found != head && *(const unsigned *)(found + 0x1c) == tag ? *(const int *)(found + 0x20) : 0;
    return count == amount;
}

// ---------------------------------------------------------------- ui_input.c (mouse position, wheel, clip)
// All inlined on Windows. The globals (MSVC reordered them; the SDL event handler 0x3703e0 writes them, 0x234830
// clears the deltas each frame): g_mousex / y (motion x / y), g_mousewheeldeltax / y. s_Canvas as macOS: the display
// offset +0, scale +8, size +0x10. The clip (TRANSFORM_STACK, glstate on macOS): on +0x20, rect {x0, x1, y0, y1}
// +0x44 in the space of the matrix at +0x68 (View_SetClip 0x26bce0 writes them, the inlined tests read them).
#define MOUSE_X       0x117cda8
#define MOUSE_Y       0x117cdac
#define MOUSE_WHEEL_X 0x117d004
#define MOUSE_WHEEL_Y 0x117cfd4
#define CANVAS        0x2c28730
// plinthGetCanvasSize(float&, float&)
static float canvas_size(float *w, float *h) {
    const float *c = (const float *)(image() + CANVAS);
    *w = c[4];
    *h = c[5];
    return c[4] / c[5];
}
// Input_GetMousePosition(float&, float&): the mouse through plinthDisplayToCanvas ((p - offset) / scale), true when
// inside the canvas
static bool input_mouse_position(float *x, float *y) {
    const float *c = (const float *)(image() + CANVAS);
    *x = (*(const float *)(image() + MOUSE_X) - c[0]) * (1.0f / c[2]);
    *y = (*(const float *)(image() + MOUSE_Y) - c[1]) * (1.0f / c[3]);
    return *x >= 0 && *x < c[4] && *y >= 0 && *y < c[5];
}
// Input_GetMouseWheelDelta(float&, float&): true when either moved
static bool input_wheel_delta(float *x, float *y) {
    *x = *(const float *)(image() + MOUSE_WHEEL_X);
    *y = *(const float *)(image() + MOUSE_WHEEL_Y);
    return *x != 0 || *y != 0;
}
// Transform_CanvasToLocal(vec3 const&): the game's (result, point); a vec3, of which callers read x and y
static Vec2 canvas_to_local(const float *xyz) {
    static void (*to_local)(float *out, const float *xyz);
    if (!to_local)
        to_local = (void (*)(float *, const float *))game("win:Transform_CanvasToLocal");
    float out[3] = {0, 0, 0};
    if (to_local)
        to_local(out, xyz);
    return (Vec2){out[0], out[1]};
}
// View_IsPointClipped(vec2) (GUI_IsPointClipped jumps to it): with the clip on, the point (z 0) through the clip's
// matrix (transform_pos: column-major, divided by w) outside its rect
static bool view_is_point_clipped(Vec2 p) {
    const uint8_t *t = image() + TRANSFORM_STACK;
    if (t[0x20] != 1)
        return false;
    const float *m = (const float *)(t + 0x68), *r = (const float *)(t + 0x44);
    float w = 1.0f / (m[3] * p.x + m[7] * p.y + m[15]);
    float x = (m[0] * p.x + m[4] * p.y + m[12]) * w, y = (m[1] * p.x + m[5] * p.y + m[13]) * w;
    return x < r[0] || x > r[1] || y < r[2] || y > r[3];
}
// GUI_GetMousePosition(vec2&): the mouse in the current transform; true when inside the canvas and not clipped
static bool gui_mouse_position(Vec2 *out) {
    float xyz[3] = {0, 0, 0};
    bool in = input_mouse_position(&xyz[0], &xyz[1]);
    *out = canvas_to_local(xyz);
    return in && !view_is_point_clipped(*out);
}

// ---------------------------------------------------------------- ui_panel.c, ui_invgrid.c (transforms, grids)
// Transform_Translate(float z): as Transform_Translate(vec2 const&), the translation in z
static void transform_translate_z(float z) {
    static void (*multiply)(const float *m);
    if (!multiply)
        multiply = (void (*)(const float *))game("win:Transform_Multiply");
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    m[14] = z;
    if (multiply)
        multiply(m);
}
// Transform_LocalToWorld(vec2 const&): transform_pos(the current matrix (+0x68), v), column-major, no divide
static Vec2 transform_local_to_world(const Vec2 *v) {
    const float *m = (const float *)(image() + TRANSFORM_STACK + 0x68);
    return (Vec2){m[0] * v->x + m[4] * v->y + m[12], m[1] * v->x + m[5] * v->y + m[13]};
}
// Transform_Scale(float): a uniform scale matrix multiplied onto the top, as Transform_Translate(float)
static void transform_scale(float k) {
    static void (*multiply)(const float *m);
    if (!multiply)
        multiply = (void (*)(const float *))game("win:Transform_Multiply");
    float m[16] = {k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, 0, 0, 0, 1};
    if (multiply)
        multiply(m);
}
// TweenFloat::operator float: its current value at +4 (inlined; RenderPartyReorderButton reads DDTabPanel+0x0c)
static float tween_float(const void *tween) {
    return *(const float *)((const uint8_t *)tween + 4);
}
// HeroDragData::HeroDragData: its vtable, the rest zero
static void hero_drag_ctor(void *drag) {
    memset(drag, 0, 0x20);
    *(const void **)drag = image() + 0xecf420;
}
// HeroDragData::SetHero: the hero, and its roster entry (Roster::System::GetHeroEntry: the entry whose hero has the
// same roster id, +0x130c), as RecruitHeroSlotInterface::OnPickedUp does inline
static void hero_drag_set_hero(void *drag, const void *hero) {
    *(const void **)((uint8_t *)drag + 8) = hero;
    const uint8_t *roster = current_roster(), *entry = NULL;
    uint8_t *const *b = roster && hero ? *(uint8_t *const *const *)(roster + 0x20) : NULL;
    uint8_t *const *e = b ? *(uint8_t *const *const *)(roster + 0x28) : NULL;
    for (uint8_t *const *p = b; p && p < e; p++)
        if (*p && *(const int *)(*p + 8 + 0x130c) == *(const int *)((const uint8_t *)hero + 0x130c)) {
            entry = *p;
            break;
        }
    *(const void **)((uint8_t *)drag + 0x10) = entry;
}
// HeroSlotInterface::GetHero: +0x28, as macOS
static void *hero_slot_hero(const void *iface) {
    return *(void *const *)((const uint8_t *)iface + 0x28);
}
// Hero::GetIsFromTownEvent: +0x13d8, as macOS (Hero's save writes is_from_town_event from there, 0x5c78a5)
static bool hero_from_town_event(const void *hero) {
    return *((const uint8_t *)hero + 0x13d8) != 0;
}
// ui_font.c. FontLibrary::_Data::FindDefaultFont(id): the listed Font (+0x108) whose id hash (+0) is the id's
static void *font_find_default(void *data, const char *id) {
    unsigned h = string_hash(id, 0);
    void *const *b = *(void *const *const *)((uint8_t *)data + 0x108), *const *e =
                                                                           *(void *const *const *)((uint8_t *)data +
                                                                                                   0x110);
    for (void *const *p = b; p && p < e; p++)
        if (*p && *(const unsigned *)*p == h)
            return *p;
    return NULL;
}
// FileExists(mount, path) (inlined): StorageManager::FileExists(s_Instance 0x1174ac8, the mount's handle at
// manager+0x10 + 8 x mount, path), as its callers pass them (0x26d01b, 0x28e5e0)
static bool file_exists(int mount, const char *path) {
    static bool (*exists)(void *manager, uint64_t handle, const char *path);
    if (!exists)
        exists = (bool (*)(void *, uint64_t, const char *))game("win:StorageManager::FileExists");
    uint8_t *manager = *(uint8_t **)(image() + 0x1174ac8);
    if (!exists || !manager || mount < 0 || mount > 7 || !path)
        return false;
    return exists(manager, *(const uint64_t *)(manager + 0x10 + 8 * mount), path);
}
// FixedLengthString<256>(char const*): strlcpy into its 256 bytes
static void fls256_ctor(char *s, const char *text) {
    size_t n = text ? strlen(text) : 0;
    if (n > 255)
        n = 255;
    if (n)
        memcpy(s, text, n);
    s[n] = 0;
}
// Input_IsUsingController: the input mode (1: a controller; 0: the byte below decides; else not), as InventoryButtonGrid
// reads it inline
static bool input_using_controller(void) {
    int mode = *(const int *)(image() + 0x117cd94);
    return mode == 1 || (mode == 0 && *(const uint8_t *)(image() + 0x1174ada));
}
// InventorySystemInterface::GetInventorySystem: its Inventory::System* at +8 (InventoryButtonGrid reads it inline)
static void *iface_inventory_system(const uint8_t *iface) {
    return *(void *const *)(iface + 8);
}

// ---------------------------------------------------------------- item_sale.c (prices, the stash's slots)
// Inventory::System::GetTotalSlotCount: the items vector's size (empty slots included)
static unsigned inventory_slot_count(const uint8_t *system) {
    const uint8_t *b = *(const uint8_t *const *)(system + SYSTEM_ITEMS),
                  *e = *(const uint8_t *const *)(system + SYSTEM_ITEMS + 8);
    return b ? (unsigned)((size_t)(e - b) / ITEM_SIZE) : 0;
}
// Inventory::System::GetItemFromSlotIndex(i): the slot's item, empty or not; NULL past the end (macOS asserts)
static uint8_t *inventory_slot_item(uint8_t *system, unsigned i) {
    uint8_t *b = *(uint8_t **)(system + SYSTEM_ITEMS);
    return i < inventory_slot_count(system) ? b + (size_t)i * ITEM_SIZE : NULL;
}
// Inventory::System::RemoveSlotItem(i, amount) (inlined on Windows, e.g. DungeonController::HeroUseItem 0x425feb):
// the slot's amount goes down, an emptied item is cleared (Item::Clear; the slots don't shift), then the filled
// count (+0x24) is recounted
static void inventory_remove_slot_item(uint8_t *system, unsigned i, int amount) {
    static void (*clear)(void *item);
    if (!clear)
        clear = (void (*)(void *))game("_ZN9Inventory4Item5ClearEv");
    uint8_t *item = inventory_slot_item(system, i);
    if (item && (*(int *)item -= amount) <= 0 && clear)
        clear(item);
    unsigned n = inventory_slot_count(system), filled = 0;
    for (unsigned k = 0; k < n; k++)
        filled += *(const int *)inventory_slot_item(system, k) > 0;
    *(unsigned *)(system + SYSTEM_FILLED) = filled;
}
// Inventory::System::RemoveSlot(i): the slot's item cleared whatever its amount (Item::Clear; the slots don't shift),
// then the filled count recounted, as macOS
static void inventory_remove_slot(uint8_t *system, unsigned i) {
    uint8_t *item = inventory_slot_item(system, i);
    if (item)
        inventory_remove_slot_item(system, i,
                                   *(const int *)(item + ITEM_AMOUNT) > 0 ? *(const int *)(item + ITEM_AMOUNT) : 1);
}
// Inventory::System::RemoveItem(Item const*): RemoveItemInternal(the item's amount, items with its type and id hashes),
// as macOS; the slots holding such items lose that many between them (the item itself may be one of them, so its
// fields are read first). Returns the amount removed.
static unsigned inventory_remove_item(uint8_t *system, const uint8_t *item) {
    int want = *(const int *)(item + ITEM_AMOUNT);
    unsigned type = *(const unsigned *)(item + ITEM_TYPE_HASH), id = *(const unsigned *)(item + ITEM_ID_HASH);
    unsigned n = inventory_slot_count(system), removed = 0;
    for (unsigned k = 0; k < n && want > 0; k++) {
        const uint8_t *s = inventory_slot_item(system, k);
        int have = *(const int *)(s + ITEM_AMOUNT);
        if (have <= 0 || *(const unsigned *)(s + ITEM_TYPE_HASH) != type || *(const unsigned *)(s + ITEM_ID_HASH) != id)
            continue;
        int take = have < want ? have : want;
        inventory_remove_slot_item(system, k, take);
        want -= take;
        removed += (unsigned)take;
    }
    return removed;
}
static unsigned item_id_hash(const uint8_t *item) {
    return *(const unsigned *)(item + ITEM_ID_HASH);
}
// InventorySystemInterface::GetSlotCount / GetSlotInterface(i): its vector of slot interfaces (IFACE_SLOTS)
static unsigned iface_slot_count(const uint8_t *iface) {
    void *const *b = *(void *const *const *)(iface + IFACE_SLOTS), *const *e =
                                                                       *(void *const *const *)(iface + IFACE_SLOTS + 8);
    return (unsigned)(e - b);
}
static void *iface_slot(uint8_t *iface, unsigned i) {
    return i < iface_slot_count(iface) ? (*(void *const *const *)(iface + IFACE_SLOTS))[i] : NULL;
}
// ---------------------------------------------------------------- ui_confirm.c (asking)
// ConfirmDialog::System::Instance (inlined: RealmInventory::DrawUnequipButton's $_0 0x69d0d0 passes the pointer at
// 0x14117de50 to PushEntry); FixedLengthString<N>::operator= (inlined as strncpy_s(dst, N, src, _TRUNCATE))
#define CONFIRM_SYSTEM 0x117de50
static void *confirm_system(void) {
    return *(void **)(image() + CONFIRM_SYSTEM);
}
static void fls512_assign(char *dst, const char *src) {
    snprintf(dst, 512, "%s", src ? src : "");
}
static void fls128_assign(char *dst, const char *src) {
    snprintf(dst, 128, "%s", src ? src : "");
}
// ConfirmDialog::System::CloseEntry (inlined, as ShowEntry 0x7bb320 does it): the closing count (+0x20) goes up under
// the System's mutex (an object at +0x18: lock in vtable slot 1, unlock slot 3); Update pops that many entries
static void confirm_lock(uint8_t *sys, unsigned slot) {
    void *m = *(void **)(sys + 0x18);
    (*(void (**)(void *))(*(uint8_t **)m + slot * 8))(m);
}
static void confirm_close_entry(uint8_t *sys) {
    confirm_lock(sys, 1);
    ++*(int *)(sys + SYS_CLOSING);
    confirm_lock(sys, 3);
}
// ConfirmDialog::System::BackoutOfCurrentEntry (inlined): with an entry queued and its back-out set, run it (MSVC
// _Do_call, vtable slot 2) and close the entry
static void confirm_backout(uint8_t *sys) {
    uint8_t *const *ev = (uint8_t *const *)(sys + SYS_ENTRIES);
    if (ev[0] == ev[1])
        return;
    void *f = *(void **)(ev[0] + E_BACKOUT_F);
    if (!f)
        return;
    (*(void (**)(void *))(*(uint8_t **)f + 0x10))(f);
    confirm_close_entry(sys);
}
// TownUI::Assets::Current: the global. Its fields read on Windows sit at the macOS offsets from it: the stash's
// "Sell for:" and price positions (+0x1b58 / +0x1b64: 0x2b7ccf8 / 0x2b7cd04, RealmInventory::PreChildrenShowInternal)
// and guild.c's two (+0x1ee8 / +0x1ef8: 0x2b7d088 / 0x2b7d098, GuildDisplay::OnActionUpdateInternal and the Guild's
// trees)
static void *town_assets(void) {
    return image() + 0x2b7b1a0;
}
// ---------------------------------------------------------------- combat_context.c
// Actor::IsCorpse (inlined; Battle 0x5a9d50's copy): the class (vtable GetClass) has the id "corpse" at +0x48
static bool actor_is_corpse(const void *actor) {
    const uint8_t *(*get_class)(const void *) =
        *(const uint8_t *(**)(const void *))(*(const uint8_t *const *)actor + ACTOR_GET_CLASS);
    const uint8_t *cls = get_class(actor);
    return cls && !strcmp((const char *)cls + 0x48, "corpse");
}
// ---------------------------------------------------------------- game_fixes.c
// RaidDisplay::SetPresentationOverride(kind, trait) (inlined; BuildAfflictionVirtuePresentation 0x49f790 and
// Overlay_Event::InvestigateCurio 0x704d70 store the two fields)
static void raid_set_presentation(void *display, int kind, unsigned trait) {
    int *f = (int *)((uint8_t *)display + RAIDDISPLAY_PRESENTATION);
    f[0] = kind;
    f[1] = (int)trait;
}
// ---------------------------------------------------------------- barks.c
// Actor::GetBarkType(source) (inlined; RaidDisplay::PrePanelShowInternal 0x74f620, HeroDragObject::Render 0x7c3320):
// 3 cursed (the vtable check, for a source of kind 2 or 3), 1 afflicted, 2 virtued, else 0 neutral
static int actor_bark_type(const void *actor, SourceId source) {
    bool (*cursed)(const void *) = *(bool (**)(const void *))(*(const uint8_t *const *)actor + ACTOR_BARK_CURSED);
    if (cursed(actor) && (source.w[0] & ~1u) == 2)
        return 3;
    const uint8_t *a = actor;
    if (*(const int *)(a + ACTOR_TRAIT))
        return 1;
    return *(const int *)(a + ACTOR_TRAIT_ALT) ? 2 : 0;
}
// RaidStat::GetValue(clamped): Windows has only the form taking the mask (0x4bb890; Actor::ApplyHealthDamageInternal
// passes the stat's own, at +0x28 as macOS)
static float raid_stat_value(const void *stat, bool clamped) {
    static float (*value)(const void *stat, unsigned mask, bool clamped);
    if (!value)
        value = (float (*)(const void *, unsigned, bool))game("win:RaidStat::GetValue(mask)");
    return value ? value(stat, *(const unsigned *)((const uint8_t *)stat + 0x28), clamped) : 0;
}
// ActorCombatStat::GetIdFromType (inlined; EffectGetBuffInstanceList 0x4bbde0 reads k_Data at 0x142a9d310, 0x6c each,
// the id first)
static const char *combat_stat_id(int type) {
    return (const char *)image() + 0x2a9d310 + (size_t)type * 0x6c;
}
// ActOut::CombatStartTurn::GetIdFromType (inlined; Combat_StartTurn 0x4a02d0 copies the id from k_Data at 0x142a9f450,
// 0x50 per type, the id first, as macOS; filled by the game's static initializers)
static const char *act_out_id(int type) {
    return (const char *)image() + 0x2a9f450 + (size_t)type * 0x50;
}
// Trait::Library::GetType(hash) (inlined; Actor::QueueHeartAttack 0x47ef50): the last of the library's types (0x100
// each, hash at +0x40), from its +0x18 list under the campaign's other-roster flag, as macOS
static const uint8_t *trait_type(const uint8_t *lib, unsigned hash) {
    const uint8_t *const *v = (const uint8_t *const *)(lib + (image()[ALT_ROSTER_FLAG] ? 0x18 : 0)), *found = NULL;
    for (const uint8_t *t = v[0]; t && t != v[1]; t += 0x100)
        if (*(const unsigned *)(t + 0x40) == hash)
            found = t;
    return found;
}
// ---------------------------------------------------------------- combat_events.c
// Monster::GetMonsterClass (inlined; Wave::WaveLogic::AddQueuedMonstersToBattle reads it): the class at MONSTER_CLASS
static const void *monster_class(const void *monster) {
    return *(const void *const *)((const uint8_t *)monster + MONSTER_CLASS);
}
// ---------------------------------------------------------------- value_hooks.c
// GetIdFromDamageType / GetIdFromDamageSourceType (inlined; PlayFabAnalytics::SetActorValue 0x3c19f0 and
// GraveyardDisplay::AddHeroDeathString 0x675d40 index them): arrays of id pointers, 20 and 14 ids in macOS's order
static const char *damage_type_id(int type) {
    return ((const char *const *)(image() + 0xdde3b0))[type];
}
static const char *damage_source_id(int source) {
    return ((const char *const *)(image() + 0xdde320))[source];
}
// Stress::Source::GetIdFromType (inlined; Actor::ApplyStressDamage 0x47c480 reads the entries' flags at +0x46 /
// +0x47): k_TypeData, 0x48 per entry, id first, hash +0x40 (its static initialiser 0xad830 fills 24, in macOS's order)
static const char *stress_source_id(int source) {
    return (const char *)image() + 0x2ad3dd0 + (size_t)source * 0x48;
}
// create_Timeline (inlined; Actor::ApplyHealthDamageInternal 0x47e570 does the same): operator new 0x40, then
// TimelineInternal's constructor (vtables Timeline, then TimelineInternal)
static void *create_timeline(void) {
    void *t = ((void *(*)(size_t))(image() + 0xb93930))(0x40);
    return ((void *(*)(void *))(image() + 0x295d00))(t);
}
// ---------------------------------------------------------------- hero_hooks.c
// Quirk::Library::GetClass(char const*) (inlined; CurioInteractionQuirkResult's constructor 0x4ada80): the last of
// the library's classes (0x1f8 each on Windows, macOS 0x200) whose hash (+0x40) is the id's
static const void *quirk_class(const void *lib, const char *id) {
    const uint8_t *const *v = lib, *found = NULL;
    unsigned h = string_hash(id, 0);
    for (const uint8_t *c = v[0]; c && c != v[1]; c += 0x1f8)
        if (*(const unsigned *)(c + 0x40) == h)
            found = c;
    return found;
}
// Camping::Camp::DeductCampingSkillPoints (inlined into PerformCampingSkill 0x5bce30): the camp's points at +0x1c
static void deduct_camping_points(void *camp, int points) {
    *(int *)((uint8_t *)camp + 0x1c) -= points;
}
// Camping::Camp::LogSkill(SkillLogEntryId {hero roster id, a, b}) (inlined into PerformCampingSkill): the camp's
// std::map at +0x20 counts each entry; its try_emplace 0x5bc910 (map, pair<node, inserted> out, key) adds a node
// (key +0x1c, count +0x28 = 0), then the count is 1, or one more for an entry already there
static void camp_log_skill(void *camp, uint64_t hero_and_a, uint32_t b) {
    uint32_t key[3] = {(uint32_t)hero_and_a, (uint32_t)(hero_and_a >> 32), b};
    uint64_t out[2] = {0, 0};
    ((void *(*)(void *, uint64_t *, const uint32_t *))(image() + 0x5bc910))((uint8_t *)camp + 0x20, out, key);
    int *count = (int *)((uint8_t *)out[0] + 0x28);
    if (out[0])
        *count = (uint8_t)out[1] ? 1 : *count + 1;
}
// ---------------------------------------------------------------- expedition_hooks.c
// All four inlined into Behaviors::GenerateAfflictionOrVirtueForHero 0x49eef0 (its asserts name them).
// Trait::Library::GetType(char const*): the id's hash, then GetType(hash) (trait_type), as macOS
static const uint8_t *trait_type_by_id(const uint8_t *lib, const char *id) {
    return trait_type(lib, string_hash(id, 0));
}
// Actor::SetPendingVirtue / SetPendingAffliction(hash, level): the pending trait (ACTOR_PENDING_*), the affliction's
// level at +0x11dc (macOS +0x11b4) and stress set to 100 (ACTOR_STRESS 0x128c), as macOS
static void set_pending_virtue(void *actor, unsigned hash) {
    *(unsigned *)((uint8_t *)actor + 0x11ec) = hash;
}
static void set_pending_affliction(void *actor, const unsigned *hash, int level) {
    uint8_t *a = actor;
    *(unsigned *)(a + 0x11e8) = *hash;
    *(int *)(a + 0x11dc) = level;
    *(float *)(a + 0x128c) = 100.0f;
}
// RandomTable<unsigned>::Roll<prng_minstd>(rng, f): the rng's vtable +0x18 (a uniform float) times the chance sum
// (+0x48), walked over the chances (vector at +0x18) to an entry (vector at +0x30); the last entry if none
static const unsigned *random_table_roll(const uint8_t *table, void *rng, float f) {
    (void)f;
    float r = ((float (*)(void *))(*(void ***)rng)[3])(rng) * *(const float *)(table + 0x48), sum = 0;
    const float *c = *(const float *const *)(table + 0x18), *ce = *(const float *const *)(table + 0x20);
    const unsigned *e = *(const unsigned *const *)(table + 0x30), *ee = *(const unsigned *const *)(table + 0x38);
    for (size_t i = 0; c + i < ce; i++)
        if (r < (sum += c[i]))
            return e + i;
    return ee - 1;
}
// ---------------------------------------------------------------- riposte.c
// All inlined (RiposteAttempt 0x5b0c20 reads the same fields); offsets as macOS unless noted.
// Battle::SkillValidResult::operator bool: not blocked (+0), the two checks at +8 / +0x10 passed, then +0x18
// (GetIsSkillBattleValid 0x5a7d50 writes the same fields)
static bool skill_valid_bool(const uint8_t *r) {
    return !r[0] && r[8] == 1 && r[0x10] == 1 && r[0x18];
}
static bool skill_multi_target(const uint8_t *skill) {
    return skill[0x3a9] & 1;
} // GetIsTargetable 0x484430
static bool skill_move_type(const uint8_t *skill) {
    return skill[0x3ac] != 0;
}
static bool skill_can_be_riposted(const uint8_t *skill) {
    return skill[0x430] != 0;
} // macOS +0x458
static bool actor_riposte_enabled(const uint8_t *actor) {
    return actor[0xb6c] != 0;
}
static bool actor_stunned(const uint8_t *actor) {
    return *(const float *)(actor + 0x1020) > 0;
}
// Actor::SetRiposteEnabled: the flag, then UpdateBuffsInternal(NULL, NULL, NULL) if it changed (as RiposteAttempt's copy)
static void actor_set_riposte_enabled(uint8_t *actor, bool on) {
    static void (*internal)(void *, const void *, const void *, const void *);
    if (!internal)
        internal = (void (*)(void *, const void *, const void *, const void *))game("win:Actor::UpdateBuffsInternal");
    bool was = actor[0xb6c] != 0;
    actor[0xb6c] = on;
    if (was != on && internal)
        internal(actor, NULL, NULL, NULL);
}
// ActorCombatSkill::GetIsSkillTargetValid(performer, target), as the copy in DungeonController::
// GetBestValidCombatTargetFocusId 0x42c230: launchable (HasAttacksUsableAnyPosition, or the skill's launch ranks
// +0x3a4 hold the performer's rank +0x1018), GetIsTargetable, then a guarded target (guard value +0xc20 not 0) of a
// performer with +0x1296 set (macOS +0x127e) only if none of the skill's effects (+0x3c8, macOS +0x3d0) has +0x158 > 0
static bool skill_target_valid(const uint8_t *skill, const uint8_t *performer, const uint8_t *target) {
    static bool (*any)(const void *);
    static bool (*targetable)(const void *, const void *, const void *);
    if (!any)
        any = (bool (*)(const void *))game("_ZNK5Actor27HasAttacksUsableAnyPositionEv");
    if (!targetable)
        targetable = (bool (*)(const void *, const void *, const void *))game(
            "_ZNK16ActorCombatSkill15GetIsTargetableEPK5ActorS2_");
    if (!performer || !any || !targetable)
        return false;
    if (!any(performer) && !(*(const uint32_t *)(performer + 0x1018) & *(const uint32_t *)(skill + 0x3a4)))
        return false;
    if (!targetable(skill, performer, target))
        return false;
    if (!target || *(const float *)(target + 0xc20) == 0 || !performer[0x1296])
        return true;
    const uint8_t *const *e = *(const uint8_t *const *const *)(skill + 0x3c8),
                         *const *ee = *(const uint8_t *const *const *)(skill + 0x3d0);
    for (; e && e < ee; e++)
        if (*(const int32_t *)(*e + 0x158) > 0)
            return false;
    return true;
}
// ---------------------------------------------------------------- a client mod debug items (client_example)
// Actor::RemoveAffliction / RemoveVirtue (inlined; StressRelief::OnActivityComplete 0x535110): the affliction clears
// the actor's trait pair at +0x11dc / +0x11e0 (macOS 8 bytes at +0x11b4), then both remove the buffs of their source
// (3 affliction, 4 virtue) through Actor vtable +0x70 (macOS +0x78) with a std::function holding
// RemoveBuffsOfSource's lambda (vtable 0xe78010, the source at +8; MSVC: the impl pointer at +0x38, the callee
// destroys it)
static void remove_buffs_of_source(void *actor, int source) {
    _Alignas(16) uint64_t fn[8] = {0};
    fn[0] = (uint64_t)(image() + 0xe78010);
    *(int *)&fn[1] = source;
    fn[7] = (uint64_t)fn;
    void (*remove)(void *, void *) = (*(void (***)(void *, void *))actor)[0x70 / 8];
    remove(actor, fn);
}
static void actor_remove_affliction(void *actor) {
    *(uint32_t *)((uint8_t *)actor + 0x11dc) = 0;
    *(uint32_t *)((uint8_t *)actor + 0x11e0) = 0;
    remove_buffs_of_source(actor, 3);
}
static void actor_remove_virtue(void *actor) {
    remove_buffs_of_source(actor, 4);
}
// Raid::GetArea (inlined): the current area
static void *raid_area(const void *raid) {
    return raid ? *(void *const *)((const uint8_t *)raid + RAID_AREA) : NULL;
}
// Raid::GetNumberOfActors / GetActorFromIndex (inlined; macOS 0x94e320 / 0x94e350): the heroes, then the monsters
static size_t raid_count(const void *raid, size_t vec) {
    const uint8_t *const *v = (const uint8_t *const *)((const uint8_t *)raid + vec);
    return v[0] && v[1] > v[0] ? (size_t)(v[1] - v[0]) / 8 : 0;
}
static int raid_actor_count(const void *raid) {
    return raid ? (int)(raid_count(raid, RAID_HEROES) + raid_count(raid, RAID_MONSTERS)) : 0;
}
static void *raid_actor_at(const void *raid, unsigned i) {
    if (!raid)
        return NULL;
    size_t nh = raid_count(raid, RAID_HEROES), vec = i < nh ? RAID_HEROES : RAID_MONSTERS;
    if (i >= nh)
        i -= (unsigned)nh;
    return i < raid_count(raid, vec) ? (*(void *const *const *)((const uint8_t *)raid + vec))[i] : NULL;
}

// popups.c. RaidUI::Screen::GetIdFromType is inlined: k_PopupTextTypeData (0xdc each: id +0, the sfx
// FixedLengthString<64>s +0x54 (plain) and +0x94 (plus), colours +0xd4 / +0xd8, as AddPlusPopupText 0x764200 reads it).
#define POPUP_TYPE_SIZE 0xdc
#define POPUP_TYPE_SFX  0x54
static const char *popup_type_entry(int type) {
    const uint8_t *data = game("win:RaidUI::Screen::k_PopupTextTypeData");
    return data ? (const char *)data + (size_t)type * POPUP_TYPE_SIZE : NULL;
}

// RaidDisplay::AddPlusPopupStringText is inlined; Windows' AddPlusPopupText reads the colours itself, so it is
// (display, actor, type, text, number, sfx, plus), the sfx the type's plain or plus one (as AddPopupText 0x764130).
static void add_plus_popup_string(void *display, void *actor, int type, const char *text, bool plus) {
    static void (*add)(void *, void *, int, const char *, unsigned, const void *, bool);
    if (!add)
        add = (void (*)(void *, void *, int, const char *, unsigned, const void *, bool))game(
            "_ZN6RaidUI6Screen11RaidDisplay16AddPlusPopupTextEP5ActorNS0_"
            "14PopupTextTypes5eTypeEPKcjjjRK17FixedLengthStringILi64EEb");
    const char *entry = popup_type_entry(type);
    if (add && entry)
        add(display, actor, type, text, 0, entry + POPUP_TYPE_SFX + (plus ? 0x40 : 0), plus);
}

typedef struct {
    uint8_t *begin, *end, *cap;
} WinPtrVec; // a std::vector a game function returns

// Actor::GetSprite is inlined
static void *actor_get_sprite(const void *actor) {
    return *(void *const *)((const uint8_t *)actor + ACTOR_SPRITE);
}

// buff_origin.c. MSVC's std::_Deallocate (inlined, as the vector destructors free their storage, e.g. 0x4c36f0): a
// block of 0x1000 bytes or more was allocated with an alignment header (std::_Allocate 0x233e20), the real pointer
// just before it; then operator delete 0xb93d30.
static void msvc_deallocate(void *p, size_t bytes) {
    static void (*del)(void *);
    if (!del)
        del = (void (*)(void *))game("win:operator delete");
    if (!p || !del)
        return;
    if (bytes >= 0x1000)
        p = ((void **)p)[-1];
    del(p);
}
// Buff::GetSourceId (inlined; StatAddAdditionalTooltipBuffLines 0x7d1ef0): k_SourceData, 0x4c per source, the id first
#define BUFF_SOURCES 0x22
static const char *buff_source_id(unsigned source) {
    const uint8_t *data = game("win:Buff::k_SourceData");
    return data && source < BUFF_SOURCES ? (const char *)data + (size_t)source * 0x4c : NULL;
}
// Buff::Library::GetBuff(hash) (inlined; EffectGetBuffInstanceList 0x4bbde0): two MSVC std::map<unsigned,
// Buff::Instance> (+0x00, or +0x10 while the byte at 0x142a5c08b is set, as macOS's +0x18 by its flag), nodes as the
// item tags' (key +0x20, the buff +0x28)
static const void *buff_library_get(const void *library, unsigned hash) {
    const uint8_t *alt = game("win:Buff::Library alt flag");
    if (!library || !alt)
        return NULL;
    const uint8_t *head = *(const uint8_t *const *)((const uint8_t *)library + (*alt ? 0x10 : 0));
    const uint8_t *n = head ? *(const uint8_t *const *)(head + 8) : NULL;
    while (n && !n[0x19]) {
        unsigned key = *(const unsigned *)(n + 0x20);
        if (key == hash)
            return n + 0x28;
        n = *(const uint8_t *const *)(n + (hash < key ? 0 : 0x10));
    }
    return NULL;
}
// Buff::Stat's table (inlined in Buff::Library::LoadFile 0x4a51d0 and Buff::Display::GetHasDescription 0x7b9e50):
// 0x58 per stat type, the id's hash at +0, the is-status flag at +5 (the riposte list's filter 0x6f44b0), 0x5f types
#define BUFF_STATS 0x5f
static int buff_stat_type(const char *id) {
    const uint8_t *data = game("win:Buff::Stat::k_Data");
    unsigned h = id ? string_hash(id, 0) : 0;
    for (int i = 0; data && id && i < BUFF_STATS; i++)
        if (*(const unsigned *)(data + (size_t)i * 0x58) == h)
            return i;
    return BUFF_STATS; // as the game: invalid
}
static bool buff_stat_is_status(int type) {
    const uint8_t *data = game("win:Buff::Stat::k_Data");
    return data && type >= 0 && type < BUFF_STATS && data[(size_t)type * 0x58 + 5];
}
// ResistanceType::GetIdFromType (inlined; ActorClass::LoadResistances 0x4875f0): k_Data, 0xb8 per type, the id first
static const char *resistance_id(int type) {
    const uint8_t *data = game("win:ResistanceType::k_Data");
    return data && type >= 0 && type < 16 ? (const char *)data + (size_t)type * 0xb8 : NULL;
}
// ActorCombatSkill::GetNumberOfEffects / GetEffectAtIndex (inlined; Actor::GetSkillEffects 0x474cd0): the base
// effects (vector at +0x3c8), then those of the effect set's entry in the std::map at +0x3e0 (MSVC: the head node;
// its parent +8 is the root; a node: left +0, right +0x10, is-nil +0x19, the set +0x20, its effects' vector +0x28)
static const uint8_t *skill_effect_set(const uint8_t *skill, unsigned set) {
    const uint8_t *head = *(const uint8_t *const *)(skill + 0x3e0), *best = head;
    const uint8_t *node = head ? *(const uint8_t *const *)(head + 8) : NULL;
    while (node && !node[0x19]) {
        if (*(const unsigned *)(node + 0x20) < set)
            node = *(const uint8_t *const *)(node + 0x10);
        else {
            best = node;
            node = *(const uint8_t *const *)node;
        }
    }
    return best && best != head && *(const unsigned *)(best + 0x20) <= set ? best : NULL;
}
static unsigned skill_effect_count(const uint8_t *skill, unsigned set) {
    const void *const *base = (const void *const *)(skill + 0x3c8);
    const uint8_t *e = skill_effect_set(skill, set);
    const void *const *more = e ? (const void *const *)(e + 0x28) : NULL;
    return (unsigned)(((const uint8_t *)base[1] - (const uint8_t *)base[0]) / 8 +
                      (more ? ((const uint8_t *)more[1] - (const uint8_t *)more[0]) / 8 : 0));
}
static const void *skill_effect_at(const uint8_t *skill, unsigned set, unsigned i) {
    const void *const *const *base = (const void *const *const *)(skill + 0x3c8);
    unsigned n = (unsigned)(base[1] - base[0]);
    if (i < n)
        return base[0][i];
    const uint8_t *e = skill_effect_set(skill, set);
    const void *const *const *more = e ? (const void *const *const *)(e + 0x28) : NULL;
    return more && i - n < (unsigned)(more[1] - more[0]) ? more[0][i - n] : NULL;
}
// Actor::GetActiveBuffs() is inlined: GetActiveBuffs(NULL, NULL, NULL) 0x4750e0, MSVC (this, result, ...). Free it
// with "win:std::_Deallocate" (its capacity in bytes).
static WinPtrVec actor_active_buffs(const void *actor) {
    static void *(*get)(const void *, WinPtrVec *, const void *, const void *, const void *);
    if (!get)
        get = (void *(*)(const void *, WinPtrVec *, const void *, const void *, const void *))game(
            "_ZNK5Actor14GetActiveBuffsEPKS_S1_PK16ActorCombatSkill");
    WinPtrVec v = {0};
    if (get)
        get(actor, &v, NULL, NULL, NULL);
    return v;
}
// RaidStat::GetBuffAddValue (inlined; ShowBaseStats' stat lambdas, e.g. 0x7d3c80): the parts' floats at +0x30
static float raid_stat_buff_add(const void *stat, unsigned part) {
    return (*(const float *const *)((const uint8_t *)stat + 0x30))[part];
}

// Overlay_Basic's tray buff lists: MSVC member functions returning a vector (this, then the hidden result)
static WinPtrVec tray_buff_list(const void *overlay, const void *actor, bool positive) {
    static void *(*get)(const void *, WinPtrVec *, const void *, bool);
    if (!get)
        get = (void *(*)(const void *, WinPtrVec *, const void *, bool))game(
            "win:Overlay_Basic::GetBuffTrayBuffInstanceList");
    WinPtrVec v = {0};
    get(overlay, &v, actor, positive);
    return v;
}
static WinPtrVec tray_riposte_list(void *overlay, void *actor) {
    static void *(*get)(void *, WinPtrVec *, void *);
    if (!get)
        get = (void *(*)(void *, WinPtrVec *, void *))game("win:Overlay_Basic::GetTrayRiposteBuffInstanceList");
    WinPtrVec v = {0};
    get(overlay, &v, actor);
    return v;
}
// ---------------------------------------------------------------- game_actions.c
// GetDamageTypeFromId / GetDamageSourceTypeFromId (inlined): the index of the id in the arrays damage_type_id /
// damage_source_id read (20 and 14 ids, macOS's order); an unknown id gives the count, as macOS's strcmp chains
static int damage_type_from_id(const char *id) {
    int i = 0;
    while (i < 20 && strcmp(id, damage_type_id(i)) != 0)
        i++;
    return i;
}
static int damage_source_from_id(const char *id) {
    int i = 0;
    while (i < 14 && strcmp(id, damage_source_id(i)) != 0)
        i++;
    return i;
}
// Stress::Source::GetTypeFromId (inlined): the id's hash against k_TypeData's hashes (+0x40 in each 0x48 entry, 24
// types as macOS's GetTypeFromTypeId); unknown: 24 (0x18)
static int stress_source_from_id(const char *id) {
    unsigned h = string_hash(id, 0);
    int i = 0;
    while (i < 24 && *(const unsigned *)(stress_source_id(i) + 0x40) != h)
        i++;
    return i;
}
// FixedLengthString<64> / <128>(char const*) (inlined as strncpy_s(dst, N, src, _TRUNCATE))
static void fls64_init(char *fls, const char *s) {
    snprintf(fls, 64, "%s", s ? s : "");
}
static void fls128_init(char *fls, const char *s) {
    snprintf(fls, 128, "%s", s ? s : "");
}
// Actor::ApplyHealthDamage (inlined; e.g. 0x42df35): the HP (+0x1038, as macOS), ApplyHealthDamageInternal (vtable
// +0x190, same arguments), AttemptHpReaction(the HP before), UpdateBuffsInternal(NULL, NULL, NULL)
static void *apply_health_damage(void *actor, void *rng, float amount, const int *type, unsigned u, const int *source,
                                 bool b, void *attacker) {
    static void (*reaction)(void *, float);
    static void (*buffs)(void *, const void *, const void *, const void *);
    if (!reaction)
        reaction = (void (*)(void *, float))game("win:Actor::AttemptHpReaction");
    if (!buffs)
        buffs = (void (*)(void *, const void *, const void *, const void *))game("win:Actor::UpdateBuffsInternal");
    float before = *(const float *)((const uint8_t *)actor + 0x1038);
    void *(*internal)(void *, void *, float, const int *, unsigned, const int *, bool, void *) =
        (*(void *(***)(void *, void *, float, const int *, unsigned, const int *, bool, void *))actor)[0x190 / 8];
    void *r = internal(actor, rng, amount, type, u, source, b, attacker);
    if (reaction)
        reaction(actor, before);
    if (buffs)
        buffs(actor, NULL, NULL, NULL);
    return r;
}
// Inventory::System::AddItem(Item&, bool): the hooked function at 0x5d40c0
static unsigned inventory_add_item(void *inventory, void *item, bool stack) {
    static unsigned (*add)(void *, void *, bool);
    if (!add)
        add = (unsigned (*)(void *, void *, bool))game("win:Inventory::System::AddItem");
    return add ? add(inventory, item, stack) : 0;
}
// Loot::Generate::Types::GetTypeById: Windows' copy (0x442230) takes the id in the second register
static const void *loot_type_by_id(const char *id) {
    static const void *(*get)(void *, const char *);
    if (!get)
        get = (const void *(*)(void *, const char *))game("win:Loot::Generate::Types::GetTypeById");
    return get ? get(NULL, id) : NULL;
}

// ---------------------------------------------------------------- localization.c
// Localization::Data (0x2a87e20, as macOS: the language id first, rng +0x100, the language +0x128; the platform byte
// +0x3f8, macOS +0x420). LocalizeStringFromId(unsigned) 0x43c700 reads all four.
#define LOC_DATA 0x2a87e20
typedef struct {
    uint64_t a, b;
} WinLocStr; // Localization::LocalizedString; found if (+8 & 0xfffff)
// Localization::GetCurrentLanguage: the language id at Data (SetProfileLanguage 0x3ae3d0 reads it there)
static const char *loc_language(void) {
    return (const char *)(image() + LOC_DATA);
}
// Localization::DoesLocalizedStringExist(unsigned): LocalizationLanguage::FindString on the current language, as
// macOS (MSVC: the result's pointer after `this`; it returns that pointer)
static bool loc_exists_hash(unsigned hash) {
    static WinLocStr *(*find)(void *, WinLocStr *, unsigned, void *, uint8_t);
    if (!find)
        find = (WinLocStr * (*)(void *, WinLocStr *, unsigned, void *, uint8_t))
            game("win:LocalizationLanguage::FindString");
    if (!find)
        return true; // (unknown: don't report it missing)
    WinLocStr s = {0, 0x80000000u};
    uint8_t *d = image() + LOC_DATA;
    const WinLocStr *r = find(d + 0x128, &s, hash, d + 0x100, d[0x3f8]);
    return r && (((const uint32_t *)r)[2] & 0xfffff) != 0;
}
// Localization::DoesLocalizedStringExist(char const*): the id's string_hash
static bool loc_exists_id(const char *id) {
    return loc_exists_hash(string_hash(id ? id : "", 0));
}

const WinSym g_win_syms[] = {
    // stand-ins
    {"_ZN4Raid10s_InstanceE",
     0x117db48,
     0,
     {0},
     NULL}, // Raid::s_Instance: Darkest::PostRaidStart caller reads raid+0x55d0 here (Area*)
    {"_Z11string_hashPKcj", 0, 0, {0}, (void *)string_hash},
    {"_ZN4Hero22RemoveQuirkWithClassIdEjb", 0, 0, {0}, (void *)remove_quirk_with_class_id},
    {"_ZNK4Raid10IsInBattleEv", 0, 0, {0}, (void *)raid_in_battle},
    {"_ZNK5Actor22GetWasDeathStartCalledEv", 0, 0, {0}, (void *)actor_death_started},
    {"_ZNK6Battle15GetCurrentActorEv", 0, 0, {0}, (void *)battle_current_actor},
    {"_ZNK17EffectApplyResultcvbEv", 0, 0, {0}, (void *)effect_result_bool},
    {"_ZN5Actor20UpdateBuffsForTargetEPKS_S1_", 0, 0, {0}, (void *)update_buffs_for_target},
    {"_ZNK11SpineSprite12HasAnimationEPKc", 0, 0, {0}, (void *)sprite_has_animation},
    {"_ZdlPv", 0, 0, {0}, (void *)op_delete},
    // data
    {"_ZTV4Hero", 0xe7c628, 0, {0}, NULL},    // Hero's vtable - 16 (game.c skips offset-to-top and RTTI)
    {"_ZTV7Monster", 0xe9b010, 0, {0}, NULL}, // Monster's vtable - 16
    {"_ZN4Raid10s_InstanceE", 0x117db48, 0, {0}, NULL},
    // game.c, effects.c, herodata.c, skills.c, anim.c, stats.c, items.c, cooldown.c
    {"_ZNK4Hero7GetGuidEv", 0x49bde0, 6, {0x8B, 0x81, 0x0C, 0x13, 0x00, 0x00}, NULL}, // mov eax, [rcx+0x130c]
    {"_ZN4Buff5Group10RemoveBuffEj",
     0x5b9f20,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30},
     NULL}, // push rbx ; sub rsp, 0x30
    {"_ZNK8RuleBook10FindEffectE8c_string",
     0x4ea320,
     6,
     {0x48, 0x83, 0xEC, 0x28, 0x33, 0xC9},
     NULL}, // sub rsp, 0x28 ; xor ecx, ecx
    {"_Z19EffectApplyInternalR11prng_minstdP5ActorS2_"
     "PK6EffectPN9Inventory4ItemEfbfN4Buff6SourceEjR17EffectApplyResultbb",
     0x4bc4f0,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    {"_ZNK4Hero4SaveEP8SaveData", 0x5c7200, 5, {0x48, 0x89, 0x5C, 0x24, 0x18}, NULL}, // mov [rsp+0x18], rbx
    {"_ZN4Hero7RestoreEP11RestoreDatab",
     0x5c7a60,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx
    // monsterdata.c (Monster vtable slots 5 / 20, as Hero's; Restore: its only caller is at 0x5a2fd9)
    {"_ZNK7Monster7GetGuidEv", 0x5ef420, 6, {0x8B, 0x81, 0xB0, 0x12, 0x00, 0x00}, NULL},       // mov eax, [rcx+0x12b0]
    {"_ZNK7Monster4SaveEP8SaveData", 0x5f4440, 5, {0x48, 0x89, 0x5C, 0x24, 0x18}, NULL},       // mov [rsp+0x18], rbx
    {"_ZN7Monster7RestoreEP11RestoreData", 0x5f4730, 5, {0x48, 0x89, 0x5C, 0x24, 0x18}, NULL}, // mov [rsp+0x18], rbx
    {"_ZNK4Hero27GetModeSelectedCombatSkillsEv",
     0x5caf20,
     5,
     {0x48, 0x89, 0x54, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rdx
    {"_ZN5Actor18SetSpriteAnimationEPKcS1_bb",
     0x47a920,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    {"_ZN4Hero29CalculateDerivedStatsInternalEPK5Actor",
     0x5c37f0,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40},
     NULL}, // push rbx ; sub rsp, 0x40
    {"_ZN6Battle21Actor_DoSkillFriendlyEP5ActorPK16ActorCombatSkillRKNSt3__16vectorIS1_NS5_9allocatorIS1_EEEEb",
     0x5b4ce0,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx
    {"_ZNK16ActorCombatSkill17GetDamageModifierEPK5Actor",
     0x4849e0,
     8,
     {0xF3, 0x0F, 0x10, 0x81, 0x1C, 0x04, 0x00, 0x00},
     NULL}, // movss xmm0, [rcx+0x41c]
    {"_ZNK17DungeonController14CanHeroUseItemEPK4Herojj",
     0x424850,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08},
     NULL}, // mov rax, rsp ; mov [rax+8], rbx
    {"_ZN6Battle13Actor_DoSkillEP5ActorPK16ActorCombatSkillRKNSt3__16vectorIS1_NS5_9allocatorIS1_EEEEb",
     0x5af430,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    {"_ZNK6Battle21GetIsSkillBattleValidEPK5ActorPK16ActorCombatSkill",
     0x5a7d50,
     6,
     {0x40, 0x57, 0x41, 0x56, 0x41, 0x57},
     NULL}, // push rdi ; push r14 ; push r15
    // events.c
    {"_ZN4Hero11OnRaidStartEv",
     0x5c2a90,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // mov rax, rsp ; mov [rax+0x10], rbx
    {"_ZN4Hero12OnRaidFinishEv", 0x5c3040, 6, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x30}, NULL}, // push rdi ; sub rsp, 0x30
    // act_outs.c (once ported)
    {"_ZN9Behaviors16Combat_StartTurnER11prng_minstdP5Actor",
     0x49ffb0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx
    // Battle::SetState (state +0x288, the multiplayer lock +0x2e2). Battle::IncrementState is inlined: some steps call
    // SetState(state + 1) (out of bs_after_turn_actor_dot: UpdateBattleState 0x75e382), others write the state.
    {"_ZN6Battle8SetStateE11BattleState", 0x5a7f90, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL}, // mov [rsp+8], rbx
    // UpdateBattleState's inlined steps (before the write, the Raid in rsi): bs_before_turn_wait_for_drag ->
    // bs_before_turn_Bark, and the jump to bs_after_turn shared by the stun case (0x75bd32) and the bark's
    // ignore-command turn (0x75bf57)
    {"win:UpdateBattleState turn start",
     0x75b572,
     10,
     {0xC7, 0x86, 0x20, 0x4B, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00},
     NULL}, // mov dword [rsi+0x4b20], 0x18
    {"win:UpdateBattleState after turn",
     0x75bf98,
     10,
     {0xC7, 0x86, 0x20, 0x4B, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00},
     NULL}, // mov dword [rsi+0x4b20], 0x22
    // RaidDisplay::OnBattleFinish(bool): Hero::OnBattleFinish inlined for each party hero (flag +0x46f8)
    {"win:RaidDisplay::OnBattleFinish",
     0x760f20,
     6,
     {0x41, 0x56, 0x48, 0x83, 0xEC, 0x20},
     NULL}, // push r14 ; sub rsp, 0x20
    // inlined per-hero events: each party hero's loop step (after it). Hero::OnBattleCreate (hero in rdi / rsi),
    // UpdateBattleState's $_18 (OnBattleFinish(true, true) + OnBattleComplete, rbx), Battle::NextInitiative 0x5a7140
    // (OnRoundEnd, rdi), Party::OnStartCamp 0x5fa580 (OnCampStart, rdi), CampaignPartyRetreat's $_0 _Do_call
    // 0x5ac1e0 (OnBattleAfterRetreat, rbx)
    {"win:Battle::Create hero loop",
     0x59a4e8,
     7,
     {0x48, 0x83, 0xC6, 0x08, 0x48, 0x3B, 0xF5},
     NULL}, // add rsi, 8 ; cmp rsi, rbp
    {"win:Raid::ResumeBattle hero loop",
     0x6020bf,
     7,
     {0x49, 0x83, 0xC6, 0x08, 0x4D, 0x3B, 0xF4},
     NULL}, // add r14, 8 ; cmp r14, r12
    {"win:UpdateBattleState battle finish hero loop",
     0x772b96,
     7,
     {0x48, 0x83, 0xC7, 0x08, 0x48, 0x3B, 0xFE},
     NULL}, // add rdi, 8 ; cmp rdi, rsi
    {"win:Battle::NextInitiative hero loop",
     0x5a732d,
     7,
     {0x48, 0x83, 0xC6, 0x08, 0x49, 0x3B, 0xF7},
     NULL}, // add rsi, 8 ; cmp rsi, r15
    {"win:Party::OnStartCamp hero loop",
     0x5fa6d9,
     7,
     {0x48, 0x83, 0xC6, 0x08, 0x48, 0x3B, 0xF5},
     NULL}, // add rsi, 8 ; cmp rsi, rbp
    {"win:CampaignPartyRetreat hero loop",
     0x5ac290,
     7,
     {0x48, 0x83, 0xC7, 0x08, 0x48, 0x3B, 0xFE},
     NULL}, // add rdi, 8 ; cmp rdi, rsi
    // combat_hooks.c (Attack: attacker +0x20, skill +0x28; TargetData: target +0, amount +8, hit +0x10, crit +0x11,
    // as macOS)
    {"_ZN6Battle8ApplyHitEPKNS_6AttackERKNS0_10TargetDataE",
     0x5afcd0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20},
     NULL}, // mov rax, rsp ; mov [rax+0x20], rbx
    {"_ZN6Battle9ApplyMissEPKNS_6AttackERKNS0_10TargetDataE",
     0x5b06b0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx
    {"_ZN6Battle21Actor_ApplySideEffectEP5ActorRKNS_6Attack10TargetDataEPK6EffectRNSt3__16vectorIS8_NS9_9allocatorIS8_"
     "EEEEbb",
     0x5b5850,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    {"_ZN4Hero23OnDeathDoorDeathAvoidedEv",
     0x5cd880,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    // effects.c: Buff::Group::AddBuff is inlined (the next guid from +0x18, then AddBuffInternal(guid, instance)), so
    // effects.c hooks AddBuffInternal and acts on the calls of macOS's AddBuff callers (return addresses: Hero::ApplyBuff
    // 0x5c3b30, Monster::ApplyBuff 0x5f1600, Monster::PerformCompanionActions' $_0 0x5f3de0), not
    // ApplyBurnStacksWithCreate's (0x5ba5eb); Buff::Group::Restore inlines it
    {"win:Buff::Group::AddBuffInternal",
     0x5ba9f0,
     9,
     {0x40, 0x53, 0x48, 0x81, 0xEC, 0x00, 0x02, 0x00, 0x00},
     NULL}, // push rbx ; sub rsp, 0x200
    {"win:Buff::Group::AddBuff from Hero::ApplyBuff", 0x5c3b73, 0, {0}, NULL},
    {"win:Buff::Group::AddBuff from Monster::ApplyBuff", 0x5f1643, 0, {0}, NULL},
    {"win:Buff::Group::AddBuff from Monster companion", 0x5f431f, 0, {0}, NULL},
    // its std::function by pointer (the callee destroys it)
    {"_ZN4Buff5Group11RemoveBuffsENSt3__18functionIFbRKNS_10GroupEntryEEEE",
     0x5b9f90,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    // ui.c
    {"_Z11GUI_SetFontj", 0, 0, {0}, (void *)gui_set_font},
    {"_ZN21ImageButtonParametersC2E4vec214ResourceHandleI7TextureEybP20GuiTooltipParameters",
     0,
     0,
     {0},
     (void *)button_params},
    {"_ZN21ImageButtonParametersC2E4vec214ResourceHandleI7TextureEjP20GuiTooltipParameters",
     0,
     0,
     {0},
     (void *)button_params32},
    {"_Z15GUI_ImageButtonRK21ImageButtonParametersNSt3__18functionIFvvEEES5_S5_S5_S5_",
     0x275be0,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx
    // the vec4 overload the plain one calls (0x275e11: result rcx, params rdx, &{left, right, top, bottom} r8, clicks)
    {"_Z15GUI_ImageButtonRK21ImageButtonParameters4vec4NSt3__18functionIFvvEEES6_S6_S6_S6_",
     0x275770,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    {"_Z8GUI_TextRK14TextParametersPKc",
     0x276af0,
     7,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08},
     NULL}, // mov r11, rsp ; mov [r11+8], rbx
    {"_Z8GUI_TextRK14TextParametersRKN12Localization15LocalizedStringE",
     0x276cc0,
     7,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08},
     NULL},                                                                         // mov r11, rsp ; mov [r11+8], rbx
    {"_ZN14TextParametersD1Ev", 0x271090, 5, {0x48, 0x89, 0x4C, 0x24, 0x08}, NULL}, // (the parameters' base destructor)
    {"_Z10GUI_Colourj",
     0x271390,
     7,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x10},
     NULL},                                                               // mov r11, rsp ; mov [r11+0x10], rbx
    {"_Z10GUI_Colour4rgba", 0x2712e0, 4, {0x48, 0x83, 0xEC, 0x48}, NULL}, // sub rsp, 0x48
    // ui_font.c (mod fonts): FontLibrary's _Data as macOS (fonts +0x108, current +0x120, the ref map +0x138; Font 0x118:
    // TextureFont +0x108, loaded +0x110). c_string is the char pointer (rcx).
    {"_ZN11FontLibraryL6s_DataE", 0x2c1ef40, 0, {0}, NULL}, // GetFontFromId: mov rax, [rip+..]
    {"_ZN11FontLibrary5_Data15FindDefaultFontEPKc", 0, 0, {0}, (void *)font_find_default},
    {"_ZN11FontLibrary5_Data16AddFontReferenceEPKcS2_",
     0x270520,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x20},
     NULL}, // "FontLibrary::_Data::AddFontReference"
    {"_Z26new_TextureFont_FromBMFont8c_stringbfP11TextureFont", 0x281400, 5, {0x40, 0x53, 0x55, 0x56, 0x57}, NULL},
    {"_Z24TextureFont_LoadTexturesP11TextureFont",
     0x281260,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // SetCurrentLanguage's loads
    {"win:StorageManager::FileExists",
     0x246e40,
     10,
     {0x4C, 0x8B, 0xDC, 0x48, 0x81, 0xEC, 0x88, 0x04, 0x00, 0x00},
     NULL}, // (manager, its mount's handle, path)
    {"_Z10FileExists12StorageMountPKc", 0, 0, {0}, (void *)file_exists},
    {"_ZN17FixedLengthStringILi256EEC1EPKc", 0, 0, {0}, (void *)fls256_ctor},
    {"win:std::vector<T*>::_Emplace_reallocate",
     0x238260,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // (vector, where, &value): the fonts file's push of a Font*, 0x27003d
    {"_ZN11FontLibrary13GetFontFromIdEj", 0x26fa50, 4, {0x48, 0x83, 0xEC, 0x28}, NULL}, // sub rsp, 0x28
    // paging.c
    {"_ZN12Panel_Banner10ShowBattleERN6RaidUI6Screen11RaidDisplayE",
     0x70c8e0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL},                                                                                     // mov [rsp+0x18], rbx
    {"_ZNK4Hero22GetSelectedCombatSkillEj", 0x5c97e0, 5, {0x48, 0x89, 0x5C, 0x24, 0x10}, NULL}, // mov [rsp+0x10], rbx
    {"_ZNK4Hero31GetNumberOfSelectedCombatSkillsEv",
     0x5c96e0,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40},
     NULL}, // push rbx ; sub rsp, 0x40
    {"_ZNK6Battle18CanBeHeroSkillUsedEPK5ActorPK16ActorCombatSkill",
     0x5a3d70,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x20},
     NULL}, // mov [rsp+0x20], rbx
    {"_ZNK17DungeonController31GetBestValidCombatTargetFocusIdEPK5ActorPK16ActorCombatSkill",
     0x42c230,
     5,
     {0x48, 0x8B, 0xC4, 0x56, 0x57},
     NULL}, // mov rax, rsp ; push rsi ; push rdi
    {"_ZNK6RaidUI6Screen11RaidDisplay30CanPerformSkillOutsideOfBattleEPK15HeroCombatSkill",
     0x761f00,
     7,
     {0x4C, 0x8B, 0x0D, 0x41, 0xBC, 0xA1, 0x00},
     NULL,
     3,
     7}, // mov r9, [rip+0xa1bc41] (Raid::s_Instance)
    {"_ZNK5Party15GetSelectedHeroEv", 0, 0, {0}, (void *)party_selected_hero},
    {"_Z20new_Texture_FromFile8c_stringj",
     0x384540,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    // sheet.c
    {"_ZN2UI14CampaignAssets7CurrentEv", 0, 0, {0}, (void *)campaign_assets},
    {"_ZNK4Hero19GetCombatSkillLevelEj", 0, 0, {0}, (void *)hero_skill_level},
    {"_ZNK4Hero24GetIsCombatSkillSelectedEj", 0, 0, {0}, (void *)hero_skill_selected},
    {"_ZN4Hero33AttemptCombatSkillSelectionToggleEj",
     0x5c9b10,
     5,
     {0x89, 0x54, 0x24, 0x10, 0x53},
     NULL}, // mov [rsp+0x10], edx ; push rbx
    {"_Z14Transform_Pushv", 0, 0, {0}, (void *)transform_push},
    {"_Z13Transform_Popv", 0, 0, {0}, (void *)transform_pop},
    {"_Z19Transform_TranslateRK4vec2", 0, 0, {0}, (void *)transform_translate},
    {"_ZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClass",
     0x7ef130,
     5,
     {0x48, 0x89, 0x54, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rdx
    // ShowCombatSkillGrid's lambdas ($_n, in the order the function builds them): _Func_impl::_Do_call (vtable slot 2)
    {"_ZNSt3__110__function6__funcIZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClassE3$_0NS_"
     "9allocatorISB_EEFjSA_EEclEOSA_",
     0x7f7a80,
     10,
     {0x48, 0x8B, 0x0A, 0x48, 0x8B, 0x81, 0xD8, 0x0B, 0x00, 0x00},
     NULL}, // count
    {"_ZNSt3__110__function6__funcIZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClassE3$_1NS_"
     "9allocatorISB_EEFiPKS6_jEEclEOSF_Oj",
     0x7f7a30,
     6,
     {0x48, 0x8B, 0x02, 0x45, 0x8B, 0x08},
     NULL}, // level
    {"_ZNSt3__110__function6__funcIZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClassE3$_2NS_"
     "9allocatorISB_EEF14ResourceHandleI7TextureESA_jjEEclEOSA_OjSK_",
     0x7f7990,
     5,
     {0x48, 0x89, 0x54, 0x24, 0x10},
     NULL}, // icon
    {"_ZNSt3__110__function6__funcIZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClassE3$_3NS_"
     "9allocatorISB_EEFbS7_jEEclEOS7_Oj",
     0x7f7920,
     5,
     {0x48, 0x8B, 0x0A, 0x32, 0xC0},
     NULL}, // selected
    {"_ZNSt3__110__function6__funcIZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClassE3$_5NS_"
     "9allocatorISB_EEFbS7_jEEclEOS7_Oj",
     0x7f7720,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // toggle
    {"_ZNSt3__110__function6__funcIZN2UI5Panel16CharacterDisplay19ShowCombatSkillGridE4vec2P4HeroPK9HeroClassE3$_6NS_"
     "9allocatorISB_EEF20GuiTooltipParametersS7_jS5_fEEclEOS7_OjOS5_Of",
     0x7f76a0,
     5,
     {0x48, 0x89, 0x54, 0x24, 0x10},
     NULL}, // tooltip
    // tooltip.c, guild.c (shared small functions)
    {"_ZN20GuiTooltipParameters4Text4Line9SetStringEPKc", 0, 0, {0}, (void *)line_set_string},
    {"_ZN20GuiTooltipParameters4Text7AddLineERKNS0_4LineE", 0, 0, {0}, (void *)text_add_line},
    {"_ZN20GuiTooltipParameters4Text5ClearEv", 0, 0, {0}, (void *)text_clear},
    {"_ZNK12Localization15LocalizedString9GetStringEv", 0, 0, {0}, (void *)loc_string},
    {"_ZN13ColourLibrary8GetEntryEj", 0, 0, {0}, (void *)colour_entry},
    {"_ZNK13ColourLibrary5Entrycv5hvec4Ev", 0, 0, {0}, (void *)colour_entry_hvec4},
    // tooltip.c
    {"_ZZN11HeroDisplay3App12CreateAssetsEbENK3$_1clEP9HeroClassRNS0_12SharedAssets8HeroDataERK15HeroCombatSkillbb",
     0x7c8710,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx
    {"_ZN11HeroDisplay3App24CombatCreateSkillTooltipENS0_24CombatSkillTooltipParamsE",
     0x7ca770,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    {"_ZN6TownUI5Panel17PartySetupDisplay20ShowQuickSelectSkillE4vec2P4Herojb",
     0x647860,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10},
     NULL}, // mov rax, rsp ; mov [rax+0x10], rdx
    {"_ZN12Localization20LocalizeStringFromIdEPKc",
     0x43c770,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    {"_ZN11HeroDisplay3App15CombatSkillInfoEPK9HeroClassPK15HeroCombatSkill", 0, 0, {0}, (void *)combat_skill_info},
    {"_ZN11HeroDisplay3App15CombatSkillNameEPK9HeroClassPK15HeroCombatSkill", 0, 0, {0}, (void *)combat_skill_name},
    {"_ZN12ActorDisplay3App18GetCombatSkillNameEPK10ActorClassPK16ActorCombatSkillb",
     0,
     0,
     {0},
     (void *)win_actor_skill_name},
    {"_ZNK5Actor13GetCombatStatEN15ActorCombatStat5Types5eTypeE", 0, 0, {0}, (void *)actor_combat_stat},
    {"_Z26TextureFont_CatColourStartR17FixedLengthStringILi512EEPKc", 0, 0, {0}, (void *)colour_start},
    {"_Z24TextureFont_CatColourEndR17FixedLengthStringILi512EE", 0, 0, {0}, (void *)colour_end},
    {"_ZN6TownUI6Screen11TownDisplay10s_InstanceE", 0x117dbe0, 0, {0}, NULL},
    // guild.c
    {"_ZN8Campaign13CurrentRosterEv", 0, 0, {0}, (void *)current_roster},
    {"_ZN6Roster6System7GetHeroEj", 0, 0, {0}, (void *)roster_hero},
    {"_ZN2UI5Panel4Base10SetBasePosE4vec2", 0, 0, {0}, (void *)panel_set_base_pos},
    {"_ZNK2UI5Panel4Base11GetIsActiveEv", 0, 0, {0}, (void *)panel_is_active},
    {"_ZN9Inventory7Display26ShowInventoryItemHighlightEN22InventorySlotInterface13HighlightTypeE",
     0,
     0,
     {0},
     (void *)inventory_item_highlight},
    {"_ZN7Upgrade14GetIsPurchasedERKNS_18PurchaseIdentifierE", 0, 0, {0}, (void *)upgrade_is_purchased},
    {"_ZN7Upgrade11GetIsLockedERKNS_18PurchaseIdentifierE", 0, 0, {0}, (void *)upgrade_is_locked},
    {"_ZN7Upgrade19GetRequirementClassERKNS_18PurchaseIdentifierE", 0, 0, {0}, (void *)upgrade_req_class},
    {"_ZN7Upgrade12GetTreeClassEj", 0, 0, {0}, (void *)upgrade_tree_class},
    {"_ZN7Upgrade18GetDoesTreeHaveTagEPKNS_9TreeClassEj", 0, 0, {0}, (void *)upgrade_tree_has_tag},
    {"_ZN7Upgrade20GetCanAffordPurchaseERKNS_18PurchaseIdentifierE", 0, 0, {0}, (void *)upgrade_can_afford},
    {"_ZN6Estate8CurrencyD1Ev", 0, 0, {0}, (void *)currency_dtor},
    {"_ZN17FixedLengthStringILi64EEaSEPKc", 0, 0, {0}, (void *)fls64_assign},
    {"_ZN15ImageParametersC2E4vec214ResourceHandleI7TextureE", 0, 0, {0}, (void *)image_params},
    {"_Z9GUI_ImageRK15ImageParameters", 0, 0, {0}, (void *)gui_image},
    {"_ZN7Upgrade22GetAllRequirementCodesERKj",
     0x590bb0,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // (out vector<char>, tree hash)
    {"_ZN7Upgrade27GetCurrencyCostWithDiscountERKNS_18PurchaseIdentifierE",
     0x591600,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // (out vector<Currency>, id)
    {"_ZNK12Localization15LocalizedString6FormatEPcjz",
     0x4405e0,
     5,
     {0x44, 0x89, 0x44, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], r8d
    {"_ZN6TownUI5Panel12GuildDisplay22OnActionUpdateInternalEf",
     0x6774a0,
     11,
     {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x91, 0x70, 0x02, 0x00, 0x00},
     NULL}, // sub rsp, 0x28 ; mov rdx, [rcx+0x270]
    {"_ZNK6TownUI5Panel18UpgradeTreeDisplay22GetNumberOfChildPanelsEv",
     0x6c86e0,
     7,
     {0x48, 0x8B, 0x81, 0xA8, 0x00, 0x00, 0x00},
     NULL}, // mov rax, [rcx+0xa8]
    {"_ZN6TownUI5Panel18UpgradeTreeDisplay23PreChildrenShowInternalEv",
     0x6c92c0,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    {"_ZN11CostDisplay12CurrencyCostE4vec2NSt3__16vectorIN6Estate8CurrencyENS1_9allocatorIS4_EEEEbbb",
     0x7bdfb0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    // (the Guild's skill tooltip: GuildDisplay::OnActionEnterStartInternal's $_1's lambda(Text&, bool): closure, text, next)
    {"_ZZZN6TownUI5Panel12GuildDisplay26OnActionEnterStartInternalEvENK3$_1clEjENKUlRN20GuiTooltipParameters4TextEbE_"
     "clES5_b",
     0x677180,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    // (UpgradeTreeDisplay::OnEnterStart's $_0 _Do_call: movzx edx, byte [rdx] ; add rcx, 8 ; jmp <the lambda>)
    {"_ZNSt3__110__function6__funcIZN6TownUI5Panel18UpgradeTreeDisplay12OnEnterStartEvE3$_0NS_9allocatorIS5_"
     "EEFvcEEclEOc",
     0x6ca3c0,
     7,
     {0x0F, 0xB6, 0x12, 0x48, 0x83, 0xC1, 0x08},
     NULL},
    // mods.c (Darkest::SetupUserGeneratedContent: the list count, ColourLibrary::Destroy, then UserServicesInterface;
    // Cleanup: right after it, called after ColourLibrary::Destroy from the campaign's cleanup)
    {"_ZN7Darkest25SetupUserGeneratedContentEv",
     0x3ed370,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    {"_ZN7Darkest27CleanupUserGeneratedContentEv",
     0x3ed900,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    // game_hooks.c (doc 18): GetCurrentResistance's vtable calls are macOS's less 8 (one destructor slot), its Hero
    // flag macOS's + 0x18; UpdateWeaponAndArmourRanks has the same "%s.weapon" / "%s.armour" lookups
    {"_ZNK5Actor20GetCurrentResistanceEN15ResistanceTypes15eResistanceTypeE",
     0x478600,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx
    // Actor::GetAdditionalResistance: Hero vtable +0x1b0 (GetCurrentResistance 0x478648 calls it; the deathblow roll
    // in ApplyHealthDamageInternal 0x47e790 calls GetCurrentResistance(6))
    {"_ZNK5Actor23GetAdditionalResistanceEN15ResistanceTypes15eResistanceTypeE",
     0x47f830,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20},
     NULL}, // push rbx ; sub rsp, 0x20
    {"_ZN4Hero26UpdateWeaponAndArmourRanksEv",
     0x5c9da0,
     9,
     {0x40, 0x55, 0x53, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xB9},
     NULL}, // push rbp ; push rbx ; push rsi ; lea rbp, [rsp-0x47]
    {"_ZN4Hero21OnDeathFinishInternalEv",
     0x5caab0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // Hero::OnDeathFinishInternal: the hero_died_event / arena_hero_died_event strings
    {"_ZNK5Quest6System20GenerateQuestRewardsERNSt3__16vectorINS_14GeneratedQuestENS1_9allocatorIS3_EEEERKNS_"
     "15GenerationClassE",
     0x575000,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // Quest::System::GenerateQuestRewards: 'Quest %s-%s Rewards: %s, %d\n'
    {"_ZN2UI7Display9NameFrameE4vec214ResourceHandleI7TextureEPKcRfS6_S4_NSt3__18functionIFvvEEEP11SpineSpriteSD_PNS_"
     "5Panel12InputPreviewEyPKNSF_17ElementParamatersE",
     0x7bff90,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // UI::Display::NameFrame: 'town_name_%s'; the stash's call 0x699ec7
    {"_ZN8Building5Store13TrinketSupply13GenerateItemsEv",
     0x53c870,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // Building::Store::TrinketSupply::GenerateItems: its vtable slot; 15 shared constants
    {"_ZN6TownUI5Panel10RosterList6OnDropERK4vec2P8DragData",
     0x6a7b30,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x20},
     NULL}, // TownUI::Panel::RosterList::OnDrop: character_add(_full), hero_recruited_event
    {"_ZN24RecruitHeroSlotInterface10OnPickedUpERK4vec2P10DragObjectP8DragData",
     0x7d79e0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // RecruitHeroSlotInterface::OnPickedUp: character_pickup; its vtable slot
    {"_ZN6RaidUI6Screen11RaidDisplay20PrePanelShowInternalEv",
     0x74f620,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // RaidDisplay::PrePanelShowInternal: /ui/dungeon/button_pickup
    {"_ZN6TownUI5Panel13EstateSummary23PreChildrenShowInternalEv",
     0x6709b0,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // EstateSummary::PreChildrenShowInternal: town_estate_bar_tooltip_%s
    {"_ZN6TownUI5Panel17BlacksmithDisplay26OnActionEnterStartInternalEv",
     0x65da90,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // BlacksmithDisplay::OnActionEnterStartInternal: BlacksmithDisplay vtable slot 41
    // OnActionEnterStartInternal's callbacks: lambda_1 (vtable 0xeaa6b8) unselects (its _Do_call 0x65eee0 jumps to the
    // body with (closure, guid)), lambda_2 (0xeaa530, _Do_call 0x65eca0) selects and calls the per-type lambda with
    // (closure {panel, hero, ..}, type), as macOS
    {"_ZNSt3__110__function6__funcIZN6TownUI5Panel17BlacksmithDisplay26OnActionEnterStartInternalEvE3$_0NS_"
     "9allocatorIS5_EEFvjEEclEOj",
     0x65dc90,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // the unselect lambda's body: (closure, unsigned guid), not macOS's (func, unsigned *)
    {"_ZZZN6TownUI5Panel17BlacksmithDisplay26OnActionEnterStartInternalEvENK3$_1clEjENKUl14eEquipmentTypeE_clES3_",
     0x65de40,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx
    {"_ZN6TownUI5Panel17BlacksmithDisplay24OnActionShowBodyInternalEv",
     0x65ebc0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // BlacksmithDisplay::OnActionShowBodyInternal: slot 44; the same draw and release calls
    {"_ZN6TownUI5Panel17BlacksmithDisplay25GetUpgradeDescriptionListEN7Upgrade21RequirementIdentifierE",
     0x65d900,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x20},
     NULL}, // BlacksmithDisplay vtable slot 32; MSVC (this, out, id): upgrade_tree_tooltip_description_allows_weapon_upgrades_of_rank_format
    {"_ZNK6TownUI5Panel15BuildingDisplay19GetChildPanelOffsetEj",
     0x661580,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // BuildingDisplay::GetChildPanelOffset: the same slot in 14 building vtables; MSVC (this, out, i), the trees at Assets+0x318 + 0x35c + i x 0x364
    {"_ZN6TownUI5Panel17HeroActionDisplay27PreChildrenShowBodyInternalEv",
     0x67af70,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // HeroActionDisplay::PreChildrenShowBodyInternal: action_select_hero, action_verbose_body_%s_%s
    // client_example (hire). RecruitHeroSlotInterface: its vtable at +0 (OnPickedUp slot 1, OnDropAccepted slot 3), a
    // second at +0x10; the hero at +0x28 as macOS (OnPickedUp). HeroDragData: {vtable 0xecf420, hero +8, entry +0x10,
    // +0x18}, 0x20 bytes (Clone 0x7c2a40); GetTypeId 0x7c29d0 returns its thread-safe static.
    {"_ZTV24RecruitHeroSlotInterface", 0xed1eb8 - 0x10, 0, {0}, NULL}, // its vtable - 16 (constructor 0x7d7787)
    {"_ZN24RecruitHeroSlotInterface14OnDropAcceptedEPK8DragData",
     0x7d7c20,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40},
     NULL}, // push rbx ; sub rsp, 0x40
    {"_ZN12HeroDragDataC1Ev", 0, 0, {0}, (void *)hero_drag_ctor},
    {"_ZN12HeroDragData7SetHeroEPK4Hero", 0, 0, {0}, (void *)hero_drag_set_hero},
    {"_ZZN12HeroDragData15GetTypeIdStaticEvE7type_id", 0x2c37038, 0, {0}, NULL}, // GetTypeId: lea rax, [rip+..]
    {"_ZNK17HeroSlotInterface7GetHeroEv", 0, 0, {0}, (void *)hero_slot_hero},
    {"_ZNK4Hero18GetIsFromTownEventEv", 0, 0, {0}, (void *)hero_from_town_event},
    {"_ZNK6Roster6System19GetRosterLimitCountEv",
     0x5814b0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // RosterList::OnDrop: count, then max
    {"_ZNK6Roster6System17GetRosterLimitMaxEv", 0x5815a0, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL},
    {"_ZN7Upgrade24GetAmountWithTagDiscountEij",
     0x591560,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL},                                                    // amount - (three discounts) x amount
    {"_ZN7Upgrade4TagsL9k_TrinketE", 0x2a6ba34, 0, {0}, NULL}, // the wagon's buy price (+0x9c) discount, 0x686e35
    {"_ZN6RaidUI6Screen11RaidDisplay21HeroesRerankToDefaultEv",
     0x762bc0,
     5,
     {0x48, 0x8B, 0xC4, 0x53, 0x55},
     NULL}, // RaidDisplay::HeroesRerankToDefault: the party order button's lambda_1 (s_Instance, jmp)
    {"_ZN6RaidUI6Screen10DDTabPanel24RenderPartyReorderButtonEv",
     0x744be0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // DDTabPanel::RenderPartyReorderButton: str_party_default_order
    // InventoryItemSlotInterface: OnDropAccepted is primary vtable slot 3; OnDrop is in the drop-target base's vtable
    // (0xed2b48) and gets this = the slot + SLOT_DROP_THIS (its picked-up flag at +0xb0, the slot's vtable at -0x10)
    {"_ZN26InventoryItemSlotInterface14OnDropAcceptedEPK8DragData",
     0x7e1b50,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    {"_ZN26InventoryItemSlotInterface6OnDropERK4vec2P8DragData",
     0x7e22f0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rbx
    // RosterElement::OnPickedUp: slot 1 of its first vtable (Windows puts the drag interface first), plays
    // /ui/town/character_pickup; its fields aren't at the macOS offsets (another base comes first)
    {"_ZN6TownUI5Panel13RosterElement10OnPickedUpERK4vec2P10DragObjectP8DragData",
     0x6a5470,
     7,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x10},
     NULL}, // mov r11, rsp ; mov [r11+0x10], rbx
    // Actor::UpdateTrinketBuffs: called from Hero::Restore and four more of its callers; 16 shared constants
    {"_ZN5Actor18UpdateTrinketBuffsEv",
     0x47ca10,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    // its add-buff lambda $_5 (closure, buff id hash, source id, Buff::Source), its three callers there. MSVC's closure
    // holds `this` (the lambda reads the actor from [closure]); macOS's closure is the actor itself.
    // Sets the instance's source / source id (+0x54 / +0x58) and calls the actor's add-buff virtual (+0x68)
    {"_ZZN5Actor18UpdateTrinketBuffsEvENK3$_5clEjjN4Buff6SourceE",
     0x47d2c0,
     7,
     {0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC},
     NULL}, // push rbx ; push rsi ; push rdi ; sub rsp, 0x200
    // RosterList::DismissHero: called where CharacterDisplay::OnExitFinish dismisses; the same event and order
    {"_ZN6TownUI5Panel10RosterList11DismissHeroEj",
     0x6a8300,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    // BaseHeroRecruitDisplay::AddHeroSlot: the same six calls in order (slot interface, HeroSlot, callbacks)
    {"_ZN6TownUI5Panel22BaseHeroRecruitDisplay11AddHeroSlotEP4Herob",
     0x6b7430,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    // Hero::AddResolveXpAndLevelEquipmentAndSkills: add [hero+0x12b8], the resolve level (vtable 0x168) before and after
    {"_ZN4Hero38AddResolveXpAndLevelEquipmentAndSkillsEj",
     0x5cb8e0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx
    // Hero::~Hero: what the scalar deleting destructor (Hero vtable slot 0) calls; it stores the Hero vtable first
    {"_ZN4HeroD2Ev", 0x5c2190, 5, {0x48, 0x89, 0x5C, 0x24, 0x10}, NULL}, // mov [rsp+0x10], rbx
    // game_hooks.c sites ("site:<id>"). EstateSummary currency entry: in PreChildrenShowInternal's currency loop,
    // GetCurrencyClassFromIndex is inlined (mov rsi, [rdx+r8*8]); the site is right after it, the class in RSI
    // (macOS: RAX), before the same copy of its first 0x40 bytes and its hash (+0x40)
    {"site:EstateSummary currency entry",
     0x670c0d,
     10,
     {0x0F, 0x10, 0x06, 0x0F, 0x29, 0x85, 0x90, 0x07, 0x00, 0x00},
     NULL}, // movups xmm0, [rsi] ; movaps [rbp+0x790], xmm0
    // BaseHeroRecruitDisplay recruit tooltip: just before PreChildrenShowInternal's GUI_Tooltip call, the hovered HeroSlot*
    // in r13 (its interface at +0x158: "str_hero_slot_unlocked_%s" with the interface's +0x30)
    {"site:BaseHeroRecruitDisplay recruit tooltip",
     0x6b6de1,
     9,
     {0x33, 0xD2, 0x48, 0x8D, 0x8D, 0x50, 0x14, 0x00, 0x00},
     NULL}, // xor edx, edx ; lea rcx, [rbp+0x1450]
    // itemdata.c (Item::Save: the same keys in order; Restore: "Item: No type or id specified" and the tags it adds;
    // AddTag / RemoveTag / GetItemClass are inlined: stand-ins above; the class list global is what the inlined
    // GetItemClass loops read)
    {"_ZNK9Inventory4Item4SaveEP8SaveData", 0x5d2fd0, 5, {0x48, 0x89, 0x5C, 0x24, 0x18}, NULL}, // mov [rsp+0x18], rbx
    {"_ZN9Inventory4Item7RestoreEP11RestoreData",
     0x5d3190,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    {"_ZN9Inventory4Item6AddTagEji", 0, 0, {0}, (void *)item_add_tag},
    {"_ZN9Inventory4Item9RemoveTagEj", 0, 0, {0}, (void *)item_remove_tag},
    {"_ZNK9Inventory11SystemClass12GetItemClassEjj", 0, 0, {0}, (void *)get_item_class},
    {"_ZN9Inventory11SystemClass10s_InstanceE", 0x117d978, 0, {0}, NULL},
    {"win:Item tags operator[]",
     0x3831b0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // (map, const unsigned *key) -> &value
    // items.c: the drag layer (InputManager, InventoryItemDragData), items, the stash's validity lambda ($_2 on macOS:
    // RealmInventory::PreChildrenShowInternal's <lambda_4> _Do_call, (this, out, Item const**, ItemClass const**))
    {"_ZN12InputManager10s_InstanceE", 0x117cd98, 0, {0}, NULL}, // (IsDragging's +0x1bc read after it, 41 sites)
    {"_ZNK12InputManager10IsDraggingEv", 0, 0, {0}, (void *)input_dragging},
    {"_ZNK21InventoryItemDragData7GetItemEv", 0, 0, {0}, (void *)drag_get_item},
    {"_ZNK21InventoryItemDragData12GetSlotIndexEv", 0, 0, {0}, (void *)drag_slot},
    {"_ZN21InventoryItemDragData7SetItemEPKN9Inventory4ItemEj", 0, 0, {0}, (void *)drag_set_item},
    {"win:InventoryItemDragData::GetTypeIdStatic",
     0x6885b0,
     9,
     {0x48, 0x83, 0xEC, 0x28, 0xBA, 0x04, 0x00, 0x00, 0x00},
     NULL}, // its vtable slot 0 jumps here
    {"win:EquipmentDragData::GetTypeIdStatic",
     0x688620,
     9,
     {0x48, 0x83, 0xEC, 0x28, 0xBA, 0x04, 0x00, 0x00, 0x00},
     NULL},
    {"win:RealmInventoryItemDragData::GetTypeIdStatic",
     0x7c2ef0,
     9,
     {0x48, 0x83, 0xEC, 0x28, 0xBA, 0x04, 0x00, 0x00, 0x00},
     NULL},
    // equip_slots.c: ShowTrinkets (hero, iface, vec2 pos (by value, r8), three std::functions (it destroys them), u64,
    // unsigned), ShowEquipment (hero, vec2 pos (rdx)); its callers, told apart by the return address
    {"_ZN11HeroDisplay3App12ShowTrinketsERK4HeroP24InventorySystemInterface4vec2NSt3__"
     "18functionIFN9Inventory7Display15ItemValidResultEPKNS9_4ItemEPKNS9_9ItemClassEEEENS8_IFvjEEESL_yj",
     0x7cf7b0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    {"_ZN11HeroDisplay3App13ShowEquipmentERK4Hero4vec2",
     0x7cee60,
     7,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x18},
     NULL}, // mov r11, rsp ; mov [r11+0x18], rbx
    {"_ZN2UI5Panel16CharacterDisplay13ShowEquipmentE4vec2P4HeroPK9HeroClassb",
     0x7ee220,
     5,
     {0x48, 0x89, 0x54, 0x24, 0x10},
     NULL},
    {"_ZN10Panel_Hero16ShowSelectedHeroEv", 0x728050, 4, {0x40, 0x55, 0x53, 0x56}, NULL}, // called by Panel_Hero::Show
    {"_ZN6TownUI5Panel17PartySetupDisplay23ShowQuickSelectTrinketsE4vec2P4HeroS2_",
     0x647380,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL},
    {"win:EquipmentDragObject::GetTypeIdStatic",
     0x437c30,
     9,
     {0x48, 0x83, 0xEC, 0x28, 0xBA, 0x04, 0x00, 0x00, 0x00},
     NULL}, // its GetTypeId jumps here
    {"_ZNK22InventorySlotInterface7GetItemEv", 0, 0, {0}, (void *)slot_item},
    {"_ZNK22InventorySlotInterface10IsPickedUpEv", 0, 0, {0}, (void *)slot_picked_up},
    {"_ZNK22InventorySlotInterface16GetHighlightTypeEv", 0, 0, {0}, (void *)slot_highlight_type},
    {"_ZNK9Inventory4Item7IsValidEv", 0, 0, {0}, (void *)item_valid},
    {"_ZNK9Inventory4Item15GetTrinketClassEv", 0, 0, {0}, (void *)item_trinket_class},
    {"_ZNK2UI14CampaignAssets8Instance16GetTrinketRarityEj", 0, 0, {0}, (void *)trinket_rarity},
    {"_ZN17EquipmentDragData7SetHeroEP4Hero", 0, 0, {0}, (void *)equip_drag_set_hero},
    {"_ZN9Inventory4ItemC2ERKS0_",
     0x3ca5f0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // copy (OnDropAccepted's local copy)
    {"_ZN9Inventory4ItemD1Ev",
     0x3ca790,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x20},
     NULL}, // the four vectors and the tags, as macOS
    // inv_slots.c: SetInventorySystem (iface, system, id, std::function by address; returns early while the system
    // and its slot count are unchanged) and Restore (calls Init 0x5d3990, which sizes the vector from max_slots)
    {"_ZN24InventorySystemInterface18SetInventorySystemEPN9Inventory6SystemERK12StrongTypeIdI22InventorySlotInterfaceEN"
     "St3__18functionIFvPS4_EEE",
     0x7e1330,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    {"_ZN9Inventory6System7RestoreEjP11RestoreData",
     0x5d5420,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rbx
    {"_ZNK9Inventory11SystemClass20GetSystemConfigClassEj", 0, 0, {0}, (void *)get_system_config},
    {"_ZNSt3__16vectorIN9Inventory4ItemENS_9allocatorIS2_EEE8__appendEm", 0, 0, {0}, (void *)item_vector_append},
    {"_ZNSt3__16vectorIN9Inventory4ItemENS_9allocatorIS2_EEE22__base_destruct_at_endB8ne200100EPS2_",
     0,
     0,
     {0},
     (void *)item_vector_destruct_at_end},
    {"win:vector<Item> construct n",
     0x5d6800,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // (at, n, vec): Item() 0x5d19a0 each
    {"win:vector<Item> copy range",
     0x453e40,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // (first, last, dest): copy ctor 0x3ca5f0
    {"win:vector<Item> change array", 0x454140, 5, {0x48, 0x89, 0x5C, 0x24, 0x18}, NULL}, // (vec, buf, size, cap)
    {"_ZN9Inventory4Item5ClearEv",
     0x5d2ed0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // the same fields cleared as macOS
    {"win:Inventory::Item::operator=",
     0x3f1f80,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // (SetItem, inlined in OnDrop)
    {"win:Inventory::System::GetSpaceForItem", 0x5d4980, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL},
    {"win:Inventory::System::AddItem",
     0x5d40c0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // (system, Item&, bool): fires InventoryAddItemEvent
    {"_ZN8Campaign7CurrentEv", 0x54c950, 4, {0x48, 0x83, 0xEC, 0x38}, NULL},
    {"_ZN6Estate6System17GetTrinketStorageENS0_19ETrinketStorageTypeE", 0, 0, {0}, (void *)trinket_storage},
    {"_ZNK9Inventory6System16GetIsRoomForItemEPKNS_4ItemE", 0, 0, {0}, (void *)inventory_has_room},
    {"_ZN9Inventory6System13AddCopyOfItemEPKNS_4ItemE", 0, 0, {0}, (void *)inventory_add_copy},
    {"_ZN4Loot6System14QueueOwnedItemERKN9Inventory4ItemE", 0, 0, {0}, (void *)loot_queue_owned},
    {"_ZN4Loot6System15SetGenerateTypeEPKNS_8Generate4TypeE", 0, 0, {0}, (void *)loot_set_generate_type},
    {"_ZN5Quest12RaidInstance13GetDifficultyEv", 0, 0, {0}, (void *)quest_difficulty},
    {"_ZN8Building5Store17BaseTrinketSupply18GetInventorySystemEv", 0, 0, {0}, (void *)supply_inventory},
    {"_ZN9Inventory7Display8GetTitleERKNS_4ItemE", 0, 0, {0}, (void *)item_title},
    {"_ZNK6TownUI6Screen11TownDisplay21GetCharacterPanelHeroEv", 0, 0, {0}, (void *)town_panel_hero},
    // announce.c (match.py's IsAnyHamletDisplayActive pick 0x6d3980 was a caller-like neighbour, not it)
    {"_ZNK6TownUI6Screen11TownDisplay19GetIsFadingInternalEv",
     0x6d7400,
     7,
     {0x83, 0xB9, 0x58, 0x17, 0x00, 0x00, 0x02},
     NULL}, // cmp dword [rcx+0x1758], 2 (macOS +0x16a8)
    {"_ZNK6TownUI6Screen11TownDisplay24IsAnyHamletDisplayActiveEv",
     0x6d3920,
     7,
     {0x83, 0xB9, 0x00, 0x18, 0x00, 0x00, 0x00},
     NULL}, // cmp dword [rcx+0x1800], 0
    {"_ZNK6TownUI6Screen11TownDisplay23GetIsNewsDisplayVisibleEv", 0, 0, {0}, (void *)town_news_visible},
    {"_ZNK6TownUI6Screen11TownDisplay28GetIsTownEventDisplayVisibleEv", 0, 0, {0}, (void *)town_event_visible},
    {"_ZN6TownUI6Screen11TownDisplay19IsAnyBuildingActiveEv", 0, 0, {0}, (void *)town_any_building},
    {"_ZNSt3__110__function6__funcIZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEvE3$_2NS_9allocatorIS5_"
     "EEFN9Inventory7Display15ItemValidResultEPKNS8_4ItemEPKNS8_9ItemClassEEEclEOSD_OSG_",
     0x6a1ef0,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20},
     NULL}, // push rbx ; sub rsp, 0x20
    // ui_draw.c (ObtainASync: TextureFactory::CreateTexture is inlined in new_Texture_FromFile, which calls it as
    // (manager, result handle*, path, type id, ResourceBuildOptions*); the texture type id is *(0x255b10)())
    {"win:ResourceManager::ObtainASync",
     0x380d00,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    {"win:Texture type id", 0x255b10, 9, {0x48, 0x83, 0xEC, 0x28, 0xBA, 0x04, 0x00, 0x00, 0x00}, NULL},
    {"_ZN15ResourceManager10s_InstanceE", 0x117d858, 0, {0}, NULL},
    {"_ZN15ResourceManager11GetResourceERK19ResourceEntryHandle",
     0x382c90,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20},
     NULL}, // entries +0x98
    {"win:ResourceHandle<Texture> copy",
     0x24ce10,
     0,
     {0},
     NULL}, // (dst, src): empties dst, lock inc src's entry refcount
    {"_ZN15ResourceManager7ReleaseERK19ResourceEntryHandle",
     0x380ee0,
     6,
     {0x4C, 0x8B, 0xC9, 0x4C, 0x8B, 0xD2},
     NULL}, // mov r9, rcx ; mov r10, rdx
    {"_ZN21ImageButtonParametersD1Ev",
     0x271090,
     5,
     {0x48, 0x89, 0x4C, 0x24, 0x08},
     NULL}, // one destructor for all the parameters
    {"_Z14GUI_Draw_ImageRK16WidgetParameters",
     0x273ae0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // GUI_DrawWidget types 1, 5
    {"_Z17GUI_Image_DrawNowRK15ImageParameters", 0, 0, {0}, (void *)gui_image_now},
    {"_Z21GUI_HandleDropContextRK16WidgetParametersRK4vec3RK4vec2",
     0x273f20,
     10,
     {0x4C, 0x8B, 0xDC, 0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00},
     NULL}, // mov r11, rsp ; sub rsp, 0xf8
    {"_Z21GUI_HandleDragContextRK16WidgetParametersRK4vec3RK4vec2",
     0x2743c0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x70, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rsi
    {"_Z21GUI_HandleDropContextRK16WidgetParameters", 0, 0, {0}, (void *)handle_drop},
    {"_Z21GUI_HandleDragContextRK16WidgetParameters", 0, 0, {0}, (void *)handle_drag},
    {"win:OffsetFromAnchor",
     0x27d440,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // (vec2 *result, anchor, vec2 const &size)
    {"_Z13Transform_Topv", 0, 0, {0}, (void *)transform_top},
    {"_Z26TextureFont_GetWidthOfTextPK11TextureFontPKcj",
     0x27fdc0,
     6,
     {0x41, 0x54, 0x41, 0x55, 0x41, 0x57},
     NULL}, // push r12 ; push r13 ; push r15
    {"_Z27TextureFont_GetHeightOfTextPK11TextureFontfPKc",
     0x280130,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // (font, width in xmm1, text)
    // (this, sprite, vec2 const&, function *before, function *after): copies both to +0xb0 / +0xf0 and destroys the arguments
    {"_ZN16SpriteParametersC2EPK11SpineSpriteRK4vec2NSt3__18functionIFvPK16WidgetParametersEEESC_",
     0x40eab0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL},
    {"_Z10GUI_SpriteRK16SpriteParameters", 0, 0, {0}, (void *)gui_sprite},
    {"_ZN9Inventory14OutlineManager10s_InstanceE",
     0x117de60,
     0,
     {0},
     NULL}, // (OutlineManager::Create stores it; Load's caller reads it)
    // ui_tooltip.c (TextureFont_Display(anchor, width) is inlined: GUI_TooltipRender calls DisplayRaw(font, anchor, x,
    // y, width, +inf, text, flag, 0x7fffffff, true, false, -1) per line; hooked for it)
    {"_Z11Draw_Colour4rgba", 0x268140, 4, {0x48, 0x83, 0xEC, 0x58}, NULL}, // sub rsp, 0x58
    {"_Z11Draw_Colour5hvec4", 0, 0, {0}, (void *)draw_colour_hvec4},
    {"_Z11GUI_TooltipRK20GuiTooltipParametersb", 0x2788b0, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL}, // (params, force)
    {"_Z17GUI_TooltipRenderRK20GuiTooltipParameters", 0x278ac0, 5, {0x48, 0x8B, 0xC4, 0x55, 0x53}, NULL},
    {"_Z18GUI_RenderTooltipsv",
     0x2782a0,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    {"win:TextureFont_DisplayRaw",
     0x27ef10,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx
    {"_Z12GUI_CallbackRK24WidgetCallbackParameters", 0, 0, {0}, (void *)gui_callback},
    {"_ZN24WidgetCallbackParametersC2ENSt3__18functionIFvPK16WidgetParametersEEE", 0, 0, {0}, (void *)callback_params},
    {"_ZN24WidgetCallbackParametersD1Ev",
     0x271090,
     5,
     {0x48, 0x89, 0x4C, 0x24, 0x08},
     NULL}, // the parameters' destructor
    {"_ZN20GuiTooltipParametersD1Ev", 0, 0, {0}, (void *)tip_params_dtor},
    {"_Z36TextureFont_PushEncodedColourEnabledb", 0, 0, {0}, (void *)encoded_push},
    {"_Z35TextureFont_PopEncodedColourEnabledv", 0, 0, {0}, (void *)encoded_pop},
    {"win:vector<bool>::insert",
     0x2443e0,
     6,
     {0x40, 0x53, 0x56, 0x57, 0x41, 0x57},
     NULL}, // (v, result, where*, count, value*)
    {"win:vector<bool>::pop_back", 0x279ed0, 7, {0x48, 0x83, 0xEC, 0x48, 0x4C, 0x8B, 0xC9}, NULL},
    {"_Z15Texture_GetSizeRK14ResourceHandleI7TextureE", 0, 0, {0}, (void *)texture_get_size},
    // (actor, class, item, vec2 pos (by value, r9), int, bool, char const*, GuiTooltipParameters&)
    {"_ZN9Inventory7Display17CreateItemTooltipEPK5ActorPKNS_9ItemClassEPKNS_4ItemE4vec2ibPKcR20GuiTooltipParameters",
     0x7d8dc0,
     5,
     {0x4C, 0x89, 0x4C, 0x24, 0x20},
     NULL}, // mov [rsp+0x20], r9
    // ui_input.c, ui_dialog.c (stand-ins)
    {"_Z19plinthGetCanvasSizeRfS_", 0, 0, {0}, (void *)canvas_size},
    {"_Z22Input_GetMousePositionRfS_", 0, 0, {0}, (void *)input_mouse_position},
    {"_Z24Input_GetMouseWheelDeltaRfS_", 0, 0, {0}, (void *)input_wheel_delta},
    {"_Z23Transform_CanvasToLocalRK4vec3", 0, 0, {0}, (void *)canvas_to_local},
    {"_Z18GUI_IsPointClipped4vec2", 0, 0, {0}, (void *)view_is_point_clipped},
    {"_Z20GUI_GetMousePositionR4vec2", 0, 0, {0}, (void *)gui_mouse_position},
    // ui_panel.c (GUI_PushClip(left, right, top, bottom): View_Clip 0x26bc50, then the clip deque; GUI_PushUnclip:
    // View_Unclip 0x26c2d0 if the top clip is on, then an unclipped entry)
    {"_Z12GUI_PushClipffff",
     0x271460,
     10,
     {0x48, 0x8B, 0xC4, 0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00},
     NULL}, // mov rax, rsp ; sub rsp, 0xd8
    {"_Z14GUI_PushUnclipv", 0x271530, 7, {0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00}, NULL}, // sub rsp, 0x98
    {"_Z11GUI_PopClipv", 0x2715f0, 4, {0x48, 0x83, 0xEC, 0x38}, NULL},                      // sub rsp, 0x38
    {"_Z19Transform_Translatef", 0, 0, {0}, (void *)transform_translate_z},
    {"_Z15Transform_Scalef", 0, 0, {0}, (void *)transform_scale},
    {"_ZNK10TweenFloatcvfEv", 0, 0, {0}, (void *)tween_float},
    {"_Z22Transform_LocalToWorldRK4vec2", 0, 0, {0}, (void *)transform_local_to_world},
    // ui_hotspot.c: (result, params, f1..f4): the 5-byte input state through a hidden pointer; it destroys the four
    // by-value std::functions (MSVC: the callee does)
    {"_Z11GUI_HotSpotRK17HotSpotParametersNSt3__18functionIFvvEEES5_S5_S5_",
     0x275260,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    // ui_invgrid.c: (iface, actor, layout, vec2 pos (by value, r9), params, info) -> the hovered item
    {"_ZN9Inventory7Display19InventoryButtonGridEP24InventorySystemInterfacePK5ActorRKNS0_10GridLayoutE4vec2RKNS0_"
     "29InventoryButtonGridParametersEPNS0_30InventoryButtonGridDisplayInfoE",
     0x7de520,
     5,
     {0x4C, 0x89, 0x4C, 0x24, 0x20},
     NULL}, // mov [rsp+0x20], r9
    {"_Z23Input_IsUsingControllerv", 0, 0, {0}, (void *)input_using_controller},
    {"_ZNK24InventorySystemInterface18GetInventorySystemEv", 0, 0, {0}, (void *)iface_inventory_system},
    // item_sale.c: InventoryButton is (result (6 bytes), params, actor, fn1, fn2, b1, anim, name, b2, sprite, fn3, b3)
    // and destroys its std::functions; the wagon's buy lambda's _Do_call (this, unsigned&) passes this + 8 (the stock
    // at +0) and the slot to the body (0x686cf0; the shard wagon's is 0x687780)
    {"_ZN9Inventory7Display15InventoryButtonERKNS0_25InventoryButtonParametersEPK5ActorNSt3__18functionIFvvEEESA_bPNS0_"
     "20TreasureItemAnimVarsEP17FixedLengthStringILi64EEbP11SpineSpriteNS8_IFPKSG_PKNS_4ItemEjbEEEb",
     0x7db790,
     7,
     {0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20},
     NULL}, // mov rax, rsp ; mov [rax+0x20], r9
    {"_ZNSt3__110__function6__funcIZN6TownUI5Panel20TrinketSupplyDisplay23PreChildrenShowInternalEvE3$_1NS_"
     "9allocatorIS5_EEFvjEEclEOj",
     0x688470,
     6,
     {0x8B, 0x12, 0x48, 0x83, 0xC1, 0x08},
     NULL}, // mov edx, [rdx] ; add rcx, 8
    {"_ZN9Inventory6System7GetItemEj", 0, 0, {0}, (void *)inventory_get_item},
    {"_ZNK9Inventory6System17GetTotalSlotCountEv", 0, 0, {0}, (void *)inventory_slot_count},
    {"_ZN9Inventory6System20GetItemFromSlotIndexEj", 0, 0, {0}, (void *)inventory_slot_item},
    {"_ZNK9Inventory4Item9GetIdHashEv", 0, 0, {0}, (void *)item_id_hash},
    {"_ZN6TownUI6Assets7CurrentEv", 0, 0, {0}, (void *)town_assets},
    // ui_confirm.c: EntryParameters (0x310: text 0, answers vector 0x200 (0x148-byte answers), two std::functions
    // 0x218 / 0x258 (the back-out), four texture set pointers 0x2e8); PushEntry copies it into the System's vector of
    // 0x330-byte entries (8-byte header); the answers are freed by MSVC's std::_Deallocate, so they're allocated with
    // its std::_Allocate (0x233e20: big blocks get an aligned header), which stands in for operator new here
    {"_ZN13ConfirmDialog15EntryParametersC1Ev", 0x7ba610, 5, {0x48, 0x89, 0x4C, 0x24, 0x08}, NULL},
    {"_ZN13ConfirmDialog15EntryParametersD1Ev", 0x3d39a0, 5, {0x48, 0x89, 0x5C, 0x24, 0x18}, NULL},
    {"_ZN13ConfirmDialog6System9PushEntryERKNS_15EntryParametersE", 0x7bafd0, 5, {0x48, 0x89, 0x5C, 0x24, 0x18}, NULL},
    {"_Znwm", 0x233e20, 7, {0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9}, NULL},
    {"_ZN13ConfirmDialog6System8InstanceEv", 0, 0, {0}, (void *)confirm_system},
    {"_ZN17FixedLengthStringILi512EEaSEPKc", 0, 0, {0}, (void *)fls512_assign},
    {"_ZN17FixedLengthStringILi128EEaSEPKc", 0, 0, {0}, (void *)fls128_assign},
    // ui_confirm.c's drawing: ShowEntry (this, Entry const&); Input_IsKeyPressed (key, the second argument unused,
    // bool); CloseEntry and BackoutOfCurrentEntry inlined
    {"_ZN13ConfirmDialog6System9ShowEntryERKNS_5EntryE",
     0x7bb320,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    {"_Z18Input_IsKeyPressedjbb", 0x289c90, 6, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20}, NULL},
    {"_ZN13ConfirmDialog6System10CloseEntryEv", 0, 0, {0}, (void *)confirm_close_entry},
    {"_ZN13ConfirmDialog6System21BackoutOfCurrentEntryEv", 0, 0, {0}, (void *)confirm_backout},
    // stash_view.c: RealmInventory's fields sit 0x10 later than macOS from its interface (+0xc0) on (game_layout.h);
    // DrawSortButton (this, texture, tooltip, type, std::function (5th, on the stack; it destroys it)); DrawUnequipButton
    // (this, vec2 pos (rdx)); the sell callback's _Do_call (this, unsigned&) passes this + 8 and the slot to the body
    // (0x69aeb0), so its inventory is at +0x10 as macOS; ScrollBarWidget::RenderInternal opens with a je rel32 (moved
    // like a RIP operand); Roster::UnequipAllTrinkets (0x583110) and ExecuteUnequipTrinkets (0x6a1da0) checked for
    // the roster entry's fields
    {"_ZN6TownUI5Panel14RealmInventory12ActiveUpdateEf",
     0x699440,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    {"_ZN6TownUI5Panel14RealmInventory13UpdateBasePosER4vec2",
     0x698fa0,
     7,
     {0x48, 0x8B, 0x81, 0x40, 0x05, 0x00, 0x00},
     NULL}, // mov rax, [rcx+0x540]
    {"_ZN6TownUI5Panel14RealmInventory14UpdateBaseSizeER4vec2",
     0x698fb0,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30},
     NULL}, // push rbx ; sub rsp, 0x30
    {"_ZN6TownUI5Panel14RealmInventory14DrawSortButtonERK14ResourceHandleI7TextureEPKcjNSt3__18functionIFvvEEE",
     0x69bb00,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx
    {"_ZN6TownUI5Panel14RealmInventory15DrawTrinketAreaEv",
     0x69e940,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    {"_ZN6TownUI5Panel14RealmInventory17DrawUnequipButtonE4vec2",
     0x69c900,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08},
     NULL}, // mov rax, rsp ; mov [rax+8], rbx
    {"_ZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEv",
     0x699960,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx
    {"_ZNSt3__110__function6__funcIZN6TownUI5Panel14RealmInventory23PreChildrenShowInternalEvE3$_0NS_9allocatorIS5_"
     "EEFvjEEclEOj",
     0x6a2050,
     6,
     {0x8B, 0x12, 0x48, 0x83, 0xC1, 0x08},
     NULL}, // mov edx, [rdx] ; add rcx, 8
    {"_ZNK15ScrollBarWidget14RenderInternalEj",
     0x3950b0,
     9,
     {0xF6, 0xC2, 0x01, 0x0F, 0x84, 0xF8, 0x0E, 0x00, 0x00},
     NULL,
     .rip_disp = 5,
     .rip_next = 9}, // test dl, 1 ; je rel32
    {"_ZN6Roster6System4SaveEb",
     0x57be70,
     11,
     {0x40, 0x55, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x78, 0x01, 0x00, 0x00},
     NULL}, // (roster, bool)
    {"_ZN6Estate6System4SaveEv", 0x55dda0, 5, {0x40, 0x53, 0x55, 0x56, 0x57}, NULL},
    {"_ZN7Trinket7Library15GetTrinketClassEj", 0, 0, {0}, (void *)trinket_library_class},
    {"_ZN9Inventory6System14RemoveSlotItemEji", 0, 0, {0}, (void *)inventory_remove_slot_item},
    {"_ZN9Inventory6System10RemoveItemEPKNS_4ItemE", 0, 0, {0}, (void *)inventory_remove_item},
    {"_ZN9Inventory6System10RemoveSlotEj", 0, 0, {0}, (void *)inventory_remove_slot},
    {"_ZN24InventorySystemInterface16GetSlotInterfaceEj", 0, 0, {0}, (void *)iface_slot},
    {"_ZNK24InventorySystemInterface12GetSlotCountEv", 0, 0, {0}, (void *)iface_slot_count},
    // item_draw.c (GUI_Image is inlined: its copies end in win:GUI_AppendWidget(5, params), hooked for it)
    {"_ZNK9Inventory4Item7GetIconEv",
     0x5d2d00,
     5,
     {0x48, 0x89, 0x54, 0x24, 0x10},
     NULL}, // (this, result): InventoryButton's icon
    {"_ZN9Inventory7Display14GetIconTextureEPKNS_4ItemE",
     0x7d7f30,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // (result, item)
    // item_types.c (Estate::System::Save's filter: its _Func_impl's _Do_call, vtable 0xe91908 slot 2, (this, Item const&))
    {"_ZN6Estate6System18AddInventorySystemERKN9Inventory6SystemEjPKNSt3__13mapINS_12CurrencyTypeEfNS5_4lessIS7_EENS5_"
     "9allocatorINS5_4pairIKS7_fEEEEEE",
     0x55fd30,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL},                                                            // mov [rsp+0x18], rbx
    {"_ZN6Estate11SystemClass10s_InstanceE", 0x117d910, 0, {0}, NULL}, // EstateSummary's loop: mov rcx, [rip+..]
    {"_ZNK6Estate11SystemClass26GetNumberOfCurrencyClassesEv", 0, 0, {0}, (void *)estate_class_count},
    {"_ZNK6Estate11SystemClass25GetCurrencyClassFromIndexEj", 0, 0, {0}, (void *)estate_class_at},
    {"_ZNK6Estate6System9GetAmountEj", 0, 0, {0}, (void *)estate_amount},
    {"_ZN6Estate6System9AddAmountEji",
     0x55e910,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // (estate, currency hash, amount)
    {"_ZNSt3__110__function6__funcIZN6Estate6System4SaveEvE3$_0NS_9allocatorIS4_EEFbRKN9Inventory4ItemEEEclESA_",
     0x560de0,
     6,
     {0x8B, 0x05, 0x72, 0xA1, 0x4F, 0x02},
     NULL,
     .rip_disp = 2,
     .rip_next = 6}, // mov eax, [rip+kTrinket]
    {"_ZN22InventorySlotInterface11PlayTakeSfxEv",
     0x7e11c0,
     7,
     {0x48, 0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00},
     NULL}, // sub rsp, 0xc8
    {"_Z11Audio_EventPKcbNSt3__18functionIFvS0_EEE",
     0x22fac0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // (name, bool unread, function*)
    // in the drop-target base's vtable: this = slot + SLOT_DROP_THIS
    {"_ZN22EquipmentSlotInterface6OnDropERK4vec2P8DragData", 0x7e0d40, 5, {0x48, 0x89, 0x5C, 0x24, 0x10}, NULL},
    {"_ZNK9Inventory6System16GetAmountOfItemsEv", 0, 0, {0}, (void *)inventory_count},
    {"_ZNK9Inventory6System7GetItemEj", 0, 0, {0}, (void *)inventory_get_item},
    {"_ZNK9Inventory4Item9GetHasTagEji", 0, 0, {0}, (void *)item_has_tag},
    // compare sites: RealmInventory::PreChildrenShowInternal's hovered item class (r10) before its sell price; the
    // shared validity function's sell mode (0x69b4b0, item in rbx); RealmInventorySlotInterface::IsDropItemValidInternal
    // (0x7e3340, the second test: kTrinketUnlock)
    {"win:item_types stash sell price",
     0x69a800,
     10,
     {0x8B, 0x05, 0xAE, 0x3A, 0x3D, 0x02, 0x41, 0x39, 0x42, 0x40},
     NULL}, // mov eax, [rip+kTrinket] ; cmp [r10+0x40], eax
    // the sell mode's validity, $_3's _Do_call 0x6a1e70 (as macOS: sell mode flag at +8, type, class sell price > 0;
    // not 0x69b61d, the same test in another lambda's 0x69b4b0, which macOS doesn't site either)
    {"win:item_types stash sell mode",
     0x6a1e80,
     10,
     {0x8B, 0x05, 0x2E, 0xC4, 0x3C, 0x02, 0x41, 0x39, 0x40, 0x44},
     NULL}, // mov eax, [rip+kTrinket] ; cmp [r8+0x44], eax
    {"win:item_types stash drop",
     0x7e334b,
     6,
     {0x3B, 0x05, 0xC3, 0xE5, 0x29, 0x02},
     NULL}, // cmp eax, [rip+kTrinketUnlock]
    // act_outs.c: GetTypeFromId (its assert names it; the k_Data getters are inlined everywhere: no new type numbers)
    {"_ZN6ActOut15CombatStartTurn13GetTypeFromIdEPKc",
     0x48f310,
     9,
     {0x40, 0x53, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00},
     NULL}, // push rbx ; sub rsp, 0x80
    {"_ZN6ActOut15CombatStartTurn13GetIdFromTypeENS0_5Types5eTypeE", 0, 0, {0}, (void *)act_out_id},
    // Combat_StartTurn 0x49ffb0 inlines Actor::RollActOutCombatStartTurnData and the three checks: the site after the
    // roll (the entry in rdi and [rbp-0x68]; actor [rbp-0x40], rng [rbp-0x50]) and the one where the checks' verdicts
    // meet (r14b not a repeat, dl not disabled, al under the raid limit; 0x4a0228 jumps to its start)
    {"win:Combat_StartTurn act-out roll",
     0x4a015d,
     7,
     {0x48, 0x8D, 0x8D, 0x68, 0x01, 0x00, 0x00},
     NULL}, // lea rcx, [rbp+0x168]
    {"win:Combat_StartTurn act-out checks",
     0x4a027c,
     9,
     {0x45, 0x84, 0xF6, 0x0F, 0x84, 0x37, 0x14, 0x00, 0x00},
     NULL,
     .rip_disp = 5,
     .rip_next = 9}, // test r14b, r14b ; je rel32
    // (the act-out data loader, 0x48dda0, inlines GetTypeFromId: the site before it hashes its local copy of the type)
    {"win:ActOut data loader: CombatStartTurn type lookup",
     0x48e014,
     9,
     {0x33, 0xD2, 0x48, 0x8D, 0x8D, 0xE0, 0x00, 0x00, 0x00},
     NULL}, // xor edx, edx ; lea rcx, [rbp+0xe0]
    // helpers of the stand-ins
    {"_ZN4Hero11RemoveQuirkEjb",
     0x5c66d0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx (hooked by hero_hooks.c)
    {"win:Actor::UpdateBuffsInternal", 0x47b510, 5, {0x48, 0x89, 0x5C, 0x24, 0x20}, NULL},   // mov [rsp+0x20], rbx
    {"win:SpineData handle", 0x24cec0, 6, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20}, NULL},       // push rdi ; sub rsp, 0x20
    {"win:spSkeletonData_findAnimation", 0x842c10, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL}, // mov [rsp+8], rbx
    {"win:ImageButtonParameters base", 0x270ed0, 5, {0x48, 0x89, 0x4C, 0x24, 0x08}, NULL},   // mov [rsp+8], rcx
    {"win:Transform_Multiply", 0x26ab30, 4, {0x48, 0x83, 0xEC, 0x68}, NULL},                 // sub rsp, 0x68
    {"win:AllocateWidgetString", 0x273310, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL},         // mov [rsp+8], rbx
    {"win:HeroData map insert",
     0x7d2560,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // (map, result {node, inserted}, key)
    {"win:SkillData map index", 0x7d23b0, 5, {0x40, 0x53, 0x55, 0x56, 0x57}, NULL}, // (map, key) -> the value
    {"win:Campaign::Current", 0x54c950, 4, {0x48, 0x83, 0xEC, 0x38}, NULL},         // sub rsp, 0x38
    {"win:Upgrade::GetRequirementClass", 0x471f40, 3, {0x48, 0x8B, 0x05}, NULL},    // (tree hash, code)
    {"win:Upgrade purchases insert",
     0x592760,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // (map, result {node, inserted}, key)
    {"win:ResourceHandle<Texture> resolve",
     0x258780,
     6,
     {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20},
     NULL},                                                                             // push rdi ; sub rsp, 0x20
    {"win:GUI_AppendWidget", 0x2733e0, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL},        // (type, params)
    {"win:ColourLibrary::GetEntry", 0x26ec10, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL}, // (out, id, bool *found)
    {"win:Transform_CanvasToLocal",
     0x26b270,
     10,
     {0x48, 0x8B, 0xC4, 0x48, 0x81, 0xEC, 0x08, 0x01, 0x00, 0x00},
     NULL}, // (result, point): mov rax, rsp ; sub rsp, 0x108
    {"win:RaidStat::GetValue(mask)", 0x4bb890, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL}, // (stat, mask, clamped)
    // combat_context.c
    {"_ZN5Trait7Library10s_InstanceE", 0x117d9a8, 0, {0}, NULL},
    {"_ZNK4Hero15GetResolveLevelEv",
     0x5cd150,
     6,
     {0x40, 0x57, 0x48, 0x83, 0xEC, 0x30},
     NULL}, // push rdi ; sub rsp, 0x30 (Hero vtable slot 45)
    {"_ZNK5Actor6HasTagEv", 0x477ce0, 5, {0x48, 0x89, 0x5C, 0x24, 0x10}, NULL}, // Combat_StartTurn's case 8
    {"_ZNK5Actor8IsCorpseEv", 0, 0, {0}, (void *)actor_is_corpse},
    {"_ZNK8RaidStat8GetValueEb", 0, 0, {0}, (void *)raid_stat_value},
    {"_ZN15ActorCombatStat13GetIdFromTypeENS_5Types5eTypeE", 0, 0, {0}, (void *)combat_stat_id},
    {"_ZNK5Trait7Library7GetTypeEj", 0, 0, {0}, (void *)trait_type},
    // barks.c: QueueBark (display, actor, id, bool, bool, SourceIdentifier by hidden pointer; returns the duration)
    {"_ZN6RaidUI6Screen11RaidDisplay9QueueBarkEP5Actor8c_stringbbN6ActOut16SourceIdentifierE",
     0x764850,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx (QueuePartyBark calls it)
    {"_ZN6RaidUI6Screen11RaidDisplay21QueueTriggerCurioBarkEP5ActorPKcS5_PK4Propb",
     0x764ed0,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx ('curio_bark')
    {"_ZNK5Actor11GetBarkTypeEN6ActOut16SourceIdentifierE", 0, 0, {0}, (void *)actor_bark_type},
    // combat_events.c (the Dispatcher<T> probes stay off: Dispatch is inlined into each sender on Windows)
    {"_ZN6Battle10StartRoundEv", 0x59c570, 5, {0x48, 0x89, 0x5C, 0x24, 0x10}, NULL}, // mov [rsp+0x10], rbx
    {"_ZN5Actor8SetRanksEj", 0x479ee0, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL}, // mov [rsp+8], rbx ('invalid rank')
    {"_ZN6Battle15Actor_MoveSkillEP5Actori", 0x5af000, 5, {0x48, 0x89, 0x5C, 0x24, 0x20}, NULL}, // mov [rsp+0x20], rbx
    {"_ZN6Battle18AddMonsterToBattleEP12MonsterClassbbjbbb",
     0x59ae40,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx (Wave's caller 0x50720b)
    {"_ZNK7Monster15GetMonsterClassEv", 0, 0, {0}, (void *)monster_class},
    // value_hooks.c (all same prototypes as macOS; ApplyHealthHeal and ApplyHealthDamageInternal return as macOS)
    {"_ZN5Actor25ApplyHealthDamageInternalER11prng_minstdfRK11eDamageTypejRK17eDamageSourceTypebPS_",
     0x47e570,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // mov rax, rsp ; mov [rax+0x10], rbx (Hero vtable slot 50)
    {"_ZN7Monster25ApplyHealthDamageInternalER11prng_minstdfRK11eDamageTypejRK17eDamageSourceTypebP5Actor",
     0x5f0930,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx (Monster slot 50, 'str_prisoner_damage_%s')
    {"_ZN5Actor15ApplyHealthHealER11prng_minstdfPKS_N4Heal6Source5Types5eTypeEb",
     0x47aba0,
     5,
     {0x4C, 0x8B, 0xDC, 0x53, 0x57},
     NULL}, // mov r11, rsp ; push rbx ; push rdi (vtable +0x198, then the heal hash)
    {"_ZN5Actor17ApplyStressDamageER11prng_minstdfPKS_N6Stress6Source5Types5eTypeE",
     0x47c480,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rbx (Hero slot 36)
    {"_ZN5Actor20ApplyRawStressDamageER11prng_minstdf",
     0x47bfb0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx ('str_bark_increasingstress')
    {"_ZN5Actor15ApplyStressHealER11prng_minstdfPKS_N6Stress6Source5Types5eTypeE",
     0x47c6d0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // mov rax, rsp ; mov [rax+0x10], rbx (Hero slot 37)
    {"_ZN4Raid16ChangeTorchlightEfb",
     0x6091a0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20},
     NULL}, // mov rax, rsp ; mov [rax+0x20], rbx
    {"_ZN4Heal6SourceL10k_TypeDataE", 0x2ab6e80, 0, {0}, NULL},
    // ResistanceType::k_Data: 0xb8 per type, the clamp {min, max} +0x84 / flag +0x8c (inlined in GetCurrentResistance
    // 0x47868b: lea 0x4786a0 + 0x265b170)
    {"_ZN14ResistanceTypeL6k_DataE",
     0x2ad3810,
     0,
     {0},
     NULL}, // 15 x 0x68, id first (initialiser 0x8a100; ApplyHealthHeal reads its hashes)
    {"_Z19GetIdFromDamageType11eDamageType", 0, 0, {0}, (void *)damage_type_id},
    {"_Z25GetIdFromDamageSourceType17eDamageSourceType", 0, 0, {0}, (void *)damage_source_id},
    {"_ZN6Stress6Source13GetIdFromTypeENS0_5Types5eTypeE", 0, 0, {0}, (void *)stress_source_id},
    {"_Z15create_Timelinev", 0, 0, {0}, (void *)create_timeline},
    // hero_hooks.c (same prototypes as macOS). Camping::Camp::FinishCamp is inlined into RaidDisplay::FinishCamp
    // (its only caller on both), hooked for it. HeroUsedItemEvent's dispatch is inlined into its one sender
    // (DungeonController::HeroUseItem: a site after it, the event at rbp+0x2a0); TrinketTriggeredEvent's
    // Dispatcher::Dispatch is not: its seven callers are the six senders and the debug input
    {"_ZN4Hero8AddQuirkEPKN5Quirk5ClassEbfbbbbbP8Timeline",
     0x5c45a0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx ('Hero tried to gain an incompatible quirk')
    {"_ZN4Hero18OnDeathDoorReachedERK11eDamageTypejRK17eDamageSourceType",
     0x5cda80,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi ('arena_hero_deathdoor_reached')
    {"_ZN4Hero20OnDeathStartInternalEv",
     0x5caa30,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx (Hero slot 23)
    {"_ZN5Actor16QueueHeartAttackER11prng_minstdb",
     0x47ef50,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx ('str_heart_attack_%s')
    {"_ZN7Camping19PerformCampingSkillERNS_4CampER5PartyRN4Loot6SystemEjjj",
     0x5bce30,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi ('%s: Hero chooses not to use skill!')
    {"win:RaidDisplay::FinishCamp",
     0x765d30,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    {"win:HeroUseItem after HeroUsedItemEvent",
     0x425fc0,
     5,
     {0x48, 0x8B, 0x7C, 0x24, 0x68},
     NULL}, // mov rdi, [rsp+0x68] (then the inlined RemoveSlotItem)
    {"_ZN10DispatcherIK5Event26EventListenerDataContainerLb1EE8DispatchI21TrinketTriggeredEventEEvPT_",
     0x3ef4f0,
     6,
     {0x40, 0x53, 0x55, 0x56, 0x41, 0x56},
     NULL}, // push rbx ; push rbp ; push rsi ; push r14 ('Dispatcher<...>::ListenerListData::Dispatch' inlined; the event's hero at +0x10)
    {"_ZNK4Hero28GetSelectedCampingSkillClassEj",
     0x5c92b0,
     0,
     {0},
     NULL}, // SkillLibrary::GetSkillClass inlined (classes 0x78 apart)
    {"_ZN5Quirk7Library10s_InstanceE", 0x117d988, 0, {0}, NULL},
    {"_ZNK5Quirk7Library8GetClassEPKc", 0, 0, {0}, (void *)quirk_class},
    {"_ZN7Camping4Camp24DeductCampingSkillPointsEi", 0, 0, {0}, (void *)deduct_camping_points},
    {"_ZN7Camping4Camp8LogSkillENS_15SkillLogEntryIdE", 0, 0, {0}, (void *)camp_log_skill},
    // expedition_hooks.c (same prototypes as macOS, but ScoutArea and Party::CanGoHungry are inlined: see there)
    {"_ZN13LoadingScreen12ShowContinueEfb",
     0x7e4b70,
     15,
     {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x55, 0x57, 0x41, 0x56},
     NULL},
    {"_ZN4Raid8ApplyDotEP5ActorbRKNSt3__18functionIFbPKN4Buff8InstanceEEEE",
     0x5fe820,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // mov rax, rsp ; mov [rax+0x10], rbx ('/general/status/burn_DOT', calls OnApplyDot)
    {"_ZN4Raid12ApplyBurnDotEP5Actor",
     0x5feaf0,
     9,
     {0x48, 0x85, 0xD2, 0x0F, 0x84, 0x8A, 0x02, 0x00, 0x00},
     NULL,
     .rip_disp = 5,
     .rip_next = 9}, // test rdx, rdx ; je rel32 ('/general/status/burn_DOT', queues the death sequence)
    {"_ZN5Party13OnNewTileStepEv",
     0x5fab80,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi (Hero::StepTaken inlined: +0x13e0)
    {"_ZN5Party11OnAreaVisitEP4Areab", 0x5f9920, 5, {0x48, 0x89, 0x5C, 0x24, 0x18}, NULL}, // mov [rsp+0x18], rbx
    {"win:Map::NagivateMap",
     0x5dab20,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rbx (ScoutArea inlined into its callers: hooked in its place)
    {"win:Map::NagivateMap from Party::OnAreaVisit", 0x5f9bfb, 0, {0}, NULL}, // return addresses
    {"win:Map::NagivateMap from Party::OnRaidStart", 0x5f72ee, 0, {0}, NULL},
    {"win:Party::CanGoHungry result",
     0x766d4c,
     5,
     {0x40, 0x84, 0xED, 0x74, 0x2D},
     NULL,
     .rip_disp = 4,
     .rip_next = 5}, // test bpl, bpl ; je rel8 (HandleTileContents' $_1: both paths join, bpl hungry)
    {"_ZN6RaidUI6Screen11RaidDisplay15OnPostRaidStartEv",
     0x755590,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx
    {"_ZNK6RaidUI6Screen11RaidDisplay18GetCanProcessInputEv",
     0x761b50,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rbx (ends in Input_IsActive 0x28a370)
    {"_ZN9Behaviors33GenerateAfflictionOrVirtueForHeroEP5Actor",
     0x49eef0,
     5,
     {0x48, 0x8B, 0xC4, 0x55, 0x53},
     NULL}, // mov rax, rsp ; push rbp ; push rbx (ApplyRawStressDamage's call)
    {"_ZNK5Trait7Library7GetTypeEPKc", 0, 0, {0}, (void *)trait_type_by_id},
    {"_ZN5Actor16SetPendingVirtueEj", 0, 0, {0}, (void *)set_pending_virtue},
    {"_ZN5Actor20SetPendingAfflictionERKji", 0, 0, {0}, (void *)set_pending_affliction},
    {"_ZNK11RandomTableIjE4RollI11prng_minstdEERKjRT_f", 0, 0, {0}, (void *)random_table_roll},
    {"Rules",
     0x2acb850,
     0,
     {0},
     NULL}, // _Rules: its rng first (the rng Battle::ApplyHit passes); its affliction levels +0x208
    // loot_hooks.c (QueueGenerateElement takes its FixedLengthString<32> by reference; QueueGenerateItem is inlined
    // into its callers as an Item copy and AddItem(loot + 0x28), so loot_item hooks AddItem 0x5d40c0 above)
    {"_ZN4Loot6System20QueueGenerateElementEPKNS_8Generate4TypeE17FixedLengthStringILi32EE",
     0x4408c0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx (EffectApplyOnTrinketInternal's call)
    {"_ZN4Loot6System21ProcessGenerateQueuesEv",
     0x440ab0,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi ('Loot: Processing queued loot codes')
    // prop_hooks.c: PropInteraction::GenerateResult is inlined into PropInteractionTable::Evaluate 0x4a92e0 (its only
    // caller of CreatePropInteractionResult, hooked in its place); ShowTrap and Hero::GetTrapDodgeChance are inlined
    // into Overlay_Event::ShowScreen (hooked in ShowTrap's place; the chance through Actor::GetCurrentResistance)
    {"_Z27CreatePropInteractionResultRK25PropInteractionResultDataP4PropP4Hero",
     0x4a9dd0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rbx (switch on the data's type)
    {"_ZNK26CurioInteractionLootResult5ApplyEv",
     0x4ac5c0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // the Apply functions: vtable slot 2
    {"_ZNK27CurioInteractionQuirkResult5ApplyEv", 0x4ae050, 5, {0x48, 0x89, 0x5C, 0x24, 0x10}, NULL},
    {"_ZNK28CurioInteractionEffectResult5ApplyEv", 0x4ac260, 5, {0x48, 0x89, 0x5C, 0x24, 0x08}, NULL},
    {"_ZNK30CurioInteractionScoutingResult5ApplyEv", 0x4ae5d0, 5, {0x40, 0x55, 0x53, 0x56, 0x57}, NULL},
    {"_ZNK27CurioInteractionPurgeResult5ApplyEv",
     0x4ad750,
     8,
     {0x48, 0x83, 0xEC, 0x68, 0x80, 0x79, 0x18, 0x00},
     NULL}, // sub rsp, 0x68 ; cmp byte [rcx+0x18], 0
    {"_ZNK29CurioInteractionDiseaseResult5ApplyEv", 0x4abcd0, 5, {0x48, 0x89, 0x5C, 0x24, 0x10}, NULL},
    {"_ZNK31CurioInteractionQuestItemResult5ApplyEv", 0x4ad7d0, 5, {0x40, 0x55, 0x53, 0x56, 0x57}, NULL},
    {"_ZNK28CurioInteractionSummonResult5ApplyEv", 0x4ac9e0, 5, {0x40, 0x53, 0x55, 0x56, 0x57}, NULL},
    {"_ZNK32ObstacleDefaultInteractionResult5ApplyEv",
     0x4b6a80,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // mov rax, rsp ; mov [rax+0x10], rbx
    {"_ZNK30ClearObstacleInteractionResult5ApplyEv",
     0x4b7230,
     9,
     {0x40, 0x53, 0x48, 0x81, 0xEC, 0xC0, 0x02, 0x00, 0x00},
     NULL}, // push rbx ; sub rsp, 0x2c0
    {"_ZNK28DefaultDoorInteractionResult5ApplyEv",
     0x4b68f0,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30},
     NULL}, // push rbx ; sub rsp, 0x30
    {"_ZNK27UnlockDoorInteractionResult5ApplyEv", 0x4b7d50, 5, {0x40, 0x55, 0x53, 0x56, 0x57}, NULL},
    {"_ZNK29PrisonerTalkInteractionResult5ApplyEv", 0x4b7730, 5, {0x40, 0x55, 0x53, 0x56, 0x57}, NULL},
    {"_ZNK32PrisonerDisplayInteractionResult5ApplyEv",
     0x4b7530,
     8,
     {0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x49, 0x18},
     NULL}, // sub rsp, 0x38 ; mov rcx, [rcx+0x18]
    {"_ZNK4Raid7GetAreaEv", 0, 0, {0}, (void *)raid_area},
    {"win:Overlay_Event::ShowScreen",
     0x701850,
     5,
     {0x40, 0x55, 0x57, 0x41, 0x57},
     NULL}, // push rbp ; push rdi ; push r15 (ShowTrap inlined: 'PID: %d Name: %s Random int range')
    {"_ZN13Overlay_Event11TriggerTrapERN6RaidUI6Screen11RaidDisplayEib",
     0x7020a0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x20},
     NULL}, // mov [rsp+0x20], rbx ('str_its_a_trap')
    {"_ZN13Overlay_Event19TriggerDisarmedTrapERN6RaidUI6Screen11RaidDisplayEi",
     0x701c40,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20},
     NULL}, // mov rax, rsp ; mov [rax+0x20], rbx ('str_ui_disarm')
    // a client mod debug items
    {"_ZN5Actor16RemoveAfflictionEv", 0, 0, {0}, (void *)actor_remove_affliction},
    {"_ZN5Actor12RemoveVirtueEv", 0, 0, {0}, (void *)actor_remove_virtue},
    // popups.c
    {"_ZN6RaidUI6Screen11RaidDisplay16AddPlusPopupTextEP5ActorNS0_"
     "14PopupTextTypes5eTypeEPKcjjjRK17FixedLengthStringILi64EEb",
     0x764200,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi (AddPopupText 0x764130's call; no colour arguments)
    {"_ZN6RaidUI6Screen11RaidDisplay22ProcessQueuedPopupTextEjf",
     0x76abd0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx ('PopupText: Forcing process of popup text')
    {"_ZN13Overlay_Basic14OnBuffActivateEP5ActorN13TrayIconTypes13eTrayIconTypeEj",
     0x6f0510,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi ('str_ui_entering_%s')
    {"_ZZN13Overlay_Basic10ShowScreenERN6RaidUI6Screen11RaidDisplayEENK3$_5clEP5Actor4vec2b",
     0x6edc50,
     0,
     {0},
     NULL}, // one actor's tray: OnBuffActivate twice, ShowBuffIconTooltip 0x6f08b0
    {"win:Overlay_Basic::GetBuffTrayBuffInstanceList", 0x6f3ae0, 0, {0}, NULL},    // its lambda's vtable 0xeb7d90
    {"win:Overlay_Basic::GetTrayRiposteBuffInstanceList", 0x6f3b40, 0, {0}, NULL}, // its lambda's vtable 0xeb7f88
    {"_ZNK13Overlay_Basic27GetBuffTrayBuffInstanceListEPK5Actorb", 0, 0, {0}, (void *)tray_buff_list},
    {"_ZN13Overlay_Basic30GetTrayRiposteBuffInstanceListEP5Actor", 0, 0, {0}, (void *)tray_riposte_list},
    {"_ZN16ActorDisplayData15GetTrayIconDataEN13TrayIconTypes13eTrayIconTypeEj",
     0x48b6a0,
     0,
     {0},
     NULL}, // (display data +0x148, 0x18 per type, 0x20 per icon, as macOS)
    {"_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE",
     0x117ddf0,
     0,
     {0},
     NULL}, // (OnBuffActivate passes it to AddPopupText)
    {"win:RaidUI::Screen::k_PopupTextTypeData", 0x2bd68d0, 0, {0}, NULL},
    {"_ZN6RaidUI6Screen13GetIdFromTypeENS0_14PopupTextTypes5eTypeE", 0, 0, {0}, (void *)popup_type_entry},
    {"_ZN6RaidUI6Screen11RaidDisplay22AddPlusPopupStringTextEP5ActorNS0_14PopupTextTypes5eTypeEPKcb",
     0,
     0,
     {0},
     (void *)add_plus_popup_string},
    {"_ZNK5Actor9GetSpriteEv", 0, 0, {0}, (void *)actor_get_sprite},
    // buff_origin.c: the sheet's stat tooltips
    {"_ZN11HeroDisplay3App13ShowBaseStatsERK4Hero4vec2jbbPK5Actor12LayoutAnchorb",
     0x7cc620,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx ('%+.0f', '%.1f%%')
    {"_ZN11HeroDisplay3App28StatSetBaseTooltipStatAmountER20GuiTooltipParametersPKcfS4_j",
     0x7d1b50,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi (" %.<n>f%s %s", never skipped)
    {"_ZN11HeroDisplay3App34StatAddAdditionalTooltipStatAmountER20GuiTooltipParametersPKcfS4_j",
     0x7d1d10,
     5,
     {0x4C, 0x8B, 0xDC, 0x55, 0x56},
     NULL}, // mov r11, rsp ; push rbp ; push rsi ("%+.<n>f%s %s", skipped at |v| <= 1e-6)
    {"_ZN11HeroDisplay3App33StatAddAdditionalTooltipBuffLinesER20GuiTooltipParametersRKNSt3__"
     "16vectorIPKN4Buff8InstanceENS3_9allocatorIS8_EEEEbjfPKcj",
     0x7d1ef0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx (0x22 sources, its total inlined, "buff_%s")
    {"_ZNSt3__110__function6__funcIZN11HeroDisplay3App13ShowBaseStatsERK4Hero4vec2jbbPK5Actor12LayoutAnchorbE3$_5NS_"
     "9allocatorISC_EEFvvEEclEv",
     0x7d3710,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40},
     NULL}, // push rbx ; sub rsp, 0x40 (PROT: 'str_stat_protection', the tooltip at closure +8)
    // crit_info.c: the CRIT tooltip ($_3: 'str_stat_crit'; the tooltip at closure +8, the actor +0x18 as macOS)
    {"_ZNSt3__110__function6__funcIZN11HeroDisplay3App13ShowBaseStatsERK4Hero4vec2jbbPK5Actor12LayoutAnchorbE3$_3NS_"
     "9allocatorISC_EEFvvEEclEv",
     0x7d38e0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx
    {"_ZN4Buff7Library10s_InstanceE", 0x117d960, 0, {0}, NULL},
    {"win:Buff::Library alt flag", 0x2a5c08b, 0, {0}, NULL},
    {"_ZN4Buff7Library7GetBuffEj", 0, 0, {0}, (void *)buff_library_get},
    {"_ZNK4Raid17GetNumberOfActorsEv", 0, 0, {0}, (void *)raid_actor_count},
    {"_ZNK4Raid17GetActorFromIndexEj", 0, 0, {0}, (void *)raid_actor_at},
    {"_ZNK8RaidStat15GetBuffAddValueEj", 0, 0, {0}, (void *)raid_stat_buff_add},
    // buff_origin.c: resistance tooltips. Buff::GetTotalFromFiltredInstanceList is inlined in SingleResistance, so
    // Windows hooks the filter: its lambda_4's _Do_call (vtable 0xed62c8 slot 2; stat +0 == 0xb, resistance type
    // +0x44 == closure +0xc, source +0x54 == closure +8)
    {"_ZN17ResistanceDisplay16SingleResistanceEPK5ActorN15ResistanceTypes15eResistanceTypeE4vec2",
     0x80d610,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20},
     NULL}, // mov rax, rsp ; mov [rax+0x20], rbx (' %.0f%s %s', 'resistance_base'; GUI_Tooltip at +0xb3d)
    {"win:ResistanceDisplay::SingleResistance lambda_4",
     0x80e8b0,
     7,
     {0x4C, 0x8B, 0x02, 0x41, 0x83, 0x38, 0x0B},
     NULL}, // mov r8, [rdx] ; cmp dword [r8], 0xb
    {"_ZNK5Actor14GetActiveBuffsEPKS_S1_PK16ActorCombatSkill",
     0x4750e0,
     0,
     {0},
     NULL}, // (SingleResistance's call at 0x80d838; the group +0x460, 0x1d8 per entry)
    {"_ZNK5Actor14GetActiveBuffsEv", 0, 0, {0}, (void *)actor_active_buffs},
    {"win:operator delete", 0xb93d30, 0, {0}, NULL}, // jmp -> free 0xb949a2
    {"win:std::_Deallocate", 0, 0, {0}, (void *)msvc_deallocate},
    {"win:Buff::k_SourceData", 0x2aa62a0, 0, {0}, NULL},
    {"_ZN4Buff11GetSourceIdENS_6SourceE", 0, 0, {0}, (void *)buff_source_id},
    {"win:Buff::Stat::k_Data", 0x2aa7020, 0, {0}, NULL},
    {"_ZN4Buff4Stat13GetTypeFromIdEPKc", 0, 0, {0}, (void *)buff_stat_type},
    {"_ZN4Buff4Stat19GetIsStatusFromTypeENS0_5Types5eTypeE", 0, 0, {0}, (void *)buff_stat_is_status},
    {"win:ResistanceType::k_Data", 0x2ad3810, 0, {0}, NULL},
    {"_ZN14ResistanceType13GetIdFromTypeEN15ResistanceTypes15eResistanceTypeE", 0, 0, {0}, (void *)resistance_id},
    {"_ZNK16ActorCombatSkill18GetNumberOfEffectsEj", 0, 0, {0}, (void *)skill_effect_count},
    {"_ZNK16ActorCombatSkill16GetEffectAtIndexEjj", 0, 0, {0}, (void *)skill_effect_at},
    // buff_origin.c: combat tray tooltips. AddTrayBuffDescriptions (0x6f3db0) is inlined in most of
    // ShowBuffIconTooltip's cases (the Buffs / Debuffs icons among them), so Windows hooks what every copy calls:
    // Buff::Display::ConsolidateFilteredInstanceList (MSVC (result, list)) and the descriptions it writes per instance
    {"_ZN13Overlay_Basic19ShowBuffIconTooltipEP5ActorN13TrayIconTypes13eTrayIconTypeEy4vec2S4_"
     "12LayoutAnchorRN6RaidUI6Screen11RaidDisplayEj",
     0x6f08b0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx ('tray_icon_tooltip_bleed_format'; GUI_HotSpot at +0x25f4)
    {"_ZN4Buff7Display31ConsolidateFilteredInstanceListERKNSt3__16vectorIPKNS_8InstanceENS1_9allocatorIS5_EEEE",
     0x7b9a70,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx
    {"_ZN4Buff7Display26GetDescriptionWithDurationEPKNS_8InstanceEPK5Actorb",
     0x7ba490,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx (MSVC (out, instance, actor, b), returns the text)
    {"_ZN4Buff7Display14GetDescriptionEPKNS_8InstanceEPK5Actor",
     0x7b9f70,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi ('buff_rule_data_tooltip_%s'; returns the text, not always out)
    // client_example: Buff::Display::GetHasDescription (the same +0x188 / +0x151 checks and 0x59-case stat switch as macOS)
    {"_ZN4Buff7Display17GetHasDescriptionEPKNS_8InstanceE",
     0x7b9e50,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20},
     NULL}, // push rbx ; sub rsp, 0x20
    // HeroDisplay::App::EquipmentAdd{Weapon,Armour}StatsLines are one function on Windows (text, row, hvec4 colour):
    // the row's type +0x114 picks weapon (0: damage, crit, speed) or armour (1: defense, health points)
    {"_ZN11HeroDisplay3App28EquipmentAddWeaponStatsLinesERN20GuiTooltipParameters4TextEPK6Weapon5hvec4",
     0x7ca4b0,
     5,
     {0x40, 0x53, 0x55, 0x41, 0x56},
     NULL}, // push rbx ; push rbp ; push r14 ('str_stat_base_damage')
    {"_ZN11HeroDisplay3App28EquipmentAddArmourStatsLinesERN20GuiTooltipParameters4TextEPK6Armour5hvec4",
     0x7ca4b0,
     5,
     {0x40, 0x53, 0x55, 0x41, 0x56},
     NULL}, // (the same function: 'str_stat_base_defense')
    // riposte.c. Actor::GetRiposteSkill (GetClass()->GetRiposteSkill(): the class's vtable +0x18 count, +0x20 skill(0))
    // is inlined in its three callers: a read-write site after each copy swaps the skill register.
    {"_ZN6Battle14RiposteAttemptEPNS_6AttackE",
     0x5b0c20,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx ('Riposte: guid %u countered guid %u')
    {"win:RiposteAttempt riposte skill",
     0x5b0d19,
     6,
     {0x48, 0x8B, 0x16, 0x48, 0x8B, 0xCE},
     NULL}, // mov rdx, [rsi] ; mov rcx, rsi (hero rsi, skill r15)
    {"win:CalculateDerivedStatsInternal riposte skill",
     0x5c3899,
     5,
     {0x4C, 0x8B, 0x74, 0x24, 0x28},
     NULL}, // mov r14, [rsp+0x28] (Hero 0x5c37f0; hero rbx, skill rsi)
    {"win:UpdateStanceAnimation riposte skill",
     0x5c34e2,
     7,
     {0x0F, 0x10, 0x87, 0x9C, 0x00, 0x00, 0x00},
     NULL}, // movups xmm0, [rdi+0x9c] (Hero 0x5c32b0; hero rbx, skill rdi)
    {"_ZN6Battle20PostSkillSideEffectsEPK16ActorCombatSkill",
     0x5a6400,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18},
     NULL}, // mov rax, rsp ; mov [rax+0x18], rbx ('Post skill side effects %s - ...')
    {"_ZN6Battle32CreatePerformerSkillMoveTimelineEP5ActorPK16ActorCombatSkill",
     0x5b76e0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rbx (Actor_DoSkillFriendly 0x5b5126; .move +0x35c / +0x360)
    {"_ZN5Actor37IncrementBattleLimitedCombatSkillUsesEj",
     0x47b040,
     11,
     {0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05, 0x75, 0xBE, 0xC0, 0x00},
     NULL,
     7,
     11}, // sub rsp, 0x38 ; mov rax, [rip+...] (PostSkillSideEffects 0x5a6c48, after skill+0x98)
    {"_ZN6RaidUI6Screen11RaidDisplay13QueueTimelineEP8Timelineb",
     0x7654c0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x18},
     NULL}, // mov [rsp+0x18], rbx ('timeline'; the deque's emplace inlined)
    {"_ZN19prng_minstd_wrapped14random_uniformEv",
     0x28e0c0,
     7,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10},
     NULL}, // mov rax, rsp ; mov [rax+0x10], rbx (vtable slot 3)
    {"_ZNK16ActorCombatSkill15GetIsTargetableEPK5ActorS2_",
     0x484430,
     5,
     {0x40, 0x53, 0x57, 0x41, 0x57},
     NULL}, // push rbx ; push rdi ; push r15
    {"_ZNK5Actor27HasAttacksUsableAnyPositionEv",
     0x477d80,
     7,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x18},
     NULL}, // mov r11, rsp ; mov [r11+0x18], rbx ('ATTACKS_ANY_LAUNCH_RANK')
    {"_ZNK6Battle16SkillValidResultcvbEv", 0, 0, {0}, (void *)skill_valid_bool},
    {"_ZNK16ActorCombatSkill16GetIsMultiTargetEv", 0, 0, {0}, (void *)skill_multi_target},
    {"_ZNK16ActorCombatSkill18GetIsMoveTypeSkillEv", 0, 0, {0}, (void *)skill_move_type},
    {"_ZNK16ActorCombatSkill16GetCanBeRipostedEv", 0, 0, {0}, (void *)skill_can_be_riposted},
    {"_ZNK5Actor16IsRiposteEnabledEv", 0, 0, {0}, (void *)actor_riposte_enabled},
    {"_ZN5Actor17SetRiposteEnabledEb", 0, 0, {0}, (void *)actor_set_riposte_enabled},
    {"_ZNK5Actor12GetIsStunnedEv", 0, 0, {0}, (void *)actor_stunned},
    {"_ZNK16ActorCombatSkill21GetIsSkillTargetValidEPK5ActorS2_", 0, 0, {0}, (void *)skill_target_valid},
    // game_actions.c
    {"_Z19GetDamageTypeFromIdPKc", 0, 0, {0}, (void *)damage_type_from_id},
    {"_Z25GetDamageSourceTypeFromIdPKc", 0, 0, {0}, (void *)damage_source_from_id},
    {"_ZN6Stress6Source13GetTypeFromIdEPKc", 0, 0, {0}, (void *)stress_source_from_id},
    {"_ZN4Heal6Source13GetTypeFromIdEPKc",
     0x4b5870,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL}, // mov [rsp+0x10], rbx (hashes the id, walks Heal's k_TypeData 0x2ab6e80)
    {"_ZN5Actor17ApplyHealthDamageER11prng_minstdfRK11eDamageTypejRK17eDamageSourceTypebPS_",
     0,
     0,
     {0},
     (void *)apply_health_damage},
    {"win:Actor::AttemptHpReaction",
     0x47b8d0,
     5,
     {0x48, 0x8B, 0xC4, 0x53, 0x55},
     NULL}, // mov rax, rsp ; push rbx ; push rbp (this+0x768, flag 1, as macOS)
    {"_ZN4Raid15QueueSideEffectEP5ActorS1_8c_string",
     0x5ffa10,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi (FindEffect, 'No effect named: %s')
    {"_ZN4Raid22ProcessSideEffectQueueEv",
     0x5ffc30,
     5,
     {0x48, 0x8B, 0xC4, 0x53, 0x55},
     NULL}, // mov rax, rsp ; push rbx ; push rbp ('Raid: Failed to apply effect that is by')
    {"_ZN17FixedLengthStringILi64EEC1EPKc", 0, 0, {0}, (void *)fls64_init},
    {"_ZN17FixedLengthStringILi128EEC1EPKc", 0, 0, {0}, (void *)fls128_init},
    {"_ZN9Inventory4ItemC1ERK17FixedLengthStringILi64EES4_iiibRKNSt3__16vectorIjNS5_9allocatorIjEEEENS_10TrinketLogE",
     0x5d1af0,
     5,
     {0x48, 0x89, 0x4C, 0x24, 0x08},
     NULL}, // mov [rsp+8], rcx (Hero::SwapTrinket's call 0x5c8f83; copies the 0x488-byte TrinketLog)
    {"_ZN9Inventory6System7AddItemERNS_4ItemEb", 0, 0, {0}, (void *)inventory_add_item},
    {"win:Loot::Generate::Types::GetTypeById",
     0x442230,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi (Loot::System::Restore's call; k_AllTypes, 6)
    {"_ZN4Loot8Generate5Types11GetTypeByIdEPKc", 0, 0, {0}, (void *)loot_type_by_id},
    // game_announce (InvestigateCurio's purge lambda 0x705570 calls all three, as macOS; so does the quirk
    // announcement 0x76a1a0)
    {"_ZN6RaidUI6Screen11RaidDisplay16ShowAnnouncementE17FixedLengthStringILi128EENS1_"
     "17eAnnouncementTypeEP8TimelinejNS1_23eAnnouncementRenderTimeEbb",
     0x7670e0,
     5,
     {0x40, 0x53, 0x55, 0x56, 0x57},
     NULL}, // push rbx ; push rbp ; push rsi ; push rdi
    {"_ZN6RaidUI6Screen11RaidDisplay16HideAnnouncementEP8Timeline",
     0x7673c0,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x10},
     NULL},                                                                       // mov [rsp+0x10], rbx
    {"_Z16instant_Timelinev", 0x295bd0, 5, {0x48, 0x89, 0x5C, 0x24, 0x10}, NULL}, // mov [rsp+0x10], rbx
    {"_ZN6RaidUI6Screen11RaidDisplay1TE",
     0x109a6e0,
     0,
     {0},
     NULL}, // the purge's wait reads +0x518 (0x109abf8), the quirk announcement +0x4fc (new_quirk)
    // localization.c (same prototypes as macOS: mingw returns the 16-byte LocalizedString through a hidden pointer, as
    // MSVC does). LocalizeStringFromId(char const*) calls the by-hash one; the variation lookup by hash is inlined
    // into the one by id (it has no callers on macOS either)
    {"_ZN12Localization20LocalizeStringFromIdEj",
     0x43c700,
     6,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40},
     NULL}, // push rbx ; sub rsp, 0x40
    {"_ZN12Localization38LocalizeStringFromIdWithVariationIndexEPKcj",
     0x43c900,
     5,
     {0x48, 0x89, 0x5C, 0x24, 0x20},
     NULL}, // mov [rsp+0x20], rbx (GoMissingLogItem::GetStory's call 0x54f53b)
    {"win:LocalizationLanguage::FindString",
     0x43d660,
     5,
     {0x40, 0x55, 0x53, 0x56, 0x57},
     NULL}, // push rbp ; push rbx ; push rsi ; push rdi
    {"_ZN12Localization18GetCurrentLanguageEv", 0, 0, {0}, (void *)loc_language},
    {"_ZN12Localization24DoesLocalizedStringExistEj", 0, 0, {0}, (void *)loc_exists_hash},
    {"_ZN12Localization24DoesLocalizedStringExistEPKc", 0, 0, {0}, (void *)loc_exists_id},
    // game_fixes.c: SetPresentationOverride (inlined), and Texture_Bind3D, inlined three times in
    // Panel_DungeonView::RunPostEffects 0x724960 (units 1-3, the grades' handles at rbp+7, rbp-9, rbp-0x19): the sites
    // right after each `ResourceHandle<Texture> resolve` call, whose result is the Texture the bind reads +0x30 of
    {"_ZN6RaidUI6Screen11RaidDisplay23SetPresentationOverrideENS1_21ePresentationOverrideEj",
     0,
     0,
     {0},
     (void *)raid_set_presentation},
    {"win:RunPostEffects bind 1",
     0x725118,
     6,
     {0x48, 0x8B, 0xD8, 0x8B, 0x4D, 0x07},
     NULL}, // mov rbx, rax ; mov ecx, [rbp+7]
    {"win:RunPostEffects bind 2",
     0x725154,
     6,
     {0x48, 0x8B, 0xD8, 0x8B, 0x4D, 0xF7},
     NULL}, // mov rbx, rax ; mov ecx, [rbp-9]
    {"win:RunPostEffects bind 3",
     0x725190,
     6,
     {0x48, 0x8B, 0xD8, 0x8B, 0x4D, 0xE7},
     NULL}, // mov rbx, rax ; mov ecx, [rbp-0x19]
    // campaigndata.c: Options::System::SaveProfileOptions / RestoreProfileOptions are inlined into ProfileGameFile::Save
    // 0x5619f0 and ProfileGameFile::RestoreOptions 0x562050 (persist.game.json, "profile_options", "values"): the sites
    // right after each closes "values" (SaveData in rdi; RestoreData in r14)
    {"win:ProfileGameFile::Save profile options",
     0x561c88,
     6,
     {0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF},
     NULL}, // mov rax, [rdi] ; mov rcx, rdi
    {"win:ProfileGameFile::RestoreOptions profile options",
     0x5621a1,
     6,
     {0x33, 0xFF, 0x0F, 0x1F, 0x40, 0x00},
     NULL}, // xor edi, edi ; nop dword [rax]
    {"_ZN7Options6System19ResetProfileOptionsEv",
     0x3f7a10,
     6,
     {0x40, 0x55, 0x56, 0x57, 0x41, 0x56},
     NULL}, // push rbp ; push rsi ; push rdi ; push r14 (callers: UpdateStateInternal, RestoreOptions)
};
const int g_win_nsyms = (int)(sizeof g_win_syms / sizeof g_win_syms[0]);
#endif
