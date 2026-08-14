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

struct madspack {
	struct madspack_header header;

	uint8_t *decoded[MADSPACK_RECORDS];

	int error_member;  /* failing member, or -1 for the file itself */
};

int madspack_load(struct madspack *mp, const char *path);
void madspack_free(struct madspack *mp);

/*
 * FAB stream codec (src/lzss_fab.cc). Arguments follow zlib: the
 * destination comes first, dest_size carries the capacity of dest in
 * and the delivered size out. Both return 0 or a MADS_ code, leave
 * dest_size untouched on failure, and never read or write outside
 * the given buffers.
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

/* the one pixel value the sprite RLE grammar cannot draw */
#define MADS_RLE_TRANSPARENT 0xfd

int mads_rle_decode(uint8_t *dest, size_t width, size_t height,
                    const uint8_t *src, uint32_t src_size);
int mads_rle_encode(uint8_t *dest, uint32_t *dest_size,
                    const uint8_t *src, size_t width, size_t height);
uint32_t mads_rle_encode_bound(size_t width, size_t height);

/* the full palette form: 256 six-bit RGB triples */
#define MADS_PALETTE_SIZE 768

struct mads_ss_header {
	uint8_t  unk00[0x26];
	uint16_t sprite_table_length;
	uint8_t  unk28[0x6c];
	uint32_t sprites_size;
} __attribute__ ((packed));

static_assert(sizeof (struct mads_ss_header) == 0x98,
              "SS series header must be 0x98 bytes");

struct mads_sprite_record {
	uint32_t offset;
	uint32_t size;
	uint16_t unk08;
	uint16_t unk0a;
	uint16_t width;
	uint16_t height;
} __attribute__ ((packed));

static_assert(sizeof (struct mads_sprite_record) == 16,
              "sprite record must be 16 bytes");

struct mads_ss {
	const struct mads_ss_header *header;

	uint16_t length;
	const struct mads_sprite_record *sprite_table;  /* length records */

	const uint8_t *palette;
	uint32_t       palette_size;

	/* one RLE stream per sprite, the records' offsets indexing it --
	   encoded bytes, not a raster */
	const uint8_t *streams;
	uint32_t       streams_size;
};

int mads_ss_bind(struct mads_ss *ss, const struct madspack *mp);

struct mads_ff_header {
	uint8_t  cell_height;       /* every glyph is this tall */
	uint8_t  max_width;         /* no glyph is wider */
	uint8_t  width[128];
	uint16_t offset[128];
} __attribute__ ((packed));

static_assert(sizeof (struct mads_ff_header) == 0x182,
              "FF header must be 0x182 bytes");

uint16_t font(const uint8_t *font_data, const size_t font_data_size);

/* Failure codes; 0 is success. */
enum {
	MADS_PACK_OPEN = 1,    /* cannot open */
	MADS_PACK_SHORT,       /* file ends inside the header or a member */
	MADS_PACK_SIGNATURE,   /* not a MADSPACK archive */
	MADS_PACK_HEADER,      /* table length past MADSPACK_RECORDS */
	MADS_PACK_CODEC,       /* codec id without a handler */
	MADS_PACK_SIZE,        /* stream size contradicts the header */

	MADS_FAB_TRUNCATED,    /* src ran out */
	MADS_FAB_NOT_FAB,      /* no "FAB" magic */
	MADS_FAB_BAD_DISTANCE, /* distance bits not 0x0a..0x0d */
	MADS_FAB_OVERFLOW,     /* dest too small */
	MADS_FAB_CORRUPT,      /* match before start of dest */

	MADS_RLE_TRUNCATED,    /* src ran out before the last row ended */
	MADS_RLE_OVERFLOW,     /* dest too small */
	MADS_RLE_CORRUPT,      /* a run crosses the row end */

	MADS_SS_LAYOUT,        /* not an SS archive, or a member is missing */
	MADS_SS_HEADER,        /* sprite table contradicts the series header */
	MADS_SS_RECORD,        /* sprite record outside the stream member */
	MADS_SS_PALETTE,       /* palette component above 0x3f */

	MADS_NOMEM,            /* out of memory */
};

const char *mads_strerror(int error);

#ifdef __cplusplus
}
#endif

#endif
