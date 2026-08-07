#include "chronopipe/chronopipe.h"

#include <stdio.h>
#include <string.h>

#define SOURCE_FRAMES (200u * 60u * 60u)

typedef struct {
    uint32_t count[6];
    uint32_t gaps;
} counters_t;

static void count_frame(void *user, const cp_frame_t *frame) {
    counters_t *counters = (counters_t *)user;
    if (frame->stream_id < 6u) ++counters->count[frame->stream_id];
}

static void count_event(void *user, const cp_event_t *event) {
    counters_t *counters = (counters_t *)user;
    if (event->type == CP_EVENT_SEQUENCE_GAP) ++counters->gaps;
}

static void stream(cp_stream_desc_t *descriptor,
                   uint32_t id,
                   const char *name,
                   cp_dtype_t dtype,
                   uint32_t period_us) {
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->id = id;
    descriptor->schema_id = id;
    descriptor->name = name;
    descriptor->dtype = dtype;
    descriptor->channel_count = 8u;
    descriptor->period_num_us = period_us;
    descriptor->period_den = 1u;
}

int main(void) {
    static uint8_t arena_memory[65536];
    float scales[8] = {0.001f, 0.001f, 0.001f, 0.001f, 4.0f, 4.0f, 4.0f, 4.0f};
    float offsets[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float coefficients[8] = {0.125f, 0.125f, 0.125f, 0.125f,
                             0.125f, 0.125f, 0.125f, 0.125f};
    cp_quantize_config_t quant = {8u, CP_DTYPE_I16, scales, offsets};
    cp_fir_decimate_config_t fir = {8u, 4u, 8u, coefficients};
    cp_moving_mean_config_t mean = {8u, 5u};
    cp_window_config_t window = {8u, 512u, 102u};
    cp_stream_desc_t streams[5];
    cp_stage_t stages[4];
    cp_runtime_t runtime;
    cp_arena_t arena;
    counters_t counters;
    uint32_t seq;
    const uint32_t low_rate_frames = SOURCE_FRAMES / 4u;
    const uint32_t expected_windows = 1u + (low_rate_frames - 512u) / 102u;

    memset(&counters, 0, sizeof(counters));
    stream(&streams[0], 1u, "raw", CP_DTYPE_F32, 5000u);
    stream(&streams[1], 2u, "quantized", CP_DTYPE_I16, 5000u);
    stream(&streams[2], 3u, "fir_50hz", CP_DTYPE_F32, 20000u);
    stream(&streams[3], 4u, "moving_mean", CP_DTYPE_F32, 20000u);
    stream(&streams[4], 5u, "window", CP_DTYPE_F32, 20000u);
    cp_arena_init(&arena, arena_memory, sizeof(arena_memory));
    if (cp_stage_quantize_init(&stages[0], 1u, 1u, 2u, &quant, &arena) != CP_OK ||
        cp_stage_fir_decimate_init(&stages[1], 2u, 1u, 3u, &fir, &arena) != CP_OK ||
        cp_stage_moving_mean_init(&stages[2], 3u, 3u, 4u, &mean, &arena) != CP_OK ||
        cp_stage_window_init(&stages[3], 4u, 4u, 5u, &window, &arena) != CP_OK ||
        cp_runtime_init(&runtime, streams, 5u, stages, 4u,
                        count_frame, count_event, &counters) != CP_OK) {
        return 1;
    }
    for (seq = 0u; seq < SOURCE_FRAMES; ++seq) {
        float values[8];
        cp_frame_t frame;
        unsigned channel;
        for (channel = 0u; channel < 8u; ++channel) {
            values[channel] = channel < 4u ? (float)(seq % 660u) * 0.005f : (float)(seq % 22750u) * 4.0f;
        }
        memset(&frame, 0, sizeof(frame));
        frame.stream_id = 1u; frame.seq = seq;
        frame.event_time_us = (uint64_t)seq * 5000u; frame.emit_time_us = frame.event_time_us;
        frame.period_num_us = 5000u; frame.period_den = 1u;
        frame.dtype = CP_DTYPE_F32; frame.channel_count = 8u; frame.sample_count = 1u;
        frame.data = values; frame.data_bytes = sizeof(values);
        if (cp_runtime_push(&runtime, &frame) != CP_OK) return 2;
    }
    if (counters.gaps != 0u || counters.count[1] != SOURCE_FRAMES ||
        counters.count[2] != SOURCE_FRAMES || counters.count[3] != low_rate_frames ||
        counters.count[4] != low_rate_frames || counters.count[5] != expected_windows) {
        fprintf(stderr, "unexpected counts: %u %u %u %u %u gaps=%u\n",
                counters.count[1], counters.count[2], counters.count[3],
                counters.count[4], counters.count[5], counters.gaps);
        return 3;
    }
    printf("simulated one hour: source=%u low_rate=%u windows=%u arena=%lu bytes\n",
           SOURCE_FRAMES, low_rate_frames, expected_windows, (unsigned long)arena.used);
    return 0;
}
