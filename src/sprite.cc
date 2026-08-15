#include <stddef.h>
#include <stdint.h>

#include "mads.h"

/*
 * Sprite series binder and draw. Binding is pointer arithmetic over
 * the loaded decoded members: the view aliases the archive and owns
 * no memory, and the layout checks tell a series apart from the other
 * MADSPACK layouts -- and vouch for every record the draw reads.
 */

int mads_ss_bind(struct mads_ss *ss, const struct madspack *mp)
{
	const struct madspack_record *member = mp->header.member;

	if (mp->header.length != 4)
		return MADS_SS_LAYOUT;

	if (member[0].decoded_size != sizeof (struct mads_ss_header) ||
	    mp->decoded[0] == NULL)
		return MADS_SS_LAYOUT;

	ss->header = (const struct mads_ss_header *) mp->decoded[0];
	ss->length = ss->header->sprite_table_length;
	ss->sprite_table = (const struct mads_sprite_record *) mp->decoded[1];

	ss->palette      = mp->decoded[2];
	ss->palette_size = member[2].decoded_size;

	ss->streams      = mp->decoded[3];
	ss->streams_size = member[3].decoded_size;

	/* the header owns the length: the sprite table is exactly length
	   records */
	if (member[1].decoded_size !=
	    (uint32_t) ss->length * sizeof (struct mads_sprite_record))
		return MADS_SS_HEADER;

	if (ss->length > 0 && (ss->sprite_table == NULL || ss->streams == NULL))
		return MADS_SS_LAYOUT;

	uint32_t sprites_size = 0;

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record *sprite = &ss->sprite_table[i];

		if (sprite->offset > ss->streams_size ||
		    sprite->size > ss->streams_size - sprite->offset)
			return MADS_SS_RECORD;

		/* refuse a sum past the claim before it can wrap */
		if (sprite->size > ss->header->sprites_size - sprites_size)
			return MADS_SS_HEADER;

		sprites_size += sprite->size;
	}

	if (ss->header->sprites_size != sprites_size)
		return MADS_SS_HEADER;

	/* only the full form is component triples; sparse forms differ */
	if (ss->palette_size == MADS_PALETTE_SIZE)
		for (size_t i = 0; i < MADS_PALETTE_SIZE; ++i)
			if (ss->palette[i] > 0x3f)
				return MADS_SS_PALETTE;

	return 0;
}

/* The bind validated every record against the stream member. A
   zero-area record is a placeholder with nothing to draw. */
int mads_sprite_draw(const struct mads_ss *ss, uint16_t index,
                     const struct mads_surface *dst, int x, int y)
{
	if (index >= ss->length)
		return MADS_SS_RECORD;

	const struct mads_sprite_record *sprite = &ss->sprite_table[index];

	if (sprite->width == 0 || sprite->height == 0)
		return 0;

	return mads_rle_draw(dst, x, y, sprite->width, sprite->height,
	                     ss->streams + sprite->offset, sprite->size);
}
