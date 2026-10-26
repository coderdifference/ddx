// Focused popup description checks; unused game entry points are stripped by the linker.
#include "../src/popups.c"
#include <assert.h>

bool loc_exists(const char *id) { return false; }
const char *ui_loc(const char *id) { return id; }
static const char *scope;
const char *buff_origin_scope_popup(const void *actor) { return actor ? scope : NULL; }
int mods_list_order(ModsList *list, bool top) { return 0; }

static uint8_t entry[BUFF_ENTRY_SIZE];
static uint8_t *instances[] = {entry + 8};
static PtrVec tray(const void *overlay, const void *actor, bool positive) {
    return (PtrVec){(uint8_t *)instances, (uint8_t *)(instances + 1), NULL};
}
static PtrVec ripostes(void *overlay, void *actor) { return tray(overlay, actor, true); }
const char *buff_origin_popup(const void *actor, unsigned guid) { return NULL; }
bool buff_origin_label(const void *actor, const void *buff, char *out, size_t cap) {
    snprintf(out, cap, "{colour_start|harmful}Tempting Goblet{colour_end} (Bone Courtier)");
    return true;
}
void combat_logf(const char *fmt, ...) { }

int main(void) {
    PopupLine line;
    PopupEvent p = {.actor = (void *)1, .text = "Resist!", .number = 0};
    assert(!popup_line(NULL, &line));
    assert(!popup_line(&p, NULL));
    assert(!popup_line(&p, &line)); // no guessed source outside a live effect
    scope = "Tempting Goblet (Bone Courtier)";
    assert(popup_line(&p, &line));
    assert(!strcmp(line.source, scope));
    assert(!strcmp(line.detail, "Resist!"));
    p.number = 7;
    assert(popup_line(&p, &line));
    assert(!strcmp(line.detail, "Resist! 7"));
    p.named = true;
    assert(!popup_line(&p, &line)); // keep the client's wording
    g_tray_buffs = tray;
    g_tray_ripostes = ripostes;
    *(unsigned *)entry = 42;
    *(int *)(entry + 8 + BUFF_DURATION) = 3;
    *(int *)(entry + 8 + BUFF_DURATION_TYPE) = DURATION_AFTER_TURN;
    popups_buff_added(p.actor, 42);
    int rounds, dtype;
    const void *by;
    const char *text = icon_text(NULL, (void *)p.actor, ICON_DEBUFFS, &rounds, &dtype, &by);
    assert(text && !strcmp(text, "Tempting Goblet (Bone Courtier)"));
    assert(by == entry + 8 && rounds == 3 && dtype == DURATION_AFTER_TURN);
    assert(g_nnew == 0);
    assert(!icon_text(NULL, (void *)p.actor, ICON_DEBUFFS, &rounds, &dtype, &by));
    puts("popup sources: all passed");
}
