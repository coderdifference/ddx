// Fixes for the game's own bugs.
//   presentation  RaidDisplay's presentation override (RAIDDISPLAY_PRESENTATION: 1 affliction / virtue, 3 / 4 trap,
//                 curio) picks the colour grade Panel_DungeonView::RunPostEffects binds. Only InvestigateCurio sets it
//                 back to 0, so an affliction, virtue or trap leaves it set into the next expedition of the session.
//                 Cleared on raid_start and raid_finish (SetPresentationOverride(0, 0), the curio's own reset).
//   colour grade  Texture_Bind3D(unit, ResourceHandle<Texture> const&) has no null check: a handle with a valid
//                 index and generation whose resource no longer resolves (ResourceManager::GetResource NULL: the
//                 entry was freed and its generation moved on) reads Texture+0x30 from NULL. Seen on the first
//                 dungeon frame of a second expedition in a session, binding the presentation grade and (with the
//                 override cleared) RaidDisplay's stealth grades (+0x3ee0 / +0x3ef0, GetStealthColourGrades).
//                 Such a bind uses colours/default_colour_grade.png (the game's neutral LUT) instead, logged.
//                 Windows: Texture_Bind3D is inlined three times in RunPostEffects (resolve the handle, then bind
//                 the Texture's +0x30): a site after each resolve swaps a NULL result for the default grade's.
#include "common.h"

static void **g_raid_display; // &RaidDisplay::s_Instance
static void (*g_set_override)(void *display, int kind, unsigned trait);

static void clear_presentation(HookContext *c, void *ud) {
    (void)c;
    (void)ud;
    if (*g_raid_display)
        g_set_override(*g_raid_display, 0, 0);
}

static void (*g_texture_from_file)(void *out, const char *path, unsigned flags);
static _Alignas(16) uint8_t g_fallback[16]; // the default grade's ResourceHandle<Texture>
static int g_fallback_state;                // 0 not loaded, 1 loaded, -1 missing
static unsigned g_logged;

static bool load_fallback(void) {
    if (!g_fallback_state) {
        *(int64_t *)g_fallback = (int64_t)0xFFFFFFFF00000000;
        g_texture_from_file(g_fallback, "colours/default_colour_grade.png", LUT_FLAGS);
        g_fallback_state = ((const int *)g_fallback)[0] >= 0 ? 1 : -1;
    }
    return g_fallback_state > 0;
}

static void log_stale(unsigned unit, const int *n, bool bound) {
    if (g_logged++ < 8)
        logf_("game fixes: Texture_Bind3D unit %u: stale handle %d/%d, %s", unit, n[0], n[1],
              bound ? "bound the default colour grade" : "skipped (no default grade)");
}

#ifndef _WIN32
static void **g_rm; // &ResourceManager::s_Instance
static void *(*g_get_resource)(void *rm, const void *handle);
static void (*o_bind3d)(unsigned unit, void *handle);

static void h_bind3d(unsigned unit, void *handle) {
    const int *n = handle;
    // the game's own test: a cached Texture, or an empty handle, binds as before
    if (*(void **)((uint8_t *)handle + 8) || n[0] < 0 || n[1] < 0 || g_get_resource(*g_rm, handle)) {
        o_bind3d(unit, handle);
        return;
    }
    bool ok = load_fallback();
    log_stale(unit, n, ok);
    if (ok)
        o_bind3d(unit, g_fallback);
}
#else
static void *(*g_resolve)(void *handle); // ResourceHandle<Texture> -> Texture (or NULL)

// After the inlined bind's resolve: rax the Texture, the handle at rbp + disp. The bind follows for a handle with a
// valid index and generation (or the index -1234), so a NULL Texture there becomes the default grade's; without
// one, a zeroed Texture (texture id 0: the bind unbinds, as skipping it would leave unit's last grade bound).
static void stale_site(Regs *r, unsigned unit, int disp) {
    if (r->rax)
        return;
    const int *n = (const int *)(uintptr_t)(r->rbp + (int64_t)disp);
    if (!((n[0] >= 0 && n[1] >= 0) || n[0] == -1234))
        return;
    static _Alignas(16) uint8_t empty[0x40];
    void *tex = load_fallback() ? g_resolve(g_fallback) : NULL;
    log_stale(unit, n, tex != NULL);
    r->rax = (uint64_t)(uintptr_t)(tex ? tex : empty);
}
static void site1(Regs *r) {
    stale_site(r, 1, 7);
}
static void site2(Regs *r) {
    stale_site(r, 2, -9);
}
static void site3(Regs *r) {
    stale_site(r, 3, -0x19);
}
#endif

void game_fixes_install(void) {
    g_raid_display = (void **)sym("_ZN6RaidUI6Screen11RaidDisplay10s_InstanceE");
    *(void **)&g_set_override =
        sym("_ZN6RaidUI6Screen11RaidDisplay23SetPresentationOverrideENS1_21ePresentationOverrideEj");
    if (!g_raid_display || !g_set_override)
        logf_("game fixes: presentation override symbols missing, off");
    else if (!hook_on("raid_start", HOOK_AFTER, clear_presentation, NULL) ||
             !hook_on("raid_finish", HOOK_AFTER, clear_presentation, NULL))
        logf_("game fixes: presentation override listeners failed, off");

    *(void **)&g_texture_from_file = sym("_Z20new_Texture_FromFile8c_stringj");
#ifndef _WIN32
    g_rm = (void **)sym("_ZN15ResourceManager10s_InstanceE");
    *(void **)&g_get_resource = sym("_ZN15ResourceManager11GetResourceERK19ResourceEntryHandle");
    if (!g_rm || !g_get_resource || !g_texture_from_file) {
        logf_("game fixes: colour grade symbols missing, off");
        return;
    }
    // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; mov r14,rsi; mov ebx,edi
    static const uint8_t prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                       0x41, 0x54, 0x53, 0x49, 0x89, 0xF6, 0x89, 0xFB};
    *(void **)&o_bind3d =
        install_hook("Texture_Bind3D (colour grade fix)", sym("_Z14Texture_Bind3DjRK14ResourceHandleI7TextureE"),
                     (void *)h_bind3d, prologue, sizeof prologue);
    if (!o_bind3d)
        logf_("game fixes: Texture_Bind3D hook unavailable, colour grade fix off");
#else
    *(void **)&g_resolve = sym("win:ResourceHandle<Texture> resolve");
    if (!g_resolve || !g_texture_from_file) {
        logf_("game fixes: colour grade symbols missing, off");
        return;
    }
    if (!install_mid_hook_rw("RunPostEffects bind 1 (colour grade fix)", sym("win:RunPostEffects bind 1"), site1) ||
        !install_mid_hook_rw("RunPostEffects bind 2 (colour grade fix)", sym("win:RunPostEffects bind 2"), site2) ||
        !install_mid_hook_rw("RunPostEffects bind 3 (colour grade fix)", sym("win:RunPostEffects bind 3"), site3))
        logf_("game fixes: RunPostEffects sites unavailable, colour grade fix off (in part)");
#endif
}
