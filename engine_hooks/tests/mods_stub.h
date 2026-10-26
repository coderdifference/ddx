// Stand-in for mods.c in offline tests: mods 0..3, each at g_mod_pos (its place in the campaign's list, -1 not
// applied); g_mod_current is the registering mod; campaign() runs the mods_on_campaign listeners.
static int g_mod_current = -1, g_mod_pos[4] = {-1, -1, -1, -1};
static void (*g_campaign_fns[8])(void);
static int g_ncampaign_fns;
static unsigned g_mod_generation = 1;
int mods_current(void) {
    return g_mod_current;
}
unsigned mods_generation(void) {
    return g_mod_generation;
}
bool mods_active(int mod) {
    return mod < 0 || (mod < 4 && g_mod_pos[mod] >= 0);
}
int mods_by_priority(int *out, int max) {
    int n = 0;
    for (int pos = 0; pos < 4; pos++)
        for (int m = 0; m < 4 && n < max; m++)
            if (g_mod_pos[m] == pos)
                out[n++] = m;
    return n;
}
void mods_on_campaign(void (*fn)(void)) {
    g_campaign_fns[g_ncampaign_fns++] = fn;
}
static void campaign(int pos0, int pos1, int pos2, int pos3) {
    g_mod_pos[0] = pos0;
    g_mod_pos[1] = pos1;
    g_mod_pos[2] = pos2;
    g_mod_pos[3] = pos3;
    g_mod_generation++;
    for (int i = 0; i < g_ncampaign_fns; i++)
        g_campaign_fns[i]();
}
