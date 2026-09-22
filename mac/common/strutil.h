#ifndef MAC_STRUTIL_H
#define MAC_STRUTIL_H

#include <string.h>

/* Convert C string to Pascal Str255 in place buffer. */
static void c_to_pstr(unsigned char *pstr, const char *cstr) {
    unsigned int n = 0;
    while (cstr[n] && n < 255) {
        pstr[n + 1] = (unsigned char)cstr[n];
        n++;
    }
    pstr[0] = (unsigned char)n;
}

#endif
