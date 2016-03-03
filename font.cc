#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/util.h"

uint16_t font(const uint8_t *unpacked, const size_t unpacked_size)
{
	struct font_header {
		uint8_t  max_height;
		uint8_t  max_width;
		uint8_t  width[128];
		uint16_t offset[128];
	} __attribute__ ((packed)) *font = (struct font_header *) unpacked;

	printf("font: max_width: %d, max_height: %d sizeof(header) = %04x\n",
		font->max_width, font->max_height, sizeof(struct font_header));

	char filename[0x20];
	for (int i = 0; i < 128; ++i) {

		if (font->width[i] == 0)
			continue;

		int bytes_in_width = (font->width[i] / 4) + (font->width[i] % 4 ? 1 : 0);

		printf("entry[%3d] (%c) width: %d:%d, offset: %04x\n",
			i, (char) i + 1,
			font->width[i], bytes_in_width,
			font->offset[i]);

		const uint8_t *font_packed_character = unpacked + font->offset[i];

		uint8_t font_unpacked_character[font->width[i] * font->max_height];
		memset(font_unpacked_character, 0, sizeof(font_unpacked_character));

		for (int height = 0; height < font->max_height; ++height) {
			for (int width = 0; width < font->width[i]; ++width) {

				int byte = (width / 4);
				int shift = 6 - (width % 4) * 2;

				font_unpacked_character[width + (height * font->width[i])] =
					(font_packed_character[(bytes_in_width * height) + byte] >> shift) & 0x03;
			}
		}

//		snprintf(filename, 0x20, "image%03d.tga", i);
//		tgaify(font_unpacked_character, font->width[i], font->max_height, filename);
	}
}

