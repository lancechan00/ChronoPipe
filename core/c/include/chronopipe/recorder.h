#ifndef CHRONOPIPE_RECORDER_H
#define CHRONOPIPE_RECORDER_H

#include "chronopipe/store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t session_id;
    const cp_stream_desc_t *stream;
    uint16_t samples_per_block;
    uint32_t first_block_seq;
} cp_recorder_config_t;

typedef struct {
    cp_recorder_config_t config;
    cp_store_t store;
    uint8_t *record_buffer;
    size_t record_capacity;
    cp_cpb1_data_header_t header;
    uint32_t next_block_seq;
    uint32_t expected_frame_seq;
    uint16_t buffered_samples;
    bool have_frame_seq;
    bool active;
} cp_recorder_t;

cp_status_t cp_recorder_init(cp_recorder_t *recorder,
                             const cp_recorder_config_t *config,
                             cp_store_t store,
                             void *record_buffer,
                             size_t record_capacity);
cp_status_t cp_recorder_append(cp_recorder_t *recorder, const cp_frame_t *frame);
cp_status_t cp_recorder_flush(cp_recorder_t *recorder);
void cp_recorder_discard_pending(cp_recorder_t *recorder);

#ifdef __cplusplus
}
#endif

#endif
