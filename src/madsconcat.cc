#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char madsconcat_sig[] = "MADSCONCAT 1.0\x1a\x00";

int main(int argc, char *argv[])
{
	if (argc < 2)
		return EXIT_SUCCESS;

	const char *path = argv[1];

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
