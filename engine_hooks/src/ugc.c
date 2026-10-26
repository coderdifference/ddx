// Local mod list fixes (mods in the game's data/mods folder, "mod_local_source").
//
// DefaultUserServices::InitializeLocalUserGeneratedContent finds every mods/*/project.xml, loads it with
// UserGeneratedContent::LoadProjectParameters -> UGCProjectMetadata::Load -> ReadXMLFile, then sets the mod's id to its
// title (SetId(GetTitle())) and its source to "mod_local_source". Campaigns save applied mods by (id, source) (UGCId).
// Bug: ReadXMLFile checks the string fields (Title, ItemDescription, ...) but never stores them (ReadJSONFile does; only
// project.xml is scanned), so every local mod has an empty title and the same id "". Every lookup by id then finds the
// first local mod: the mod list shows only that one (its entries all resolve to it), titles never show, and a campaign
// that "applies" any local mod gets the first one.
// Fix: after ReadXMLFile, fill the empty string fields from a copy of the XML (rapidxml parses in place), so each mod's
// id is its title. Campaigns saved before the fix list their local mods as id "": a failed lookup of ("",
// mod_local_source) returns the first local mod scanned, which is what that id always meant.
//
// UGCProjectMetadata (UserGeneratedContent+0x6e0) string fields, 512 bytes each (from ReadJSONFile): +0x200
// PreviewIconFile, +0x400 ItemDescriptionShort, +0x600 ModDataPath, +0x800 Title (GetTitle), +0xa00 Language;
// +0x1048 a pointer to the ItemDescription buffer (8000 bytes, SetDescription). UGCId: id char[128], source at +0x80.
// macOS only.
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXED_LEN 0x200
#define DESC_LEN  8000

static uint64_t (*o_read_xml)(void *meta, char *text, void *logger);
static void *(*o_get_ugc)(const void *services, const void *ugc_id);
static void *g_first_local; // the first local mod scanned: what the id "" always resolved to

// the text of <tag>...</tag> (CDATA and the five XML entities decoded) into out; false if the tag is missing or empty
static bool xml_text(const char *xml, const char *tag, char *out, size_t n) {
    char open[64], close[64];
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    const char *a = strstr(xml, open);
    if (!a)
        return false;
    a += strlen(open);
    const char *b = strstr(a, close);
    if (!b)
        return false;
    while (a < b && (*a == ' ' || *a == '\t' || *a == '\r' || *a == '\n'))
        a++;
    if (!strncmp(a, "<![CDATA[", 9)) {
        a += 9;
        const char *e = strstr(a, "]]>");
        if (e && e < b)
            b = e;
    } else {
        while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n'))
            b--;
    }
    size_t o = 0;
    static const struct {
        const char *ent;
        char c;
    } ents[] = {{"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}};
    while (a < b && o + 1 < n) {
        bool done = false;
        for (size_t i = 0; *a == '&' && i < sizeof ents / sizeof ents[0]; i++) {
            size_t l = strlen(ents[i].ent);
            if (a + l <= b && !strncmp(a, ents[i].ent, l)) {
                out[o++] = ents[i].c;
                a += l;
                done = true;
                break;
            }
        }
        if (!done)
            out[o++] = *a++;
    }
    out[o] = 0;
    return o > 0;
}

static void fill(uint8_t *meta, size_t off, const char *xml, const char *tag) {
    char *field = (char *)meta + off;
    if (*field)
        return; // the game stored it (a fixed build)
    xml_text(xml, tag, field, FIXED_LEN);
}

static uint64_t h_read_xml(void *meta, char *text, void *logger) {
    char *copy = text ? strdup(text) : NULL;
    uint64_t r = o_read_xml(meta, text, logger);
    if (!copy)
        return r;
    uint8_t *m = meta;
    fill(m, META_TITLE, copy, "Title");
    fill(m, META_SHORT_DESC, copy, "ItemDescriptionShort");
    fill(m, META_MOD_DATA_PATH, copy, "ModDataPath");
    fill(m, META_LANGUAGE, copy, "Language");
    char *desc = *(char **)(m + META_DESC_PTR);
    if (desc && !*desc)
        xml_text(copy, "ItemDescription", desc, DESC_LEN);
    logf_("mods: project \"%s\" read", (const char *)m + META_TITLE);
    free(copy);
    return r;
}

static void *h_get_ugc(const void *services, const void *ugc_id) {
    void *u = o_get_ugc(services, ugc_id);
    const char *id = ugc_id, *source = (const char *)ugc_id + UGCID_SOURCE;
    if (u || *id || strcmp(source, "mod_local_source"))
        return u;
    if (!g_first_local) { // the first local mod in the list (scan order)
        unsigned (*count)(const void *) =
            (unsigned (*)(const void *))sym("_ZNK21UserServicesInterface29GetUserGeneratedContentCountEv");
        void *(*at)(const void *, unsigned) =
            (void *(*)(const void *, unsigned))sym("_ZNK21UserServicesInterface30GetUserGeneratedContentByIndexEj");
        const char *(*src)(const void *) = (const char *(*)(const void *))sym("_ZNK20UserGeneratedContent9GetSourceEv");
        for (unsigned i = 0; count && at && src && i < count(services); i++) {
            void *c = at(services, i);
            if (c && !strcmp(src(c), "mod_local_source")) {
                g_first_local = c;
                break;
            }
        }
    }
    if (g_first_local) {
        static bool logged;
        if (!logged)
            logf_("mods: a campaign's local mod saved before the title fix (id \"\") resolves to the first local mod");
        logged = true;
    }
    return g_first_local;
}

void ugc_install(void) {
#ifdef _WIN32
    return;
#else
    // push rbp; mov rbp,rsp; push r15; push r14; push r12; push rbx; mov eax,0x10290 (then a call to ___chkstk)
    static const uint8_t read_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56,
                                            0x41, 0x54, 0x53, 0xB8, 0x90, 0x02, 0x01, 0x00};
    *(void **)&o_read_xml =
        install_hook("UGCProjectMetadata::ReadXMLFile", sym("_ZN18UGCProjectMetadata11ReadXMLFileEPcP6Logger"),
                     (void *)h_read_xml, read_prologue, sizeof read_prologue);
    // push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; push rax
    static const uint8_t get_prologue[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41,
                                           0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50};
    *(void **)&o_get_ugc = install_hook("UserServicesInterface::GetUserGeneratedContent",
                                        sym("_ZNK21UserServicesInterface23GetUserGeneratedContentERK5UGCId"),
                                        (void *)h_get_ugc, get_prologue, sizeof get_prologue);
#endif
}
