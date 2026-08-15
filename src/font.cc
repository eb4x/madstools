#include <stddef.h>
#include <stdint.h>

#include "mads.h"

/*
 * Font binder and renderer. Binding validates every glyph up front
 * -- the width against its cell, the bitmap against the member's end
 * -- which is also what tells a font apart from the other
 * single-member archives, and what lets the renderer trust the
 * header outright.
 */

int mads_ff_bind(struct mads_ff *ff, const struct madspack *mp)
{
	const struct madspack_record *member = mp->header.member;

	if (mp->header.length != 1)
		return MADS_FF_LAYOUT;

	if (member[0].decoded_size < sizeof (struct mads_ff_header) ||
	    mp->decoded[0] == NULL)
		return MADS_FF_LAYOUT;

	ff->header = (const struct mads_ff_header *) mp->decoded[0];
	ff->data = mp->decoded[0];
	ff->size = member[0].decoded_size;

	if (ff->header->cell_height == 0 || ff->header->max_width == 0)
		return MADS_FF_LAYOUT;

	for (int g = 0; g < 128; ++g) {
		uint32_t width = ff->header->width[g];

		if (width == 0)
			continue;

		if (width > ff->header->max_width)
			return MADS_FF_GLYPH;

		uint32_t bytes_in_width = (width + 3) / 4;  /* 2-bit pixels */

		if (ff->header->offset[g] +
		    bytes_in_width * ff->header->cell_height > ff->size)
			return MADS_FF_GLYPH;
	}

	return 0;
}

int mads_text_width(const struct mads_ff *ff, const char *s, int spacing)
{
	const struct mads_ff_header *header = ff->header;
	int width = 0;
	int drawn = 0;

	for (const unsigned char *p = (const unsigned char *) s; *p != '\0'; ++p) {
		int g = *p - 1;

		if (g < 0 || g >= 128 || header->width[g] == 0)
			continue;

		width += header->width[g] + spacing;
		++drawn;
	}

	return drawn > 0 ? width - spacing : 0;
}

int mads_glyph_draw(const struct mads_ff *ff, int g,
                    const struct mads_surface *dst, int x, int y,
                    const uint8_t color[4])
{
	const struct mads_ff_header *header = ff->header;

	if (g < 0 || g >= 128)
		return MADS_FF_GLYPH;

	int width = header->width[g];

	if (width == 0)
		return 0;

	/* the bind validated every glyph's extent */
	const uint8_t *glyph = ff->data + header->offset[g];
	int bytes_in_width = (width + 3) / 4;  /* 2-bit pixels */

	for (int row = 0; row < header->cell_height; ++row) {
		int py = y + row;

		if (py < 0 || (size_t) py >= dst->h)
			continue;

		for (int col = 0; col < width; ++col) {
			int px = x + col;

			if (px < 0 || (size_t) px >= dst->w)
				continue;

			uint8_t byte = glyph[row * bytes_in_width + col / 4];
			uint8_t value = color[(byte >> (6 - col % 4 * 2)) & 0x03];

			if (value != 0xff)
				dst->pixels[(size_t) py * dst->pitch + px] = value;
		}
	}

	return 0;
}

int mads_text_draw(const struct mads_ff *ff, const char *s,
                   const struct mads_surface *dst, int x, int y,
                   int spacing, const uint8_t color[4])
{
	const struct mads_ff_header *header = ff->header;

	for (const unsigned char *p = (const unsigned char *) s; *p != '\0'; ++p) {
		int g = *p - 1;

		if (g < 0 || g >= 128)
			continue;

		int width = header->width[g];

		if (width == 0)
			continue;

		mads_glyph_draw(ff, g, dst, x, y, color);

		x += width + spacing;
	}

	return x;
}
