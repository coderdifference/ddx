// Campaign-wide integer fields by name, saved with the campaign as ddx_<name> (see common.h, "campaigndata.c").
//
// The carrier is the campaign's persist.game.json, which ProfileGameFile::Save writes at every save: inside it,
// the "profile_options" object is written by Options::System::SaveProfileOptions(SaveData*) (its only caller), and
// read back by Options::System::RestoreProfileOptions(RestoreData*), called only by ProfileGameFile::RestoreOptions
// with the RestoreData inside the same object (campaign load, Darkest::ProcessProfileRestore; and the front end's
// SavePersistGameSaveSlot, which restores a slot's options and saves them straight back). Our keys sit after the
// game's in that object; the game reads its own by name and ignores ours. Options::System::ResetProfileOptions
// (Darkest::UpdateStateInternal, after CleanupCampaign) clears the values, so a campaign started after another doesn't
// inherit them. Windows: Save and Restore are inlined (mid-function sites, below).
#include "common.h"
#include <stdio.h>
#include <string.h>

#define MAX_CAMPAIGN_FIELDS 32
#define NAME_MAX            40
typedef struct {
    char name[NAME_MAX];
    bool present;
    int value;
} CampaignField;
static CampaignField g_fields[MAX_CAMPAIGN_FIELDS];
static int g_nfields;

static CampaignField *find(const char *name, bool create) {
    if (!name || !name[0])
        return NULL;
    for (int i = 0; i < g_nfields; i++)
        if (!strcmp(g_fields[i].name, name))
            return &g_fields[i];
    if (!create)
        return NULL;
    if (g_nfields >= MAX_CAMPAIGN_FIELDS || strlen(name) >= NAME_MAX) {
        logf_("campaign data: can't add field %s (%d fields, names under %d characters)", name, MAX_CAMPAIGN_FIELDS,
              NAME_MAX);
        return NULL;
    }
    CampaignField *f = &g_fields[g_nfields++];
    memset(f, 0, sizeof *f);
    snprintf(f->name, sizeof f->name, "%s", name);
    return f;
}

void campaigndata_field(const char *name) {
    find(name, true);
}
int campaigndata_get(const char *name) {
    CampaignField *f = find(name, false);
    return f && f->present ? f->value : 0;
}
bool campaigndata_has(const char *name) {
    CampaignField *f = find(name, false);
    return f && f->present;
}
void campaigndata_set(const char *name, int value) {
    CampaignField *f = find(name, true);
    if (!f)
        return;
    f->present = true;
    f->value = value;
}

static void clear(void) {
    for (int i = 0; i < g_nfields; i++)
        g_fields[i].present = false, g_fields[i].value = 0;
}

typedef void (*savedata_add_u32_fn)(void *self, const char *key, const unsigned *value);
typedef bool (*restoredata_get_u32_fn)(void *self, const char *key, unsigned *value);

// Our keys, written into the SaveData after the game's profile options (inside "profile_options", after "values").
static void save_fields(void *save) {
    if (!save)
        return;
    savedata_add_u32_fn add = (*(savedata_add_u32_fn **)save)[SAVEDATA_ADD_U32 / 8];
    int n = 0;
    for (int i = 0; i < g_nfields; i++) {
        if (!g_fields[i].present)
            continue;
        char key[64];
        snprintf(key, sizeof key, "ddx_%s", g_fields[i].name);
        unsigned v = (unsigned)g_fields[i].value;
        add(save, key, &v);
        n++;
    }
    static int last = -1; // logged when the count changes (saves are frequent)
    if (n != last) {
        logf_("campaign data: %d field(s) saved with the campaign", n);
        last = n;
    }
}

// Read back from the same place; the values were cleared just before (another campaign's never carry over).
static void restore_fields(void *restore) {
    if (!restore)
        return;
    restoredata_get_u32_fn get = (*(restoredata_get_u32_fn **)restore)[RESTOREDATA_GET_U32 / 8];
    for (int i = 0; i < g_nfields; i++) {
        char key[64];
        snprintf(key, sizeof key, "ddx_%s", g_fields[i].name);
        unsigned v = 0;
        if (!get(restore, key, &v))
            continue;
        g_fields[i].present = true;
        g_fields[i].value = (int)v;
    }
    dbgf_("campaign data restored (%d fields known)", g_nfields);
}

static void (*o_reset)(void *options);
static void h_reset(void *options) {
    o_reset(options);
    clear();
}

#ifdef _WIN32
// MSVC inlined SaveProfileOptions into ProfileGameFile::Save and RestoreProfileOptions into
// ProfileGameFile::RestoreOptions: the sites are where each closes "values", still inside "profile_options" (the
// SaveData in rdi, the RestoreData in r14). RestoreOptions calls ResetProfileOptions first, which clears ours.
static void m_save(const Regs *r) {
    save_fields((void *)r->rdi);
}
static void m_restore(const Regs *r) {
    restore_fields((void *)r->r14);
}
#else
static void (*o_save)(void *options, void *save);
static void (*o_restore)(void *options, void *restore);

static void h_save(void *options, void *save) {
    o_save(options, save);
    save_fields(save);
}

static void h_restore(void *options, void *restore) {
    o_restore(options, restore);
    clear(); // another campaign's values never carry over
    restore_fields(restore);
}
#endif

void campaigndata_install(void) {
    // push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; push rax
    static const uint8_t push_rax[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41,
                                       0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
#ifdef _WIN32
    if (!install_mid_hook("ProfileGameFile::Save: profile options (campaign data)",
                          sym("win:ProfileGameFile::Save profile options"), m_save) ||
        !install_mid_hook("ProfileGameFile::RestoreOptions: profile options (campaign data)",
                          sym("win:ProfileGameFile::RestoreOptions profile options"), m_restore)) {
        logf_("campaign data: off (fields aren't saved)");
        return;
    }
#else
    static const uint8_t sub18[] = PROLOGUE_SUB8(0x18);
    *(void **)&o_save = install_hook("Options::System::SaveProfileOptions (campaign data)",
                                     sym("_ZN7Options6System18SaveProfileOptionsEP8SaveData"), (void *)h_save, push_rax,
                                     sizeof push_rax);
    *(void **)&o_restore = install_hook("Options::System::RestoreProfileOptions (campaign data)",
                                        sym("_ZN7Options6System21RestoreProfileOptionsEP11RestoreData"),
                                        (void *)h_restore, sub18, sizeof sub18);
#endif
    *(void **)&o_reset =
        install_hook("Options::System::ResetProfileOptions (campaign data)",
                     sym("_ZN7Options6System19ResetProfileOptionsEv"), (void *)h_reset, push_rax, sizeof push_rax);
}
