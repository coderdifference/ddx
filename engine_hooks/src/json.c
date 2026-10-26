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

static bool hex4(P *s, unsigned *out) {
    unsigned n = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)*s->p;
        int v = c >= '0' && c <= '9'   ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                       : -1;
        if (v < 0)
            return false;
        s->p++;
        n = (n << 4) | (unsigned)v;
    }
    *out = n;
    return true;
}
static char *parse_string(P *s) {
    if (*s->p != '"') {
        fail(s, "expected string");
        return NULL;
    }
    s->p++;
    size_t cap = 32, n = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;
    while (*s->p && *s->p != '"') {
        unsigned char c = (unsigned char)*s->p++;
        char bytes[4];
        size_t count = 1;
        bytes[0] = (char)c;
        if (c < 32)
            goto invalid;
        if (c == 92) {
            char e = *s->p;
            if (!e)
                goto invalid;
            s->p++;
            switch (e) {
            case 'n':
                bytes[0] = '\n';
                break;
            case 't':
                bytes[0] = '\t';
                break;
            case 'r':
                bytes[0] = '\r';
                break;
            case 'b':
                bytes[0] = '\b';
                break;
            case 'f':
                bytes[0] = '\f';
                break;
            case 92:
            case '/':
            case '"':
                bytes[0] = e;
                break;
            case 'u': {
                unsigned cp;
                if (!hex4(s, &cp))
                    goto invalid;
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (s->p[0] != 92 || s->p[1] != 'u')
                        goto invalid;
                    s->p += 2;
                    unsigned low;
                    if (!hex4(s, &low) || low < 0xdc00 || low > 0xdfff)
                        goto invalid;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    goto invalid;
                // JVal stores C strings: embedded NUL cannot be represented safely.
                if (!cp)
                    goto invalid;
                if (cp < 0x80)
                    bytes[0] = (char)cp;
                else if (cp < 0x800) {
                    count = 2;
                    bytes[0] = (char)(0xc0 | (cp >> 6));
                    bytes[1] = (char)(0x80 | (cp & 63));
                } else if (cp < 0x10000) {
                    count = 3;
                    bytes[0] = (char)(0xe0 | (cp >> 12));
                    bytes[1] = (char)(0x80 | ((cp >> 6) & 63));
                    bytes[2] = (char)(0x80 | (cp & 63));
                } else {
                    count = 4;
                    bytes[0] = (char)(0xf0 | (cp >> 18));
                    bytes[1] = (char)(0x80 | ((cp >> 12) & 63));
                    bytes[2] = (char)(0x80 | ((cp >> 6) & 63));
                    bytes[3] = (char)(0x80 | (cp & 63));
                }
                break;
            }
            default:
                goto invalid;
            }
        }
        if (n + count + 1 > cap) {
            size_t next = cap * 2;
            char *p = realloc(buf, next);
            if (!p) {
                free(buf);
                return NULL;
            }
            buf = p;
            cap = next;
        }
        memcpy(buf + n, bytes, count);
        n += count;
    }
    if (*s->p != '"')
        goto invalid;
    s->p++;
    buf[n] = 0;
    return buf;
invalid:
    fail(s, "invalid JSON string");
    free(buf);
    return NULL;
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
