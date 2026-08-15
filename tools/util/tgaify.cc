#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mads.h"
#include "util.h"

/*
 * Colormapped TGA writer. data is one byte per pixel and palette is
 * 256 six-bit RGB triples -- the archive's own palette member, whose
 * register mapping is the identity: triple N colors pixel value N.
 */

int tgaify(const uint8_t *data, const size_t width, const size_t height,
           const uint8_t *palette, const char *filename)
{
	/* The header holds the extents in sixteen bits while the pixels
	   below are written from the arguments, so a larger image would
	   describe itself wrongly and still write every byte. */
	if (width > 0xffff || height > 0xffff) {
		fprintf(stderr, "%s: %zux%zu is larger than a TGA header describes\n",
			filename, width, height);
		return 1;
	}

	struct tga_color_map_record {
		uint8_t b, g, r;
	} __attribute__ ((packed)) tga_color_map[256];

	for (int i = 0; i < 256; ++i) {
		tga_color_map[i].r = mads_rgb6_to_8(palette[i * 3 + 0]);
		tga_color_map[i].g = mads_rgb6_to_8(palette[i * 3 + 1]);
		tga_color_map[i].b = mads_rgb6_to_8(palette[i * 3 + 2]);
	}

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

	tga.cms.first_entry_index = 0;
	tga.cms.color_map_length = 256;
	tga.cms.color_map_entry_size = 24; /* in bits */

	tga.is.x_origin = 0;
	tga.is.y_origin = 0;
	tga.is.image_width = width;
	tga.is.image_height = height;
	tga.is.pixel_depth = 8;
	tga.is.image_descriptor = 0 + 0x20; /* top-left origin: rows top-down, as data is */

	FILE *fp = fopen(filename, "wb");
	if (fp == NULL) {
		fprintf(stderr, "%s: cannot write: %s\n", filename, strerror(errno));
		return 1;
	}

	fwrite(&tga,          sizeof (struct tga_hdr),                1, fp);
	fwrite(tga_color_map, sizeof (struct tga_color_map_record), 256, fp);
	fwrite(data, width, height, fp);

	/* A write that fails while flushing a full buffer leaves fclose
	   nothing to flush; read the error flag while the stream still
	   exists. */
	int failed = ferror(fp);

	if (fclose(fp) != 0)
		failed = 1;

	if (failed) {
		fprintf(stderr, "%s: cannot write: %s\n", filename, strerror(errno));
		return 1;
	}

	return 0;
}
