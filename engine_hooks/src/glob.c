// glob_match: the library's glob patterns (buff, quirk, item, class and act-out ids in configs). macOS: fnmatch with no
// flags; Windows (mingw has no fnmatch): the same for * and ?.
#include "common.h"
#ifdef _WIN32
static bool match(const char *p, const char *s) {
    if (!*p)
        return !*s;
    if (*p == '*') {
        for (const char *t = s;; t++) {
            if (match(p + 1, t))
                return true;
            if (!*t)
                return false;
        }
    }
    if (!*s || (*p != '?' && *p != *s))
        return false;
    return match(p + 1, s + 1);
}
bool glob_match(const char *pattern, const char *s) {
    return match(pattern, s);
}
#else
#include <fnmatch.h>
bool glob_match(const char *pattern, const char *s) {
    return fnmatch(pattern, s, 0) == 0;
}
#endif
