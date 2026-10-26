// Recruitment, hero drags, and tooltip line ownership, independent of any client's fees or item rules.
#include "common.h"
#include <stdlib.h>
#include <string.h>
void *game_hero_slot_hero(void *slot) {
    static void *(*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK17HeroSlotInterface7GetHeroEv");
    return slot && fn ? fn(slot) : NULL;
}
bool game_hero_town_event(const void *hero) {
    static bool (*fn)(const void *);
    if (!fn)
        *(void **)&fn = sym("_ZNK4Hero18GetIsFromTownEventEv");
    return hero && fn && fn(hero);
}
int game_trinket_discount(int price) {
    static int (*fn)(int, unsigned);
    static unsigned *tag;
    if (!fn)
        *(void **)&fn = sym("_ZN7Upgrade24GetAmountWithTagDiscountEij");
    if (!tag)
        tag = sym("_ZN7Upgrade4TagsL9k_TrinketE");
    return fn && tag && *tag ? fn(price, *tag) : price;
}
void *game_recruit_drag_hero(void *drag) {
    static void *type;
    if (!type)
        type = sym("_ZZN12HeroDragData15GetTypeIdStaticEvE7type_id");
    if (!drag || !type)
        return NULL;
    void **v = *(void ***)drag;
    if (((void *(*)(const void *))v[0])(drag) != type || *(void **)((uint8_t *)drag + DRAG_ENTRY))
        return NULL;
    return *(void **)((uint8_t *)drag + DRAG_HERO);
}
bool game_roster_is_full(void) {
    static void *(*roster)(void);
    static unsigned (*count)(const void *), (*max)(const void *);
    if (!roster)
        *(void **)&roster = sym("_ZN8Campaign13CurrentRosterEv");
    if (!count)
        *(void **)&count = sym("_ZNK6Roster6System19GetRosterLimitCountEv");
    if (!max)
        *(void **)&max = sym("_ZNK6Roster6System17GetRosterLimitMaxEv");
    void *r = roster ? roster() : NULL;
    return r && count && max && count(r) >= max(r);
}
bool game_recruit_hire(void *list, const Vec2 *pos, void *slot, void *hero, void *drop, const CurrencyCost *costs,
                       int ncosts) {
    static void (*ctor)(void *), (*set)(void *, const void *), (*accepted)(void *, const void *);
    if (!ctor)
        *(void **)&ctor = sym("_ZN12HeroDragDataC1Ev");
    if (!set)
        *(void **)&set = sym("_ZN12HeroDragData7SetHeroEPK4Hero");
    if (!accepted)
        *(void **)&accepted = sym("_ZN24RecruitHeroSlotInterface14OnDropAcceptedEPK8DragData");
    if (!ctor || !set || !accepted || !drop || !hero || game_hero_slot_hero(slot) != hero)
        return false;
    for (int i = 0; i < ncosts; i++)
        if (costs[i].amount < 0 || game_estate_amount(costs[i].currency) < costs[i].amount)
            return false;
    _Alignas(16) uint8_t drag[32] = {0};
    ctor(drag);
    set(drag, hero);
    if (((uint64_t (*)(void *, const Vec2 *, void *))drop)(list, pos, drag) != 1)
        return false;
    game_estate_pay(costs, ncosts);
    accepted(slot, drag);
    return true;
}
struct TipPrefix {
    uint8_t *tip;
    unsigned count;
    uint8_t lines[32 * LINE_SIZE];
};
void *ui_tooltip_prefix_begin(void *tip, unsigned reserve) {
    unsigned n = tip ? *(unsigned *)((uint8_t *)tip + TIP_COUNT) : 0;
    if (!n || n > 32 || reserve > 32 - n)
        return NULL;
    TipPrefix *p = malloc(sizeof *p);
    if (!p)
        return NULL;
    p->tip = tip;
    p->count = n;
    // Move the existing line records aside without destroying their native string storage.
    // end restores them after the new prefix, transferring ownership back to the tooltip.
    memcpy(p->lines, p->tip + TIP_LINES, n * LINE_SIZE);
    *(unsigned *)(p->tip + TIP_COUNT) = 0;
    return p;
}
const char *ui_tooltip_prefix_line(TipPrefix *p, const char *text, uint64_t colour, bool replace_colour) {
    static void (*set)(void *, const char *);
    if (!set)
        *(void **)&set = sym("_ZN20GuiTooltipParameters4Text4Line9SetStringEPKc");
    if (!p || !set)
        return NULL;
    unsigned *n = (unsigned *)(p->tip + TIP_COUNT);
    if (*n >= 32 - p->count)
        return NULL;
    uint8_t *line = p->tip + TIP_LINES + (*n)++ * LINE_SIZE;
    memcpy(line, p->lines, LINE_SIZE);
    if (replace_colour)
        *(uint64_t *)(line + LINE_COLOUR) = colour;
    set(line, text);
    return *(const char **)(line + LINE_STRING);
}
float ui_tooltip_prefix_width(TipPrefix *p, float minimum) {
    if (!p)
        return 0;
    float *width = (float *)(p->tip + TIP_WIDTH);
    if (*width < minimum)
        *width = minimum;
    return *width;
}
void ui_tooltip_prefix_end(TipPrefix *p) {
    if (!p)
        return;
    unsigned *n = (unsigned *)(p->tip + TIP_COUNT);
    unsigned room = 32 - *n, count = p->count < room ? p->count : room;
    memcpy(p->tip + TIP_LINES + *n * LINE_SIZE, p->lines, count * LINE_SIZE);
    *n += count;
    free(p);
}
static void (*recruit_tip)(void *, void *);
static void *tip_params, *tip_slot;
static void tip_site(const Regs *r) {
#ifdef _WIN32
    tip_params = (void *)(r->rbp + 0x1450);
    tip_slot = (void *)r->r13;
#else
    tip_params = (void *)r->rbx;
    tip_slot = *(void **)(r->rbp - 0x1120);
#endif
}
static void on_tip(void *parameters, const void *caller, const uint8_t *ret) {
    if (!recruit_tip || !parameters || parameters != tip_params)
        return;
    tip_params = NULL;
    void *iface = tip_slot ? *(void **)((uint8_t *)tip_slot + SLOT_INTERFACE) : NULL;
    uint8_t *vt = sym("_ZTV24RecruitHeroSlotInterface");
    if (iface && vt && *(void **)iface == vt + 16)
        recruit_tip(parameters, game_hero_slot_hero(iface));
}
bool ui_recruit_tooltip_on(void (*callback)(void *, void *)) {
    if (!callback || recruit_tip)
        return false;
    if (!game_hook_site("BaseHeroRecruitDisplay recruit tooltip", tip_site))
        return false;
    recruit_tip = callback;
    ui_tooltip_on_show(on_tip);
    return true;
}
