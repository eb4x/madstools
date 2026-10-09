/* for strnlen, which C11 alone does not declare */
#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mads.h"

int madsconcat_bind(struct madsconcat *mc, const uint8_t *data, size_t size)
{
	if (size < sizeof (struct madsconcat_header))
		return MADS_CONCAT_SHORT;

	const struct madsconcat_header *header =
		(const struct madsconcat_header *) data;

	if (memcmp(MADSCONCAT_SIG, header->signature, sizeof (MADSCONCAT_SIG) - 1) != 0)
		return MADS_CONCAT_SIGNATURE;

	if (sizeof (struct madsconcat_header) +
	    (size_t) header->length * sizeof (struct madsconcat_record) > size)
		return MADS_CONCAT_SHORT;

	for (int i = 0; i < header->length; ++i) {
		const struct madsconcat_record *file = &header->file[i];

		if (file->offset > size || file->size > size - file->offset)
			return MADS_CONCAT_SHORT;

		/* the name was only ever a NUL-terminated bare filename; a
		   separator would name a path outside the extract directory,
		   and a control byte would land in the listing's terminal */
		size_t length = strnlen(file->name, sizeof (file->name));

		if (length == 0 || length == sizeof (file->name))
			return MADS_CONCAT_NAME;

		if (strchr(file->name, '/') != NULL ||
		    strchr(file->name, '\\') != NULL)
			return MADS_CONCAT_NAME;

		/* refuse a byte outside printable ASCII */
		for (const char *c = file->name; *c; ++c) {
			unsigned char byte = *c;
			if (byte < 0x20 || byte > 0x7e)
				return MADS_CONCAT_NAME;
		}
	}

	mc->header = header;
	mc->data = data;
	mc->size = size;

	return 0;
}
