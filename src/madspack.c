#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mads.h"

int madspack_load(struct madspack *mp, const char *path)
{
	struct madspack_header *header = &mp->header;

	/* ahead of every failure path, so madspack_free finds a pointer
	   or NULL in each slot however early the load gives up */
	memset(mp->decoded, 0, sizeof (mp->decoded));

	mp->error_member = -1;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL)
		return MADS_PACK_OPEN;

	if (fread(header, sizeof (struct madspack_header), 1, fp) != 1) {
		fclose(fp);
		return MADS_PACK_SHORT;
	}
	if (strncmp(MADSPACK_SIG, header->signature, sizeof (header->signature)) != 0) {
		fclose(fp);
		return MADS_PACK_SIGNATURE;
	}
	if (header->length > MADSPACK_RECORDS) {
		fclose(fp);
		return MADS_PACK_HEADER;
	}

	for (int i = 0; i < header->length; ++i) {
		const struct madspack_record *member = &header->member[i];
		mp->error_member = i;

		if (member->codec > 1) {
			fclose(fp);
			return MADS_PACK_CODEC;
		}

		/* an empty member is empty on both sides */
		if (member->encoded_size == 0 || member->decoded_size == 0) {
			if (member->encoded_size != member->decoded_size) {
				fclose(fp);
				return MADS_PACK_SIZE;
			}
			continue;
		}

		uint8_t *encoded = (uint8_t *) malloc(member->encoded_size);
		if (encoded == NULL) {
			fclose(fp);
			return MADS_NOMEM;
		}

		if (fread(encoded, member->encoded_size, 1, fp) != 1) {
			free(encoded);
			fclose(fp);
			return MADS_PACK_SHORT;
		}

		mp->decoded[i] = (uint8_t *) malloc(member->decoded_size);
		if (mp->decoded[i] == NULL) {
			free(encoded);
			fclose(fp);
			return MADS_NOMEM;
		}

		/* the codec byte alone does not decide: codec 1 records
		   without the FAB magic are stored raw */
		if (member->codec == 1 && member->encoded_size >= 3 &&
		    memcmp(encoded, "FAB", 3) == 0) {
			uint32_t size = member->decoded_size;
			int result = mads_fab_decode(mp->decoded[i], &size,
			                             encoded, member->encoded_size);
			if (result != 0) {
				free(encoded);
				fclose(fp);
				return result;
			}
			if (size != member->decoded_size) {
				free(encoded);
				fclose(fp);
				return MADS_PACK_SIZE;
			}
		} else {
			if (member->encoded_size != member->decoded_size) {
				free(encoded);
				fclose(fp);
				return MADS_PACK_SIZE;
			}
			memcpy(mp->decoded[i], encoded, member->decoded_size);
		}

		free(encoded);
	}

	mp->error_member = -1;
	fclose(fp);
	return 0;
}

void madspack_free(struct madspack *mp)
{
	/* every slot rather than header.length of them, so the load owes
	   the free nothing: a length it never read cannot bound this */
	for (int i = 0; i < MADSPACK_RECORDS; ++i) {
		free(mp->decoded[i]);
		mp->decoded[i] = NULL;
	}
}
