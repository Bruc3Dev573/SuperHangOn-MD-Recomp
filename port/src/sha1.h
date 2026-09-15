#ifndef SHA1_H
#define SHA1_H
#include <stddef.h>
#include <stdint.h>

/* writes the lowercase hex digest (40 characters + NUL) */
void sha1(const uint8_t *data, size_t len, char hex[41]);

#endif
