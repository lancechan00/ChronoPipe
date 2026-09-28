#ifndef CHRONOPIPE_CPB2_H
#define CHRONOPIPE_CPB2_H

#include "chronopipe/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP_CPB2_FILE_HEADER_BYTES 16u
#define CP_CPB2_RECORD_PREFIX_BYTES 8u
#define CP_CPB2_COMPACT_HEADER_BYTES 17u
#define CP_CPB2_WIDE_RECORD_HEADER_BYTES 74u
#define CP_CPB2_EVENT_PAYLOAD_BYTES 36u
#define CP_CPB2_COMPACT_MAX_STREAMS 32u
#define CP_CPB2_COMPACT_MAX_CHANNELS 16u
#define CP_CPB2_COMPACT_MAX_SAMPLES 65535u
#define CP_CPB2_COMPACT_MAX_QUALITY 63u

typedef enum {
    CP_CPB2_SESSION = 1,
    CP_CPB2_SCHEMA = 2,
    CP_CPB2_DATA_COMPACT = 3,
    CP_CPB2_EVENT = 4,
    CP_CPB2_SESSION_END = 5,
    CP_CPB2_INDEX = 6,
    CP_CPB2_DATA_WIDE = 7
} cp_cpb2_record_kind_t;

typedef struct {
    uint32_t stream_id;
    uint32_t dtype;
    uint32_t layout;
    uint32_t channel_count;
    uint32_t quality_flags;
    uint32_t sample_count;
    uint32_t block_seq;
    uint32_t first_sample_seq;
} cp_cpb2_compact_header_t;

typedef struct {
    cp_cpb2_compact_header_t header;
    const uint8_t *payload;
    size_t payload_bytes;
} cp_cpb2_compact_view_t;

typedef struct {
    uint32_t stream_id;
    uint32_t schema_id;
    uint32_t block_seq;
    uint32_t first_sample_seq;
    uint64_t t0_us;
    uint32_t period_num_us;
    uint32_t period_den;
    uint32_t sample_count;
    uint32_t channel_count;
    cp_dtype_t dtype;
    uint8_t layout;
    uint16_t quality_flags;
    uint32_t payload_bytes;
    uint32_t payload_crc32c;
    uint32_t input_stream_id;
    uint32_t input_first_seq;
    uint32_t input_last_seq;
} cp_cpb2_wide_header_t;

typedef struct {
    uint16_t flags;
    cp_cpb2_wide_header_t header;
    const uint8_t *payload;
} cp_cpb2_wide_view_t;

typedef struct {
    uint64_t session_id;
    uint16_t type;
    uint16_t flags;
    uint32_t stream_id;
    uint32_t sequence;
    uint64_t event_time_us;
    int32_t detail;
    float value;
} cp_cpb2_event_t;

bool cp_cpb2_shape_fits_compact(uint32_t stream_id,
                                uint32_t channel_count,
                                uint32_t sample_count,
                                uint32_t quality_flags,
                                uint32_t layout,
                                cp_dtype_t dtype,
                                uint32_t period_num_us);

cp_status_t cp_cpb2_compact_record_bytes(uint32_t sample_count,
                                         uint32_t channel_count,
                                         cp_dtype_t dtype,
                                         size_t *out);

cp_status_t cp_cpb2_file_header_encode(uint16_t minor, uint8_t *out, size_t capacity);
cp_status_t cp_cpb2_file_header_decode(const uint8_t *data, size_t bytes, uint16_t *minor);

cp_status_t cp_cpb2_record_encode(uint16_t kind,
                                  uint16_t flags,
                                  const void *payload,
                                  size_t payload_bytes,
                                  uint8_t *out,
                                  size_t capacity,
                                  size_t *written);

cp_status_t cp_cpb2_compact_encode(const cp_cpb2_compact_header_t *header,
                                   const void *payload,
                                   uint8_t *out,
                                   size_t capacity,
                                   size_t *written);
cp_status_t cp_cpb2_compact_decode(const uint8_t *data,
                                   size_t bytes,
                                   cp_cpb2_compact_view_t *out);

cp_status_t cp_cpb2_wide_encode(const cp_cpb2_wide_header_t *header,
                                const void *payload,
                                uint8_t *out,
                                size_t capacity,
                                size_t *written);
cp_status_t cp_cpb2_wide_decode(const uint8_t *data,
                                size_t bytes,
                                cp_cpb2_wide_view_t *out);

cp_status_t cp_cpb2_event_encode(const cp_cpb2_event_t *event,
                                 uint8_t *out,
                                 size_t capacity,
                                 size_t *written);
cp_status_t cp_cpb2_event_decode(const uint8_t *data, size_t bytes, cp_cpb2_event_t *out);

/* Accepts a complete CPB1 record or a complete CPB2 record. */
cp_status_t cp_evidence_validate_record(const uint8_t *data, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
