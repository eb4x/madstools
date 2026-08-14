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

static int split(const char *path, int header_only)
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

	const struct madsconcat_header *header =
		(const struct madsconcat_header *) map;

	if (memcmp(MADSCONCAT_SIG, header->signature, sizeof (header->signature)) != 0) {
		fprintf(stderr, "MADSCONCAT signature mismatch.\n");
		munmap((void *) map, size);
		return EXIT_FAILURE;
	}

	printf("length: %d\n", header->length);

	const struct madsconcat_record *file_table =
		(const struct madsconcat_record *) (map + sizeof (struct madsconcat_header));

	if (sizeof (struct madsconcat_header) +
	    (size_t) header->length * sizeof (struct madsconcat_record) > size) {
		fprintf(stderr, "<%s> ends inside its file table\n", path);
		munmap((void *) map, size);
		return EXIT_FAILURE;
	}

	for (int i = 0; i < header->length; ++i) {
		const struct madsconcat_record &file = file_table[i];

		printf("%s[%3d]: offset(%06x), size(%06x) - [%s]\n", path, i, file.offset, file.size, file.name);

		if ((size_t) file.offset + file.size > size) {
			fprintf(stderr, "<%s> ends inside member [%s]\n", path, file.name);
			munmap((void *) map, size);
			return EXIT_FAILURE;
		}

		if (header_only)
			continue;

		FILE *out = fopen(file.name, "wb");
		if (out == NULL) {
			fprintf(stderr, "%s: cannot write: %s\n", file.name, strerror(errno));
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
			fprintf(stderr, "%s: cannot write: %s\n", file.name, strerror(errno));
			munmap((void *) map, size);
			return EXIT_FAILURE;
		}
	}

	munmap((void *) map, size);

	return EXIT_SUCCESS;
}

static int sprite(const struct mads_ss *ss, const char *path)
{
	printf("ss_header: sprite_table_length: %u, sprites_size: %u\n",
		ss->header->sprite_table_length, ss->header->sprites_size);

	for (uint16_t i = 0; i < ss->length; ++i) {
		const struct mads_sprite_record &sprite = ss->sprite_table[i];

		size_t pixels_size = (size_t) sprite.width * sprite.height;

		printf("%s/sprite[%2d]: offset: 0x%04x, size: 0x%04x:%04zx, %3dx%3d\n", path, i,
			sprite.offset, sprite.size, pixels_size,
			sprite.width,  sprite.height);

		/* a zero-area sprite is a placeholder; nothing to decode,
		   and malloc(0) may return the NULL the error path reads
		   as out of memory */
		if (pixels_size == 0)
			continue;

		uint8_t *pixels = (uint8_t *) malloc(pixels_size);
		if (pixels == NULL) {
			fprintf(stderr, "%s: sprite %d: %s\n", path, i,
				mads_strerror(MADS_NOMEM));
			return 1;
		}

		int error = mads_rle_decode(pixels, sprite.width, sprite.height,
		                            ss->streams + sprite.offset, sprite.size);
		if (error != 0) {
			fprintf(stderr, "%s: sprite %d: %s\n", path, i, mads_strerror(error));
			free(pixels);
			return 1;
		}

		std::string filename = std::format("{}.{:02}.tga", path, i);
		tgaify(pixels, sprite.width, sprite.height, filename.c_str());
		free(pixels);
	}

	return 0;
}

static int unpack(const char *path, int header_only)
{
	enum { ANYTHING = 0, PIK = 2, SS = 4, FF = 7 };

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

	/* the readable part of the signature; %s would print its SUB byte */
	printf("signature: %.12s\n", mp.header.signature);
	printf("length: %d\n", mp.header.length);

	size_t offset = 0;
	for (int i = 0; i < mp.header.length; ++i) {
		const struct madspack_record &member = mp.header.member[i];

		printf(
			"  [%2d]: 0x%05lx %s %2x (%8x/%8x) ",
			i, sizeof (struct madspack_header) + offset,
			member.codec ? "fab" : "raw",
			member.stamp,
			member.decoded_size,
			member.encoded_size
		);

		switch (member.stamp) {
			case PIK: printf("pic\n"); break;
			case SS:  printf("sprite\n"); break;
			case FF:  printf("font\n"); break;
			default: printf("unknown\n"); break;
		}

		offset += member.encoded_size;
	}

	if (header_only) {
		madspack_free(&mp);
		return EXIT_SUCCESS;
	}

	int status = 0;

	switch (mp.header.member[0].stamp) {
		case PIK: break;

		case SS: {
			struct mads_ss ss;
			error = mads_ss_bind(&ss, &mp);
			if (error != 0) {
				fprintf(stderr, "%s in <%s>\n", mads_strerror(error), path);
				status = 1;
			} else {
				status = sprite(&ss, path);
			}
			break;
		}

		case FF:  font(  mp.decoded[0], mp.header.member[0].decoded_size); break;

		case ANYTHING: break;

		default: break;
	}

	madspack_free(&mp);

	return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
	const char *program = argv[0];
	int header_only = 0;

	static struct option long_options[] = {
		{ "help",    0, 0, 'h' },
		{ "header",  0, 0, 'i' },
		{ "version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	int c;
	while ((c = getopt_long(argc, argv, "hiv", long_options, NULL)) != -1) {
		switch (c) {
			case 'h':
				printf(
					"Usage: %s [options] <FILE>\n"
					"MADS archive inspection and extraction tool\n"
					"\n"
					"Options:\n"
					"      --help     display this help and exit\n"
					"      --version  display version information and exit\n"
					"\n"
					"Report bugs to <madstools@slipsprogrammor.no>.\n", program);
				return EXIT_SUCCESS;

			case 'i':
				header_only = 1;
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

	const char *filename = argv[optind];

	int fmt = sniff(filename);

	switch(fmt) {
		case FORMAT_MADSCONCAT:
			return split(filename, header_only);
		case FORMAT_MADSPACK:
			return unpack(filename, header_only);
		case FORMAT_UNKNOWN:
			fprintf(stderr, "%s: unrecognized signature\n", filename);
			return EXIT_FAILURE;
		case FORMAT_UNREADABLE:
			return EXIT_FAILURE;
	}
}
