#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

/* palette is 256 six-bit RGB triples, 768 bytes; tgaify writes them
   as the image's color map. Returns 0, or 1 after reporting. */
int tgaify(const uint8_t *data, const size_t width, const size_t height,
           const uint8_t *palette, const char *filename);

#endif
