#ifndef DDX_MODMENU_INTERNAL_H
#define DDX_MODMENU_INTERNAL_H
#include "common.h"
#define MM_LIMIT (1024u * 1024u)
typedef struct {
    char *text;
    size_t len, cap;
    bool failed;
} MmBuffer;
void mm_put(MmBuffer *b, const char *s);
void mm_quote(MmBuffer *b, const char *s);
void mm_json(MmBuffer *b, const JVal *v);
char *mm_stringify(const JVal *v);
const char *mm_str(const JVal *v, const char *key);
const JVal *mm_view(void);
const char *mm_last_error(void);
void mm_event(const char *event, const char *extra);
bool mm_available(void);
void mm_set_open(bool value);
bool mm_is_open(void);
void mm_draw(void);
void mm_blur(void);
void mm_reset_draw(void);
bool modmenu_closed_this_frame(void);
bool mm_text_ready(void);
void mm_text_begin(void);
void mm_text_end(void);
unsigned mm_text_take(char *, unsigned);
bool mm_platform_ready(void);
void mm_platform_install(void);
bool mm_platform_save(void);
void modmenu_save_fields(void *save);
void modmenu_restore_fields(void *restore);
void modmenu_reset(void);
void modmenu_loaded(void);
const char *modmenu_saved_namespace(void);
#endif
