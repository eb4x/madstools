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

static int load_palette(const char *path, uint8_t *palette)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL) {
		fprintf(stderr, "%s: cannot open: %s\n", path, strerror(errno));
		return 1;
	}

	size_t size = fread(palette, 1, MADS_PALETTE_SIZE, fp);
	fclose(fp);

	if (size != MADS_PALETTE_SIZE) {
		fprintf(stderr, "%s: shorter than a full palette\n", path);
		return 1;
	}

	for (size_t i = 0; i < MADS_PALETTE_SIZE; ++i) {
		if (palette[i] > 0x3f) {
			fprintf(stderr, "%s: component above 0x3f\n", path);
			return 1;
		}
	}

	return 0;
}

static int split(const char *path, const char *extract_dir)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: cannot open: %s\n", path, strerror(errno));
		return 1;
	}

	struct stat st;
	if (fstat(fd, &st) != 0) {
		fprintf(stderr, "%s: cannot stat: %s\n", path, strerror(errno));
		close(fd);
		return 1;
	}

	if (st.st_size < (off_t) sizeof (struct madsconcat_header)) {
		fprintf(stderr, "%s: shorter than a MADSCONCAT header\n", path);
		close(fd);
		return 1;
	}

	size_t size = st.st_size;
	const uint8_t *map = (const uint8_t *) mmap(NULL, size, PROT_READ,
	                                            MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		fprintf(stderr, "%s: cannot map: %s\n", path, strerror(errno));
		return 1;
	}

	struct madsconcat mc;
	int error = madsconcat_bind(&mc, map, size);
	if (error != 0) {
		fprintf(stderr, "%s: %s\n", path, mads_strerror(error));
		munmap((void *) map, size);
		return 1;
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
			return 1;
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
			return 1;
		}
	}

	munmap((void *) map, size);

	return 0;
}

static int picture(const struct mads_pik *pik, const uint8_t *palette,
                   const char *extract_dir, const char *filename)
{
	printf("picture: %ux%u\n", pik->header->width, pik->header->height);

	std::string outpath = std::format("{}/{}.tga", extract_dir, filename);
	if (tgaify(pik->pixels, pik->header->width, pik->header->height,
	           palette, outpath.c_str()) != 0)
		return 1;

	return 0;
}

/* One image per sprite. The listing is of the bound view: a packed
   series shows its decoded offsets, and the member's disk size beside
   the decoded total says so. */
static int sprite(const struct mads_ss *ss, const struct madspack *mp,
                  const uint8_t *palette, const char *extract_dir,
                  const char *filename)
{
	printf("ss_header: sprite_table_length: %u, sprites_size: %u (member 3: %u on disk)\n",
		ss->header->sprite_table_length, ss->header->sprites_size,
		mp->header.member[3].encoded_size);

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

		/* the draw leaves transparent pixels to the backdrop */
		memset(pixels, MADS_RLE_TRANSPARENT, pixels_size);

		const struct mads_surface dst = { sprite.width, sprite.height,
		                                  sprite.width, pixels };

		int error = mads_sprite_draw(ss, i, &dst, 0, 0);
		if (error != 0) {
			fprintf(stderr, "%s: sprite %d: %s\n", filename, i, mads_strerror(error));
			free(pixels);
			return 1;
		}

		std::string outpath = std::format("{}/{}.{:02}.tga", extract_dir, filename, i);
		error = tgaify(pixels, sprite.width, sprite.height,
		               palette, outpath.c_str());
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

	const struct mads_surface dst = { sheet_width, sheet_height, sheet_width, sheet };

	/* every glyph pixel lands as its own 2-bit level; the palette
	   below grays them */
	const uint8_t level[4] = { 0, 1, 2, 3 };

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

		mads_glyph_draw(ff, i, &dst,
		                (i % 16) * header->max_width,
		                (i / 16) * header->cell_height,
		                level);
	}

	/* Four six-bit grays, 0 to 63, one per level: their eight-bit
	   expansions are 0, 85, 170 and 255. */
	uint8_t ramp[MADS_PALETTE_SIZE] = { 0 };
	for (int i = 0; i < 4; ++i)
		ramp[i * 3 + 0] = ramp[i * 3 + 1] = ramp[i * 3 + 2] = i * 21;

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

static int identify(struct view *view, struct madspack *mp)
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

static int unpack(const char *path, const char *extract_dir,
                  const uint8_t *fallback_palette)
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
		return 1;
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
		if (mp.error_member < 0)
			fprintf(stderr, "%s: %s\n", path, mads_strerror(view.error));
		else
			fprintf(stderr, "%s: member %d: %s\n",
				path, mp.error_member, mads_strerror(view.error));
		madspack_free(&mp);
		return 1;
	}

	if (extract_dir == NULL) {
		madspack_free(&mp);
		return 0;
	}

	/* The images are named after the input, but land in the extract
	   directory rather than beside it. */
	const char *filename = strrchr(path, '/');
	filename = filename != NULL ? filename + 1 : path;

	/* Pictures and sprites are colored by the archive's own palette
	   member when it is the full form, triple N coloring pixel value N.
	   A two-member picture and a sparse series carry none and fall
	   back to the one --palette names. */
	const uint8_t *palette = NULL;

	if (view.type == MADS_TYPE_PIK || view.type == MADS_TYPE_SS) {
		bool is_picture = view.type == MADS_TYPE_PIK;

		uint32_t member_size = is_picture ? view.pik.palette_size
		                                  : view.ss.palette_size;

		if (member_size == MADS_PALETTE_SIZE)
			palette = is_picture ? view.pik.palette : view.ss.palette;
		else
			palette = fallback_palette;

		if (palette == NULL) {
			fprintf(stderr, "%s: no full palette member; "
				"name one with --palette\n", path);
			madspack_free(&mp);
			return 1;
		}
	}

	int status = 0;

	switch (view.type) {
		case MADS_TYPE_PIK:
			status = picture(&view.pik, palette, extract_dir, filename);
			break;

		case MADS_TYPE_SS:
			status = sprite(&view.ss, &mp, palette, extract_dir, filename);
			break;

		case MADS_TYPE_FF:
			status = font(&view.ff, extract_dir, filename);
			break;

		default:
			break;
	}

	madspack_free(&mp);

	return status;
}

int main(int argc, char *argv[])
{
	const char *program = argv[0];
	const char *extract_dir = NULL;
	const char *palette_path = NULL;

	static struct option long_options[] = {
		{ "extract", 1, 0, 'x' },
		{ "palette", 1, 0, 'p' },
		{ "help",    0, 0, 'h' },
		{ "version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	int c;
	while ((c = getopt_long(argc, argv, "hv", long_options, NULL)) != -1) {
		switch (c) {
			case 'h':
				printf(
					"Usage: %s [options] <FILE>...\n"
					"MADS archive inspection and extraction tool\n"
					"\n"
					"The file's signature picks the handler. The file is listed\n"
					"only: writing anything requires --extract DIR.\n"
					"\n"
					"Options:\n"
					"      --extract DIR  write the images or the container's files into DIR\n"
					"      --palette FILE color the archives that carry no palette\n"
					"                     with this one, 256 six-bit RGB triples\n"
					"      --help         display this help and exit\n"
					"      --version      display version information and exit\n"
					"\n"
					"Report bugs to <madstools@slipsprogrammor.no>.\n", program);
				return EXIT_SUCCESS;

			case 'x':
				extract_dir = optarg;
				break;

			case 'p':
				palette_path = optarg;
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

	uint8_t fallback_palette[MADS_PALETTE_SIZE];

	if (palette_path != NULL) {
		if (load_palette(palette_path, fallback_palette) != 0)
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

	/* One bad file fails the run without stopping the rest. */
	int failures = 0;
	for (int i = optind; i < argc; ++i) {
		const char *filename = argv[i];

		switch (sniff(filename)) {
			case FORMAT_MADSCONCAT:
				if (split(filename, extract_dir) != 0)
					++failures;
				break;

			case FORMAT_MADSPACK:
				if (unpack(filename, extract_dir,
				           palette_path != NULL ? fallback_palette : NULL) != 0)
					++failures;
				break;

			case FORMAT_UNKNOWN:
				fprintf(stderr, "%s: unrecognized signature\n", filename);
				++failures;
				break;

			case FORMAT_UNREADABLE:
				++failures;
				break;
		}
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
