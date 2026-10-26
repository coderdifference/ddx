// Mouse buttons and modifier keys read straight from SDL, below the game's GUI (see common.h, "ui_input.c").
//
// The game's own reads can't be relied on in every panel: GUI_GetIsPrimaryMouseButtonPressed reads the GUI's input
// state, which a panel's controls take as they draw (the stash's cells do: after them it no longer reads the press),
// and Input_IsMouseButtonDown never reads down while the town GUI has the input. SDL itself always knows: the game
// links SDL2.framework dynamically, so SDL_GetMouseState (button mask: bit 0 left, 1 middle, 2 right) and
// SDL_GetModState (KMOD_SHIFT 0x3, KMOD_CTRL 0xc0, KMOD_ALT 0x300, KMOD_GUI 0xc00) are found with dlsym. Positions are
// GUI_GetMousePosition(vec2&), the GUI's coordinates. On macOS SDL turns Ctrl + left click into a right click (seen:
// button mask 4 with KMOD_LCTRL on every Ctrl-click), hence ui_ctrl_pressed / ui_ctrl_click taking either button.
//
// The wheel is the game's own (Input_GetMouseWheelDelta: g_mousewheeldelta x / y, summed from SDL's wheel events and
// cleared each frame in Input_UpdateEnd), read at the same sample. ui_mouse_local maps the mouse into the current
// transform as GUI_MouseOver does (Input_GetMousePosition, then Transform_CanvasToLocal).
// On Windows SDL's functions are SDL2.dll's exports, and the game's, inlined there, are stand-ins (syms_win.c).
//
// Sampled once a frame, at the frame's first use (the first ui_mouse / ui_modifiers / ui_mouse_pos call after the
// previous frame ended; ui_on_frame_end counts the frames): a press or release is reported in the frame it happened
// in, with the position and modifiers at that moment.
#include "common.h"
#include <math.h>
#include <string.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

#define BUTTONS 3

static uint32_t (*g_sdl_mouse)(int *x, int *y);
static unsigned (*g_sdl_mods)(void);
static void (*g_gui_mouse)(Vec2 *out);
static UiMouseButton g_buttons[BUTTONS];
static unsigned g_mods;
static Vec2 g_pos;
static float g_wheel;
static bool (*g_wheel_delta)(float *x, float *y);
static bool (*g_input_mouse)(float *x, float *y);
static Vec2 (*g_canvas_to_local)(const float *xyz);
static bool (*g_point_clipped)(Vec2 p);   // GUI_IsPointClipped (View_IsPointClipped: the active clip)
static unsigned g_frame, g_sampled = ~0u; // the frame count, and the frame last sampled

static void frame_end(void) {
    g_frame++;
}
unsigned ui_frame(void) {
    return g_frame;
}
static void sample(void);
static void ensure(void) {
    if (g_sampled != g_frame) {
        g_sampled = g_frame;
        sample();
    }
}

static unsigned mods_now(void) {
    unsigned m = g_sdl_mods ? g_sdl_mods() : 0, r = 0;
    if (m & 0x0003)
        r |= UI_MOD_SHIFT;
    if (m & 0x00c0)
        r |= UI_MOD_CTRL;
    if (m & 0x0300)
        r |= UI_MOD_ALT;
    if (m & 0x0c00)
        r |= UI_MOD_CMD;
    return r;
}

static void sample(void) {
    if (!g_sdl_mouse)
        return;
    uint32_t mask = g_sdl_mouse(NULL, NULL);
    static const uint32_t bit[BUTTONS] = {1u << 0, 1u << 2, 1u << 1}; // left, right, middle (SDL_BUTTON(1, 3, 2))
    g_mods = mods_now();
    if (g_gui_mouse)
        g_gui_mouse(&g_pos);
    float wx = 0, wy = 0;
    if (g_wheel_delta)
        g_wheel_delta(&wx, &wy);
    g_wheel = wy;
    for (int i = 0; i < BUTTONS; i++) {
        UiMouseButton *b = &g_buttons[i];
        bool down = (mask & bit[i]) != 0;
        b->pressed = down && !b->down;
        b->released = !down && b->down;
        if (b->pressed) {
            b->press_pos = g_pos;
            b->press_mods = g_mods;
        }
        if (b->released) {
            b->release_pos = g_pos;
            b->release_mods = g_mods;
        }
        b->down = down;
    }
}

const UiMouseButton *ui_mouse(int button) {
    static const UiMouseButton none;
    ensure();
    return button >= 0 && button < BUTTONS ? &g_buttons[button] : &none;
}

Vec2 ui_mouse_pos(void) {
    ensure();
    return g_pos;
}
unsigned ui_modifiers(void) {
    ensure();
    return g_mods;
}
float ui_mouse_wheel(void) {
    ensure();
    return g_wheel;
}
float ui_mouse_wheel_take(void) {
    ensure();
    float w = g_wheel;
    g_wheel = 0;
    return w;
}

Vec2 ui_mouse_local(void) {
    float xyz[3] = {0, 0, 0};
    if (!g_input_mouse || !g_canvas_to_local)
        return (Vec2){-1e9f, -1e9f};
    g_input_mouse(&xyz[0], &xyz[1]);
    return g_canvas_to_local(xyz);
}

bool ui_mouse_in(Vec2 pos, Vec2 size) {
    Vec2 m = ui_mouse_local();
    if (g_point_clipped && g_point_clipped(m))
        return false; // outside the active clip, as GUI_MouseOver
    return m.x >= pos.x && m.y >= pos.y && m.x < pos.x + size.x && m.y < pos.y + size.y;
}

bool ui_click(int button, float slop) {
    const UiMouseButton *b = ui_mouse(button);
    return b->released && fabsf(b->release_pos.x - b->press_pos.x) <= slop &&
           fabsf(b->release_pos.y - b->press_pos.y) <= slop;
}

int ui_ctrl_pressed(void) {
    for (int b = UI_MOUSE_LEFT; b <= UI_MOUSE_RIGHT; b++) {
        const UiMouseButton *m = ui_mouse(b);
        if (m->pressed && (m->press_mods & UI_MOD_CTRL))
            return b;
    }
    return -1;
}

int ui_ctrl_click(float slop) {
    for (int b = UI_MOUSE_LEFT; b <= UI_MOUSE_RIGHT; b++) {
        const UiMouseButton *m = ui_mouse(b);
        if (ui_click(b, slop) && (m->press_mods & UI_MOD_CTRL) && (m->release_mods & UI_MOD_CTRL))
            return b;
    }
    return -1;
}

void ui_input_install(void) {
#ifdef _WIN32
    g_sdl_mouse = (uint32_t (*)(int *, int *))sym("SDL_GetMouseState"); // SDL2.dll's exports
    g_point_clipped = (bool (*)(Vec2))sym("_Z18GUI_IsPointClipped4vec2");
    g_sdl_mods = (unsigned (*)(void))sym("SDL_GetModState");
#else
    g_sdl_mouse = (uint32_t (*)(int *, int *))dlsym(RTLD_DEFAULT, "SDL_GetMouseState");
    g_point_clipped = (bool (*)(Vec2))sym("_Z18GUI_IsPointClipped4vec2");
    g_sdl_mods = (unsigned (*)(void))dlsym(RTLD_DEFAULT, "SDL_GetModState");
#endif
    g_gui_mouse = (void (*)(Vec2 *))sym("_Z20GUI_GetMousePositionR4vec2");
    g_wheel_delta = (bool (*)(float *, float *))sym("_Z24Input_GetMouseWheelDeltaRfS_");
    g_input_mouse = (bool (*)(float *, float *))sym("_Z22Input_GetMousePositionRfS_");
    g_canvas_to_local = (Vec2 (*)(const float *))sym("_Z23Transform_CanvasToLocalRK4vec3");
    if (!g_sdl_mouse || !g_sdl_mods) {
        logf_("ui_input: SDL not found, no mouse input");
        g_sdl_mouse = NULL;
        return;
    }
    ui_on_frame_end(frame_end);
}
