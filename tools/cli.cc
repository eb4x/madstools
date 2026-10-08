#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <format>
#include <string>

#include "mads.h"
#include "util/util.h"

enum format { FORMAT_MADSCONCAT, FORMAT_MADSPACK, FORMAT_UNKNOWN,
              FORMAT_UNREADABLE };

static enum format sniff(const char *path)
{
	char signature[0x10];

	FILE *fp = fopen(path, "rb");
	if (fp == NULL) {
		fprintf(stderr, "%s: cannot open: %s\n", path, strerror(errno));
		return FORMAT_UNREADABLE;
	}

	size_t size = fread(signature, 1, sizeof (signature), fp);

	if (ferror(fp)) {
		fprintf(stderr, "%s: cannot read: %s\n", path, strerror(errno));
		fclose(fp);
		return FORMAT_UNREADABLE;
	}
	fclose(fp);

	if (size >= sizeof (MADSCONCAT_SIG) - 1 &&
	    memcmp(signature, MADSCONCAT_SIG, sizeof (MADSCONCAT_SIG) - 1) == 0)
		return FORMAT_MADSCONCAT;

	if (size >= sizeof (MADSPACK_SIG) - 1 &&
	    memcmp(signature, MADSPACK_SIG, sizeof (MADSPACK_SIG) - 1) == 0)
		return FORMAT_MADSPACK;

	return FORMAT_UNKNOWN;
}

static int split(const char *path, const char *extract_dir)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: cannot open: %s\n", path, strerror(errno));
		return EXIT_FAILURE;
	}

	struct stat st;
	if (fstat(fd, &st) != 0) {
		fprintf(stderr, "%s: cannot stat: %s\n", path, strerror(errno));
		close(fd);
		return EXIT_FAILURE;
	}

	if (st.st_size < (off_t) sizeof (struct madsconcat_header)) {
		fprintf(stderr, "%s: shorter than a MADSCONCAT header\n", path);
		close(fd);
		return EXIT_FAILURE;
	}

	size_t size = st.st_size;
	const uint8_t *map = (const uint8_t *) mmap(NULL, size, PROT_READ,
	                                            MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		fprintf(stderr, "%s: cannot map: %s\n", path, strerror(errno));
		return EXIT_FAILURE;
	}

	struct madsconcat mc;
	int error = madsconcat_bind(&mc, map, size);
	if (error != 0) {
		fprintf(stderr, "%s: %s\n", path, mads_strerror(error));
		munmap((void *) map, size);
		return EXIT_FAILURE;
	}

	printf("length: %d\n", mc.header->length);

	for (int i = 0; i < mc.header->length; ++i) {
		const struct madsconcat_record &file = mc.header->file[i];

		printf("%s[%3d]: offset(%06x), size(%06x) - [%s]\n", path, i, file.offset, file.size, file.name);

		if (extract_dir == NULL)
			continue;

		std::string outpath = std::format("{}/{}", extract_dir, file.name);

		FILE *out = fopen(outpath.c_str(), "wb");
		if (out == NULL) {
			fprintf(stderr, "%s: cannot write: %s\n", outpath.c_str(), strerror(errno));
			munmap((void *) map, size);
			return EXIT_FAILURE;
		}

		fwrite(map + file.offset, 1, file.size, out);

		/* a write that fails while flushing a full buffer leaves
		   fclose nothing to flush, so read the error flag while the
		   stream still exists */
		int failed = ferror(out);

		if (fclose(out) != 0)
			failed = 1;

		if (failed) {
			fprintf(stderr, "%s: cannot write: %s\n", outpath.c_str(), strerror(errno));
			munmap((void *) map, size);
			return EXIT_FAILURE;
		}
	}

	munmap((void *) map, size);

	return EXIT_SUCCESS;
}

static int picture(const struct mads_pik *pik, const char *extract_dir,
                   const char *filename)
{
	printf("picture: %ux%u\n", pik->header->width, pik->header->height);

	std::string outpath = std::format("{}/{}.tga", extract_dir, filename);
	if (tgaify(pik->pixels, pik->header->width, pik->header->height,
	           pik->palette, outpath.c_str()) != 0)
		return 1;

	return 0;
}

static int sprite(const struct mads_ss *ss, const char *extract_dir,
                  const char *filename)
{
	printf("ss_header: sprite_table_length: %u, sprites_size: %u\n",
		ss->header->sprite_table_length, ss->header->sprites_size);

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record &sprite = ss->sprite_table[i];

		size_t pixels_size = (size_t) sprite.width * sprite.height;

		printf("%s/sprite[%2d]: offset: 0x%04x, size: 0x%04x:%04zx, %3dx%3d\n", filename, i,
			sprite.offset, sprite.size, pixels_size,
			sprite.width,  sprite.height);

		/* a zero-area sprite is a placeholder; nothing to decode,
		   and malloc(0) may return the NULL the error path reads
		   as out of memory */
		if (pixels_size == 0)
			continue;

		uint8_t *pixels = (uint8_t *) malloc(pixels_size);
		if (pixels == NULL) {
			fprintf(stderr, "%s: sprite %d: %s\n", filename, i,
				mads_strerror(MADS_NOMEM));
			return 1;
		}

		int error = mads_rle_decode(pixels, sprite.width, sprite.height,
		                            ss->streams + sprite.offset, sprite.size);
		if (error != 0) {
			fprintf(stderr, "%s: sprite %d: %s\n", filename, i, mads_strerror(error));
			free(pixels);
			return 1;
		}

		std::string outpath = std::format("{}/{}.{:02}.tga", extract_dir, filename, i);
		error = tgaify(pixels, sprite.width, sprite.height,
		               ss->palette, outpath.c_str());
		free(pixels);

		if (error != 0)
			return 1;
	}

	return 0;
}

static int font(const struct mads_ff *ff, const char *extract_dir,
                const char *filename)
{
	const struct mads_ff_header *header = ff->header;

	printf("font: max_width: %d, cell_height: %d\n",
		header->max_width, header->cell_height);

	/* One cell per glyph, 16 columns by 8 rows, glyphs left-aligned. */
	size_t sheet_width  = 16 * header->max_width;
	size_t sheet_height =  8 * header->cell_height;

	uint8_t *sheet = (uint8_t *) malloc(sheet_width * sheet_height);
	if (sheet == NULL) {
		fprintf(stderr, "%s: %s\n", filename, mads_strerror(MADS_NOMEM));
		return 1;
	}
	memset(sheet, 0, sheet_width * sheet_height);

	for (int i = 0; i < 128; ++i) {

		if (header->width[i] == 0)
			continue;

		int bytes_in_width = (header->width[i] + 3) / 4;

		int character = i + 1;
		if (character >= 0x20 && character <= 0x7e)
			printf("glyph[%3d] (%c) width: %d:%d, offset: %04x\n",
				i, character,
				header->width[i], bytes_in_width,
				header->offset[i]);
		else
			printf("glyph[%3d] %02x  width: %d:%d, offset: %04x\n",
				i, character,
				header->width[i], bytes_in_width,
				header->offset[i]);

		const uint8_t *packed = ff->data + header->offset[i];

		size_t cell = (i % 16) * header->max_width
		            + (i / 16) * header->cell_height * sheet_width;

		for (int height = 0; height < header->cell_height; ++height) {
			for (int width = 0; width < header->width[i]; ++width) {

				int byte = (width / 4);
				int shift = 6 - (width % 4) * 2;

				sheet[cell + width + (height * sheet_width)] =
					((packed[(bytes_in_width * height) + byte] >> shift) & 0x03) * 85;
			}
		}
	}

	/* The sheet holds coverage, not palette indices: a glyph pixel is
	   two bits scaled to 0, 85, 170 or 255. A gray ramp writes it out
	   as it stands -- i / 4 is the six-bit gray whose eight-bit
	   expansion is i again at all four levels. */
	uint8_t ramp[MADS_PALETTE_SIZE];
	for (int i = 0; i < 256; ++i)
		ramp[i * 3 + 0] = ramp[i * 3 + 1] = ramp[i * 3 + 2] = i / 4;

	std::string outpath = std::format("{}/{}.tga", extract_dir, filename);
	int error = tgaify(sheet, sheet_width, sheet_height, ramp, outpath.c_str());
	free(sheet);

	if (error != 0)
		return 1;

	return 0;
}

/*
 * Trying the binders is the identification: the archives carry no
 * readable type, and the formats' layouts do not overlap, so at
 * most one binds. A binder that gets past its layout checks has
 * identified the archive, so its refusal is the archive's error, not
 * a reason to try the next -- except the font binder, whose layout is
 * any single member and whose glyph check fails on 157 real archives
 * of other kinds.
 */

struct view {
	int type;   /* the MADS_TYPE_ that bound, or 0 */
	int error;  /* when type is 0: the code of a binder that knew the
	               layout but refused the content, else 0 */
	union {
		struct mads_pik pik;
		struct mads_ss ss;
		struct mads_ff ff;
	};
};

/* nonzero when the binder settled the archive: it bound, or it knew
   the layout and refused the content */
static int settled(struct view *view, int result, int layout, int type)
{
	if (result == 0)
		view->type = type;
	else if (result != layout)
		view->error = result;
	else
		return 0;

	return 1;
}

static int identify(struct view *view, const struct madspack *mp)
{
	view->type = 0;
	view->error = 0;

	if (settled(view, mads_pik_bind(&view->pik, mp), MADS_PIK_LAYOUT, MADS_TYPE_PIK))
		return view->type;

	if (settled(view, mads_ss_bind(&view->ss, mp), MADS_SS_LAYOUT, MADS_TYPE_SS))
		return view->type;

	if (mads_ff_bind(&view->ff, mp) == 0)
		view->type = MADS_TYPE_FF;

	return view->type;
}

static int unpack(const char *path, const char *extract_dir)
{
	struct madspack mp;
	int error = madspack_load(&mp, path);
	if (error != 0) {
		if (mp.error_member < 0)
			fprintf(stderr, "%s: %s\n", path, mads_strerror(error));
		else
			fprintf(stderr, "%s: member %d: %s\n",
				path, mp.error_member, mads_strerror(error));
		madspack_free(&mp);
		return EXIT_FAILURE;
	}

	/* The stamp never decides: identify does. One that binds as
	   nothing is listed only; one a binder refused is listed, then
	   fails. */
	struct view view;

	const char *format;
	switch (identify(&view, &mp)) {
		case MADS_TYPE_PIK: format = "picture"; break;
		case MADS_TYPE_SS:  format = "sprite"; break;
		case MADS_TYPE_FF:  format = "font"; break;
		default:            format = "unknown"; break;
	}

	/* the readable part of the signature; %s would print its SUB byte */
	printf("signature: %.12s\n", mp.header.signature);
	printf("length: %d\n", mp.header.length);

	size_t offset = 0;
	for (int i = 0; i < mp.header.length; ++i) {
		const struct madspack_record &member = mp.header.member[i];

		printf(
			"  [%2d]: 0x%05zx %s %2x (%8x/%8x) %s\n",
			i, sizeof (struct madspack_header) + offset,
			member.codec ? "fab" : "raw",
			member.stamp,
			member.decoded_size,
			member.encoded_size,
			format
		);

		offset += member.encoded_size;
	}

	if (view.error != 0) {
		fprintf(stderr, "%s: %s\n", path, mads_strerror(view.error));
		madspack_free(&mp);
		return EXIT_FAILURE;
	}

	if (extract_dir == NULL) {
		madspack_free(&mp);
		return EXIT_SUCCESS;
	}

	/* The images are named after the input, but land in the extract
	   directory rather than beside it. */
	const char *filename = strrchr(path, '/');
	filename = filename != NULL ? filename + 1 : path;

	int status = 0;

	switch (view.type) {
		case MADS_TYPE_PIK:
			if (view.pik.palette_size != MADS_PALETTE_SIZE) {
				fprintf(stderr, "%s: no full palette member\n", path);
				status = 1;
				break;
			}
			status = picture(&view.pik, extract_dir, filename);
			break;

		case MADS_TYPE_SS:
			if (view.ss.palette_size != MADS_PALETTE_SIZE) {
				fprintf(stderr, "%s: no full palette member\n", path);
				status = 1;
				break;
			}
			status = sprite(&view.ss, extract_dir, filename);
			break;

		case MADS_TYPE_FF:
			status = font(&view.ff, extract_dir, filename);
			break;

		default:
			break;
	}

	madspack_free(&mp);

	return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
	const char *program = argv[0];
	const char *extract_dir = NULL;

	static struct option long_options[] = {
		{ "extract", 1, 0, 'x' },
		{ "help",    0, 0, 'h' },
		{ "version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	int c;
	while ((c = getopt_long(argc, argv, "hv", long_options, NULL)) != -1) {
		switch (c) {
			case 'h':
				printf(
					"Usage: %s [options] <FILE>\n"
					"MADS archive inspection and extraction tool\n"
					"\n"
					"The file's signature picks the handler. The file is listed\n"
					"only: writing anything requires --extract DIR.\n"
					"\n"
					"Options:\n"
					"      --extract DIR  write the images or the container's files into DIR\n"
					"      --help         display this help and exit\n"
					"      --version      display version information and exit\n"
					"\n"
					"Report bugs to <madstools@slipsprogrammor.no>.\n", program);
				return EXIT_SUCCESS;

			case 'x':
				extract_dir = optarg;
				break;

			case 'v':
				printf(
					"mads - MADS archive inspection and extraction tool\n"
					"Copyright (C) 2008 - 2026 Erik Berg\n"
					"License GPLv3: GNU GPL version 3 <http://gnu.org/licenses/gpl.html>\n"
					"This is free software: you are free to change and redistribute it.\n"
					"There is NO WARRANTY, to the extent permitted by law.\n");
				return EXIT_SUCCESS;

			case '?':
				fprintf(stderr, "Try '%s --help' for more information.\n", program);
				return EXIT_FAILURE;
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "%s: no file given\n", program);
		fprintf(stderr, "Try '%s --help' for more information.\n", program);
		return EXIT_FAILURE;
	}

	if (extract_dir != NULL) {
		if (mkdir(extract_dir, 0777) != 0 && errno != EEXIST) {
			fprintf(stderr, "%s: cannot create: %s\n", extract_dir, strerror(errno));
			return EXIT_FAILURE;
		}

		/* EEXIST alone does not make it a directory */
		struct stat st;
		if (stat(extract_dir, &st) != 0) {
			fprintf(stderr, "%s: cannot stat: %s\n", extract_dir, strerror(errno));
			return EXIT_FAILURE;
		}
		if (!S_ISDIR(st.st_mode)) {
			fprintf(stderr, "%s: not a directory\n", extract_dir);
			return EXIT_FAILURE;
		}
	}

	const char *filename = argv[optind];

	int fmt = sniff(filename);

	switch(fmt) {
		case FORMAT_MADSCONCAT:
			return split(filename, extract_dir);
		case FORMAT_MADSPACK:
			return unpack(filename, extract_dir);
		case FORMAT_UNKNOWN:
			fprintf(stderr, "%s: unrecognized signature\n", filename);
			return EXIT_FAILURE;
		case FORMAT_UNREADABLE:
			return EXIT_FAILURE;
	}
}
