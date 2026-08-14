#include <stdint.h>
#include <string.h>

#include "mads.h"

/*
 * Sprite pixel codec. The row grammar is the 8086 sprite drawing
 * routines'; the decoder accepts everything they accept, including a
 * discarded mode byte and the byte skip that terminates a full row.
 * The encoder emits each row in whichever of the two coded forms is
 * shorter.
 */

enum {
	RUN_MAX = 0xfc,  /* longest run either form emits; the length
	                    byte stays clear of the three sentinels */
};

int mads_rle_decode(uint8_t *dest, size_t width, size_t height,
                    const uint8_t *src, uint32_t src_size)
{
	uint32_t idx = 0;

	memset(dest, MADS_RLE_TRANSPARENT, width * height);

	for (size_t y = 0; y < height; ++y) {
		uint8_t *row = dest + y * width;
		size_t x = 0;

		if (idx == src_size)
			return MADS_RLE_TRUNCATED;

		uint8_t mode = src[idx++];

		if (mode == 0xff)  /* empty row: no pixels, no terminator */
			continue;

		if (mode == 0xfd) {
			/* (length, value) pairs; a length of 0xff ends the row */
			for (;;) {
				if (idx == src_size)
					return MADS_RLE_TRUNCATED;

				uint8_t length = src[idx++];
				if (length == 0xff)
					break;

				if (idx == src_size)
					return MADS_RLE_TRUNCATED;

				uint8_t value = src[idx++];
				if (value == MADS_RLE_TRANSPARENT) {
					/* skips may pass the row end; draws may not */
					x += length;
					if (x > width)
						x = width;
					continue;
				}

				if (length > width - x)
					return MADS_RLE_CORRUPT;

				memset(row + x, value, length);
				x += length;
			}
			continue;
		}

		/* per-pixel tokens; any other mode byte is discarded */
		for (;;) {
			if (x >= width) {
				/* a full row still ends with 0xff; skip up to it */
				do {
					if (idx == src_size)
						return MADS_RLE_TRUNCATED;
				} while (src[idx++] != 0xff);
				break;
			}

			if (idx == src_size)
				return MADS_RLE_TRUNCATED;

			uint8_t token = src[idx++];
			if (token == 0xff)
				break;

			if (token == MADS_RLE_TRANSPARENT) {
				++x;
				continue;
			}

			if (token == 0xfe) {
				if (src_size - idx < 2)
					return MADS_RLE_TRUNCATED;

				uint8_t length = src[idx++];
				uint8_t value  = src[idx++];
				if (value == MADS_RLE_TRANSPARENT) {
					x += length;
					if (x > width)
						x = width;
					continue;
				}

				if (length > width - x)
					return MADS_RLE_CORRUPT;

				memset(row + x, value, length);
				x += length;
				continue;
			}

			row[x++] = token;
		}
	}

	return 0;
}

static size_t run_at(const uint8_t *row, size_t x, size_t extent)
{
	size_t run = 1;

	while (x + run < extent && row[x + run] == row[x] && run < RUN_MAX)
		++run;

	return run;
}

/* The pair form spends 2 bytes on any run. */
static size_t encode_pairs(uint8_t *out, const uint8_t *row, size_t extent)
{
	size_t written = 0;

	out[written++] = 0xfd;

	for (size_t x = 0; x < extent; ) {
		size_t run = run_at(row, x, extent);

		out[written++] = run;
		out[written++] = row[x];
		x += run;
	}

	out[written++] = 0xff;
	return written;
}

/* The token form spends 1 byte per pixel and 3 on a run. A bare 0xfd
   is the one-pixel skip; 0xfe and 0xff cannot stand alone, so their
   runs always take the run form. */
static size_t encode_tokens(uint8_t *out, const uint8_t *row, size_t extent)
{
	size_t written = 0;

	out[written++] = 0xfe;

	for (size_t x = 0; x < extent; ) {
		size_t run = run_at(row, x, extent);

		if (run <= 3 && row[x] < 0xfe) {
			for (size_t i = 0; i < run; ++i)
				out[written++] = row[x];
		} else {
			out[written++] = 0xfe;
			out[written++] = run;
			out[written++] = row[x];
		}
		x += run;
	}

	out[written++] = 0xff;
	return written;
}

int mads_rle_encode(uint8_t *dest, uint32_t *dest_size,
                    const uint8_t *src, size_t width, size_t height)
{
	uint32_t capacity = *dest_size;
	uint32_t written = 0;

	for (size_t y = 0; y < height; ++y) {
		const uint8_t *row = src + y * width;
		size_t extent = width;

		while (extent > 0 && row[extent - 1] == MADS_RLE_TRANSPARENT)
			--extent;

		if (extent == 0) {
			if (capacity - written < 1)
				return MADS_RLE_OVERFLOW;
			dest[written++] = 0xff;
			continue;
		}

		/* size both forms from the runs before writing a byte */
		size_t np = 2;
		size_t nt = 2;

		for (size_t x = 0; x < extent; ) {
			size_t run = run_at(row, x, extent);

			np += 2;
			nt += (run <= 3 && row[x] < 0xfe) ? run : 3;
			x += run;
		}

		/* ties keep the token form; the corpus is exact on this */
		size_t size = np < nt ? np : nt;

		if (capacity - written < size)
			return MADS_RLE_OVERFLOW;

		written += np < nt ? encode_pairs(dest + written, row, extent)
		                   : encode_tokens(dest + written, row, extent);
	}

	/* the end-of-sprite marker; nothing reads past the last row, but
	   every stream the original packer wrote carries it */
	if (capacity - written < 1)
		return MADS_RLE_OVERFLOW;
	dest[written++] = 0xfc;

	*dest_size = written;
	return 0;
}

uint32_t mads_rle_encode_bound(size_t width, size_t height)
{
	/* The pair form never exceeds 2 * width + 2 and the shorter form
	   wins, so the token form's 3 * width + 2 is unreachable. A row
	   alternating 0xfe and 0xff pixels meets this bound exactly. */
	if (width > 0xffff || height > 0xffff)
		return UINT32_MAX;

	size_t row_bound = 2 * width + 2;

	/* saturate: the product can pass UINT32_MAX (and wrap a 32-bit size_t) */
	if (height > (UINT32_MAX - 1) / row_bound)
		return UINT32_MAX;

	return height * row_bound + 1;
}
