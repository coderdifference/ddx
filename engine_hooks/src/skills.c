// Which combat skills a hero can see and use right now.
// Hero::GetModeSelectedCombatSkills() builds the list of the hero's skills that are valid now (the game
// checks .valid_modes there). The combat bar (GetNumberOfSelectedCombatSkills / GetSelectedCombatSkill)
// and out-of-turn skills and act-outs (GetNumberOfCombatSkills(true) / GetCombatSkillAtIndex(true, ..))
// all call it. We drop the skills whose config conditions fail, and placeholder levels (below the skill's
// "guild_first_rank": a level every hero owns that isn't a real rank), for heroes of a configured class.
#include "common.h"

// libc++ std::vector<HeroCombatSkill*>
typedef struct {
    void **begin, **end, **cap;
} PtrVector;

#ifdef _WIN32
typedef PtrVector *(*get_skills_fn)(const void *hero, PtrVector *out); // MSVC: this, then the hidden pointer
#else
typedef PtrVector *(*get_skills_fn)(PtrVector *out, const void *hero); // returned via hidden pointer
#endif
static get_skills_fn g_orig;

// Keeps the skills the class config allows (in place); how many are left.
static int allowed(const ClassConfig *c, const void *hero, void **skills, int n) {
    int w = 0;
    for (int r = 0; r < n; r++) {
        unsigned h = skill_id_hash(skills[r]);
        int level = *(const int *)((const uint8_t *)skills[r] + SKILL_LEVEL); // this HeroCombatSkill's level
        if (level + 1 < config_guild_first_rank(c, h))
            continue; // a placeholder level: not usable
        if (config_skill_allowed(c, hero, h))
            skills[w++] = skills[r];
    }
    return w;
}

#ifdef _WIN32
static PtrVector *hook(const void *hero, PtrVector *out) {
    g_orig(hero, out);
#else
static PtrVector *hook(PtrVector *out, const void *hero) {
    g_orig(out, hero);
#endif
    const ClassConfig *c = config_for_hero(hero);
    if (!c || !out->begin)
        return out;
    out->end = out->begin + allowed(c, hero, out->begin, (int)(out->end - out->begin));
    int n = paging_window(c, hero, out->begin, (int)(out->end - out->begin)); // combat bar paging
    out->end = out->begin + n;
    return out;
}

static void (*g_delete)(void *); // operator delete (the vector's buffer)

int skills_usable(const void *hero, void **out, int max) {
    get_skills_fn get = g_orig ? g_orig : (get_skills_fn)sym("_ZNK4Hero27GetModeSelectedCombatSkillsEv");
    if (!g_delete)
        g_delete = (void (*)(void *))sym("_ZdlPv");
    if (!hero || !get || !g_delete)
        return 0;
    PtrVector v = {0};
#ifdef _WIN32
    get(hero, &v);
#else
    get(&v, hero);
#endif
    int n = v.begin ? (int)(v.end - v.begin) : 0;
    const ClassConfig *c = config_for_hero(hero);
    if (c)
        n = allowed(c, hero, v.begin, n);
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        out[i] = v.begin[i];
    if (v.begin)
        g_delete(v.begin);
    return n;
}

void skills_install(void) {
    static const uint8_t prologue[] = PROLOGUE_SUB8(0x38); // sub rsp,0x38
    g_orig = (get_skills_fn)install_hook("Hero::GetModeSelectedCombatSkills",
                                         sym("_ZNK4Hero27GetModeSelectedCombatSkillsEv"), (void *)hook, prologue,
                                         sizeof prologue);
}
