#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/util.h"

void rle_decode(const uint8_t *input, const size_t input_size,
                const size_t width, const size_t height, uint8_t *output);

uint16_t sprite(const uint8_t *index_data,  const size_t index_data_size,
                const uint8_t *sprite_data, const size_t sprite_data_size, const char *fn)
{
	struct index_entry_t {
		uint16_t img_offset;
		uint16_t unk00;
		uint16_t img_size;
		uint16_t unk01;
		uint16_t unk02;
		uint16_t unk03;
		uint16_t img_width;
		uint16_t img_height;
	} *index_entry = (struct index_entry_t *) index_data;

	char filename[1024];

	int index_entries = index_data_size / sizeof (struct index_entry_t);

	printf("index_entries: %d (%d / %d)\n", index_entries, index_data_size, sizeof (struct index_entry_t));

	for (int i = 0; i < index_entries; ++i) {
		size_t unpacked = index_entry[i].img_width * index_entry[i].img_height;

		uint8_t image[unpacked];

		printf("%s/image[%2d]: offset: 0x%04x, size: 0x%04x:%04x, %3dx%3d\n", fn, i,
			index_entry[i].img_offset, index_entry[i].img_size, unpacked,
			index_entry[i].img_width,  index_entry[i].img_height);

		memset(image, 0, unpacked);

		rle_decode(sprite_data + index_entry[i].img_offset, index_entry[i].img_size,
		                         index_entry[i].img_width,  index_entry[i].img_height,
			image);

		snprintf(filename, 1024, "%s.%02d.tga", fn, i);
		tgaify(image, index_entry[i].img_width, index_entry[i].img_height, filename);
	}

	return 0;
}

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
