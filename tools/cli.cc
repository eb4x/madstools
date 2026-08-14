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

static const char madsconcat_sig[] = "MADSCONCAT 1.0\x1a\x00";
static const char madspack_sig[] = "MADSPACK 2.0\x1a\x00";

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

	if (size >= sizeof (madsconcat_sig) - 1 &&
	    memcmp(signature, madsconcat_sig, sizeof (madsconcat_sig) - 1) == 0)
		return FORMAT_MADSCONCAT;

	if (size >= sizeof (madspack_sig) - 1 &&
	    memcmp(signature, madspack_sig, sizeof (madspack_sig) - 1) == 0)
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

	struct madsconcat_header {
		char signature[0x10];
		uint16_t length;
	} __attribute__ ((packed));

	struct madsconcat_record {
		uint32_t offset;
		uint32_t size;
		char name[0x0e];
	} __attribute__ ((packed));

	assert(sizeof (struct madsconcat_header) == 0x12);
	assert(sizeof (struct madsconcat_record) == 0x16);

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

	if (memcmp(madsconcat_sig, header->signature, sizeof (header->signature)) != 0) {
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

	struct madspack_header {
		char signature[0x0e];
		uint16_t entries;
		struct madspack_entry {
			struct flags {
				uint8_t packed;
				uint8_t type;
			} __attribute__ ((packed)) flag;
			uint32_t unpacked_size;
			uint32_t packed_size;
		} __attribute__ ((packed)) entry[16];
	} __attribute__ ((packed)) mp_header;

	enum { ANYTHING = 0, PIK = 2, SS = 4, FF = 7 };

	assert(sizeof (struct madspack_header) == 0xb0);
	fread(&mp_header, sizeof (struct madspack_header), 1, fp);

	if (strncmp(madspack_sig, mp_header.signature, 0x0e) != 0) {
		printf("MADSPACK Signature mismatch.\n");
		fclose(fp);
		return EXIT_FAILURE;
	}

	/* the readable part of the signature; %s would print its SUB byte */
	printf("signature: %.12s\n", mp_header.signature);
	printf("entries:   %d\n", mp_header.entries);

	struct data_container {
		uint8_t *unpacked;
		uint8_t *packed;
	} dc[mp_header.entries];

	size_t offset = 0;
	for (int i = 0; i < mp_header.entries; ++i) {

		printf(
			"  [%2d]: 0x%05lx %s %2x (%8x/%8x) ",
			i, sizeof (struct madspack_header) + offset,
			mp_header.entry[i].flag.packed ? "packed" : "unpacked",
			mp_header.entry[i].flag.type,
			mp_header.entry[i].unpacked_size,
			mp_header.entry[i].packed_size
		);

		switch (mp_header.entry[i].flag.type) {
			case PIK: printf("pic\n"); break;
			case SS:  printf("sprite\n"); break;
			case FF:  printf("font\n"); break;
			case ANYTHING: printf("anything\n"); break;
			default: printf("unknown(%02x)\n", mp_header.entry[i].flag.type); break;
		}

		offset += mp_header.entry[i].packed_size;

		if (header_only)
			continue;

		dc[i].packed   = (uint8_t *) malloc(mp_header.entry[i].packed_size);
		dc[i].unpacked = (uint8_t *) malloc(mp_header.entry[i].unpacked_size);

		fread(dc[i].packed, mp_header.entry[i].packed_size, 1, fp);

		if (mp_header.entry[i].flag.packed) {
			uint16_t result = unpack(dc[i].packed, dc[i].unpacked);
			assert(result == mp_header.entry[i].unpacked_size);
		} else
			memcpy(dc[i].unpacked, dc[i].packed, mp_header.entry[i].unpacked_size);
	}

	if (header_only)
		return EXIT_SUCCESS;

	switch (mp_header.entry[0].flag.type) {
		case PIK: break;

		case SS:  sprite(dc[1].unpacked, mp_header.entry[1].unpacked_size,
		                 dc[3].unpacked, mp_header.entry[3].unpacked_size, path); break;

		case FF:  font(  dc[0].unpacked, mp_header.entry[0].unpacked_size); break;

		case ANYTHING: break;

		default: break;
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
