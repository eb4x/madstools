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

#define MADSPACK_SIG "MADSPACK 2.0\x1a\x00"

/* rows the table always holds on disk; header.length says how many
   of them an archive uses */
#define MADSPACK_RECORDS 16

struct madspack_record {
	uint8_t  codec;   /* 0 = raw;
	                     1 = FAB stream (or raw w/o the FAB magic) */
	uint8_t  stamp;
	uint32_t decoded_size;
	uint32_t encoded_size;
} __attribute__ ((packed));

struct madspack_header {
	char signature[0x0e];
	uint16_t length;
	struct madspack_record member[MADSPACK_RECORDS];
} __attribute__ ((packed));

static_assert(sizeof (struct madspack_header) == 0xb0,
              "MADSPACK header must be 0xb0 bytes");

/*
 * FAB stream codec (src/lzss_fab.cc). Arguments follow zlib: the
 * destination comes first, dest_size carries the capacity of dest in
 * and the delivered size out. Both return 0 or -1, leave dest_size
 * untouched on failure, and never read or write outside the given
 * buffers.
 *
 * mads_fab_decode accepts streams with 10 to 13 distance bits.
 * mads_fab_encode writes the 12-bit form MADS uses, byte-identical to
 * the original packer's output, including the 4-byte header.
 * mads_fab_encode_bound gives a dest capacity that is sufficient for
 * any input, saturating at UINT32_MAX.
 */
int mads_fab_decode(uint8_t *dest, uint32_t *dest_size,
                    const uint8_t *src, uint32_t src_size);
int mads_fab_encode(uint8_t *dest, uint32_t *dest_size,
                  const uint8_t *src, uint32_t src_size);
uint32_t mads_fab_encode_bound(uint32_t src_size);

uint16_t font(const uint8_t *font_data, const size_t font_data_size);
uint16_t sprite(const uint8_t *index_data,  const size_t index_data_size,
                const uint8_t *sprite_data, const size_t sprite_data_size, const char *filename);

#ifdef __cplusplus
}
#endif

#endif
