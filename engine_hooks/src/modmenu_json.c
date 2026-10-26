#include "modmenu_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
void mm_put(MmBuffer *b, const char *s) {
    if (b->failed || !s)
        return;
    size_t n = strlen(s);
    if (n > MM_LIMIT * 2 || b->len + n > MM_LIMIT * 2) {
        b->failed = true;
        return;
    }
    if (b->len + n + 1 > b->cap) {
        size_t cap = (b->len + n + 1) * 2;
        char *p = realloc(b->text, cap);
        if (!p) {
            b->failed = true;
            return;
        }
        b->text = p;
        b->cap = cap;
    }
    memcpy(b->text + b->len, s, n + 1);
    b->len += n;
}
void mm_quote(MmBuffer *b, const char *s) {
    mm_put(b, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        char c[8] = {(char)*p, 0};
        if (*p == '"' || *p == '\\') {
            c[0] = '\\';
            c[1] = (char)*p;
            c[2] = 0;
        } else if (*p < 32)
            snprintf(c, sizeof c, "\\u%04x", *p);
        mm_put(b, c);
    }
    mm_put(b, "\"");
}
void mm_json(MmBuffer *b, const JVal *v) {
    if (!v || v->t == J_NULL) {
        mm_put(b, "null");
        return;
    }
    if (v->t == J_STR) {
        mm_quote(b, v->str);
        return;
    }
    if (v->t == J_BOOL) {
        mm_put(b, v->b ? "true" : "false");
        return;
    }
    if (v->t == J_NUM) {
        char n[40];
        snprintf(n, sizeof n, "%.17g", v->num);
        mm_put(b, isfinite(v->num) ? n : "null");
        return;
    }
    mm_put(b, v->t == J_OBJ ? "{" : "[");
    for (int i = 0; i < v->n; i++) {
        if (i)
            mm_put(b, ",");
        if (v->t == J_OBJ) {
            mm_quote(b, v->keys[i]);
            mm_put(b, ":");
        }
        mm_json(b, v->items + i);
    }
    mm_put(b, v->t == J_OBJ ? "}" : "]");
}
char *mm_stringify(const JVal *v) {
    MmBuffer b = {0};
    mm_json(&b, v);
    if (b.failed) {
        free(b.text);
        return NULL;
    }
    return b.text;
}
const char *mm_str(const JVal *v, const char *key) {
    const JVal *s = json_get(v, key);
    return s && s->t == J_STR ? s->str : "";
}
