#include <assert.h>
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

#include "mads.h"

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

static int unpack(const char *path, int header_only)
{
	FILE *fp = fopen(path, "r");
	if (fp == NULL) {
		printf("Failed to open file <%s>\n", path);
		return EXIT_FAILURE;
	}

	struct madspack_header mp_header;

	enum { ANYTHING = 0, PIK = 2, SS = 4, FF = 7 };

	fread(&mp_header, sizeof (struct madspack_header), 1, fp);

	if (strncmp(MADSPACK_SIG, mp_header.signature, sizeof (mp_header.signature)) != 0) {
		printf("MADSPACK Signature mismatch.\n");
		fclose(fp);
		return EXIT_FAILURE;
	}

	/* the readable part of the signature; %s would print its SUB byte */
	printf("signature: %.12s\n", mp_header.signature);
	printf("length: %d\n", mp_header.length);

	uint8_t *decoded[MADSPACK_RECORDS] = { NULL };

	size_t offset = 0;
	for (int i = 0; i < mp_header.length; ++i) {
		const struct madspack_record &member = mp_header.member[i];

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

		if (header_only)
			continue;

		uint8_t *encoded = (uint8_t *) malloc(member.encoded_size);
		decoded[i]       = (uint8_t *) malloc(member.decoded_size);

		fread(encoded, member.encoded_size, 1, fp);

		if (member.codec) {
			uint16_t result = unpack(encoded, decoded[i]);
			assert(result == member.decoded_size);
		} else
			memcpy(decoded[i], encoded, member.decoded_size);

		free(encoded);
	}

	if (header_only)
		return EXIT_SUCCESS;

	switch (mp_header.member[0].stamp) {
		case PIK: break;

		case SS:  sprite(decoded[1], mp_header.member[1].decoded_size,
		                 decoded[3], mp_header.member[3].decoded_size, path); break;

		case FF:  font(  decoded[0], mp_header.member[0].decoded_size); break;

		case ANYTHING: break;

		default: break;
	}

	for (int i = 0; i < mp_header.length; ++i) {
		free(decoded[i]);
	}

	return EXIT_SUCCESS;
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
