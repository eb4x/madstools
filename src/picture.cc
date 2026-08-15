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

	if (pik->pixels == NULL)
		return MADS_PIK_LAYOUT;

	if ((uint32_t) pik->header->width * pik->header->height != pik->pixels_size)
		return MADS_PIK_HEADER;

	int result;
	if (mp->header.length == 3)
		result = mads_palette_bind(&pik->palette, mp->decoded[2],
		                           member[2].decoded_size);
	else
		result = mads_palette_bind(&pik->palette, NULL, 0);

	if (result != 0)
		return result;

	return 0;
}
