#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mads.h"

/*
 * Sprite series binder and draw. Binding is pointer arithmetic over
 * the loaded decoded members: the view aliases the archive and owns
 * no memory, and the layout checks tell a series apart from the other
 * MADSPACK layouts -- and vouch for every record the draw reads.
 *
 * A packed series (header byte 0) is the exception: its streams are
 * FAB-compressed one by one, and the first bind decodes them into the
 * archive, so the draw never has to.
 */

/* the most a FAB stream decodes to per encoded byte: a 3-byte escape
   item yields 256 */
enum { FAB_MAX_RATIO = 86 };

/* Decode every stream into a fresh buffer; nothing in the archive
   changes until all of them came out at their record's size. */
static int unpack(struct mads_ss *ss, struct madspack *mp)
{
	struct mads_ss_header *header = (struct mads_ss_header *) mp->decoded[0];
	struct mads_sprite_record *sprite_table =
		(struct mads_sprite_record *) mp->decoded[1];
	uint32_t sprites_size = header->sprites_size;

	uint8_t *decoded = (uint8_t *) malloc(sprites_size);
	if (decoded == NULL)
		return MADS_NOMEM;

	uint32_t pos = 0;

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record *sprite = &sprite_table[i];
		const uint8_t *src = ss->streams + sprite->offset;
		uint32_t src_size = ss->streams_size - sprite->offset;

		if (header->codec == 1) {
			uint32_t size = sprite->size;
			int result = mads_fab_decode(decoded + pos, &size, src, src_size);

			if (result == 0 && size != sprite->size)
				result = MADS_SS_STREAM;
			if (result != 0) {
				free(decoded);
				return result;
			}
		} else {
			memcpy(decoded + pos, src, sprite->size);
		}

		pos += sprite->size;
	}

	/* The swap: the archive now owns the decoded streams in place of
	   the packed member, and madspack_free releases them with the rest.
	   Its record reads like any FAB member's, encoded_size still the
	   size on disk. */
	free(mp->decoded[3]);
	mp->decoded[3] = decoded;
	mp->header.member[3].codec = 1;
	mp->header.member[3].decoded_size = sprites_size;

	pos = 0;
	for (uint16_t i = 0; i < ss->length; ++i) {
		sprite_table[i].offset = pos;
		pos += sprite_table[i].size;
	}

	header->packed = 0;

	ss->streams      = decoded;
	ss->streams_size = sprites_size;

	return 0;
}

int mads_ss_bind(struct mads_ss *ss, struct madspack *mp)
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

	ss->streams     = mp->decoded[3];
	ss->streams_size = member[3].decoded_size;

	/* the header owns the length: the sprite table is exactly length
	   records */
	if (member[1].decoded_size !=
	    (uint32_t) ss->length * sizeof (struct mads_sprite_record))
		return MADS_SS_HEADER;

	if (ss->length > 0 && (ss->sprite_table == NULL || ss->streams == NULL))
		return MADS_SS_LAYOUT;

	/* the engine tests the flag for nonzero, and knows two codecs */
	int packed = ss->header->packed != 0;
	int fab = packed && ss->header->codec == 1;

	if (packed && ss->header->codec > 1)
		return MADS_SS_LAYOUT;

	uint32_t sprites_size = 0;

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record *sprite = &ss->sprite_table[i];

		if (sprite->offset > ss->streams_size)
			return MADS_SS_RECORD;

		if (fab) {
			/* a FAB header fits, and its magic is there */
			if (ss->streams_size - sprite->offset < 6 ||
			    memcmp(ss->streams + sprite->offset, "FAB", 3) != 0)
				return MADS_SS_RECORD;
		} else if (sprite->size > ss->streams_size - sprite->offset) {
			return MADS_SS_RECORD;
		}

		/* refuse a sum past the claim before it can wrap */
		if (sprite->size > ss->header->sprites_size - sprites_size)
			return MADS_SS_HEADER;

		sprites_size += sprite->size;
	}

	if (ss->header->sprites_size != sprites_size)
		return MADS_SS_HEADER;

	/* Records may alias the same bytes of the member, so their sum is
	   not bounded by its size; it is bounded by what those bytes can
	   decode to, and the unpack is sized from the sum. */
	uint32_t yield = fab ? FAB_MAX_RATIO : 1;
	if (packed && sprites_size / yield > ss->streams_size)
		return MADS_SS_HEADER;

	int result = mads_palette_bind(&ss->palette, mp->decoded[2],
	                               member[2].decoded_size);
	if (result != 0)
		return result;

	/* nothing to decode: skip, so malloc(0) cannot read as MADS_NOMEM */
	if (packed && sprites_size > 0) {
		result = unpack(ss, mp);

		if (result != 0) {
			mp->error_member = 3;
			return result;
		}
	}

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
