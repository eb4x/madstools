#include <assert.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mads.h"

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

	static const char madspack_sig[] = "MADSPACK 2.0\x1a\x00";
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
					"Usage: %s <FILE.SS>\n"
					"MADSPACK 2.0 image extracting tool\n"
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
					"viceroy - MADSPACK 2.0 image extracting tool\n"
					"Copyright (C) 2008 - 2009 Erik Berg\n"
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

	return unpack(filename, header_only);
}
