// Split an existing NameFrame's icon and text at a validated caller-selected call site.
#include "common.h"
#include <string.h>
#include <stdlib.h>

typedef bool (*NameFramePlacement)(Vec2 *offset);
static struct {
    const void *caller;
    char *title, *blank_title, *texture;
    NameFramePlacement placement;
} entries[8];
static unsigned count;
static const char *blank_from, *blank_to;
static void (*next_frame)(Vec2, const void *, const char *, float *, const char *, const void *, void *, void *, void *,
                          void *, uint64_t, const void *);
static const char *redirect(const char *id) {
    return blank_from && !strcmp(id, blank_from) ? blank_to : NULL;
}
static void frame(Vec2 pos, const void *icon, const char *name, float *width, const char *sub, const void *icon2,
                  void *fn, void *s1, void *s2, void *preview, uint64_t id, const void *elem) {
    const void *caller = __builtin_return_address(0);
    for (unsigned i = 0; i < count; i++) {
        Vec2 offset = {0};
        uint8_t blank[16];
        if (caller != entries[i].caller || !entries[i].placement(&offset) || !ui_texture(entries[i].texture, blank))
            continue;
        const char *old_from = blank_from, *old_to = blank_to;
        blank_from = entries[i].title;
        blank_to = entries[i].blank_title;
#ifdef _WIN32
        uint8_t second_icon[16], blank_copy[16];
        _Alignas(16) StdFunction function;
        ui_texture_copy(second_icon, icon2);
        ui_function_copy(&function, fn);
        next_frame((Vec2){pos.x + offset.x, pos.y + offset.y}, icon, name, width, sub, second_icon, &function, s1, s2,
                   preview, id, elem);
        blank_from = old_from;
        blank_to = old_to;
        ui_texture_copy(blank_copy, blank);
        next_frame(pos, blank_copy, name, width, sub, icon2, fn, s1, s2, preview, id, elem);
#else
        next_frame((Vec2){pos.x + offset.x, pos.y + offset.y}, icon, name, width, sub, icon2, fn, s1, s2, preview, id,
                   elem);
        blank_from = old_from;
        blank_to = old_to;
        next_frame(pos, blank, name, width, sub, icon2, fn, s1, s2, preview, id, elem);
#endif
        return;
    }
    next_frame(pos, icon, name, width, sub, icon2, fn, s1, s2, preview, id, elem);
}
bool ui_name_frame_split(const void *caller, const char *title, const char *blank_title, const char *texture,
                         NameFramePlacement placement) {
    if (!caller || !title || !blank_title || !texture || !placement || count == 8)
        return false;
    if (!next_frame) {
        if (!game_hook_wrap("UI::Display::NameFrame", (void *)frame, (void **)&next_frame))
            return false;
        loc_on_redirect(redirect);
    }
    char *a = strdup(title), *b = strdup(blank_title), *c = strdup(texture);
    if (!a || !b || !c) {
        free(a);
        free(b);
        free(c);
        return false;
    }
    entries[count].caller = caller;
    entries[count].title = a;
    entries[count].blank_title = b;
    entries[count].texture = c;
    entries[count++].placement = placement;
    return true;
}
