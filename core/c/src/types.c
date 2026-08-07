#include "chronopipe/types.h"

#include <limits.h>

size_t cp_dtype_size(cp_dtype_t dtype) {
    switch (dtype) {
        case CP_DTYPE_F32:
        case CP_DTYPE_I32:
        case CP_DTYPE_U32:
            return 4u;
        case CP_DTYPE_I16:
        case CP_DTYPE_U16:
            return 2u;
        default:
            return 0u;
    }
}
const char *cp_status_string(cp_status_t status) {
    switch (status) {
        case CP_OK: return "ok";
        case CP_EINVAL: return "invalid argument";
        case CP_ENOMEM: return "out of memory";
        case CP_EBOUNDS: return "out of bounds";
        case CP_EFORMAT: return "invalid format";
        case CP_ECRC: return "checksum mismatch";
        case CP_ESTATE: return "invalid state";
        case CP_EFULL: return "full";
        case CP_ENOTFOUND: return "not found";
        case CP_EIO: return "I/O failure";
        case CP_ECYCLE: return "graph cycle";
        case CP_EGAP: return "sequence gap";
        default: return "unknown";
    }
}

bool cp_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (out == NULL) return false;
    if (a != 0u && b > SIZE_MAX / a) return false;
    *out = a * b;
    return true;
}

bool cp_frame_shape_bytes(const cp_frame_t *frame, size_t *out) {
    size_t samples_and_channels;
    size_t width;
    if (frame == NULL || out == NULL || frame->sample_count == 0u ||
        frame->channel_count == 0u) {
        return false;
    }
    width = cp_dtype_size(frame->dtype);
    if (width == 0u) return false;
    if (!cp_checked_mul_size((size_t)frame->sample_count,
                             (size_t)frame->channel_count,
                             &samples_and_channels)) {
        return false;
    }
    return cp_checked_mul_size(samples_and_channels, width, out);
}
