#include "chronopipe/chronopipe.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned frame_count[8];
    float last_peak_hz[8];
    cp_recorder_t *quantized_recorder;
    cp_status_t recorder_status;
} demo_sink_t;

typedef struct {
    unsigned records;
    size_t bytes;
} upload_sink_t;

static void on_frame(void *user, const cp_frame_t *frame) {
    demo_sink_t *sink = (demo_sink_t *)user;
    if (frame->stream_id < 8u) {
        ++sink->frame_count[frame->stream_id];
    }
    if (frame->stream_id == 7u) {
        memcpy(sink->last_peak_hz, frame->data, sizeof(sink->last_peak_hz));
    }
    if (frame->stream_id == 2u && sink->quantized_recorder != NULL) {
        if (sink->recorder_status == CP_OK) {
            sink->recorder_status = cp_recorder_append(sink->quantized_recorder, frame);
        }
    }
}

static cp_status_t upload_record(void *user,
                                 uint32_t block_seq,
                                 const uint8_t *data,
                                 size_t bytes) {
    upload_sink_t *sink = (upload_sink_t *)user;
    (void)block_seq;
    if (cp_evidence_validate_record(data, bytes) != CP_OK) return CP_EFORMAT;
    ++sink->records;
    sink->bytes += bytes;
    return CP_OK;
}

static void configure_stream(cp_stream_desc_t *stream,
                             uint32_t id,
                             const char *name,
                             cp_dtype_t dtype,
                             uint32_t period_us) {
    memset(stream, 0, sizeof(*stream));
    stream->id = id;
    stream->schema_id = id;
    stream->name = name;
    stream->dtype = dtype;
    stream->channel_count = 8u;
    stream->period_num_us = period_us;
    stream->period_den = 1u;
}

int main(void) {
    static uint8_t arena_memory[16384];
    enum { k_demo_record_bytes = CP_CPB2_COMPACT_HEADER_BYTES + 32u * 8u * 2u };
    static uint8_t ring_memory[8u * k_demo_record_bytes];
    static uint8_t record_buffer[k_demo_record_bytes];
    const float quant_scale[8] = {0.001f, 0.001f, 0.001f, 0.001f,
                                  4.0f, 4.0f, 4.0f, 4.0f};
    const float quant_offset[8] = {0.0f, 0.0f, 0.0f, 0.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f};
    const float fir_coefficients[8] = {0.125f, 0.125f, 0.125f, 0.125f,
                                       0.125f, 0.125f, 0.125f, 0.125f};
    cp_quantize_config_t quant = {8u, CP_DTYPE_I16, quant_scale, quant_offset};
    cp_fir_decimate_config_t fir = {8u, 4u, 8u, fir_coefficients};
    cp_moving_mean_config_t mean = {8u, 5u};
    cp_window_config_t window = {8u, 32u, 8u};
    cp_spectrum_config_t spectrum = {8u, 32u, CP_SPECTRUM_HANN};
    cp_peak_frequency_config_t peak = {8u, 32u, 25.0f, 0.5f, 5.0f};
    cp_stream_desc_t streams[7];
    cp_stage_t stages[6];
    cp_runtime_t runtime;
    cp_arena_t arena;
    cp_ram_store_t ram_store;
    cp_store_t store;
    cp_recorder_config_t recorder_config;
    cp_recorder_t recorder;
    cp_batch_transport_t transport;
    cp_batch_sender_t sender;
    upload_sink_t uploaded;
    demo_sink_t sink;
    uint32_t seq;

    memset(&sink, 0, sizeof(sink));
    memset(&uploaded, 0, sizeof(uploaded));
    configure_stream(&streams[0], 1u, "raw", CP_DTYPE_F32, 10000u);
    configure_stream(&streams[1], 2u, "quantized", CP_DTYPE_I16, 10000u);
    configure_stream(&streams[2], 3u, "fir_25hz", CP_DTYPE_F32, 40000u);
    configure_stream(&streams[3], 4u, "moving_mean", CP_DTYPE_F32, 40000u);
    configure_stream(&streams[4], 5u, "window", CP_DTYPE_F32, 40000u);
    configure_stream(&streams[5], 6u, "spectrum", CP_DTYPE_F32, 40000u);
    configure_stream(&streams[6], 7u, "peak_hz", CP_DTYPE_F32, 320000u);

    if (cp_ram_store_init(&ram_store, ring_memory, sizeof(ring_memory), k_demo_record_bytes, 8u,
                          CP_OVERFLOW_STOP) != CP_OK) {
        fputs("RAM store initialization failed\n", stderr);
        return 1;
    }
    store.ops = &cp_ram_store_ops;
    store.ctx = &ram_store;
    recorder_config.session_id = 1u;
    recorder_config.time_origin_us = 0u;
    recorder_config.stream = &streams[1];
    recorder_config.samples_per_block = 32u;
    recorder_config.first_block_seq = 0u;
    if (cp_recorder_init(&recorder, &recorder_config, store,
                         record_buffer, sizeof(record_buffer)) != CP_OK) {
        fputs("recorder initialization failed\n", stderr);
        return 1;
    }
    sink.quantized_recorder = &recorder;

    cp_arena_init(&arena, arena_memory, sizeof(arena_memory));
    if (cp_stage_quantize_init(&stages[0], 1u, 1u, 2u, &quant, &arena) != CP_OK ||
        cp_stage_fir_decimate_init(&stages[1], 2u, 1u, 3u, &fir, &arena) != CP_OK ||
        cp_stage_moving_mean_init(&stages[2], 3u, 3u, 4u, &mean, &arena) != CP_OK ||
        cp_stage_window_init(&stages[3], 4u, 4u, 5u, &window, &arena) != CP_OK ||
        cp_stage_spectrum_init(&stages[4], 5u, 5u, 6u, &spectrum, &arena) != CP_OK ||
        cp_stage_peak_frequency_init(&stages[5], 6u, 6u, 7u, &peak, &arena) != CP_OK) {
        fputs("stage initialization failed\n", stderr);
        return 1;
    }
    if (cp_runtime_init(&runtime, streams, 7u, stages, 6u, on_frame, NULL, &sink) != CP_OK) {
        fputs("graph validation failed\n", stderr);
        return 1;
    }
    for (seq = 0u; seq < 240u; ++seq) {
        float values[8];
        cp_frame_t frame;
        unsigned channel;
        for (channel = 0u; channel < 8u; ++channel) {
            float frequency = 1.0f + 0.25f * channel;
            float amplitude = channel < 4u ? 1.5f : 30000.0f;
            values[channel] = amplitude * (1.0f + sinf((float)(2.0 * 3.14159265358979323846 * frequency * seq / 100.0)));
        }
        memset(&frame, 0, sizeof(frame));
        frame.stream_id = 1u;
        frame.seq = seq;
        frame.event_time_us = (uint64_t)seq * 10000u;
        frame.emit_time_us = frame.event_time_us;
        frame.period_num_us = 10000u;
        frame.period_den = 1u;
        frame.dtype = CP_DTYPE_F32;
        frame.channel_count = 8u;
        frame.sample_count = 1u;
        frame.data = values;
        frame.data_bytes = sizeof(values);
        if (cp_runtime_push(&runtime, &frame) != CP_OK) {
            fputs("pipeline execution failed\n", stderr);
            return 1;
        }
    }
    if (sink.recorder_status != CP_OK || cp_recorder_flush(&recorder) != CP_OK) {
        fputs("recorder append or flush failed\n", stderr);
        return 1;
    }
    transport.send = upload_record;
    transport.ctx = &uploaded;
    cp_batch_sender_init(&sender, store, transport, 2u);
    while (ram_store.count > 0u) {
        if (cp_batch_sender_poll(&sender) != CP_OK ||
            cp_batch_sender_ack(&sender, sender.pending_seq) != CP_OK) {
            fputs("batch upload failed\n", stderr);
            return 1;
        }
    }
    printf("raw=%u quantized=%u fir=%u windows=%u spectra=%u features=%u\n",
           sink.frame_count[1], sink.frame_count[2], sink.frame_count[3],
           sink.frame_count[5], sink.frame_count[6], sink.frame_count[7]);
    printf("last peak frequencies: %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f Hz\n",
           sink.last_peak_hz[0], sink.last_peak_hz[1], sink.last_peak_hz[2], sink.last_peak_hz[3],
           sink.last_peak_hz[4], sink.last_peak_hz[5], sink.last_peak_hz[6], sink.last_peak_hz[7]);
    printf("recorded and acknowledged: %u CPB2 blocks, %lu bytes\n",
           uploaded.records, (unsigned long)uploaded.bytes);
    return 0;
}
