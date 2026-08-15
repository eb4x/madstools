#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mads.h"

int mads_palette_bind(struct mads_palette *palette,
                      const uint8_t *data, uint32_t size)
{
	if (size == 0) {
		palette->length = 0;
		palette->rgb    = NULL;
		palette->stride = 0;
		return 0;
	}

	if (size == MADS_PALETTE_SIZE) {
		for (size_t i = 0; i < MADS_PALETTE_SIZE; ++i)
			if (data[i] > 0x3f)
				return MADS_PALETTE_COMPONENT;

		palette->length = 256;
		palette->rgb    = data;
		palette->stride = 3;
		return 0;
	}

	if (size < sizeof (struct mads_palette_header))
		return MADS_PALETTE_FORM;

	const struct mads_palette_header *sparse = (const struct mads_palette_header *) data;

	if (sizeof (struct mads_palette_header) +
	    sparse->length * sizeof (struct mads_palette_record) > size)
		return MADS_PALETTE_LENGTH;

	if (sparse->length > MADS_PALETTE_LENGTH_MAX)
		return MADS_PALETTE_LENGTH;

	for (uint32_t i = 0; i < sparse->length; ++i)
		if (sparse->color[i].r > 0x3f || sparse->color[i].g > 0x3f ||
		    sparse->color[i].b > 0x3f)
			return MADS_PALETTE_COMPONENT;

	palette->length = sparse->length;
	palette->rgb    = (const uint8_t *) sparse->color;
	palette->stride = sizeof (struct mads_palette_record);
	return 0;
}

void mads_palette_full(uint8_t dst[MADS_PALETTE_SIZE],
                       const struct mads_palette *palette)
{
	memset(dst, 0, MADS_PALETTE_SIZE);

	for (uint32_t i = 0; i < palette->length; ++i) {
		const uint8_t *rgb = palette->rgb + i * palette->stride;

		dst[3 * i + 0] = rgb[0];
		dst[3 * i + 1] = rgb[1];
		dst[3 * i + 2] = rgb[2];
	}
}

void mads_palette_scale(uint8_t *dst, const uint8_t *src)
{
	for (size_t i = 0; i < MADS_PALETTE_SIZE; ++i)
		dst[i] = mads_rgb6_to_8(src[i]);
}
