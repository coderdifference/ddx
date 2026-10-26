// call_site_return: checks a call instruction inside a known function (see common.h).
#include "common.h"
#include <string.h>

const uint8_t *call_site_return(const void *fn, unsigned off, const void *target) {
    if (!fn)
        return NULL;
    const uint8_t *p = (const uint8_t *)fn + off;
    if (p[0] != 0xE8)
        return NULL;
    int32_t rel;
    memcpy(&rel, p + 1, 4);
    if (target && p + 5 + rel != (const uint8_t *)target)
        return NULL;
    return p + 5;
}
