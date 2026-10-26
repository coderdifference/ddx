// Shared game hooks (common.h, "game_hooks.c"): the library owns the hook on every game function a client may change,
// and clients wrap it. A function's hook jumps through a stub to a pointer slot: the slot holds the outermost wrapper,
// and each wrapper's *next the one inside it (the innermost's: the trampoline to the game's code). So wrappers chain
// whatever the function's signature, and one function serves any number of clients.
// A mid-function site's stub saves the registers and calls the library's dispatcher, which calls each observer.
// Mods follow the campaign (mods.c): each wrapper and observer belongs to the mod registering it (mods_current; -1 the
// library), and at each campaign load and close the chain is relinked from the active ones: the library's innermost,
// then the campaign's mods from the lowest in its mod list to the highest, so the top mod's wrapper is outermost (it
// has the last word: a higher mod wins); each owner's in registration order, the later outside. Observers run in that
// order too. A mod not applied to the campaign is left out (outside a campaign: only the library's).
// The catalogue is game knowledge: each entry's symbol and the bytes its hook replaces, checked before patching (the
// game's other versions turn the hook off). Add an entry here for any new function a client needs.
#include "common.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#define MAX_SITE_FNS 8
#define MAX_WRAPS    8

typedef struct {
    const char *id;         // what clients wrap it by (also its log name)
    const char *symbol;     // mangled, as sym() takes it
    uint32_t offset;        // a site: its offset in the function
    bool site;              // a mid-function site (game_hook_site), not a function (game_hook_wrap)
    uint8_t len, bytes[24]; // the replaced bytes, as they must be
    HookRip rip[2];         // relative operands in them (install_hook_rip)
    uint8_t nrip;
} GameHookDef;

// .bytes and .len from one brace list: B({0x55, ...}) or B(PROLOGUE_SUB32(a, b))
#define B(...) .bytes = __VA_ARGS__, .len = sizeof((uint8_t[])__VA_ARGS__)

static const GameHookDef g_defs[] = {
    // actors and heroes
    {"Actor::UpdateTrinketBuffs", "_ZN5Actor18UpdateTrinketBuffsEv", B(PROLOGUE_SUB32(0x88, 0x01))},
    {"Actor::GetCurrentResistance", "_ZNK5Actor20GetCurrentResistanceEN15ResistanceTypes15eResistanceTypeE",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x10, 0x89, 0xF3, 0x49, 0x89, 0xFE})},
    // the resolve level's part of a resistance; every resistance read adds it (GetCurrentResistance, the deathblow roll
    // inlined in macOS's ApplyHealthDamageInternal, the deathblow chance shown), through the vtable
    {"Actor::GetAdditionalResistance", "_ZNK5Actor23GetAdditionalResistanceEN15ResistanceTypes15eResistanceTypeE",
     B({0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x89, 0xF3, 0x48, 0x8B, 0x07, 0xFF, 0x90, 0x70, 0x01, 0x00, 0x00})},
    {"Hero::UpdateWeaponAndArmourRanks", "_ZN4Hero26UpdateWeaponAndArmourRanksEv", B(PROLOGUE_SUB32(0x98, 0x00))},
    {"Hero::~Hero", "_ZN4HeroD2Ev", B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50,
                                       0x48, 0x89, 0xFB, 0x48, 0x8D, 0x05, 0x7C, 0x5E, 0xA7, 0x00}),
     .rip = {{16, 20}}, .nrip = 1},
    {"Hero::AddResolveXpAndLevelEquipmentAndSkills", "_ZN4Hero38AddResolveXpAndLevelEquipmentAndSkillsEj",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x10, 0x41, 0x89, 0xF6})},
    {"Hero::OnDeathFinishInternal", "_ZN4Hero21OnDeathFinishInternalEv",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x48})},
    {"Party::SetSelectedHero", "_ZN5Party15SetSelectedHeroEP4Hero",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xF3, 0x49, 0x89, 0xFE, 0x48, 0x85, 0xF6})},
    {"Party::SetSelectedHeroByPartyPosition", "_ZN5Party30SetSelectedHeroByPartyPositionEi",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x8B, 0x07, 0x48, 0x8B, 0x4F, 0x08})},
    // items, loot and quests
    {"InventoryItemSlotInterface::OnDrop", "_ZN26InventoryItemSlotInterface6OnDropERK4vec2P8DragData",
     B(PROLOGUE_SUB32(0xB8, 0x05))},
    {"InventoryItemSlotInterface::OnDropAccepted", "_ZN26InventoryItemSlotInterface14OnDropAcceptedEPK8DragData",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
        0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0xB8, 0x05, 0x00, 0x00})},
    {"Building::Store::TrinketSupply::GenerateItems", "_ZN8Building5Store13TrinketSupply13GenerateItemsEv",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
        0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x68, 0x07, 0x00, 0x00})},
    {"Quest::System::GenerateQuestRewards",
     "_ZNK5Quest6System20GenerateQuestRewardsERNSt3__16vectorINS_14GeneratedQuestENS1_9allocatorIS3_EEEERKNS_"
     "15GenerationClassE",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50})},
    // town: roster, recruits, stash, estate bar
    {"TownUI::Panel::RosterElement::OnPickedUp",
     "_ZN6TownUI5Panel13RosterElement10OnPickedUpERK4vec2P10DragObjectP8DragData",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x70})},
    {"TownUI::Panel::RosterList::OnDrop", "_ZN6TownUI5Panel10RosterList6OnDropERK4vec2P8DragData",
     B(PROLOGUE_SUB32(0xF8, 0x00))},
    {"TownUI::Panel::RosterList::DismissHero", "_ZN6TownUI5Panel10RosterList11DismissHeroEj",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x18})},
    {"TownUI::Panel::BaseHeroRecruitDisplay::AddHeroSlot",
     "_ZN6TownUI5Panel22BaseHeroRecruitDisplay11AddHeroSlotEP4Herob", B(PROLOGUE_SUB32(0x18, 0x01))},
    {"RecruitHeroSlotInterface::OnPickedUp", "_ZN24RecruitHeroSlotInterface10OnPickedUpERK4vec2P10DragObjectP8DragData",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x48, 0x49, 0x89, 0xCE})},
    // the Stagecoach's recruit tooltip, before its line is added (lea rdi,[rbp-0x1000]; lea rsi,[rbp-0x1168]): the
    // params in rbx, the hovered HeroSlot* at [rbp-0x1120] (Windows: the params at rbp+0x1450, the HeroSlot* in r13)
    {"BaseHeroRecruitDisplay recruit tooltip", "_ZN6TownUI5Panel22BaseHeroRecruitDisplay23PreChildrenShowInternalEv",
     .offset = 0x1ee3, .site = true,
     B({0x48, 0x8D, 0xBD, 0x00, 0xF0, 0xFF, 0xFF, 0x48, 0x8D, 0xB5, 0x98, 0xEE, 0xFF, 0xFF})},
    {"UI::Display::NameFrame",
     "_ZN2UI7Display9NameFrameE4vec214ResourceHandleI7TextureEPKcRfS6_S4_NSt3__18functionIFvvEEEP11SpineSpriteSD_PNS_"
     "5Panel12InputPreviewEyPKNSF_17ElementParamatersE",
     B(PROLOGUE_SUB32(0xE8, 0x00))},
    // expedition screen: drawn every frame of an expedition (an expedition log panel draws in it)
    {"RaidDisplay::PrePanelShowInternal", "_ZN6RaidUI6Screen11RaidDisplay20PrePanelShowInternalEv",
     B(PROLOGUE_SUB32(0x98, 0x0D))},
    {"EstateSummary::PreChildrenShowInternal", "_ZN6TownUI5Panel13EstateSummary23PreChildrenShowInternalEv",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
        0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0xF8, 0x0B, 0x00, 0x00})},
    // after GetCurrencyClassFromIndex in the summary's currency loop: mov r12,rax; mov eax,[rax+0x40];
    // mov [rbp-0x40],eax; movups xmm0,[r12] (rax: the entry's currency class)
    {"EstateSummary currency entry", "_ZN6TownUI5Panel13EstateSummary23PreChildrenShowInternalEv", .offset = 0x31b,
     .site = true, B({0x49, 0x89, 0xC4, 0x8B, 0x40, 0x40, 0x89, 0x45, 0xC0, 0x41, 0x0F, 0x10, 0x04, 0x24})},
    // town: the Blacksmith
    {"BlacksmithDisplay::OnActionEnterStartInternal",
     "_ZN6TownUI5Panel17BlacksmithDisplay26OnActionEnterStartInternalEv",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00})},
    {"BlacksmithDisplay upgrade trees", // OnActionEnterStartInternal's $_1, its per-equipment-type lambda
     "_ZZZN6TownUI5Panel17BlacksmithDisplay26OnActionEnterStartInternalEvENK3$_1clEjENKUl14eEquipmentTypeE_clES3_",
     B(PROLOGUE_SUB32(0xC8, 0x02))},
    {"BlacksmithDisplay unselect hero", // OnActionEnterStartInternal's $_0 (std::function<void(unsigned)>)
     "_ZNSt3__110__function6__funcIZN6TownUI5Panel17BlacksmithDisplay26OnActionEnterStartInternalEvE3$_0NS_"
     "9allocatorIS5_EEFvjEEclEOj",
     B(PROLOGUE_SUB8(0x48))},
    {"BlacksmithDisplay::OnActionShowBodyInternal", "_ZN6TownUI5Panel17BlacksmithDisplay24OnActionShowBodyInternalEv",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x81, 0xEC, 0x60, 0x03, 0x00, 0x00})},
    {"BlacksmithDisplay::GetUpgradeDescriptionList",
     "_ZN6TownUI5Panel17BlacksmithDisplay25GetUpgradeDescriptionListEN7Upgrade21RequirementIdentifierE",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x81, 0xEC, 0x28, 0x01, 0x00, 0x00})},
    {"BuildingDisplay::GetChildPanelOffset", "_ZNK6TownUI5Panel15BuildingDisplay19GetChildPanelOffsetEj",
     B(PROLOGUE_SUB8(0x28))},
    {"HeroActionDisplay::PreChildrenShowBodyInternal",
     "_ZN6TownUI5Panel17HeroActionDisplay27PreChildrenShowBodyInternalEv", B(PROLOGUE_SUB32(0x18, 0x04))},
    // the character sheet and the dungeon panel
    {"HeroDisplay::App::ShowBaseStats CRIT tooltip", // ShowBaseStats's $_3 (std::function<void()>)
     "_ZNSt3__110__function6__funcIZN11HeroDisplay3App13ShowBaseStatsERK4Hero4vec2jbbPK5Actor12LayoutAnchorbE3$_3NS_"
     "9allocatorISC_EEFvvEEclEv",
     B({0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x40, 0x02, 0x00, 0x00})},
    {"DDTabPanel::RenderPartyReorderButton", "_ZN6RaidUI6Screen10DDTabPanel24RenderPartyReorderButtonEv",
     B(PROLOGUE_SUB32(0xF8, 0x09))},
};
#define NDEFS (sizeof g_defs / sizeof *g_defs)

typedef void (*SiteFn)(const Regs *);
typedef struct {
    bool tried, on;
    void *slot;  // functions: the outermost wrapper (the stub jumps through it)
    void *tramp; // functions: the game's code (the innermost's next)
    struct {
        void *fn;
        void **next;
        int owner;
    } wraps[MAX_WRAPS]; // every wrapper, in registration order
    int nwraps;
    SiteFn fns[MAX_SITE_FNS]; // sites: every observer, in registration order
    int owners[MAX_SITE_FNS], nfns;
    SiteFn run[MAX_SITE_FNS]; // sites: the observers called, in order
    int nrun;
} GameHookState;
static GameHookState g_state[NDEFS];

// The game calls these from its threads at any time; campaigns load on the main thread. Unhook the chain first (the
// slot straight to the game) so no call meets a half-relinked chain, then each wrapper's next from the inside out,
// then the slot. A call already inside a wrapper goes on through the old or new links, both ending at the game.
static void relink(GameHookState *st) {
    int owners[MAX_WRAPS], order[MAX_WRAPS];
    for (int i = 0; i < st->nwraps; i++)
        owners[i] = st->wraps[i].owner;
    int n = mods_order(owners, st->nwraps, false, order); // the library's innermost, the top mod's outermost
    void *inner = st->tramp;
    __atomic_store_n(&st->slot, st->tramp, __ATOMIC_RELEASE);
    for (int k = 0; k < n; k++) {
        __atomic_store_n(st->wraps[order[k]].next, inner, __ATOMIC_RELEASE);
        inner = st->wraps[order[k]].fn;
    }
    __atomic_store_n(&st->slot, inner, __ATOMIC_RELEASE);
}

static void reorder_site(GameHookState *st) {
    int order[MAX_SITE_FNS], n = mods_order(st->owners, st->nfns, false, order);
    __atomic_store_n(&st->nrun, 0, __ATOMIC_RELEASE);
    for (int k = 0; k < n; k++)
        st->run[k] = st->fns[order[k]];
    __atomic_store_n(&st->nrun, n, __ATOMIC_RELEASE);
}

static void on_campaign(void) {
    for (size_t i = 0; i < NDEFS; i++) {
        GameHookState *st = &g_state[i];
        if (!st->on)
            continue;
        if (g_defs[i].site)
            reorder_site(st);
        else
            relink(st);
    }
}

static void watch_campaigns(void) {
    static bool watching;
    if (!watching) {
        watching = true;
        mods_on_campaign(on_campaign);
    }
}

static const GameHookDef *find(const char *id, int *index) {
    for (size_t i = 0; i < NDEFS; i++)
        if (!strcmp(g_defs[i].id, id)) {
            *index = (int)i;
            return &g_defs[i];
        }
    return NULL;
}

// Executable memory for the stubs, a page at a time (written while not executable, macOS).
static uint8_t *stub_alloc(size_t n, uint8_t **page_out) {
    static uint8_t *page;
    static size_t used;
    if (!page || used + n > 4096) {
#ifdef _WIN32
        page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!page)
            return NULL;
#else
        void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) {
            page = NULL;
            return NULL;
        }
        page = p;
#endif
        used = 0;
    }
#ifndef _WIN32
    if (mprotect(page, 4096, PROT_READ | PROT_WRITE) != 0)
        return NULL;
#endif
    *page_out = page;
    uint8_t *s = page + used;
    used += (n + 15) & ~(size_t)15;
    return s;
}

static bool stub_seal(uint8_t *page) {
#ifdef _WIN32
    (void)page;
    return true;
#else
    return mprotect(page, 4096, PROT_READ | PROT_EXEC) == 0;
#endif
}

// mov r11, &slot; jmp [r11] (r11: scratch at a call in both ABIs, never an argument)
static void *function_stub(void **slot) {
    uint8_t *page, *s = stub_alloc(13, &page);
    if (!s)
        return NULL;
    s[0] = 0x49;
    s[1] = 0xBB;
    uint64_t a = (uint64_t)slot;
    memcpy(s + 2, &a, 8);
    s[10] = 0x41;
    s[11] = 0xFF;
    s[12] = 0x23;
    return stub_seal(page) ? s : NULL;
}

bool game_hook_wrap(const char *id, void *fn, void **next) {
    int i;
    const GameHookDef *d = id ? find(id, &i) : NULL;
    if (next)
        *next = NULL;
    if (!d || d->site || !fn || !next) {
        logf_("game hook %s: not a catalogued function, not wrapped", id ? id : "?");
        return false;
    }
    GameHookState *st = &g_state[i];
    if (st->nwraps == MAX_WRAPS) {
        logf_("%s: more than %d wrappers, not wrapped", d->id, MAX_WRAPS);
        return false;
    }
    if (!st->tried) { // the first wrapper: hook the function, through the stub, onto the slot
        st->tried = true;
        void *stub = function_stub(&st->slot);
        void *tramp = stub ? install_hook_rip(d->id, sym(d->symbol), stub, d->bytes, d->len, d->rip, d->nrip) : NULL;
        if (!stub)
            logf_("%s: no stub memory, hook off", d->id);
        if (tramp) {
            st->slot = st->tramp = tramp;
            st->on = true;
        }
    }
    if (!st->on)
        return false;
    watch_campaigns();
    *next = st->tramp; // until linked: an unlinked wrapper's next is never left NULL
    st->wraps[st->nwraps++] = (typeof(st->wraps[0])){fn, next, mods_current()};
    relink(st);
    return true;
}

static float *g_site_xmm; // Windows: the running site's saved xmm0-15 (NULL outside one)
static Regs *g_site_regs; // the running site's saved general registers (NULL outside one)

float *game_site_xmm(int i) {
    return g_site_xmm && i >= 0 && i < 16 ? g_site_xmm + 4 * i : NULL;
}

Regs *game_site_regs(void) {
    return g_site_regs;
}

static void site_dispatch(Regs *r, GameHookState *st, float *xmm) {
    float *was = g_site_xmm;
    Regs *was_regs = g_site_regs;
    g_site_xmm = xmm;
    g_site_regs = r;
    int n = __atomic_load_n(&st->nrun, __ATOMIC_ACQUIRE);
    for (int k = 0; k < n; k++)
        st->run[k](r);
    g_site_xmm = was;
    g_site_regs = was_regs;
}

// Saves flags and the general registers (as Regs), calls site_dispatch(regs, state), restores them, then runs the
// replaced instructions (install_hook's trampoline) and goes on. macOS: SSE registers are not saved (only for sites
// where none is live). Windows: xmm0-15 are saved (16 bytes each, r13 points 0x80 into them; r13 is callee-saved, and
// the game's r13 comes back from the Regs) with the callee's 32-byte shadow space below, passed as the third argument
// (game_site_xmm), and loaded back after: MSVC keeps values in xmm6-15 across a loop, so a site may need to change one.
static const uint8_t
    k_site_save
        [] = {0x9C,                                                                                           // pushfq
              0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x41, 0x53, 0x41, 0x52, 0x41, 0x51, 0x41, 0x50, // push r15..r8
              0x57, 0x56, 0x55, 0x53, 0x52, 0x51, 0x50, // push rdi rsi rbp rbx rdx rcx rax
              0x49, 0x89, 0xE4,                         // mov r12, rsp (the Regs)
#ifdef _WIN32
              0x4C, 0x89, 0xE1,                                                       // mov rcx, r12
              0x48, 0x83, 0xE4, 0xF0,                                                 // and rsp, -16
              0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00,                               // sub rsp, 0x120
              0x4C, 0x8D, 0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00,                         // lea r13, [rsp+0xa0]
              0xF3, 0x41, 0x0F, 0x7F, 0x45, 0x80, 0xF3, 0x41, 0x0F, 0x7F, 0x4D, 0x90, // movdqu [r13-0x80+16*i], xmm0-15
              0xF3, 0x41, 0x0F, 0x7F, 0x55, 0xA0, 0xF3, 0x41, 0x0F, 0x7F, 0x5D, 0xB0, 0xF3, 0x41, 0x0F, 0x7F, 0x65,
              0xC0, 0xF3, 0x41, 0x0F, 0x7F, 0x6D, 0xD0, 0xF3, 0x41, 0x0F, 0x7F, 0x75, 0xE0, 0xF3, 0x41, 0x0F, 0x7F,
              0x7D, 0xF0, 0xF3, 0x45, 0x0F, 0x7F, 0x45, 0x00, 0xF3, 0x45, 0x0F, 0x7F, 0x4D, 0x10, 0xF3, 0x45, 0x0F,
              0x7F, 0x55, 0x20, 0xF3, 0x45, 0x0F, 0x7F, 0x5D, 0x30, 0xF3, 0x45, 0x0F, 0x7F, 0x65, 0x40, 0xF3, 0x45,
              0x0F, 0x7F, 0x6D, 0x50, 0xF3, 0x45, 0x0F, 0x7F, 0x75, 0x60, 0xF3, 0x45, 0x0F, 0x7F, 0x7D, 0x70, 0x4D,
              0x8D, 0x45, 0x80, // lea r8, [r13-0x80] (the xmm)
              0x48, 0xBA};      // mov rdx, imm64 (the state)
#else
              0x4C, 0x89, 0xE7,       // mov rdi, r12
              0x48, 0x83, 0xE4, 0xF0, // and rsp, -16
              0x48, 0xBE};            // mov rsi, imm64 (the state)
#endif
static const uint8_t k_site_call[] = {0x48, 0xB8}; // mov rax, imm64 (site_dispatch)
static const uint8_t k_site_restore[] = {
    0xFF, 0xD0, // call rax
#ifdef _WIN32
    0xF3, 0x41, 0x0F, 0x6F, 0x45, 0x80, 0xF3, 0x41, 0x0F, 0x6F, 0x4D, 0x90, // movdqu xmm0-15, [r13-0x80+16*i]
    0xF3, 0x41, 0x0F, 0x6F, 0x55, 0xA0, 0xF3, 0x41, 0x0F, 0x6F, 0x5D, 0xB0, 0xF3, 0x41, 0x0F, 0x6F, 0x65,
    0xC0, 0xF3, 0x41, 0x0F, 0x6F, 0x6D, 0xD0, 0xF3, 0x41, 0x0F, 0x6F, 0x75, 0xE0, 0xF3, 0x41, 0x0F, 0x6F,
    0x7D, 0xF0, 0xF3, 0x45, 0x0F, 0x6F, 0x45, 0x00, 0xF3, 0x45, 0x0F, 0x6F, 0x4D, 0x10, 0xF3, 0x45, 0x0F,
    0x6F, 0x55, 0x20, 0xF3, 0x45, 0x0F, 0x6F, 0x5D, 0x30, 0xF3, 0x45, 0x0F, 0x6F, 0x65, 0x40, 0xF3, 0x45,
    0x0F, 0x6F, 0x6D, 0x50, 0xF3, 0x45, 0x0F, 0x6F, 0x75, 0x60, 0xF3, 0x45, 0x0F, 0x6F, 0x7D, 0x70,
#endif
    0x4C, 0x89, 0xE4,                         // mov rsp, r12
    0x58, 0x59, 0x5A, 0x5B, 0x5D, 0x5E, 0x5F, // pop rax rcx rdx rbx rbp rsi rdi
    0x41, 0x58, 0x41, 0x59, 0x41, 0x5A, 0x41, 0x5B, 0x41, 0x5C, 0x41, 0x5D, 0x41, 0x5E, 0x41, 0x5F, // pop r8..r15
    0x9D,                                                                                           // popfq
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00}; // jmp [rip+0] -> the trampoline

// Where the site is: macOS its function's symbol + offset; Windows the table's "site:<id>" entry (the offset
// differs, and the table holds the bytes expected there).
static uint8_t *site_address(const GameHookDef *d) {
#ifdef _WIN32
    char name[160];
    snprintf(name, sizeof name, "site:%s", d->id);
    return sym(name);
#else
    uint8_t *fn = sym(d->symbol);
    return fn ? fn + d->offset : NULL;
#endif
}

static bool site_stub(const GameHookDef *d, GameHookState *st) {
    size_t n = sizeof k_site_save + 8 + sizeof k_site_call + 8 + sizeof k_site_restore + 8;
    uint8_t *page, *stub = stub_alloc(n, &page);
    if (!stub) {
        logf_("%s: no stub memory, hook off", d->id);
        return false;
    }
    uint8_t *p = stub;
    uint64_t v;
    memcpy(p, k_site_save, sizeof k_site_save);
    p += sizeof k_site_save;
    v = (uint64_t)st;
    memcpy(p, &v, 8);
    p += 8;
    memcpy(p, k_site_call, sizeof k_site_call);
    p += sizeof k_site_call;
    v = (uint64_t)site_dispatch;
    memcpy(p, &v, 8);
    p += 8;
    memcpy(p, k_site_restore, sizeof k_site_restore);
    p += sizeof k_site_restore;
    uint8_t *slot = p; // the trampoline's address
    memset(slot, 0, 8);
    if (!stub_seal(page))
        return false;
    // Nothing runs the site yet (install time), so the slot can be filled after the jump is patched in.
    uint8_t *at = site_address(d);
    void *tramp = at ? install_hook_rip(d->id, at, stub, d->bytes, d->len, d->rip, d->nrip) : NULL;
    if (!at)
        logf_("%s: symbol not found, hook off", d->id);
    if (!tramp)
        return false;
#ifndef _WIN32
    if (mprotect(page, 4096, PROT_READ | PROT_WRITE) != 0)
        return false;
#endif
    memcpy(slot, &tramp, 8);
    return stub_seal(page);
}

bool game_hook_site(const char *id, void (*fn)(const Regs *regs)) {
    int i;
    const GameHookDef *d = id ? find(id, &i) : NULL;
    if (!d || !d->site || !fn) {
        logf_("game hook %s: not a catalogued site, not observed", id ? id : "?");
        return false;
    }
    GameHookState *st = &g_state[i];
    if (st->nfns == MAX_SITE_FNS) {
        logf_("%s: too many observers", d->id);
        return false;
    }
    if (!st->tried) {
        st->tried = true;
        st->on = site_stub(d, st);
    }
    if (!st->on)
        return false;
    watch_campaigns();
    st->owners[st->nfns] = mods_current();
    st->fns[st->nfns++] = fn;
    reorder_site(st);
    return true;
}
