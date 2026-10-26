// Small JSON reader for the engine config (objects, arrays, strings, numbers, true/false/null).
#include "common.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    char *err;
    size_t errlen;
} P;

static void fail(P *s, const char *msg) {
    if (s->err && !s->err[0])
        snprintf(s->err, s->errlen, "%s near '%.20s'", msg, s->p);
}
static void ws(P *s) {
    while (*s->p && isspace((unsigned char)*s->p))
        s->p++;
}
static bool parse_value(P *s, JVal *out);

static char *parse_string(P *s) {
    if (*s->p != '"') {
        fail(s, "expected string");
        return NULL;
    }
    s->p++;
    size_t cap = 32, n = 0;
    char *buf = malloc(cap);
    while (*s->p && *s->p != '"') {
        char c = *s->p++;
        if (c == '\\') {
            char e = *s->p++;
            c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e; // \uXXXX not needed here
        }
        if (n + 2 > cap)
            buf = realloc(buf, cap *= 2);
        buf[n++] = c;
    }
    if (*s->p != '"') {
        fail(s, "unterminated string");
        free(buf);
        return NULL;
    }
    s->p++;
    buf[n] = 0;
    return buf;
}

static bool parse_value(P *s, JVal *out) {
    memset(out, 0, sizeof *out);
    ws(s);
    char c = *s->p;
    if (c == '{') {
        s->p++;
        out->t = J_OBJ;
        ws(s);
        if (*s->p == '}') {
            s->p++;
            return true;
        }
        for (;;) {
            ws(s);
            char *k = parse_string(s);
            if (!k)
                return false;
            ws(s);
            if (*s->p != ':') {
                fail(s, "expected ':'");
                free(k);
                return false;
            }
            s->p++;
            out->keys = realloc(out->keys, sizeof(char *) * (out->n + 1));
            out->items = realloc(out->items, sizeof(JVal) * (out->n + 1));
            out->keys[out->n] = k;
            if (!parse_value(s, &out->items[out->n])) {
                out->n++;
                return false;
            }
            out->n++;
            ws(s);
            if (*s->p == ',') {
                s->p++;
                continue;
            }
            if (*s->p == '}') {
                s->p++;
                return true;
            }
            fail(s, "expected ',' or '}'");
            return false;
        }
    }
    if (c == '[') {
        s->p++;
        out->t = J_ARR;
        ws(s);
        if (*s->p == ']') {
            s->p++;
            return true;
        }
        for (;;) {
            out->items = realloc(out->items, sizeof(JVal) * (out->n + 1));
            if (!parse_value(s, &out->items[out->n])) {
                out->n++;
                return false;
            }
            out->n++;
            ws(s);
            if (*s->p == ',') {
                s->p++;
                continue;
            }
            if (*s->p == ']') {
                s->p++;
                return true;
            }
            fail(s, "expected ',' or ']'");
            return false;
        }
    }
    if (c == '"') {
        out->t = J_STR;
        out->str = parse_string(s);
        return out->str != NULL;
    }
    if (!strncmp(s->p, "true", 4)) {
        s->p += 4;
        out->t = J_BOOL;
        out->b = true;
        return true;
    }
    if (!strncmp(s->p, "false", 5)) {
        s->p += 5;
        out->t = J_BOOL;
        return true;
    }
    if (!strncmp(s->p, "null", 4)) {
        s->p += 4;
        out->t = J_NULL;
        return true;
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        char *end;
        out->t = J_NUM;
        out->num = strtod(s->p, &end);
        s->p = end;
        return true;
    }
    fail(s, "unexpected character");
    return false;
}

static void free_inner(JVal *v) {
    if (!v)
        return;
    free(v->str);
    for (int i = 0; i < v->n; i++) {
        free_inner(&v->items[i]);
        if (v->keys)
            free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
}

void json_free(JVal *v) {
    free_inner(v);
    free(v);
}

JVal *json_parse(const char *text, char *err, size_t errlen) {
    if (err && errlen)
        err[0] = 0;
    P s = {text, err, errlen};
    JVal *v = calloc(1, sizeof *v);
    if (!parse_value(&s, v)) {
        json_free(v);
        return NULL;
    }
    ws(&s);
    if (*s.p) {
        fail(&s, "trailing characters");
        json_free(v);
        return NULL;
    }
    return v;
}

const JVal *json_get(const JVal *obj, const char *key) {
    if (!obj || obj->t != J_OBJ)
        return NULL;
    for (int i = 0; i < obj->n; i++)
        if (!strcmp(obj->keys[i], key))
            return &obj->items[i];
    return NULL;
}
