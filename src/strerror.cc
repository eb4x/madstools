#include "mads.h"

const char *mads_strerror(int error)
{
	switch (error) {
		case MADS_PACK_OPEN:      return "cannot open";
		case MADS_PACK_SHORT:     return "file shorter than its header claims";
		case MADS_PACK_SIGNATURE: return "not a MADSPACK archive";
		case MADS_PACK_HEADER:    return "bad header field";
		case MADS_PACK_CODEC:     return "unsupported codec";
		case MADS_PACK_SIZE:      return "stream size does not match the header";

		case MADS_FAB_TRUNCATED:    return "FAB stream truncated";
		case MADS_FAB_NOT_FAB:      return "not a FAB stream";
		case MADS_FAB_BAD_DISTANCE: return "unsupported distance width";
		case MADS_FAB_OVERFLOW:     return "output too small for the FAB stream";
		case MADS_FAB_CORRUPT:      return "FAB stream corrupt";

		case MADS_RLE_TRUNCATED:    return "RLE stream truncated";
		case MADS_RLE_OVERFLOW:     return "output too small for the RLE stream";
		case MADS_RLE_CORRUPT:      return "RLE run crosses a row end";

		case MADS_SS_LAYOUT:        return "not a sprite series archive";
		case MADS_SS_HEADER:        return "sprite table contradicts the series header";
		case MADS_SS_RECORD:        return "sprite record outside the stream member";
		case MADS_SS_PALETTE:       return "palette member holds a component above 0x3f";

		case MADS_NOMEM:            return "out of memory";
	}

	return "unknown error";
}
