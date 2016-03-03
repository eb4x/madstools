#include <stdint.h>
#include <stdio.h>

void tgaify(const uint8_t *data, const size_t img_w, const size_t img_h, const char *filename)
{
	struct rgb {
		uint8_t b, g, r;
	} __attribute__ ((packed));

	FILE *fp;
	fp = fopen("VICEROY.PAL", "r");
	if (fp == NULL) {
		fprintf(stderr, "Failed to read VICEROY.PAL\n");
		return;
	}

	struct rgb palette[256];
	for (int i = 0; i < 256; ++i) {
		fread(&palette[i].r, 1, 1, fp); palette[i].r *= 4;
		fread(&palette[i].g, 1, 1, fp); palette[i].g *= 4;
		fread(&palette[i].b, 1, 1, fp); palette[i].b *= 4;
	}
	fclose(fp);

	struct tga_hdr {
		uint8_t id_length;
		uint8_t color_map_type;
		uint8_t image_type;
		struct color_map_specification {
			uint16_t first_entry_index;
			uint16_t color_map_length;
			uint8_t  color_map_entry_size;
		} __attribute__ ((packed)) cms;
		struct image_specification {
			uint16_t x_origin;
			uint16_t y_origin;
			uint16_t image_width;
			uint16_t image_height;
			uint8_t pixel_depth;
			uint8_t image_descriptor;
		} __attribute__ ((packed)) is;
	}   __attribute__ ((packed)) tga;

	tga.id_length = 0;      /* no additional information */
	tga.color_map_type = 1; /* yes we have a palette */
	tga.image_type = 1;     /* uncompressed colormapped image. */

	tga.cms.first_entry_index = sizeof (struct tga_hdr);
	tga.cms.color_map_length = 256;
	tga.cms.color_map_entry_size = 24; /* in bits */

	tga.is.x_origin = 0;
	tga.is.y_origin = 0;
	tga.is.image_width = img_w;
	tga.is.image_height = img_h;
	tga.is.pixel_depth = 8;
	tga.is.image_descriptor = 0 + 0x20; /* flip image*/

	fopen(filename, "w");

	fwrite(&tga,     sizeof(struct tga_hdr),   1, fp);
	fwrite(&palette, sizeof(struct rgb),     256, fp);

	fwrite(data, img_w, img_h, fp);
	fclose(fp);

}
