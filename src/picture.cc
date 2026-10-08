#include <stddef.h>
#include <stdint.h>

#include "mads.h"

int mads_pik_bind(struct mads_pik *pik, const struct madspack *mp)
{
	const struct madspack_record *member = mp->header.member;

	if (mp->header.length < 2 || mp->header.length > 3)
		return MADS_PIK_LAYOUT;

	if (member[0].decoded_size != sizeof (struct mads_pik_header) ||
	    mp->decoded[0] == NULL)
		return MADS_PIK_LAYOUT;

	pik->header = (const struct mads_pik_header *) mp->decoded[0];

	pik->pixels      = mp->decoded[1];
	pik->pixels_size = member[1].decoded_size;

	if (mp->header.length == 3) {
		pik->palette      = mp->decoded[2];
		pik->palette_size = member[2].decoded_size;
	} else {
		pik->palette      = NULL;
		pik->palette_size = 0;
	}

	if (pik->pixels == NULL)
		return MADS_PIK_LAYOUT;

	if ((uint32_t) pik->header->width * pik->header->height != pik->pixels_size)
		return MADS_PIK_HEADER;

	/* only the full form is component triples; sparse forms differ */
	if (pik->palette_size == MADS_PALETTE_SIZE)
		for (size_t i = 0; i < MADS_PALETTE_SIZE; ++i)
			if (pik->palette[i] > 0x3f)
				return MADS_PIK_PALETTE;

	return 0;
}
