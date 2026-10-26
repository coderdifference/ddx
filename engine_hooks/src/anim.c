// Animation variants from hero data (config "animation_variants").
// Actor::SetSpriteAnimation(anim, file_pattern, ..) plays <anim>_<class suffix>, else <anim>_<mode id> if
// the sprite has it, else <anim>; file_pattern alone picks the skel file. For a hero whose class has
// variants, we try <anim>_<variant> for each template in order and, if the sprite has that animation, pass
// that name instead: the original then finds no mode suffix and plays it from the same file. The game
// re-applies the stance animation after every action, so a data change shows at the end of the action.
#include "common.h"
#include <stdio.h>

typedef void (*set_sprite_animation_fn)(void *actor, const char *anim, const char *file_pattern, bool b1, bool b2);
typedef bool (*has_animation_fn)(const void *sprite, const char *name);
static set_sprite_animation_fn g_orig;
static has_animation_fn g_has_animation;

static void hook(void *actor, const char *anim, const char *file_pattern, bool b1, bool b2) {
    const ClassConfig *c = anim ? config_for_hero(actor) : NULL;
    const void *sprite = actor ? *(void *const *)((const uint8_t *)actor + ACTOR_SPRITE) : NULL;
    if (c && sprite && g_has_animation) {
        char variant[64], name[160];
        for (int i = 0; config_animation_variant(c, actor, i, variant, sizeof variant); i++) {
            snprintf(name, sizeof name, "%s_%s", anim, variant);
            if (g_has_animation(sprite, name)) {
                dbgf_("anim hero %u: %s (file %s) -> %s", hero_guid(actor), anim, file_pattern ? file_pattern : "-",
                      name);
                g_orig(actor, name, file_pattern, b1, b2);
                return;
            }
            dbgf_("anim hero %u: %s (file %s): no %s on the current sprite", hero_guid(actor), anim,
                  file_pattern ? file_pattern : "-", name);
        }
    }
    g_orig(actor, anim, file_pattern, b1, b2);
}

void anim_install(void) {
    g_has_animation = (has_animation_fn)sym("_ZNK11SpineSprite12HasAnimationEPKc");
    static const uint8_t prologue[] = PROLOGUE_SUB32(0x98, 0x00); // sub rsp,0x98
    g_orig = (set_sprite_animation_fn)install_hook("Actor::SetSpriteAnimation",
                                                   sym("_ZN5Actor18SetSpriteAnimationEPKcS1_bb"), (void *)hook,
                                                   prologue, sizeof prologue);
}
