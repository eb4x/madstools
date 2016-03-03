#ifndef _UTIL_H
#define _UTIL_H

void bmpify(const uint8_t *data, const size_t px, const size_t py, const char *filename);
void tgaify(const uint8_t *data, const size_t px, const size_t py, const char *filename);
void dump(const uint8_t *data, const size_t size, const char *filename);

char *strlwr(char *str);

#endif
