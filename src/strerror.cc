#include "mads.h"

const char *mads_strerror(int error)
{
	switch (error) {
		case MADS_FAB_TRUNCATED:    return "FAB stream truncated";
		case MADS_FAB_NOT_FAB:      return "not a FAB stream";
		case MADS_FAB_BAD_DISTANCE: return "unsupported distance width";
		case MADS_FAB_OVERFLOW:     return "output too small for the FAB stream";
		case MADS_FAB_CORRUPT:      return "FAB stream corrupt";

		case MADS_NOMEM:            return "out of memory";
	}

	return "unknown error";
}
