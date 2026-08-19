#ifndef MADS_H
#define MADS_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MADSCONCAT_SIG "MADSCONCAT 1.0\x1a\x00"

struct madsconcat_record {
	uint32_t offset;
	uint32_t size;
	char name[0x0e];
} __attribute__ ((packed));

static_assert(sizeof (struct madsconcat_record) == 0x16,
              "MADSCONCAT record must be 0x16 bytes");

struct madsconcat_header {
	char signature[0x10];
	uint16_t length;
	struct madsconcat_record file[];
} __attribute__ ((packed));

static_assert(sizeof (struct madsconcat_header) == 0x12,
              "MADSCONCAT header must be 0x12 bytes");

struct madsconcat {
	const struct madsconcat_header *header;

	const uint8_t *data;    /* the whole container; offsets index it */
	size_t         size;
};

int madsconcat_bind(struct madsconcat *mc, const uint8_t *data, size_t size);

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

/* An 8-bit surface to draw over: h rows of pitch bytes each, the
   leftmost w of them pixels. pitch >= w. */
struct mads_surface {
	size_t w, h;
	size_t pitch;
	uint8_t *pixels;
};

/* Draw one stream over dst at (x, y), clipped; transparent pixels
   leave dst untouched. Validated as mads_rle_decode does, clipped or
   not. */
int mads_rle_draw(const struct mads_surface *dst, int x, int y,
                  size_t width, size_t height,
                  const uint8_t *src, uint32_t src_size);
int mads_rle_decode(uint8_t *dest, size_t width, size_t height,
                    const uint8_t *src, uint32_t src_size);
int mads_rle_encode(uint8_t *dest, uint32_t *dest_size,
                    const uint8_t *src, size_t width, size_t height);
uint32_t mads_rle_encode_bound(size_t width, size_t height);

/* the full palette form: 256 six-bit RGB triples */
#define MADS_PALETTE_SIZE 768

/* 0xfc..0xff are RLE codes, never pixels, so a palette header has at
   most this many reachable records; the game keeps every picture's
   palette slots below it too, not only sprites' */
#define MADS_PALETTE_LENGTH_MAX 252

struct mads_palette_record {
	uint8_t r, g, b;
	uint8_t unk03[3];
} __attribute__ ((packed));

static_assert(sizeof (struct mads_palette_record) == 6,
              "palette record must be 6 bytes");

/* the sparse palette form */
struct mads_palette_header {
	uint16_t length;
	struct mads_palette_record color[];
} __attribute__ ((packed));

static_assert(sizeof (struct mads_palette_header) == 2,
              "palette header must be 2 bytes");

/* A bound palette of either form: color v is r, g, b at
   rgb[v * stride], for v below length. */
struct mads_palette {
	uint16_t       length;   /* 256 for the full form; 0 is none */
	const uint8_t *rgb;
	uint32_t       stride;   /* 3 for the full form, 6 for a header's records */
};

/* Bind the size bytes at data as a palette member of either form, or
   none (size 0); records past a header's length are not read. The
   binders pass on its code. */
int mads_palette_bind(struct mads_palette *palette,
                      const uint8_t *data, uint32_t size);

/* The full form of a bound palette: triple v from color v for its
   length, the rest black. */
void mads_palette_full(uint8_t dst[MADS_PALETTE_SIZE],
                       const struct mads_palette *palette);

/* A six-bit VGA component in eight bits: the DAC's 0x3f is full
   scale, so round(c * 255 / 63) -- not c << 2, which tops out at
   252. The multiply and shift are DOSBox's rgb6_to_8 and match the
   division over the whole six-bit range. */
static inline uint8_t mads_rgb6_to_8(uint8_t c) {
	return (c * 259 + 33) >> 6;
}

/* The full palette form in eight bits: every component of the
   MADS_PALETTE_SIZE bytes at src through mads_rgb6_to_8 into dst.
   dst may be src; a bound palette is the archive's, so expand it with
   mads_palette_full first. */
void mads_palette_scale(uint8_t *dst, const uint8_t *src);

struct mads_pik_header {
	uint16_t height;
	uint16_t width;
	uint16_t unk04;
	uint16_t unk06;
} __attribute__ ((packed));

static_assert(sizeof (struct mads_pik_header) == 8,
              "PIK picture header must be 8 bytes");

struct mads_pik {
	const struct mads_pik_header *header;

	const uint8_t *pixels;
	uint32_t       pixels_size;

	struct mads_palette palette;
};

int mads_pik_bind(struct mads_pik *pik, const struct madspack *mp);

struct mads_ss_header {
	uint8_t  packed;  /* nonzero: every stream in member 3 is encoded on
	                     its own with codec, and size is the decoded
	                     length. mads_ss_bind decodes them and clears it */
	uint8_t  codec;   /* as madspack_record.codec; 1 in every series seen */
	uint8_t  unk02[0x24];
	uint16_t sprite_table_length;
	uint8_t  unk28[0x6c];
	uint32_t sprites_size;
} __attribute__ ((packed));

static_assert(sizeof (struct mads_ss_header) == 0x98,
              "SS series header must be 0x98 bytes");

struct mads_sprite_record {
	uint32_t offset;  /* into the stream member. In a packed series
	                     the disk offset is the FAB stream's; once bound,
	                     it is the decoded stream's */
	uint32_t size;    /* of the RLE stream, decoded */
	int16_t  x, y;    /* the frame's authored screen position, as its
	                     bottom-centre: drawn at (x - width / 2,
	                     y - height + 1) */
	uint16_t width;
	uint16_t height;
} __attribute__ ((packed));

static_assert(sizeof (struct mads_sprite_record) == 16,
              "sprite record must be 16 bytes");

struct mads_ss {
	const struct mads_ss_header *header;

	uint16_t length;
	const struct mads_sprite_record *sprite_table;  /* length records */

	struct mads_palette palette;

	/* one RLE stream per sprite, the records' offsets indexing it --
	   encoded bytes, not a raster; the bind makes it so for a packed
	   series too */
	const uint8_t *streams;
	uint32_t       streams_size;
};

/* Not const: the first bind of a packed series decodes it into the
   archive. Bind once per archive, never per frame. */
int mads_ss_bind(struct mads_ss *ss, struct madspack *mp);

/* Draw one sprite of the series over dst at (x, y), via
   mads_rle_draw. An index past the sprite table is MADS_SS_RECORD. */
int mads_sprite_draw(const struct mads_ss *ss, uint16_t index,
                     const struct mads_surface *dst, int x, int y);

struct mads_ff_header {
	uint8_t  cell_height;       /* every glyph is this tall */
	uint8_t  max_width;         /* no glyph is wider */
	uint8_t  width[128];        /* glyph g is code g + 1; 0 is undefined */
	uint16_t offset[128];       /* into the member, 2-bit pixels */
} __attribute__ ((packed));

static_assert(sizeof (struct mads_ff_header) == 0x182,
              "FF header must be 0x182 bytes");

struct mads_ff {
	const struct mads_ff_header *header;
	const uint8_t *data;        /* the whole member; offset[] indexes it */
	uint32_t       size;
};

int mads_ff_bind(struct mads_ff *ff, const struct madspack *mp);

/* Pixel width of s, spacing pixels between glyphs, none trailing;
   an undefined glyph draws nothing and advances nothing. */
int mads_text_width(const struct mads_ff *ff, const char *s, int spacing);

/* Draw glyph g over dst at (x, y), clipped, each 2-bit pixel through
   color; a color of 0xff is transparent. A g outside 0..127 is
   MADS_FF_GLYPH. */
int mads_glyph_draw(const struct mads_ff *ff, int g,
                    const struct mads_surface *dst, int x, int y,
                    const uint8_t color[4]);

/* Draw s the same way, spacing pixels between glyphs; returns the
   pen's final x. */
int mads_text_draw(const struct mads_ff *ff, const char *s,
                   const struct mads_surface *dst, int x, int y,
                   int spacing, const uint8_t color[4]);

/* Failure codes; 0 is success. */
enum {
	MADS_CONCAT_SHORT = 1, /* container ends inside its header, table or a file */
	MADS_CONCAT_SIGNATURE, /* not a MADSCONCAT container */
	MADS_CONCAT_NAME,      /* member name empty, unterminated, or not a
	                          printable bare filename */

	MADS_PACK_OPEN,        /* cannot open */
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

	MADS_PALETTE_FORM,      /* not the full form, and shorter than a
	                           header */
	MADS_PALETTE_LENGTH,    /* header length past the member's end or
	                           MADS_PALETTE_LENGTH_MAX */
	MADS_PALETTE_COMPONENT, /* a component above 0x3f */

	MADS_PIK_LAYOUT,       /* not a picture archive */
	MADS_PIK_HEADER,       /* pixel member contradicts the picture header */

	MADS_SS_LAYOUT,        /* not an SS archive, or a member is missing */
	MADS_SS_HEADER,        /* sprite table contradicts the series header */
	MADS_SS_RECORD,        /* sprite record outside the stream member */
	MADS_SS_STREAM,        /* a packed sprite's stream decodes to a length
	                          other than its record's */

	MADS_FF_LAYOUT,        /* not a font archive */
	MADS_FF_GLYPH,         /* glyph wider than its cell, or outside the member */

	MADS_NOMEM,            /* out of memory */
};

const char *mads_strerror(int error);

/*
 * Record type stamps. The packer writes one per archive, into every
 * record slot; the interpreters never read it -- member order and
 * layout drive loading. The stamp exists for writing archives, never
 * for dispatch on read.
 * Values are the corpus consensus per resource class, unanimous per
 * game -- except the picture stamp, which rests on three files and
 * 2009's reading; most pictures carry arbitrary batch-correlated
 * values instead.
 */
enum {
	MADS_TYPE_PIK = 0x02,
	MADS_TYPE_SS  = 0x04,
	MADS_TYPE_FF  = 0x07,
};

#ifdef __cplusplus
}
#endif

#endif
