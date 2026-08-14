#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mads.h"
#include "util/util.h"

int sprite(const struct mads_ss_header *header, const size_t header_size,
           const struct mads_sprite_record *series_table, const size_t table_size,
           const uint8_t *palette, const size_t palette_size,
           const uint8_t *sprite_data, const size_t sprite_data_size, const char *fn)
{
	char filename[1024];

	/* madspack_load() leaves a zero-sized member's decoded slot NULL. */
	if (header == NULL || header_size < sizeof (struct mads_ss_header)) {
		fprintf(stderr, "Missing or short sprite header in <%s>\n", fn);
		return 1;
	}

	printf("ss_header: sprite_table_length: %u, sprites_size: %u\n",
		header->sprite_table_length, header->sprites_size);

	uint32_t sprites_size = 0;

	for (int i = 0; i < header->sprite_table_length; ++i) {
		const struct mads_sprite_record *sprite = &series_table[i];

		sprites_size += sprite->size;

		size_t unpacked = sprite->width * sprite->height;

		uint8_t image[unpacked];

		printf("%s/image[%2d]: offset: 0x%04x, size: 0x%04x:%04x, %3dx%3d\n", fn, i,
			sprite->offset, sprite->size, unpacked,
			sprite->width,  sprite->height);

		int error = mads_rle_decode(image, sprite->width, sprite->height,
		                            sprite_data + sprite->offset, sprite->size);
		if (error != 0) {
			fprintf(stderr, "Sprite %d: %s in <%s>\n", i, mads_strerror(error), fn);
			return 1;
		}

		snprintf(filename, 1024, "%s.%02d.tga", fn, i);
		tgaify(image, sprite->width, sprite->height, filename);
	}

	assert(header->sprites_size == sprites_size);

	return 0;
}
