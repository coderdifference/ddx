// Campaign-scoped configuration registry and the JSON menu-provider boundary.
#include "modmenu_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MAX_FORMS 64
static struct {
    int owner;
    bool ready;
    char *id, *schema;
    ModMenuDispatch event;
    ModMenuRelease release;
} forms[MAX_FORMS];
static int nforms, provider_owner = -2;
static ModMenuDispatch dispatch;
static ModMenuRelease release_reply;
static char *saved, *last_event;
static JVal *view;
static unsigned generation, closed_frame = ~0u;
static char token[32], host_error[256];
static bool loaded, opened, in_dispatch, notify_pending;

const char *modmenu_saved_namespace(void) {
    return saved;
}
int modmenu_host_version(void) {
    return mm_platform_ready() ? 1 : 0;
}
int modmenu_register(const char *id, ModMenuDispatch fn, ModMenuRelease release) {
    if (!id || !*id || !fn || !release || dispatch || !modmenu_host_version())
        return 0;
    dispatch = fn;
    release_reply = release;
    provider_owner = mods_current();
    return 1;
}
static bool active(int owner) {
    return owner < 0 || mods_active(owner);
}
static bool active_form(int i) {
    return forms[i].ready && active(forms[i].owner);
}
bool mm_available(void) {
    return mm_platform_ready() && loaded && dispatch && active(provider_owner) && mods_package_applied();
}
bool modmenu_closed_this_frame(void) {
    return closed_frame == ui_frame();
}
bool mm_is_open(void) {
    return opened && mm_available();
}
const char *mm_last_error(void) {
    return host_error;
}
const JVal *mm_view(void) {
    return view;
}

int modmenu_form_register(const char *schema, ModMenuDispatch fn, ModMenuRelease release) {
    if (!schema || strlen(schema) > MM_LIMIT || nforms >= MAX_FORMS || (fn && !release))
        return 0;
    char err[160];
    JVal *j = json_parse(schema, err, sizeof err);
    const char *id = mm_str(j, "id");
    const JVal *fields = json_get(j, "fields"), *version = json_get(j, "version");
    if (!j || j->t != J_OBJ || !*id || strlen(id) > 128 || !*mm_str(j, "title") || !fields || fields->t != J_ARR ||
        fields->n > 2048 || !version || version->t != J_NUM || version->num != 1) {
        json_free(j);
        return 0;
    }
    for (int i = 0; i < nforms; i++)
        if (!strcmp(forms[i].id, id)) {
            json_free(j);
            return 0;
        }
    int i = nforms;
    forms[i].id = strdup(id);
    forms[i].schema = strdup(schema);
    forms[i].owner = mods_current();
    forms[i].event = fn;
    forms[i].release = release;
    json_free(j);
    if (!forms[i].id || !forms[i].schema) {
        free(forms[i].id);
        free(forms[i].schema);
        return 0;
    }
    nforms++;
    return 1;
}
void modmenu_discover(const char *engine_dir) {
    // Called only after a client's successful initialization. Explicit registration wins.
    int owner = mods_current();
    for (int i = 0; i < nforms; i++)
        if (forms[i].owner == owner)
            return;
    char path[4600];
    snprintf(path, sizeof path, "%s/../mod_options/options.json", engine_dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0 || ftell(f) > (long)MM_LIMIT) {
        fclose(f);
        return;
    }
    size_t n = (size_t)ftell(f);
    rewind(f);
    char *s = malloc(n + 1);
    if (s && fread(s, 1, n, f) == n) {
        s[n] = 0;
        if (!modmenu_form_register(s, NULL, NULL))
            logf_("modmenu: invalid form %s", path);
    }
    free(s);
    fclose(f);
}
void modmenu_client_finished(int owner, bool success) {
    for (int i = 0; i < nforms;) {
        if (forms[i].owner != owner || forms[i].ready) {
            i++;
            continue;
        }
        if (success) {
            forms[i++].ready = true;
            continue;
        }
        free(forms[i].id);
        free(forms[i].schema);
        memmove(forms + i, forms + i + 1, (size_t)(nforms - i - 1) * sizeof forms[0]);
        nforms--;
    }
}
const char *modmenu_values(const char *id) {
    free(last_event);
    last_event = NULL;
    if (!loaded || !saved || !id)
        return NULL;
    for (int i = 0; i < nforms; i++)
        if (!strcmp(forms[i].id, id) && active_form(i)) {
            JVal *root = json_parse(saved, NULL, 0);
            const JVal *values = json_get(root, id);
            last_event = mm_stringify(values);
            json_free(root);
            return last_event;
        }
    return NULL;
}
const char *modmenu_client_event(const char *id, const char *request) {
    free(last_event);
    last_event = NULL;
    if (!loaded || !id || !request || strlen(request) > MM_LIMIT * 2)
        return NULL;
    for (int i = 0; i < nforms; i++)
        if (!strcmp(forms[i].id, id) && active_form(i) && forms[i].event) {
            int previous = mods_current();
            mods_set_current(forms[i].owner);
            char *reply = forms[i].event(request, strlen(request));
            mods_set_current(previous);
            if (reply) {
                if (strnlen(reply, MM_LIMIT * 2 + 1) <= MM_LIMIT * 2)
                    last_event = strdup(reply);
                forms[i].release(reply);
            }
            if (!last_event)
                last_event = strdup("{\"error\":\"The mod options callback failed.\"}");
            return last_event;
        }
    return NULL;
}
static void notify(const char *event) {
    JVal *root = saved ? json_parse(saved, NULL, 0) : NULL;
    for (int i = 0; i < nforms; i++)
        if (active_form(i) && forms[i].event) {
            MmBuffer b = {0};
            mm_put(&b, "{\"event\":");
            mm_quote(&b, event);
            mm_put(&b, ",\"values\":");
            const JVal *v = json_get(root, forms[i].id);
            if (v)
                mm_json(&b, v);
            else
                mm_put(&b, "{}");
            mm_put(&b, "}");
            if (!b.failed)
                modmenu_client_event(forms[i].id, b.text);
            free(b.text);
        }
    json_free(root);
}
void mm_event(const char *event, const char *extra) {
    if (!dispatch || in_dispatch)
        return;
    MmBuffer b = {0};
    mm_put(&b, "{\"event\":");
    mm_quote(&b, event);
    mm_put(&b, ",\"campaign\":");
    mm_quote(&b, token);
    if (extra && *extra) {
        mm_put(&b, ",");
        mm_put(&b, extra);
    }
    mm_put(&b, "}");
    if (b.failed) {
        free(b.text);
        return;
    }
    host_error[0] = 0;
    in_dispatch = true;
    int previous = mods_current();
    mods_set_current(provider_owner);
    char *reply = dispatch(b.text, b.len);
    mods_set_current(previous);
    free(b.text);
    if (reply) {
        JVal *next = strnlen(reply, MM_LIMIT * 2 + 1) <= MM_LIMIT * 2 ? json_parse(reply, NULL, 0) : NULL;
        release_reply(reply);
        if (next && next->t == J_OBJ) {
            json_free(view);
            view = next;
        } else {
            json_free(next);
            snprintf(host_error, sizeof host_error, "The mod menu service returned an invalid view.");
        }
    } else
        snprintf(host_error, sizeof host_error, "The mod menu service could not process the request.");
    in_dispatch = false;
    if (notify_pending) {
        notify_pending = false;
        notify("changed");
    }
}
void mm_set_open(bool value) {
    if (value && !mm_available())
        return;
    if (!value) {
        mm_reset_draw();
        closed_frame = ui_frame();
    }
    opened = value;
    mm_event(value ? "open" : "close", value ? "\"paused\":true" : NULL);
}
void modmenu_reset(void) {
    mm_reset_draw();
    if (loaded) {
        notify("unloaded");
        mm_event("unload", NULL);
    }
    opened = loaded = notify_pending = false;
    token[0] = 0;
    free(saved);
    saved = NULL;
    json_free(view);
    view = NULL;
}
void modmenu_loaded(void) {
    loaded = true;
    opened = false;
    snprintf(token, sizeof token, "%u", ++generation);
    if (!saved)
        saved = strdup("{}");
    MmBuffer b = {0};
    mm_put(&b, "\"saved\":");
    mm_put(&b, saved);
    mm_put(&b, ",\"schemas\":[");
    int n = 0;
    for (int i = 0; i < nforms; i++)
        if (active_form(i)) {
            if (n++)
                mm_put(&b, ",");
            mm_put(&b, forms[i].schema);
        }
    mm_put(&b, "]");
    if (!b.failed)
        mm_event("load", b.text);
    free(b.text);
    notify("loaded");
}
int modmenu_commit(const char *t, size_t tn, const char *json, size_t n) {
    if (!mm_is_open() || !in_dispatch || !t || strlen(token) != tn || memcmp(t, token, tn) || !json || n > MM_LIMIT ||
        memchr(json, 0, n))
        return 0;
    char *candidate = malloc(n + 1);
    if (!candidate)
        return 0;
    memcpy(candidate, json, n);
    candidate[n] = 0;
    JVal *j = json_parse(candidate, NULL, 0);
    bool valid = j && j->t == J_OBJ;
    json_free(j);
    if (!valid) {
        free(candidate);
        return 0;
    }
    char *old = saved;
    saved = candidate;
    if (!mm_platform_save()) {
        saved = old;
        free(candidate);
        return 0;
    }
    free(old);
    notify_pending = true;
    return 1;
}
void modmenu_save_fields(void *save) {
    if (!save || !saved)
        return;
    // Verified SaveData_JSON vtable: add(c_string, const char*, int), macOS 0x70.
    void (*add)(void *, const char *, const char *, int) = (void *)((void ***)save)[0][GL(0x70, 0x68) / 8];
    add(save, "ddx_mod_options", saved, (int)strlen(saved));
}
void modmenu_restore_fields(void *restore) {
    if (!restore)
        return;
    // Borrowed c_string is copied immediately; vtable slot verified from RestoreData_JSON.
    const char *text = NULL;
    bool (*get)(void *, const char *, const char **) = (void *)((void ***)restore)[0][GL(0x70, 0x80) / 8];
    if (!get(restore, "ddx_mod_options", &text) || !text || strnlen(text, MM_LIMIT + 1) > MM_LIMIT)
        return;
    JVal *j = json_parse(text, NULL, 0);
    if (j && j->t == J_OBJ) {
        free(saved);
        saved = strdup(text);
    }
    json_free(j);
}
void modmenu_install(void) {
    mm_platform_install();
}
