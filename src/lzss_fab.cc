#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mads.h"

/*
 * FAB stream codec: Fabrice Bellard's LZSS (Lempel-Ziv-Storer-
 * Szymanski) implementation from LZEXE 0.91, (c) 1989, MIT
 * (lzss.asm packs, exepack.asm unpacks).
 *
 * A stream is "FAB", a distance-bits byte, then 16-bit little-endian
 * flag words interleaved with the data bytes their flags describe.
 * Flags are consumed LSB first; the next word is read the moment the
 * 16th bit is, before that flag's own data bytes.
 *
 * The distance bits set how the long-match word's high byte splits
 * between the negated distance's top bits and the length code; the
 * low byte is the distance's low byte,
 *
 *     0x0c:  15 ...... 12  11 .... 8  7 ............ 0
 *            [-dist 11..8]  [ code  ]  [ -dist 7..0    ]
 *             4 bits         4 bits     window = 1 << 12
 *
 *     0x0d:  15 ....... 11  10 ... 8  7 ............ 0
 *            [-dist 12..8]   [ code ]  [ -dist 7..0    ]
 *             5 bits          3 bits    window = 1 << 13
 *
 * so each step up doubles the window and halves the length-code
 * space. LZEXE itself is the 0x0d case; MADS data uses 0x0c.
 *
 * flag 1: one literal byte.
 * flag 0, next bit 0: short match. Two more bits (MSB first) hold
 *     length - 2 (2..5), one byte holds the low byte of -distance
 *     (distance 1..255; byte 0 decodes as 256, accepted but never
 *     produced).
 * flag 0, next bit 1: long match. A 16-bit word split as drawn
 *     above holds -distance and a length code. A nonzero code means
 *     length = code + 2. Code 0 escapes to a third byte: 0 ends the
 *     stream, 1 is a segment break (the DOS decompressor renormalizes
 *     its segment registers; nothing to do in flat memory), anything
 *     else means length = byte + 1.
 */

#define FAB_DISTANCE_BITS 0x0c

enum {
	LENMAX        = 253,    /* longest match lzss.asm emits */
	HASH_SIZE     = 4096,   /* buckets over 2-byte prefixes */
	SEGMENT_LIMIT = 0xa000, /* output bytes between segment breaks */
};

struct bit_reader {
	const uint8_t *src;
	uint32_t size;
	uint32_t pos;
	uint16_t flags;
	int bits;
	int truncated;
};

static int get_bit(struct bit_reader *br)
{
	int bit = br->flags & 1;

	br->flags >>= 1;
	if (--br->bits == 0) {
		/* The next flag word is fetched the moment the 16th bit is
		   consumed, before the data bytes of that 16th flag. */
		if (br->size - br->pos < 2) {
			br->truncated = 1;
		} else {
			br->flags = br->src[br->pos] | br->src[br->pos + 1] << 8;
			br->pos += 2;
		}
		br->bits = 16;
	}

	return bit;
}

static uint8_t get_byte(struct bit_reader *br)
{
	if (br->pos == br->size) {
		br->truncated = 1;
		return 0;
	}
	return br->src[br->pos++];
}

int mads_fab_decode(uint8_t *dest, uint32_t *dest_size,
                    const uint8_t *src, uint32_t src_size)
{
	uint32_t capacity = *dest_size;

	if (src_size < 6)
		return MADS_FAB_TRUNCATED;
	if (memcmp(src, "FAB", 3))
		return MADS_FAB_NOT_FAB;

	uint8_t distance_bits = src[3];
	if (distance_bits < 0x0a || distance_bits > 0x0d)
		return MADS_FAB_BAD_DISTANCE;

	uint32_t window = 1u << distance_bits;
	int code_shift = 16 - distance_bits;
	uint32_t code_mask = (1u << code_shift) - 1;

	struct bit_reader br = {
		src, src_size, 6,
		(uint16_t) (src[4] | src[5] << 8), 16, 0,
	};

	uint32_t written = 0;
	for (;;) {
		if (get_bit(&br)) {
			uint8_t literal = get_byte(&br);
			if (br.truncated)
				return MADS_FAB_TRUNCATED;
			if (written == capacity)
				return MADS_FAB_OVERFLOW;
			dest[written++] = literal;
			continue;
		}

		uint32_t length, distance;
		if (!get_bit(&br)) {
			length  = 2 + 2 * get_bit(&br);
			length += get_bit(&br);
			distance = 0x100 - get_byte(&br);
		} else {
			uint8_t low  = get_byte(&br);
			uint8_t high = get_byte(&br);
			distance = window - (low | (high >> code_shift) << 8);

			uint32_t code = high & code_mask;
			if (code) {
				length = code + 2;
			} else {
				uint8_t escape = get_byte(&br);
				if (br.truncated)
					return MADS_FAB_TRUNCATED;
				if (escape == 0) {
					*dest_size = written;
					return 0;
				}
				if (escape == 1)
					continue;
				length = escape + 1;
			}
		}

		if (br.truncated)
			return MADS_FAB_TRUNCATED;
		if (distance > written)
			return MADS_FAB_CORRUPT;
		if (length > capacity - written)
			return MADS_FAB_OVERFLOW;
		for (; length; --length, ++written)
			dest[written] = dest[written - distance];
	}
}

/*
 * The encoder keeps lzss.asm's exact mechanics so that re-encoding a
 * stream the original packer wrote reproduces it byte for byte: a
 * ring of the last window bytes with a LENMAX lookahead, hash chains
 * over 2-byte prefixes, greedy parsing where only a strictly longer
 * match displaces the best (so the nearest wins ties), and a node
 * leaving the dictionary exactly when the ring overwrites its slot.
 */

struct bit_writer {
	uint8_t *dst;
	uint32_t size;
	uint32_t pos;
	uint32_t flags_pos;
	uint16_t flags;
	uint16_t mask;
	int overflow;
};

static void open_flags(struct bit_writer *bw)
{
	bw->flags = 0;
	bw->mask = 1;
	bw->flags_pos = bw->pos;
	if (bw->overflow || bw->size - bw->pos < 2) {
		bw->overflow = 1;
		return;
	}
	bw->pos += 2;
}

static void close_flags(struct bit_writer *bw)
{
	if (bw->overflow)
		return;
	bw->dst[bw->flags_pos] = bw->flags & 0xff;
	bw->dst[bw->flags_pos + 1] = bw->flags >> 8;
}

static void put_bit(struct bit_writer *bw, int bit)
{
	if (bit)
		bw->flags |= bw->mask;
	bw->mask <<= 1;
	if (bw->mask == 0) {
		close_flags(bw);
		open_flags(bw);
	}
}

static void put_ch(struct bit_writer *bw, uint8_t ch)
{
	if (bw->overflow || bw->pos == bw->size) {
		bw->overflow = 1;
		return;
	}
	bw->dst[bw->pos++] = ch;
}

static void put_escape(struct bit_writer *bw, uint8_t escape)
{
	/* The escape word's distance bits are never used; MADS packs them
	   as zero where lzexe wrote 0xf0. */
	put_bit(bw, 0);
	put_bit(bw, 1);
	put_ch(bw, 0x00);
	put_ch(bw, 0x00);
	put_ch(bw, escape);
}

/*
 * ring holds window + LENMAX + 1 bytes; the first LENMAX - 1 are
 * mirrored past position window - 1 so matches read linearly across the
 * wrap. rson holds the chain successor of ring positions 0..window - 1,
 * the nil node at index window, and the HASH_SIZE bucket heads above
 * that; dad holds each node's predecessor (bucket or node).
 */
struct dictionary {
	uint32_t window;
	uint8_t  *ring;
	uint16_t *rson;
	uint16_t *dad;
	uint32_t match_length;
	uint32_t match_distance;
};

static void insert_node(struct dictionary *dict, uint16_t pos)
{
	uint16_t bucket = dict->window + 1 +
		((dict->ring[pos] | dict->ring[pos + 1] << 8) & (HASH_SIZE - 1));

	dict->rson[pos] = dict->rson[bucket];
	dict->dad[dict->rson[pos]] = pos;
	dict->rson[bucket] = pos;
	dict->dad[pos] = bucket;
}

static void delete_node(struct dictionary *dict, uint16_t pos)
{
	uint16_t nil = dict->window;

	if (dict->dad[pos] == nil)
		return;
	/* The deleted node is always the oldest and so its chain's tail:
	   cutting at the predecessor equals unlinking it. */
	dict->rson[dict->dad[pos]] = nil;
	dict->dad[pos] = nil;
}

static void find_match(struct dictionary *dict, uint16_t pos)
{
	const uint8_t *ring = dict->ring;
	uint16_t nil = dict->window;
	uint16_t bucket = nil + 1 + ((ring[pos] | ring[pos + 1] << 8) & (HASH_SIZE - 1));

	/* best_rest counts compare bytes left at the mismatch, like the
	   cx register lzss.asm keeps: lower is a longer match. */
	uint16_t best = nil;
	int best_rest = LENMAX - 1;

	for (uint16_t node = dict->rson[bucket]; node != nil; node = dict->rson[node]) {
		int equal = 0;
		while (equal < LENMAX - 1 && ring[node + 1 + equal] == ring[pos + 1 + equal])
			++equal;
		if (equal == LENMAX - 1) {
			best = node;
			best_rest = -1;
			break;
		}

		int rest = LENMAX - 2 - equal;
		if (rest < best_rest) {
			best = node;
			best_rest = rest;
		}
	}

	dict->match_length = LENMAX - 1 - best_rest;
	dict->match_distance = 0;
	if (best != nil)
		dict->match_distance = (uint16_t) (pos - best) & (dict->window - 1);

	insert_node(dict, pos);
}

int mads_fab_encode(uint8_t *dest, uint32_t *dest_size,
                  const uint8_t *src, uint32_t src_size)
{
	uint32_t capacity = *dest_size;
	uint32_t window = 1u << FAB_DISTANCE_BITS;
	int code_shift = 16 - FAB_DISTANCE_BITS;
	uint32_t length_direct = (1u << code_shift) - 1 + 2;

	if (capacity < 6)
		return MADS_FAB_OVERFLOW;
	memcpy(dest, "FAB", 3);
	dest[3] = FAB_DISTANCE_BITS;

	struct bit_writer bw = { dest, capacity, 4, 0, 0, 0, 0 };
	open_flags(&bw);

	if (src_size == 0) {
		/* lzss.asm never met an empty input; the end marker alone
		   makes a well-formed stream. */
		put_escape(&bw, 0x00);
		close_flags(&bw);
		if (bw.overflow)
			return MADS_FAB_OVERFLOW;
		*dest_size = bw.pos;
		return 0;
	}

	struct dictionary dict = { window, NULL, NULL, NULL, 0, 0 };
	dict.ring = (uint8_t *) calloc(window + LENMAX + 1, 1);
	dict.rson = (uint16_t *) malloc((window + HASH_SIZE + 1) * sizeof (uint16_t));
	dict.dad  = (uint16_t *) malloc((window + 1) * sizeof (uint16_t));
	if (dict.ring == NULL || dict.rson == NULL || dict.dad == NULL) {
		free(dict.ring);
		free(dict.rson);
		free(dict.dad);
		return MADS_NOMEM;
	}
	for (uint32_t i = 0; i < window + HASH_SIZE + 1; ++i)
		dict.rson[i] = window;
	for (uint32_t i = 0; i < window + 1; ++i)
		dict.dad[i] = window;

	uint32_t in = 0;
	uint16_t pos = window - LENMAX;
	uint16_t slot = 0;
	int lookahead = 0;
	while (lookahead < LENMAX && in < src_size)
		dict.ring[pos + lookahead++] = src[in++];
	find_match(&dict, pos);

	uint32_t segment = 0;
	while (!bw.overflow) {
		uint32_t length = dict.match_length;
		if ((int) length > lookahead)
			length = lookahead;

		uint32_t distance = dict.match_distance;
		if (length < 2 || (length == 2 && distance >= 256)) {
			length = 1;
			put_bit(&bw, 1);
			put_ch(&bw, dict.ring[pos]);
		} else if (length <= 5 && distance < 256) {
			put_bit(&bw, 0);
			put_bit(&bw, 0);
			put_bit(&bw, (length - 2) & 2);
			put_bit(&bw, (length - 2) & 1);
			put_ch(&bw, (uint8_t) (0x100 - distance));
		} else {
			uint32_t negated = 0x10000 - distance;
			put_bit(&bw, 0);
			put_bit(&bw, 1);
			put_ch(&bw, (uint8_t) negated);
			uint8_t high = (uint8_t) (negated >> 8 << code_shift);
			if (length <= length_direct) {
				put_ch(&bw, high | (length - 2));
			} else {
				put_ch(&bw, high);
				put_ch(&bw, length - 1);
			}
		}

		segment += length;
		if (segment >= SEGMENT_LIMIT) {
			put_escape(&bw, 0x01);
			segment = 0;
		}

		for (uint32_t i = 0; i < length; ++i) {
			if (i)
				insert_node(&dict, pos);
			delete_node(&dict, slot);
			if (in < src_size) {
				dict.ring[slot] = src[in];
				if (slot < LENMAX - 1)
					dict.ring[slot + window] = src[in];
				++in;
			} else {
				--lookahead;
			}
			slot = (slot + 1) & (window - 1);
			pos  = (pos  + 1) & (window - 1);
		}

		find_match(&dict, pos);
		if (lookahead <= 0)
			break;
	}

	put_escape(&bw, 0x00);
	close_flags(&bw);

	free(dict.ring);
	free(dict.rson);
	free(dict.dad);

	if (bw.overflow)
		return MADS_FAB_OVERFLOW;
	*dest_size = bw.pos;
	return 0;
}

uint32_t mads_fab_encode_bound(uint32_t src_size)
{
	/* All-literal worst case: the data bytes, one flag bit each, a
	   segment break every 0xa000 output bytes, the end marker and the
	   4-byte header. */
	uint32_t bound = src_size + src_size / 8 + src_size / 4096 + 24;

	return bound < src_size ? UINT32_MAX : bound;
}
