#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

/* data is width x height pixels, one byte each, rows unpadded;
   palette is 256 six-bit RGB triples, 768 bytes, written as the
   image's color map, the transparent index (or -1 for none) with
   alpha 0. Returns 0, or 1 after reporting. */
int tgaify(const uint8_t *data, const size_t width, const size_t height,
           const uint8_t *palette, int transparent, const char *filename);

#endif
