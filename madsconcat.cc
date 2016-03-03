#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "util/util.h"

static const char madsconcat_sig[] = "MADSCONCAT 1.0\x1a\x00";

int main(int argc, char *argv[])
{
	if (argc < 2)
		return EXIT_SUCCESS;

	FILE *fp = fopen(argv[1], "r");
	if (fp == NULL)
		return EXIT_SUCCESS;
	
	char sig[0x10];
	fread(sig, 0x10, 1, fp);

	uint16_t entries;
	fread(&entries, sizeof (uint16_t), 1, fp);

	printf("Entries: %d\n", entries);

	struct madsconcat {
		uint32_t offset;
		uint32_t size;
		char filename[0x0e];
	} __attribute__ ((packed)) entry[entries];

	assert(sizeof (struct madsconcat) == 0x16);
	int result = fread(entry, sizeof(struct madsconcat), entries, fp);

	uint8_t buffer[0x100000];
	char filename[0x30];

	for (int i = 0; i < entries; ++i) {
		fprintf(stderr, "%s[%3d]: offset(%06x), size(%06x) - [%s]\n", argv[1], i, entry[i].offset, entry[i].size, entry[i].filename);
		fread(buffer, 1, entry[i].size, fp);
		dump(buffer, entry[i].size, entry[i].filename);
	}
	

	return EXIT_SUCCESS;
}
