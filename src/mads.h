#ifndef MADS_H
#define MADS_H

#include <stdint.h>

uint16_t unpack(const uint8_t *compressed, uint8_t *uncompressed);
uint16_t font(const uint8_t *font_data, const size_t font_data_size);
uint16_t sprite(const uint8_t *index_data,  const size_t index_data_size,
                const uint8_t *sprite_data, const size_t sprite_data_size, const char *filename);

#endif
