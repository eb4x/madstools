#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mads.h"
#include "util/util.h"


int main(int argc, char *argv[])
{
	int c, optindex = 0;
	int only_mp_header = 0;

	static struct option long_options[] = {
		{ "mp_header", 0, &only_mp_header, 'i' },
		{ "help",     0, 0, 'h' },
		{ "version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	c = getopt_long(argc, argv, "hiv", long_options, &optindex);
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
				"Report bugs to <madstools@slipsprogrammor.no>.\n", argv[0]);
			return EXIT_SUCCESS;

		case 'i': only_mp_header = 1;
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
			fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
			return EXIT_FAILURE;

	}
	
	if (optind >= argc) {
		fprintf(stderr, "Usage: %s <FILE.SS>\n", argv[0]);
		fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
		return EXIT_FAILURE;
	}

	char filename[1024];
	strncpy(filename, argv[optind], 1024);
	//char *ch = strchr(filename, "."); *ch = "_";
	//strlwr(filename);

	FILE *fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("Failed to open file <%s>\n", argv[1]);
		return EXIT_FAILURE;
	}

	fseek(fp, 0, SEEK_END);
	int size = ftell(fp);
	fseek(fp, 0, SEEK_SET);


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
	int res = strncmp(madspack_sig, mp_header.signature, 0x0e);
	if (res != 0) {
		printf("MADSPACK Signature mismatch.\n");
		fclose(fp);
		return EXIT_FAILURE;
	}

	if (only_mp_header) {
		printf("signature: %s\n", mp_header.signature);
		printf("entries:   %d\n", mp_header.entries);
		int offset = 0;
		for (int i = 0; i < 16; ++i) {
			if (i == mp_header.entries)
				printf("====\n");

			printf("  [%x]: %8s %2x (%8x/%8x) : 0x%05x\n",
				i,
				mp_header.entry[i].flag.packed ? "packed" : "unpacked",
				mp_header.entry[i].flag.type,
				mp_header.entry[i].packed_size,
				mp_header.entry[i].unpacked_size,
				sizeof (struct madspack_header) + offset );

			offset += mp_header.entry[i].packed_size;
		}

		return EXIT_SUCCESS;
	}

	struct data_container {
		uint8_t *unpacked;
		uint8_t *packed;
	} dc[mp_header.entries];

	uint16_t result; char dumpname[0x10];
	printf("mp_header.entries == %d\n", mp_header.entries);
	for (int i = 0; i < mp_header.entries; ++i) {
		assert(mp_header.entry[i].flag.packed == 0 ||
		       mp_header.entry[i].flag.packed == 1 );

		printf("fab[%d] packed_size: 0x%05x, unpacked_size: 0x%05x packed:%d type: ", i,
			mp_header.entry[i].packed_size,
			mp_header.entry[i].unpacked_size,
			mp_header.entry[i].flag.packed);

		switch (mp_header.entry[i].flag.type) {
			case PIK: printf("pic\n"); break;
			case SS:  printf("sprite\n"); break;
			case FF:  printf("font\n"); break;
			case ANYTHING: printf("anything\n"); break;
			default: printf("unknown(%02x)\n", mp_header.entry[i].flag.type); break;
		}

		dc[i].packed   = (uint8_t *) malloc(mp_header.entry[i].packed_size);
		dc[i].unpacked = (uint8_t *) malloc(mp_header.entry[i].unpacked_size);

		fread(dc[i].packed, mp_header.entry[i].packed_size, 1, fp);

		if (mp_header.entry[i].flag.packed) {
			printf("Unpacking entry[%d]", i);

			printf("esi:%p, edi:%p\n", dc[i].packed, dc[i].unpacked);
			result = unpack(dc[i].packed, dc[i].unpacked);
			//assert(result == mp_header.entry[i].unpacked_size);

			printf("...complete.\n");
		} else
			memcpy(dc[i].unpacked, dc[i].packed, mp_header.entry[i].unpacked_size);

		snprintf(dumpname, 0x10, "fab%d.fab", i);
		dump(dc[i].packed,   mp_header.entry[i].packed_size,   dumpname);
		//snprintf(dumpname, 0x10, "fab%d.st1", i);
		//dump(dc[i].unpacked, mp_header.entry[i].unpacked_size, dumpname);
	}

	switch (mp_header.entry[0].flag.type) {
		case PIK: break;

		case SS:  sprite(dc[1].unpacked, mp_header.entry[1].unpacked_size,
		                 dc[3].unpacked, mp_header.entry[3].unpacked_size, argv[1]); break;

		case FF:  font(  dc[0].unpacked, mp_header.entry[0].unpacked_size); break;

		case ANYTHING: break;

		default: break;
	}

/*	result = stage2(fab1_unpacked, hdr.fab[1].unpacked_size,
	             fab3_unpacked, hdr.fab[3].unpacked_size, filename);*/

	return EXIT_SUCCESS;
}

