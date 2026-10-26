// Small formula evaluator for the engine config: "-100% + 70% * hp", "(2 + 3 * hp) / 2", and conditions:
// "torch < 25 && has_quirk(\"fear_of_beasts\")". Lowest to highest precedence: || ; && ; < <= > >= == != (one per
// operand pair); + - ; * / ; unary - + ! ; numbers (70% = 0.7), names, name(args), brackets. Comparisons and logic
// give 1 or 0; anything non-zero is true. Function arguments are expressions or "strings".
#include "common.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    double (*lookup)(const char *, bool *, void *);
    double (*call)(const char *, const ExprArg *, int, bool *, void *);
    void *ctx;
    bool ok;
} E;

static double expr_or(E *e);
static void ws(E *e) {
    while (isspace((unsigned char)*e->p))
        e->p++;
}
static bool eat(E *e, const char *tok) {
    ws(e);
    size_t n = strlen(tok);
    if (strncmp(e->p, tok, n))
        return false;
    e->p += n;
    return true;
}

static double call(E *e, const char *name) {
    ExprArg args[EXPR_MAX_ARGS];
    int n = 0;
    ws(e);
    if (*e->p != ')') {
        do {
            if (n == EXPR_MAX_ARGS) {
                e->ok = false;
                return 0;
            }
            ExprArg *a = &args[n++];
            memset(a, 0, sizeof *a);
            ws(e);
            if (*e->p == '"') {
                const char *s = ++e->p;
                while (*e->p && *e->p != '"')
                    e->p++;
                if (*e->p != '"') {
                    e->ok = false;
                    return 0;
                }
                size_t len = (size_t)(e->p - s) < sizeof a->str - 1 ? (size_t)(e->p - s) : sizeof a->str - 1;
                memcpy(a->str, s, len);
                a->is_str = true;
                e->p++;
            } else
                a->num = expr_or(e);
        } while (eat(e, ","));
    }
    if (!eat(e, ")")) {
        e->ok = false;
        return 0;
    }
    if (!e->call) {
        e->ok = false;
        return 0;
    }
    bool ok = true;
    double v = e->call(name, args, n, &ok, e->ctx);
    if (!ok)
        e->ok = false;
    return v;
}

static double factor(E *e) {
    ws(e);
    if (*e->p == '-') {
        e->p++;
        return -factor(e);
    }
    if (*e->p == '+') {
        e->p++;
        return factor(e);
    }
    if (*e->p == '!' && e->p[1] != '=') {
        e->p++;
        return factor(e) == 0 ? 1 : 0;
    }
    if (*e->p == '(') {
        e->p++;
        double v = expr_or(e);
        if (!eat(e, ")"))
            e->ok = false;
        return v;
    }
    if (isdigit((unsigned char)*e->p) || *e->p == '.') {
        char *end;
        double v = strtod(e->p, &end);
        e->p = end;
        ws(e);
        if (*e->p == '%') {
            e->p++;
            v /= 100.0;
        }
        return v;
    }
    if (isalpha((unsigned char)*e->p) || *e->p == '_') {
        char name[32];
        size_t n = 0;
        while ((isalnum((unsigned char)*e->p) || *e->p == '_') && n + 1 < sizeof name)
            name[n++] = *e->p++;
        name[n] = 0;
        ws(e);
        if (*e->p == '(') {
            e->p++;
            return call(e, name);
        }
        if (!e->lookup) {
            e->ok = false;
            return 0;
        }
        bool ok = true;
        double v = e->lookup(name, &ok, e->ctx);
        if (!ok)
            e->ok = false;
        return v;
    }
    e->ok = false;
    return 0;
}

static double term(E *e) {
    double v = factor(e);
    for (;;) {
        ws(e);
        if (*e->p == '*') {
            e->p++;
            v *= factor(e);
        } else if (*e->p == '/') {
            e->p++;
            double d = factor(e);
            v = d != 0 ? v / d : 0;
        } else
            return v;
    }
}

static double sum(E *e) {
    double v = term(e);
    for (;;) {
        ws(e);
        if (*e->p == '+') {
            e->p++;
            v += term(e);
        } else if (*e->p == '-') {
            e->p++;
            v -= term(e);
        } else
            return v;
    }
}

static double compare(E *e) {
    double v = sum(e);
    if (eat(e, "<="))
        return v <= sum(e);
    if (eat(e, ">="))
        return v >= sum(e);
    if (eat(e, "=="))
        return v == sum(e);
    if (eat(e, "!="))
        return v != sum(e);
    if (eat(e, "<"))
        return v < sum(e);
    if (eat(e, ">"))
        return v > sum(e);
    return v;
}

static double expr_and(E *e) {
    double v = compare(e);
    while (eat(e, "&&")) {
        double r = compare(e);
        v = v != 0 && r != 0;
    }
    return v;
}

static double expr_or(E *e) {
    double v = expr_and(e);
    while (eat(e, "||")) {
        double r = expr_and(e);
        v = v != 0 || r != 0;
    }
    return v;
}

bool expr_eval_fn(const char *text, double (*lookup)(const char *, bool *, void *),
                  double (*fn)(const char *, const ExprArg *, int, bool *, void *), void *ctx, double *out) {
    E e = {text, lookup, fn, ctx, true};
    double v = expr_or(&e);
    ws(&e);
    if (*e.p)
        e.ok = false;
    if (!e.ok) {
        logf_("formula error: \"%s\"", text);
        return false;
    }
    *out = v;
    return true;
}

bool expr_eval(const char *text, double (*lookup)(const char *, bool *, void *), void *ctx, double *out) {
    return expr_eval_fn(text, lookup, NULL, ctx, out);
}
