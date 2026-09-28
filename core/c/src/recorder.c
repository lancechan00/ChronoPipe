#include "chronopipe/recorder.h"

#include <stdint.h>
#include <string.h>

static cp_status_t formula_time(uint64_t origin,
                                uint32_t seq,
                                uint32_t period_num_us,
                                uint32_t period_den,
                                uint64_t *out) {
    uint64_t product;
    if (period_den == 0u || out == NULL) {
        return CP_EINVAL;
    }
    if (period_num_us != 0u && (uint64_t)seq > UINT64_MAX / (uint64_t)period_num_us) {
        return CP_EBOUNDS;
    }
    product = (uint64_t)seq * (uint64_t)period_num_us / (uint64_t)period_den;
    if (origin > UINT64_MAX - product) {
        return CP_EBOUNDS;
    }
    *out = origin + product;
    return CP_OK;
}

static cp_status_t origin_from_measurement(uint32_t seq,
                                           uint32_t period_num_us,
                                           uint32_t period_den,
                                           uint64_t measured_us,
                                           uint64_t *origin) {
    uint64_t product;
    if (period_den == 0u || origin == NULL) {
        return CP_EINVAL;
    }
    if (period_num_us != 0u && (uint64_t)seq > UINT64_MAX / (uint64_t)period_num_us) {
        return CP_EBOUNDS;
    }
    product = (uint64_t)seq * (uint64_t)period_num_us / (uint64_t)period_den;
    if (measured_us < product) {
        return CP_EINVAL;
    }
    *origin = measured_us - product;
    return CP_OK;
}

static bool frame_uses_wide(const cp_recorder_t *recorder, const cp_frame_t *frame) {
    const cp_stream_desc_t *stream = recorder->config.stream;
    bool schema_period = frame->period_num_us == stream->period_num_us &&
                         frame->period_den == stream->period_den;
    return !schema_period ||
           !cp_cpb2_shape_fits_compact(frame->stream_id,
                                       frame->channel_count,
                                       frame->sample_count,
                                       frame->quality_flags,
                                       0u,
                                       frame->dtype,
                                       frame->period_num_us);
}

static cp_status_t emit_event(cp_recorder_t *recorder,
                              uint16_t type,
                              uint32_t sequence,
                              uint64_t event_time_us,
                              int32_t detail) {
    cp_cpb2_event_t event;
    uint8_t encoded[CP_CPB2_RECORD_PREFIX_BYTES + CP_CPB2_EVENT_PAYLOAD_BYTES + 4u];
    size_t written = 0u;
    cp_status_t status;
    memset(&event, 0, sizeof(event));
    event.session_id = recorder->config.session_id;
    event.type = type;
    event.stream_id = recorder->config.stream->id;
    event.sequence = sequence;
    event.event_time_us = event_time_us;
    event.detail = detail;
    status = cp_cpb2_event_encode(&event, encoded, sizeof(encoded), &written);
    if (status != CP_OK) {
        return status;
    }
    status = recorder->store.ops->append(recorder->store.ctx, recorder->next_block_seq, encoded, written);
    if (status != CP_OK) {
        return status;
    }
    if (recorder->next_block_seq == UINT32_MAX) {
        return CP_EBOUNDS;
    }
    ++recorder->next_block_seq;
    return CP_OK;
}

static cp_status_t recorder_begin(cp_recorder_t *recorder, const cp_frame_t *frame, bool wide) {
    size_t row_bytes;
    size_t header_bytes = wide ? CP_CPB2_WIDE_RECORD_HEADER_BYTES : CP_CPB2_COMPACT_HEADER_BYTES;
    if (cp_dtype_size(frame->dtype) == 0u ||
        !cp_checked_mul_size(frame->channel_count, cp_dtype_size(frame->dtype), &row_bytes) ||
        header_bytes >= recorder->record_capacity ||
        row_bytes > recorder->record_capacity - header_bytes) {
        return CP_EBOUNDS;
    }
    recorder->wide = wide;
    recorder->payload_offset = header_bytes;
    recorder->first_sample_seq = frame->seq;
    recorder->quality_flags = frame->quality_flags;
    recorder->period_num_us = frame->period_num_us;
    recorder->period_den = frame->period_den;
    recorder->block_t0_us = frame->event_time_us;
    recorder->lineage = frame->lineage;
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
    size_t header_bytes;
    bool steady_wide;
    if (recorder == NULL || config == NULL || config->stream == NULL ||
        config->samples_per_block == 0u || store.ops == NULL || store.ops->append == NULL ||
        record_buffer == NULL || cp_dtype_size(config->stream->dtype) == 0u ||
        config->stream->channel_count == 0u || config->stream->period_den == 0u) {
        return CP_EINVAL;
    }
    steady_wide = !cp_cpb2_shape_fits_compact(config->stream->id,
                                              config->stream->channel_count,
                                              config->samples_per_block,
                                              0u,
                                              0u,
                                              config->stream->dtype,
                                              config->stream->period_num_us);
    header_bytes = steady_wide ? CP_CPB2_WIDE_RECORD_HEADER_BYTES : CP_CPB2_COMPACT_HEADER_BYTES;
    if (record_capacity <= header_bytes ||
        !cp_checked_mul_size(config->stream->channel_count, cp_dtype_size(config->stream->dtype), &row_bytes) ||
        !cp_checked_mul_size(row_bytes, config->samples_per_block, &payload_bytes) ||
        payload_bytes > record_capacity - header_bytes) {
        return CP_EBOUNDS;
    }
    memset(recorder, 0, sizeof(*recorder));
    recorder->config = *config;
    recorder->store = store;
    recorder->record_buffer = (uint8_t *)record_buffer;
    recorder->record_capacity = record_capacity;
    recorder->next_block_seq = config->first_block_seq;
    recorder->time_origin_us = config->time_origin_us;
    return CP_OK;
}

cp_status_t cp_recorder_flush(cp_recorder_t *recorder) {
    size_t row_bytes;
    size_t payload_bytes;
    size_t written = 0u;
    cp_status_t status;
    if (recorder == NULL) {
        return CP_EINVAL;
    }
    if (!recorder->active || recorder->buffered_samples == 0u) {
        return CP_OK;
    }
    if (!cp_checked_mul_size(recorder->config.stream->channel_count,
                             cp_dtype_size(recorder->config.stream->dtype),
                             &row_bytes) ||
        !cp_checked_mul_size(recorder->buffered_samples, row_bytes, &payload_bytes) ||
        payload_bytes > UINT32_MAX) {
        return CP_EBOUNDS;
    }
    if (recorder->wide) {
        cp_cpb2_wide_header_t header;
        if (recorder->quality_flags > UINT16_MAX) {
            return CP_EBOUNDS;
        }
        memset(&header, 0, sizeof(header));
        header.stream_id = recorder->config.stream->id;
        header.schema_id = recorder->config.stream->schema_id;
        header.block_seq = recorder->next_block_seq;
        header.first_sample_seq = recorder->first_sample_seq;
        header.t0_us = recorder->block_t0_us;
        header.period_num_us = recorder->period_num_us;
        header.period_den = recorder->period_den;
        header.sample_count = recorder->buffered_samples;
        header.channel_count = recorder->config.stream->channel_count;
        header.dtype = recorder->config.stream->dtype;
        header.layout = 0u;
        header.quality_flags = (uint16_t)recorder->quality_flags;
        header.payload_bytes = (uint32_t)payload_bytes;
        header.input_stream_id = recorder->lineage.input_stream_id;
        header.input_first_seq = recorder->lineage.first_seq;
        header.input_last_seq = recorder->lineage.last_seq;
        status = cp_cpb2_wide_encode(&header,
                                     recorder->record_buffer + recorder->payload_offset,
                                     recorder->record_buffer,
                                     recorder->record_capacity,
                                     &written);
    } else {
        cp_cpb2_compact_header_t header;
        memset(&header, 0, sizeof(header));
        header.stream_id = recorder->config.stream->id;
        header.dtype = (uint32_t)recorder->config.stream->dtype;
        header.layout = 0u;
        header.channel_count = recorder->config.stream->channel_count;
        header.quality_flags = recorder->quality_flags;
        header.sample_count = recorder->buffered_samples;
        header.block_seq = recorder->next_block_seq;
        header.first_sample_seq = recorder->first_sample_seq;
        status = cp_cpb2_compact_encode(&header,
                                        recorder->record_buffer + recorder->payload_offset,
                                        recorder->record_buffer,
                                        recorder->record_capacity,
                                        &written);
    }
    if (status != CP_OK) {
        return status;
    }
    status = recorder->store.ops->append(recorder->store.ctx,
                                         recorder->next_block_seq,
                                         recorder->record_buffer,
                                         written);
    if (status != CP_OK) {
        return status;
    }
    if (recorder->next_block_seq == UINT32_MAX) {
        return CP_EBOUNDS;
    }
    ++recorder->next_block_seq;
    recorder->active = false;
    recorder->buffered_samples = 0u;
    return CP_OK;
}

cp_status_t cp_recorder_append(cp_recorder_t *recorder, const cp_frame_t *frame) {
    size_t frame_bytes;
    size_t destination_offset;
    cp_status_t status = CP_OK;
    bool discontinuity = false;
    bool wide;
    const cp_stream_desc_t *stream;
    if (recorder == NULL || frame == NULL || frame->data == NULL || recorder->config.stream == NULL) {
        return CP_EINVAL;
    }
    stream = recorder->config.stream;
    if (frame->stream_id != stream->id || frame->dtype != stream->dtype ||
        frame->channel_count != stream->channel_count || frame->sample_count == 0u ||
        frame->period_den == 0u || !cp_frame_shape_bytes(frame, &frame_bytes) ||
        frame_bytes != frame->data_bytes) {
        return CP_EINVAL;
    }
    if (recorder->have_frame_seq && frame->seq != recorder->expected_frame_seq) {
        int32_t detail = recorder->expected_frame_seq <= (uint32_t)INT32_MAX ?
            (int32_t)recorder->expected_frame_seq : -1;
        status = cp_recorder_flush(recorder);
        if (status != CP_OK) {
            return status;
        }
        status = emit_event(recorder, CP_EVENT_SEQUENCE_GAP, frame->seq, frame->event_time_us, detail);
        if (status != CP_OK) {
            return status;
        }
        discontinuity = true;
    }
    if (frame->period_num_us == stream->period_num_us && frame->period_den == stream->period_den &&
        frame->period_num_us != 0u) {
        uint64_t expected_us = 0u;
        status = formula_time(recorder->time_origin_us,
                              frame->seq,
                              frame->period_num_us,
                              frame->period_den,
                              &expected_us);
        if (status != CP_OK) {
            return status;
        }
        if (frame->event_time_us != expected_us) {
            uint64_t new_origin = 0u;
            status = cp_recorder_flush(recorder);
            if (status != CP_OK) {
                return status;
            }
            status = origin_from_measurement(frame->seq,
                                             frame->period_num_us,
                                             frame->period_den,
                                             frame->event_time_us,
                                             &new_origin);
            if (status != CP_OK) {
                return status;
            }
            status = emit_event(recorder, CP_EVENT_TIME_MAPPING, frame->seq, frame->event_time_us, 0);
            if (status != CP_OK) {
                return status;
            }
            recorder->time_origin_us = new_origin;
            discontinuity = true;
        }
    } else if (recorder->have_frame_seq) {
        discontinuity = true;
    }
    wide = frame_uses_wide(recorder, frame);
    if (recorder->active) {
        bool quality_overflow = !wide &&
            (recorder->quality_flags | frame->quality_flags) > CP_CPB2_COMPACT_MAX_QUALITY;
        bool period_changed = frame->period_num_us != recorder->period_num_us ||
                              frame->period_den != recorder->period_den;
        bool lineage_changed = recorder->wide &&
            frame->lineage.input_stream_id != recorder->lineage.input_stream_id;
        if (quality_overflow || period_changed || lineage_changed || wide != recorder->wide) {
            status = cp_recorder_flush(recorder);
            if (status != CP_OK) {
                return status;
            }
        }
    }
    if (frame->sample_count > 1u) {
        status = cp_recorder_flush(recorder);
        if (status != CP_OK) {
            return status;
        }
        status = recorder_begin(recorder, frame, wide);
        if (status != CP_OK) {
            return status;
        }
        if (frame_bytes > recorder->record_capacity - recorder->payload_offset) {
            recorder->active = false;
            return CP_EBOUNDS;
        }
        if (discontinuity) {
            recorder->quality_flags |= CP_Q_DISCONTINUITY;
        }
        memcpy(recorder->record_buffer + recorder->payload_offset, frame->data, frame_bytes);
        recorder->buffered_samples = frame->sample_count;
        status = cp_recorder_flush(recorder);
    } else {
        if (!recorder->active) {
            status = recorder_begin(recorder, frame, wide);
            if (status != CP_OK) {
                return status;
            }
            if (discontinuity) {
                recorder->quality_flags |= CP_Q_DISCONTINUITY;
            }
        }
        destination_offset = recorder->payload_offset + (size_t)recorder->buffered_samples * frame_bytes;
        if (destination_offset > recorder->record_capacity - frame_bytes) {
            return CP_EBOUNDS;
        }
        memcpy(recorder->record_buffer + destination_offset, frame->data, frame_bytes);
        ++recorder->buffered_samples;
        recorder->quality_flags |= frame->quality_flags;
        if (recorder->wide) {
            recorder->lineage.last_seq = frame->lineage.last_seq;
        }
        status = recorder->buffered_samples >= recorder->config.samples_per_block ?
            cp_recorder_flush(recorder) : CP_OK;
    }
    if (status == CP_OK) {
        recorder->expected_frame_seq = frame->seq + 1u;
        recorder->have_frame_seq = true;
    }
    return status;
}

void cp_recorder_discard_pending(cp_recorder_t *recorder) {
    if (recorder == NULL) {
        return;
    }
    recorder->active = false;
    recorder->buffered_samples = 0u;
}
