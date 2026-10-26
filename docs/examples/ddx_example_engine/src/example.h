// DDX Example: a minimal native client of Darkest Dungeon Extender (the library: engine_hooks/src). Copy this folder
// and DDXExample/ to start a mod of your own (docs/13-native-clients.md walks through every line).
#pragma once
#include "common.h"

// example_kills.c: hook point listeners. Counts hero kills per hero (saved with the hero) and per campaign (saved
// with the campaign), shows a popup on a crit, and softens stress damage while the torch is bright.
void example_kills_install(void);
int example_campaign_kills(void);

// example_overlay.c: a line of text on the expedition screen, drawn from a wrapped game function.
void example_overlay_install(void);
