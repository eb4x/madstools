#include <stddef.h>
#include <stdint.h>

#include "mads.h"

/*
 * Font binder. Binding validates every glyph up front -- the width
 * against its cell, the bitmap against the member's end -- which is
 * also what tells a font apart from the other single-member
 * archives.
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
