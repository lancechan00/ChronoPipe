#include "chronopipe/recorder.h"

#include <string.h>

static cp_status_t recorder_begin(cp_recorder_t *recorder, const cp_frame_t *frame) {
    size_t row_bytes;
    if (!cp_checked_mul_size(frame->channel_count, cp_dtype_size(frame->dtype), &row_bytes) ||
        row_bytes > recorder->record_capacity - CP_CPB1_DATA_RECORD_HEADER_BYTES) {
        return CP_EBOUNDS;
    }
    memset(&recorder->header, 0, sizeof(recorder->header));
    recorder->header.session_id = recorder->config.session_id;
    recorder->header.stream_id = frame->stream_id;
    recorder->header.schema_id = recorder->config.stream->schema_id;
    recorder->header.block_seq = recorder->next_block_seq;
    recorder->header.first_sample_seq = frame->seq;
    recorder->header.t0_us = frame->event_time_us;
    recorder->header.period_num_us = frame->period_num_us;
    recorder->header.period_den = frame->period_den;
    recorder->header.channel_count = frame->channel_count;
    recorder->header.dtype = frame->dtype;
    recorder->header.layout = 0u;
    recorder->header.quality_flags = (uint16_t)frame->quality_flags;
    recorder->header.input_stream_id = frame->lineage.input_stream_id;
    recorder->header.input_first_seq = frame->lineage.first_seq;
    recorder->header.input_last_seq = frame->lineage.last_seq;
    recorder->buffered_samples = 0u;
    recorder->active = true;
    return CP_OK;
}

cp_status_t cp_recorder_init(cp_recorder_t *recorder,
                             const cp_recorder_config_t *config,
                             cp_store_t store,
                             void *record_buffer,
                             size_t record_capacity) {
    size_t row_bytes;
    size_t payload_bytes;
    if (recorder == NULL || config == NULL || config->stream == NULL ||
        config->samples_per_block == 0u || store.ops == NULL || store.ops->append == NULL ||
        record_buffer == NULL || record_capacity <= CP_CPB1_DATA_RECORD_HEADER_BYTES ||
        cp_dtype_size(config->stream->dtype) == 0u || config->stream->channel_count == 0u ||
        config->stream->period_den == 0u) {
        return CP_EINVAL;
    }
    if (!cp_checked_mul_size(config->stream->channel_count,
                             cp_dtype_size(config->stream->dtype), &row_bytes) ||
        !cp_checked_mul_size(row_bytes, config->samples_per_block, &payload_bytes) ||
        payload_bytes > record_capacity - CP_CPB1_DATA_RECORD_HEADER_BYTES) {
        return CP_EBOUNDS;
    }
    memset(recorder, 0, sizeof(*recorder));
    recorder->config = *config;
    recorder->store = store;
    recorder->record_buffer = (uint8_t *)record_buffer;
    recorder->record_capacity = record_capacity;
    recorder->next_block_seq = config->first_block_seq;
    return CP_OK;
}

cp_status_t cp_recorder_flush(cp_recorder_t *recorder) {
    size_t written;
    size_t row_bytes;
    cp_status_t status;
    if (recorder == NULL) return CP_EINVAL;
    if (!recorder->active || recorder->buffered_samples == 0u) return CP_OK;
    if (!cp_checked_mul_size(recorder->header.channel_count,
                             cp_dtype_size(recorder->header.dtype), &row_bytes) ||
        !cp_checked_mul_size(recorder->buffered_samples, row_bytes, &written) ||
        written > UINT32_MAX) {
        return CP_EBOUNDS;
    }
    recorder->header.sample_count = recorder->buffered_samples;
    recorder->header.payload_bytes = (uint32_t)written;
    status = cp_cpb1_data_encode(&recorder->header,
                                 recorder->record_buffer + CP_CPB1_DATA_RECORD_HEADER_BYTES,
                                 recorder->record_buffer,
                                 recorder->record_capacity,
                                 &written);
    if (status != CP_OK) return status;
    status = recorder->store.ops->append(recorder->store.ctx,
                                         recorder->header.block_seq,
                                         recorder->record_buffer,
                                         written);
    if (status == CP_OK) {
        ++recorder->next_block_seq;
        recorder->active = false;
        recorder->buffered_samples = 0u;
    }
    return status;
}

cp_status_t cp_recorder_append(cp_recorder_t *recorder, const cp_frame_t *frame) {
    size_t frame_bytes;
    size_t destination_offset;
    cp_status_t status = CP_OK;
    bool discontinuity = false;
    if (recorder == NULL || frame == NULL || frame->data == NULL ||
        frame->stream_id != recorder->config.stream->id ||
        frame->dtype != recorder->config.stream->dtype ||
        frame->channel_count != recorder->config.stream->channel_count ||
        frame->sample_count == 0u || frame->period_den == 0u ||
        !cp_frame_shape_bytes(frame, &frame_bytes) || frame_bytes != frame->data_bytes) {
        return CP_EINVAL;
    }
    if (recorder->have_frame_seq && frame->seq != recorder->expected_frame_seq) {
        discontinuity = true;
        status = cp_recorder_flush(recorder);
        if (status != CP_OK) return status;
    }
    if (frame->sample_count > 1u) {
        status = cp_recorder_flush(recorder);
        if (status != CP_OK) return status;
        if (frame_bytes > recorder->record_capacity - CP_CPB1_DATA_RECORD_HEADER_BYTES) return CP_EBOUNDS;
        status = recorder_begin(recorder, frame);
        if (status != CP_OK) return status;
        if (discontinuity) recorder->header.quality_flags |= CP_Q_DISCONTINUITY;
        memcpy(recorder->record_buffer + CP_CPB1_DATA_RECORD_HEADER_BYTES, frame->data, frame_bytes);
        recorder->buffered_samples = frame->sample_count;
        status = cp_recorder_flush(recorder);
    } else {
        if (!recorder->active) {
            status = recorder_begin(recorder, frame);
            if (status != CP_OK) return status;
            if (discontinuity) recorder->header.quality_flags |= CP_Q_DISCONTINUITY;
        } else if (frame->period_num_us != recorder->header.period_num_us ||
                   frame->period_den != recorder->header.period_den ||
                   frame->lineage.input_stream_id != recorder->header.input_stream_id) {
            status = cp_recorder_flush(recorder);
            if (status != CP_OK) return status;
            status = recorder_begin(recorder, frame);
            if (status != CP_OK) return status;
        }
        destination_offset = CP_CPB1_DATA_RECORD_HEADER_BYTES +
            (size_t)recorder->buffered_samples * frame_bytes;
        if (destination_offset > recorder->record_capacity - frame_bytes) return CP_EBOUNDS;
        memcpy(recorder->record_buffer + destination_offset, frame->data, frame_bytes);
        ++recorder->buffered_samples;
        recorder->header.quality_flags |= (uint16_t)frame->quality_flags;
        recorder->header.input_last_seq = frame->lineage.last_seq;
        status = recorder->buffered_samples >= recorder->config.samples_per_block ?
            cp_recorder_flush(recorder) : CP_OK;
    }
    recorder->expected_frame_seq = frame->seq + 1u;
    recorder->have_frame_seq = true;
    return status;
}

void cp_recorder_discard_pending(cp_recorder_t *recorder) {
    if (recorder == NULL) return;
    recorder->active = false;
    recorder->buffered_samples = 0u;
}
