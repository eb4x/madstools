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
#include <vector>

#include "mads.h"
#include "util/util.h"

#ifndef MADS_VERSION   /* the build sets it; a bare compile still links */
#define MADS_VERSION "unknown"
#endif

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

	/* bound only for its checks: the file is the full form already */
	struct mads_palette pal;
	int error = mads_palette_bind(&pal, palette, MADS_PALETTE_SIZE);
	if (error != 0) {
		fprintf(stderr, "%s: %s\n", path, mads_strerror(error));
		return 1;
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

/* One --overlay: the sprite series it named, kept loaded for the run
   since the view aliases the archive. */
struct overlay {
	const char *path;
	const char *filename;   /* path's last component, set once loaded */
	struct madspack mp;
	struct mads_ss ss;
};

static void sprite_list(const struct mads_ss *ss, const struct madspack *mp,
                        const char *filename);

/* The picture under every overlay, each sprite at its authored
   position, one image per frame: as many frames as the longest series
   has sprites, a shorter one starting over when it runs out. Colored
   by the picture's palette. */
static int composite(const struct mads_pik *pik, const uint8_t *palette,
                     const struct overlay overlays[], size_t overlay_length,
                     const char *extract_dir, const char *filename)
{
	size_t width  = pik->header->width;
	size_t height = pik->header->height;

	printf("picture: %zux%zu\n", width, height);

	size_t frames = 0;
	for (size_t i = 0; i < overlay_length; ++i) {
		sprite_list(&overlays[i].ss, &overlays[i].mp, overlays[i].filename);

		if (overlays[i].ss.length > frames)
			frames = overlays[i].ss.length;
	}

	if (frames == 0) {
		fprintf(stderr, "%s: no sprite to draw\n", filename);
		return 0;
	}

	uint8_t *canvas = (uint8_t *) malloc(width * height);
	if (canvas == NULL) {
		fprintf(stderr, "%s: %s\n", filename, mads_strerror(MADS_NOMEM));
		return 1;
	}

	const struct mads_surface dst = { width, height, width, canvas };

	for (size_t frame = 0; frame < frames; ++frame) {
		memcpy(canvas, pik->pixels, width * height);

		for (size_t i = 0; i < overlay_length; ++i) {
			const struct mads_ss &ss = overlays[i].ss;

			if (ss.length == 0)
				continue;

			uint16_t j = frame % ss.length;
			const struct mads_sprite_record &sprite = ss.sprite_table[j];

			int error = mads_sprite_draw(&ss, j, &dst,
			                             sprite.x - sprite.width / 2,
			                             sprite.y - sprite.height + 1);
			if (error != 0) {
				fprintf(stderr, "%s: sprite %d: %s\n", overlays[i].path, j,
					mads_strerror(error));
				free(canvas);
				return 1;
			}
		}

		std::string outpath = std::format("{}/{}.{:02}.tga", extract_dir, filename, frame);
		if (tgaify(canvas, width, height, palette, -1, outpath.c_str()) != 0) {
			free(canvas);
			return 1;
		}
	}

	free(canvas);
	return 0;
}

static int picture(const struct mads_pik *pik, const uint8_t *palette,
                   const char *extract_dir, const char *filename)
{
	printf("picture: %ux%u\n", pik->header->width, pik->header->height);

	std::string outpath = std::format("{}/{}.tga", extract_dir, filename);
	if (tgaify(pik->pixels, pik->header->width, pik->header->height,
	           palette, -1, outpath.c_str()) != 0)
		return 1;

	return 0;
}

/* The one-pixel ring just outside the width x height box at (x, y),
   which the cell is sized to hold. */
static void border(const struct mads_surface *dst, size_t x, size_t y,
                   size_t width, size_t height, uint8_t color)
{
	uint8_t *top    = dst->pixels + (y - 1) * dst->pitch + x - 1;
	uint8_t *bottom = dst->pixels + (y + height) * dst->pitch + x - 1;

	memset(top,    color, width + 2);
	memset(bottom, color, width + 2);

	for (size_t row = y; row < y + height; ++row) {
		dst->pixels[row * dst->pitch + x - 1]     = color;
		dst->pixels[row * dst->pitch + x + width] = color;
	}
}

/* Every sprite of the series in one image: a near-square grid of
   uniform cells, each sprite ringed around its own box so its extent
   shows inside the shared cell. */
static int sprite_sheet(const struct mads_ss *ss,
                        const char *extract_dir, const char *filename)
{
	size_t max_width = 0;
	size_t max_height = 0;

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record &sprite = ss->sprite_table[i];

		if (sprite.width > max_width)
			max_width = sprite.width;
		if (sprite.height > max_height)
			max_height = sprite.height;
	}

	if (max_width == 0 || max_height == 0) {
		fprintf(stderr, "%s: no sprite to draw\n", filename);
		return 0;
	}

	size_t columns = 1;
	while (columns * columns < ss->length)
		++columns;
	size_t rows = (ss->length + columns - 1) / columns;

	size_t cell_width  = max_width + 3;   /* border and gap */
	size_t cell_height = max_height + 3;

	size_t sheet_width  = columns * cell_width;
	size_t sheet_height = rows * cell_height;

	uint8_t *sheet = (uint8_t *) malloc(sheet_width * sheet_height);
	if (sheet == NULL) {
		fprintf(stderr, "%s: %s\n", filename, mads_strerror(MADS_NOMEM));
		return 1;
	}

	/* the draw leaves transparent pixels to the backdrop */
	memset(sheet, MADS_RLE_TRANSPARENT, sheet_width * sheet_height);

	const struct mads_surface dst = { sheet_width, sheet_height, sheet_width, sheet };

	for (uint16_t i = 0; i < ss->length; ++i) {
		size_t x = (i % columns) * cell_width + 1;
		size_t y = (i / columns) * cell_height + 1;

		int error = mads_sprite_draw(ss, i, &dst, x, y);
		if (error != 0) {
			fprintf(stderr, "%s: sprite %d: %s\n", filename, i, mads_strerror(error));
			free(sheet);
			return 1;
		}
	}

	uint8_t sheet_palette[MADS_PALETTE_SIZE];
	mads_palette_full(sheet_palette, &ss->palette);

	/* past every palette's reach, so the border cannot be mistaken
	   for pixels */
	uint8_t border_color = MADS_PALETTE_LENGTH_MAX;
	sheet_palette[border_color * 3 + 0] = 0x20;
	sheet_palette[border_color * 3 + 1] = 0x20;
	sheet_palette[border_color * 3 + 2] = 0x20;

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record &sprite = ss->sprite_table[i];

		if (sprite.width == 0 || sprite.height == 0)
			continue;

		size_t x = (i % columns) * cell_width + 1;
		size_t y = (i / columns) * cell_height + 1;

		border(&dst, x, y, sprite.width, sprite.height, border_color);
	}

	std::string outpath = std::format("{}/{}.tga", extract_dir, filename);
	int error = tgaify(sheet, sheet_width, sheet_height, sheet_palette,
	                   MADS_RLE_TRANSPARENT, outpath.c_str());
	free(sheet);

	if (error != 0)
		return 1;

	return 0;
}

/* The listing is of the bound view: a packed series shows its decoded
   offsets, and the member's disk size beside the decoded total says so. */
static void sprite_list(const struct mads_ss *ss, const struct madspack *mp,
                        const char *filename)
{
	printf("ss_header: sprite_table_length: %u, sprites_size: %u (member 3: %u on disk)\n",
		ss->header->sprite_table_length, ss->header->sprites_size,
		mp->header.member[3].encoded_size);

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record &sprite = ss->sprite_table[i];

		size_t pixels_size = (size_t) sprite.width * sprite.height;

		printf("%s/sprite[%2d]: offset: 0x%04x, size: 0x%04x:%04zx, %3dx%3d at (%d,%d)\n",
			filename, i, sprite.offset, sprite.size, pixels_size,
			sprite.width, sprite.height, sprite.x, sprite.y);
	}
}

/* One image per sprite, colored by the series' own palette, which
   every series carries. */
static int sprite(const struct mads_ss *ss,
                  const char *extract_dir, const char *filename)
{
	uint8_t palette[MADS_PALETTE_SIZE];
	mads_palette_full(palette, &ss->palette);

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record &sprite = ss->sprite_table[i];

		size_t pixels_size = (size_t) sprite.width * sprite.height;

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
		               palette, MADS_RLE_TRANSPARENT, outpath.c_str());
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
	int error = tgaify(sheet, sheet_width, sheet_height, ramp, -1, outpath.c_str());
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
                  const uint8_t *fallback_palette, bool sheet,
                  const struct overlay overlays[], size_t overlay_length)
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

	/* A picture is colored by its own palette member, either form; one
	   that carries none falls back to the --palette file. */
	uint8_t own_palette[MADS_PALETTE_SIZE];
	const uint8_t *palette = NULL;

	if (view.type == MADS_TYPE_PIK) {
		if (view.pik.palette.length != 0) {
			mads_palette_full(own_palette, &view.pik.palette);
			palette = own_palette;
		} else {
			palette = fallback_palette;
		}

		if (palette == NULL) {
			fprintf(stderr, "%s: no palette member; name one with --palette "
				"(768 bytes of six-bit RGB, e.g. Colonization's VICEROY.PAL)\n",
				path);
			madspack_free(&mp);
			return 1;
		}
	}

	if (sheet && view.type != MADS_TYPE_SS)
		fprintf(stderr, "%s: not a sprite series; --sheet ignored\n", path);

	if (overlay_length > 0 && view.type != MADS_TYPE_PIK)
		fprintf(stderr, "%s: not a picture; --overlay ignored\n", path);

	int status = 0;

	switch (view.type) {
		case MADS_TYPE_PIK:
			if (overlay_length > 0)
				status = composite(&view.pik, palette, overlays, overlay_length,
				                   extract_dir, filename);
			else
				status = picture(&view.pik, palette, extract_dir, filename);
			break;

		case MADS_TYPE_SS:
			sprite_list(&view.ss, &mp, filename);
			status = sheet
				? sprite_sheet(&view.ss, extract_dir, filename)
				: sprite(&view.ss, extract_dir, filename);
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
	bool sheet = false;
	std::vector<struct overlay> overlays;

	static struct option long_options[] = {
		{ "extract", 1, 0, 'x' },
		{ "palette", 1, 0, 'p' },
		{ "sheet",   0, 0, 's' },
		{ "overlay", 1, 0, 'o' },
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
					"                     (Colonization's VICEROY.PAL is one)\n"
					"      --sheet        write a sprite series as one image, its\n"
					"                     sprites in a grid, instead of one image each\n"
					"      --overlay FILE write a picture as one image per animation\n"
					"                     frame, this sprite series drawn over it at its\n"
					"                     authored position; may repeat, later series\n"
					"                     on top, a shorter one starting over\n"
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

			case 's':
				sheet = true;
				break;

			case 'o':
				overlays.push_back({ optarg, NULL, {}, {} });
				break;

			case 'v':
				printf(
					"mads " MADS_VERSION " - MADS archive inspection and extraction tool\n"
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

	/* Loaded past the returns above, which would leak an archive. The
	   series' own palettes go unused: the picture's is the screen's. */
	bool loaded = true;

	for (struct overlay &overlay : overlays) {
		int error = madspack_load(&overlay.mp, overlay.path);
		if (error != 0) {
			if (overlay.mp.error_member < 0)
				fprintf(stderr, "%s: %s\n", overlay.path, mads_strerror(error));
			else
				fprintf(stderr, "%s: member %d: %s\n", overlay.path,
					overlay.mp.error_member, mads_strerror(error));
			loaded = false;
			break;
		}

		error = mads_ss_bind(&overlay.ss, &overlay.mp);
		if (error != 0) {
			fprintf(stderr, "%s: %s\n", overlay.path, mads_strerror(error));
			loaded = false;
			break;
		}

		overlay.filename = strrchr(overlay.path, '/');
		overlay.filename = overlay.filename != NULL
		                 ? overlay.filename + 1 : overlay.path;
	}

	if (!loaded) {
		/* never loaded or failed, an mp holds NULL in every slot */
		for (struct overlay &overlay : overlays)
			madspack_free(&overlay.mp);
		return EXIT_FAILURE;
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
				           palette_path != NULL ? fallback_palette : NULL,
				           sheet, overlays.data(), overlays.size()) != 0)
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

	for (struct overlay &overlay : overlays)
		madspack_free(&overlay.mp);

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
