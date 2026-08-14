#ifndef MADS_H
#define MADS_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MADSCONCAT_SIG "MADSCONCAT 1.0\x1a\x00"

struct madsconcat_header {
	char signature[0x10];
	uint16_t length;
} __attribute__ ((packed));

static_assert(sizeof (struct madsconcat_header) == 0x12,
              "MADSCONCAT header must be 0x12 bytes");

struct madsconcat_record {
	uint32_t offset;
	uint32_t size;
	char name[0x0e];
} __attribute__ ((packed));

static_assert(sizeof (struct madsconcat_record) == 0x16,
              "MADSCONCAT record must be 0x16 bytes");

uint16_t unpack(const uint8_t *compressed, uint8_t *uncompressed);
uint16_t font(const uint8_t *font_data, const size_t font_data_size);
uint16_t sprite(const uint8_t *index_data,  const size_t index_data_size,
                const uint8_t *sprite_data, const size_t sprite_data_size, const char *filename);

#ifdef __cplusplus
}
#endif

#endif
