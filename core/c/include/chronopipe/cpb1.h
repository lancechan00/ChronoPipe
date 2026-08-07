#ifndef CHRONOPIPE_CPB1_H
#define CHRONOPIPE_CPB1_H

#include "chronopipe/types.h"

#ifdef __cplusplus
extern "C" {
#endif
#define CP_CPB1_FILE_HEADER_BYTES 16u
#define CP_CPB1_RECORD_PREFIX_BYTES 8u
#define CP_CPB1_DATA_HEADER_BYTES 80u
#define CP_CPB1_DATA_RECORD_HEADER_BYTES 88u

typedef enum {
    CP_CPB1_SESSION = 1,
    CP_CPB1_SCHEMA = 2,
    CP_CPB1_DATA = 3,
    CP_CPB1_EVENT = 4,
    CP_CPB1_SESSION_END = 5,
    CP_CPB1_INDEX = 6
} cp_cpb1_record_kind_t;

typedef struct {
    uint16_t kind;
    uint16_t flags;
    uint32_t record_bytes;
} cp_cpb1_record_prefix_t;

typedef struct {
    uint64_t session_id;
    uint32_t stream_id;
    uint32_t schema_id;
    uint32_t block_seq;
    uint32_t first_sample_seq;
    uint64_t t0_us;
    uint32_t period_num_us;
    uint32_t period_den;
    uint16_t sample_count;
    uint16_t channel_count;
    cp_dtype_t dtype;
    uint8_t layout;
    uint16_t quality_flags;
    uint32_t payload_bytes;
    uint32_t payload_crc32c;
    uint32_t input_stream_id;
    uint32_t input_first_seq;
    uint32_t input_last_seq;
} cp_cpb1_data_header_t;

typedef struct {
    cp_cpb1_record_prefix_t prefix;
    cp_cpb1_data_header_t header;
    const uint8_t *payload;
} cp_cpb1_data_view_t;

uint32_t cp_crc32c(const void *data, size_t bytes);
cp_status_t cp_cpb1_file_header_encode(uint16_t minor, uint8_t *out, size_t capacity);
cp_status_t cp_cpb1_file_header_decode(const uint8_t *data, size_t bytes, uint16_t *minor);
cp_status_t cp_cpb1_record_encode(uint16_t kind,
                                  uint16_t flags,
                                  const void *payload,
                                  size_t payload_bytes,
                                  uint8_t *out,
                                  size_t capacity,
                                  size_t *written);
cp_status_t cp_cpb1_record_prefix_decode(const uint8_t *data,
                                         size_t bytes,
                                         cp_cpb1_record_prefix_t *out);
cp_status_t cp_cpb1_data_encode(const cp_cpb1_data_header_t *header,
                                const void *payload,
                                uint8_t *out,
                                size_t capacity,
                                size_t *written);
cp_status_t cp_cpb1_data_decode(const uint8_t *data,
                                size_t bytes,
                                cp_cpb1_data_view_t *out);
cp_status_t cp_cpb1_validate_record(const uint8_t *data, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
