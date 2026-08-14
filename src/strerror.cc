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

		case MADS_NOMEM:            return "out of memory";
	}

	return "unknown error";
}
