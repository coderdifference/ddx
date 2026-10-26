// Sound files: plays a WAV, OGG Vorbis, FLAC or MP3 file from the data folder or a mod, on the game's own FMOD.
//
// The game's audio is FMOD Studio (2.03, libfmodstudio.dylib and libfmod.dylib, both exporting FMOD's C API): every
// sound is an event in a bank, played by path (Audio_Event("/ui/...") becomes event:/ui/...), and the game has no call
// that plays a file. This module uses the game's systems (the globals _fmod_studio and _fmod_system, created by the
// game's audio init) and FMOD's core API: FMOD_System_CreateSound loads a file once (decoded into memory: these are
// short sounds) and FMOD_System_PlaySound plays it on the master channel group.
//
// Volume: the game's settings are the master volume (Audio_SetMasterVolume: the core master channel group, which our
// channels play under, so it applies by itself) and one VCA per channel (vca:/sfx, vca:/music, vca:/vo, set by
// Audio_SetChannelVolume through FMOD_Studio_System_GetVCA). A core channel isn't under any Studio bus, so the VCAs
// don't reach it: each play sets the channel's volume to its VCA's.
//
// Paths: StorageManager::GetFullFilePath(mount 1, path) turns a data path into a real file, looking in the mods' folders
// as the game does for mod banks (Audio_InitBankCategory). A file that can't be found or loaded is logged once.
//
// macOS only for now (the symbols are looked up by name).
#include "common.h"
#include <stdio.h>
#include <string.h>

#define MAX_SOUNDS        32
#define FMOD_LOOP_OFF     0x1
#define FMOD_2D           0x8
#define FMOD_CREATESAMPLE 0x100

typedef struct {
    char path[128];
    void *sound; // FMOD_SOUND*, NULL: failed (logged once)
} Sound;

static Sound g_sounds[MAX_SOUNDS];
static int g_nsounds;
static int g_state; // 0 not looked up, 1 ready, -1 unavailable

static void **g_studio, **g_core; // _fmod_studio, _fmod_system
static void **g_storage;          // StorageManager::s_Instance
static bool (*g_full_path)(void *storage, int mount, const char *path, char *out, unsigned cap);
static int (*g_create_sound)(void *system, const char *name, unsigned mode, void *exinfo, void **sound);
static int (*g_play_sound)(void *system, void *sound, void *group, int paused, void **channel);
static int (*g_set_volume)(void *channel, float volume);
static int (*g_set_paused)(void *channel, int paused);
static int (*g_get_vca)(void *studio, const char *path, void **vca);
static int (*g_vca_volume)(void *vca, float *volume, float *final_volume);

static bool ready(void) {
    if (!g_state) {
        g_studio = (void **)sym("fmod_studio");
        g_core = (void **)sym("fmod_system");
        g_storage = (void **)sym("_ZN14StorageManager10s_InstanceE");
        g_full_path = (bool (*)(void *, int, const char *, char *, unsigned))sym(
            "_ZNK14StorageManager15GetFullFilePathE12StorageMountPKcPcj");
        g_create_sound = (int (*)(void *, const char *, unsigned, void *, void **))sym("FMOD_System_CreateSound");
        g_play_sound = (int (*)(void *, void *, void *, int, void **))sym("FMOD_System_PlaySound");
        g_set_volume = (int (*)(void *, float))sym("FMOD_Channel_SetVolume");
        g_set_paused = (int (*)(void *, int))sym("FMOD_Channel_SetPaused");
        g_get_vca = (int (*)(void *, const char *, void **))sym("FMOD_Studio_System_GetVCA");
        g_vca_volume = (int (*)(void *, float *, float *))sym("FMOD_Studio_VCA_GetVolume");
        g_state = g_studio && g_core && g_storage && g_full_path && g_create_sound && g_play_sound && g_set_volume &&
                          g_set_paused && g_get_vca && g_vca_volume
                      ? 1
                      : -1;
        if (g_state < 0)
            logf_("audio: symbols missing, sound files won't play");
    }
    return g_state > 0 && *g_core && *g_studio && *g_storage; // the game's audio may be off or not started yet
}

static Sound *load(const char *path) {
    for (int i = 0; i < g_nsounds; i++)
        if (!strcmp(g_sounds[i].path, path))
            return &g_sounds[i];
    if (g_nsounds == MAX_SOUNDS || strlen(path) >= sizeof g_sounds[0].path) {
        logf_("audio: %s: too many sound files or too long a path, not played", path);
        return NULL;
    }
    Sound *s = &g_sounds[g_nsounds++];
    strcpy(s->path, path);
    char full[1024];
    if (!g_full_path(*g_storage, 1, path, full, sizeof full)) {
        logf_("audio: %s: file not found", path);
        return s;
    }
    int r = g_create_sound(*g_core, full, FMOD_CREATESAMPLE | FMOD_2D | FMOD_LOOP_OFF, NULL, &s->sound);
    if (r) {
        s->sound = NULL;
        logf_("audio: %s: FMOD can't load it (error %d)", full, r);
    } else
        logf_("audio: %s loaded", full);
    return s;
}

bool audio_play_file(const char *path, AudioChannel channel) {
    static const char *const vcas[] = {"vca:/sfx", "vca:/music", "vca:/vo"};
    if (!path || !*path || (unsigned)channel > AUDIO_VOICE || !ready())
        return false;
    Sound *s = load(path);
    if (!s || !s->sound)
        return false;
    float volume = 1, final_volume = 1;
    void *vca = NULL;
    if (!g_get_vca(*g_studio, vcas[channel], &vca) && vca)
        g_vca_volume(vca, &volume, &final_volume);
    void *ch = NULL;
    int r = g_play_sound(*g_core, s->sound, NULL, 1, &ch); // paused, so the volume is right from the first sample
    if (r || !ch) {
        logf_("audio: %s: FMOD can't play it (error %d)", path, r);
        return false;
    }
    g_set_volume(ch, volume);
    g_set_paused(ch, 0);
    return true;
}
