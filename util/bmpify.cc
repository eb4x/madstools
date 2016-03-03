#include <stdint.h>
#include <stdio.h>

struct rgb {
	uint8_t b, g, r, a;
} __attribute__ ((packed));

struct bmp_hdr {
	char     type[2];
	uint32_t file_size;
	uint16_t res1, res2;
	uint32_t image_offset;

	struct dib_hdr {
		uint32_t dib_hdr_size;
		int32_t image_width;
		int32_t image_height;
		uint16_t planes; //1
		uint16_t bpp;
		uint32_t compression;
		uint32_t image_size; //raw bitmap_data size
		int32_t xpelspermeter, ypelspermeter;
		uint32_t clr_used, clr_important;
	} __attribute__ ((packed)) dib;

} __attribute__ ((packed)) bmp;

void bmpify(uint8_t *data, size_t img_w, size_t img_h, const char *filename)
{
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
		palette[i].a = 0;
	}
	fclose(fp);

	bmp.type[0] = 'B';
	bmp.type[1] = 'M';
	bmp.file_size = sizeof (struct bmp_hdr) + sizeof (palette) + (img_w * img_h);
	bmp.res1 = 0; bmp.res2 = 0;
	bmp.image_offset = sizeof (struct bmp_hdr) + sizeof (palette);

	bmp.dib.dib_hdr_size = sizeof (struct bmp_hdr::dib_hdr);
	bmp.dib.image_width = img_w;
	bmp.dib.image_height = img_h;
	bmp.dib.planes = 1; bmp.dib.bpp = 8;
	bmp.dib.compression = 0; bmp.dib.image_size = (img_w * img_h);
	bmp.dib.xpelspermeter = 0; bmp.dib.ypelspermeter = 0;
	bmp.dib.clr_used = 256; bmp.dib.clr_important = 0;

	fp = fopen(filename, "w");
	fwrite(&bmp,     sizeof(struct bmp_hdr),   1, fp);
	fwrite(&palette, sizeof(struct rgb),     256, fp);
	fwrite(data, img_w, img_h, fp);
	/*flip image. */
/*	for (int i = 0; i < img_h; ++i)
		fwrite(&data[img_w * (img_h-i-1)], img_w, 1, fp);*/

	fclose(fp);

	//printf("Created %s, %d x %d px (%d)\n", filename, img_w, img_h, (img_w * img_h));
}
