#ifndef CHRONOPIPE_TYPES_H
#define CHRONOPIPE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#ifndef CP_MAX_STREAMS
#define CP_MAX_STREAMS 32u
#endif

#ifndef CP_MAX_STAGES
#define CP_MAX_STAGES 64u
#endif

#ifndef CP_MAX_GRAPH_DEPTH
#define CP_MAX_GRAPH_DEPTH 32u
#endif

#define CP_SEQ_AUTO UINT32_MAX
#define CP_STREAM_SOURCE 0u

typedef enum {
    CP_OK = 0,
    CP_EINVAL = -1,
    CP_ENOMEM = -2,
    CP_EBOUNDS = -3,
    CP_EFORMAT = -4,
    CP_ECRC = -5,
    CP_ESTATE = -6,
    CP_EFULL = -7,
    CP_ENOTFOUND = -8,
    CP_EIO = -9,
    CP_ECYCLE = -10,
    CP_EGAP = -11
} cp_status_t;

typedef enum {
    CP_DTYPE_F32 = 1,
    CP_DTYPE_I16 = 2,
    CP_DTYPE_U16 = 3,
    CP_DTYPE_I32 = 4,
    CP_DTYPE_U32 = 5
} cp_dtype_t;

typedef enum {
    CP_Q_NONE = 0,
    CP_Q_DISCONTINUITY = 1u << 0,
    CP_Q_LATE = 1u << 1,
    CP_Q_NON_FINITE = 1u << 2,
    CP_Q_SATURATED = 1u << 3,
    CP_Q_DERIVED_FROM_GAP = 1u << 4,
    CP_Q_RECOVERED = 1u << 5
} cp_quality_flag_t;

typedef enum {
    CP_EVENT_SEQUENCE_GAP = 1,
    CP_EVENT_LATE_FRAME = 2,
    CP_EVENT_NON_FINITE = 3,
    CP_EVENT_SATURATION = 4,
    CP_EVENT_BUFFER_OVERFLOW = 5,
    CP_EVENT_RECORDER_STOPPED = 6,
    CP_EVENT_ALGORITHM_RESET = 7,
    CP_EVENT_CONFIG_CHANGED = 8,
    CP_EVENT_TRANSPORT_FAILED = 9,
    CP_EVENT_STORE_RECOVERED = 10,
    CP_EVENT_TIME_MAPPING = 11
} cp_event_type_t;

typedef struct {
    uint32_t input_stream_id;
    uint32_t first_seq;
    uint32_t last_seq;
} cp_lineage_t;

typedef struct {
    uint32_t stream_id;
    uint32_t seq;
    uint64_t event_time_us;
    uint64_t emit_time_us;
    uint32_t period_num_us;
    uint32_t period_den;
    uint32_t quality_flags;
    cp_dtype_t dtype;
    uint16_t channel_count;
    uint16_t sample_count;
    const void *data;
    size_t data_bytes;
    cp_lineage_t lineage;
} cp_frame_t;

typedef struct {
    uint16_t type;
    uint16_t flags;
    uint32_t stream_id;
    uint32_t seq;
    uint64_t event_time_us;
    int32_t detail;
    float value;
} cp_event_t;

typedef struct {
    const char *name;
    const char *unit;
    double physical_min;
    double physical_max;
    double scale;
    double offset;
} cp_channel_desc_t;

typedef struct {
    uint32_t id;
    uint32_t schema_id;
    const char *name;
    cp_dtype_t dtype;
    uint16_t channel_count;
    uint16_t logical_width;
    uint32_t period_num_us;
    uint32_t period_den;
    uint32_t producer_id;
    uint64_t config_hash;
    const cp_channel_desc_t *channels;
} cp_stream_desc_t;

size_t cp_dtype_size(cp_dtype_t dtype);
const char *cp_status_string(cp_status_t status);
bool cp_checked_mul_size(size_t a, size_t b, size_t *out);
bool cp_frame_shape_bytes(const cp_frame_t *frame, size_t *out);

#ifdef __cplusplus
}
#endif

#endif
