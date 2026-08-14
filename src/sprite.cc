#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mads.h"
#include "util/util.h"

void rle_decode(const uint8_t *input, const size_t input_size,
                const size_t width, const size_t height, uint8_t *output);

int sprite(const struct mads_ss_header *header, const size_t header_size,
           const struct mads_sprite_record *series_table, const size_t table_size,
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

		memset(image, 0, unpacked);

		rle_decode(sprite_data + sprite->offset, sprite->size,
		                         sprite->width,  sprite->height,
			image);

		snprintf(filename, 1024, "%s.%02d.tga", fn, i);
		tgaify(image, sprite->width, sprite->height, filename);
	}

	assert(header->sprites_size == sprites_size);

	return 0;
}

/* Sentinels: 0xff ends a row, 0xfe introduces a run, 0xfd is
   transparency. */
void rle_decode(const uint8_t *input, const size_t input_size, const size_t width, const size_t height, uint8_t *output)
{
	int idx = 0;
	int x = 0, y = 0;
	uint8_t ch, repeat;

	while (idx < input_size) {

state1:
		if (y == height)
			return;

		ch = input[idx++];             //fprintf(stderr, "0ba8:0142 lodsb ch(%02x)\n", ch);
		switch (ch) {
			case 0xff: //transpatent line.
				++y;
				x=0;
				continue;

			case 0xfe:
				break;

			case 0xfd: //transparency
state3:
				repeat = input[idx++];             //fprintf(stderr, "0ba8:015c lodsb repeat(%02x)\n", repeat);
				if (repeat == 0xff) {
					x = 0; ++y;
					goto state1;
				}

				ch = input[idx++];             //fprintf(stderr, "0ba8:016a lodsb ch(%02x)\n", ch);
				if (ch == 0xfd) {
					x += repeat;
					goto state3;
				}
				for (int i = 0; i < repeat; ++i){
					output[x + (y * width)] = ch;
					//printf("output[%d + (%d * %d)] = %02x\n", x, y, width, ch);
					//assert(output[x + (y * width)] == image[x+ (y*width)]);
					++x;
				}
				goto state3;
		}


state2:
		if (x == width) {
			while ((ch = input[idx++]) != 0xff) {
				fprintf(stderr, "0ba8:01ea lodsb ERROR ch(%02x) expected (0xff)\n", ch);
			}
			//fprintf(stderr, "0ba8:01ea lodsb ch(%02x)\n", ch);
			x = 0; ++y;
			goto state1;
		}

		ch = input[idx++];             //fprintf(stderr, "0ba8:018c lodsb ch(%02x)\n", ch);
		switch (ch) {
			case 0xff:
				x = 0; ++y;
				goto state1;

			case 0xfe:
			{
				repeat = input[idx++]; //fprintf(stderr, "0ba8:019a lodsb repeat(%02x)\n", repeat);
				ch = input[idx++];             //fprintf(stderr, "0ba8:019d lodsb     ch(%02x)\n", ch);

				if (ch == 0xfd) {
					x += repeat;
					break;
				}
				for (int i = 0; i < repeat; ++i){
					output[x + (y * width)] = ch;
					//printf("output[%d + (%d * %d)] = %02x\n", x, y, width, ch);
					//assert(output[x + (y * width)] == image[x+ (y*width)]);
					++x;
				}
				break;
			}

			case 0xfd:
				++x;
				break;

			default:
				output[x + (y * width)] = ch;
				//printf("output[%d + (%d * %d)] = %02x\n", x, y, width, ch);
				//assert(output[x + (y * width)] == image[x+ (y*width)]);
				++x;
		}
		goto state2;
	}
}
