#include "chronopipe/stages.h"

#include <math.h>
#include <string.h>

#define CP_STAGE_ALIGNMENT 8u

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    cp_dtype_t dtype;
    uint16_t *indices;
    uint8_t *output;
} select_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    float *gain;
    float *bias;
    float *output;
} affine_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    cp_dtype_t output_dtype;
    float *scale;
    float *offset;
    uint8_t *output;
} quantize_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    uint16_t factor;
    uint16_t count;
    uint32_t first_seq;
    uint32_t quality;
    float *sum;
    float *output;
} mean_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    uint16_t factor;
    uint16_t phase;
    uint16_t count;
    cp_dtype_t dtype;
} pick_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    uint16_t window;
    uint16_t head;
    uint16_t count;
    float *ring;
    float *sum;
    float *output;
    uint32_t *quality_ring;
} moving_mean_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    uint16_t factor;
    uint16_t tap_count;
    uint16_t head;
    uint16_t count;
    uint16_t decimation_count;
    const float *coefficients;
    float *ring;
    float *output;
    uint32_t *quality_ring;
} fir_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    uint16_t length;
    uint16_t hop;
    uint16_t head;
    uint16_t count;
    uint16_t since_emit;
    float *ring;
    float *output;
    uint32_t *quality_ring;
} window_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    uint16_t length;
    uint16_t bins;
    cp_spectrum_window_t window;
    float *output;
} spectrum_state_t;

typedef struct {
    uint32_t output_stream;
    uint16_t channels;
    uint16_t fft_length;
    uint16_t first_bin;
    uint16_t last_bin;
    float bin_hz;
    float *output;
} peak_frequency_state_t;

static void stage_assign(cp_stage_t *stage,
                         uint32_t id,
                         uint32_t input_stream,
                         uint32_t output_stream,
                         const cp_stage_ops_t *ops,
                         void *state,
                         size_t bytes) {
    memset(stage, 0, sizeof(*stage));
    stage->id = id;
    stage->input_stream_id = input_stream;
    stage->output_stream_id = output_stream;
    stage->ops = ops;
    stage->state = state;
    stage->state_bytes = bytes;
    stage->alignment = CP_STAGE_ALIGNMENT;
}

static void init_output(cp_frame_t *output,
                        const cp_frame_t *input,
                        uint32_t stream_id,
                        cp_dtype_t dtype,
                        uint16_t channels,
                        uint16_t samples,
                        const void *data,
                        size_t bytes) {
    memset(output, 0, sizeof(*output));
    output->stream_id = stream_id;
    output->seq = CP_SEQ_AUTO;
    output->event_time_us = input->event_time_us;
    output->emit_time_us = input->emit_time_us;
    output->quality_flags = input->quality_flags;
    output->dtype = dtype;
    output->channel_count = channels;
    output->sample_count = samples;
    output->data = data;
    output->data_bytes = bytes;
}

static void report_quality(cp_emitter_t *emitter,
                           const cp_frame_t *input,
                           uint16_t type,
                           int32_t detail) {
    cp_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.stream_id = input->stream_id;
    event.seq = input->seq;
    event.event_time_us = input->event_time_us;
    event.detail = detail;
    cp_runtime_event(emitter->runtime, &event);
}

static cp_status_t select_process(void *opaque,
                                  const cp_frame_t *input,
                                  cp_emitter_t *emitter) {
    select_state_t *state = (select_state_t *)opaque;
    cp_frame_t output;
    size_t width = cp_dtype_size(state->dtype);
    size_t channel;
    if (input->dtype != state->dtype || input->sample_count != 1u) return CP_EINVAL;
    for (channel = 0u; channel < state->channels; ++channel) {
        uint16_t index = state->indices[channel];
        if (index >= input->channel_count) return CP_EBOUNDS;
        memcpy(state->output + channel * width,
               (const uint8_t *)input->data + (size_t)index * width,
               width);
    }
    init_output(&output, input, state->output_stream, state->dtype, state->channels, 1u,
                state->output, (size_t)state->channels * width);
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t SELECT_OPS = {select_process, NULL};

cp_status_t cp_stage_select_init(cp_stage_t *stage,
                                 uint32_t id,
                                 uint32_t input_stream,
                                 uint32_t output_stream,
                                 const cp_select_config_t *config,
                                 cp_arena_t *arena) {
    select_state_t *state;
    size_t output_bytes;
    size_t indices_bytes;
    if (stage == NULL || config == NULL || arena == NULL || config->indices == NULL ||
        config->output_channels == 0u || cp_dtype_size(config->dtype) == 0u) return CP_EINVAL;
    output_bytes = (size_t)config->output_channels * cp_dtype_size(config->dtype);
    indices_bytes = (size_t)config->output_channels * sizeof(uint16_t);
    state = (select_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    state->indices = (uint16_t *)cp_arena_alloc(arena, indices_bytes, sizeof(uint16_t));
    state->output = (uint8_t *)cp_arena_alloc(arena, output_bytes, CP_STAGE_ALIGNMENT);
    if (state->indices == NULL || state->output == NULL) return CP_ENOMEM;
    state->output_stream = output_stream;
    state->channels = config->output_channels;
    state->dtype = config->dtype;
    memcpy(state->indices, config->indices, indices_bytes);
    stage_assign(stage, id, input_stream, output_stream, &SELECT_OPS, state,
                 sizeof(*state) + indices_bytes + output_bytes);
    return CP_OK;
}

static cp_status_t affine_process(void *opaque,
                                  const cp_frame_t *input,
                                  cp_emitter_t *emitter) {
    affine_state_t *state = (affine_state_t *)opaque;
    const float *source = (const float *)input->data;
    cp_frame_t output;
    uint16_t i;
    if (input->dtype != CP_DTYPE_F32 || input->sample_count != 1u ||
        input->channel_count != state->channels) return CP_EINVAL;
    for (i = 0u; i < state->channels; ++i) {
        state->output[i] = source[i] * state->gain[i] + state->bias[i];
    }
    init_output(&output, input, state->output_stream, CP_DTYPE_F32, state->channels, 1u,
                state->output, (size_t)state->channels * sizeof(float));
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t AFFINE_OPS = {affine_process, NULL};

cp_status_t cp_stage_affine_init(cp_stage_t *stage,
                                 uint32_t id,
                                 uint32_t input_stream,
                                 uint32_t output_stream,
                                 const cp_affine_config_t *config,
                                 cp_arena_t *arena) {
    affine_state_t *state;
    size_t bytes;
    if (stage == NULL || config == NULL || arena == NULL || config->gain == NULL ||
        config->bias == NULL || config->channels == 0u) return CP_EINVAL;
    bytes = (size_t)config->channels * sizeof(float);
    state = (affine_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    state->gain = (float *)cp_arena_alloc(arena, bytes, sizeof(float));
    state->bias = (float *)cp_arena_alloc(arena, bytes, sizeof(float));
    state->output = (float *)cp_arena_alloc(arena, bytes, CP_STAGE_ALIGNMENT);
    if (state->gain == NULL || state->bias == NULL || state->output == NULL) return CP_ENOMEM;
    state->output_stream = output_stream;
    state->channels = config->channels;
    memcpy(state->gain, config->gain, bytes);
    memcpy(state->bias, config->bias, bytes);
    stage_assign(stage, id, input_stream, output_stream, &AFFINE_OPS, state,
                 sizeof(*state) + 3u * bytes);
    return CP_OK;
}

static double round_away(double value) {
    return value >= 0.0 ? floor(value + 0.5) : ceil(value - 0.5);
}

static double read_numeric(const void *data, cp_dtype_t dtype, uint16_t index) {
    switch (dtype) {
        case CP_DTYPE_F32: return ((const float *)data)[index];
        case CP_DTYPE_I16: return ((const int16_t *)data)[index];
        case CP_DTYPE_U16: return ((const uint16_t *)data)[index];
        case CP_DTYPE_I32: return ((const int32_t *)data)[index];
        case CP_DTYPE_U32: return ((const uint32_t *)data)[index];
        default: return 0.0;
    }
}

static bool write_numeric(void *data, cp_dtype_t dtype, uint16_t index, double value) {
    bool saturated = false;
    switch (dtype) {
        case CP_DTYPE_F32:
            ((float *)data)[index] = (float)value;
            break;
        case CP_DTYPE_I16:
            if (value < -32768.0) { value = -32768.0; saturated = true; }
            if (value > 32767.0) { value = 32767.0; saturated = true; }
            ((int16_t *)data)[index] = (int16_t)value;
            break;
        case CP_DTYPE_U16:
            if (value < 0.0) { value = 0.0; saturated = true; }
            if (value > 65535.0) { value = 65535.0; saturated = true; }
            ((uint16_t *)data)[index] = (uint16_t)value;
            break;
        case CP_DTYPE_I32:
            if (value < -2147483648.0) { value = -2147483648.0; saturated = true; }
            if (value > 2147483647.0) { value = 2147483647.0; saturated = true; }
            ((int32_t *)data)[index] = (int32_t)value;
            break;
        case CP_DTYPE_U32:
            if (value < 0.0) { value = 0.0; saturated = true; }
            if (value > 4294967295.0) { value = 4294967295.0; saturated = true; }
            ((uint32_t *)data)[index] = (uint32_t)value;
            break;
        default:
            saturated = true;
            break;
    }
    return saturated;
}

static cp_status_t quantize_process(void *opaque,
                                    const cp_frame_t *input,
                                    cp_emitter_t *emitter) {
    quantize_state_t *state = (quantize_state_t *)opaque;
    cp_frame_t output;
    uint16_t i;
    uint32_t quality = input->quality_flags;
    if (input->sample_count != 1u || input->channel_count != state->channels ||
        cp_dtype_size(input->dtype) == 0u) return CP_EINVAL;
    for (i = 0u; i < state->channels; ++i) {
        double source = read_numeric(input->data, input->dtype, i);
        double value;
        if (!isfinite(source)) {
            source = 0.0;
            quality |= CP_Q_NON_FINITE;
        }
        if (state->output_dtype == CP_DTYPE_F32) {
            value = source * (double)state->scale[i] + (double)state->offset[i];
        } else {
            if (state->scale[i] == 0.0f) return CP_EINVAL;
            value = round_away((source - (double)state->offset[i]) / (double)state->scale[i]);
        }
        if (write_numeric(state->output, state->output_dtype, i, value)) {
            quality |= CP_Q_SATURATED;
        }
    }
    init_output(&output, input, state->output_stream, state->output_dtype, state->channels, 1u,
                state->output, (size_t)state->channels * cp_dtype_size(state->output_dtype));
    output.quality_flags = quality;
    if ((quality & CP_Q_NON_FINITE) != 0u) report_quality(emitter, input, CP_EVENT_NON_FINITE, 0);
    if ((quality & CP_Q_SATURATED) != 0u) report_quality(emitter, input, CP_EVENT_SATURATION, 0);
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t QUANTIZE_OPS = {quantize_process, NULL};

cp_status_t cp_stage_quantize_init(cp_stage_t *stage,
                                   uint32_t id,
                                   uint32_t input_stream,
                                   uint32_t output_stream,
                                   const cp_quantize_config_t *config,
                                   cp_arena_t *arena) {
    quantize_state_t *state;
    size_t channel_bytes;
    size_t output_bytes;
    if (stage == NULL || config == NULL || arena == NULL || config->scale == NULL ||
        config->offset == NULL || config->channels == 0u ||
        cp_dtype_size(config->output_dtype) == 0u) return CP_EINVAL;
    channel_bytes = (size_t)config->channels * sizeof(float);
    output_bytes = (size_t)config->channels * cp_dtype_size(config->output_dtype);
    state = (quantize_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    state->scale = (float *)cp_arena_alloc(arena, channel_bytes, sizeof(float));
    state->offset = (float *)cp_arena_alloc(arena, channel_bytes, sizeof(float));
    state->output = (uint8_t *)cp_arena_alloc(arena, output_bytes, CP_STAGE_ALIGNMENT);
    if (state->scale == NULL || state->offset == NULL || state->output == NULL) return CP_ENOMEM;
    state->output_stream = output_stream;
    state->channels = config->channels;
    state->output_dtype = config->output_dtype;
    memcpy(state->scale, config->scale, channel_bytes);
    memcpy(state->offset, config->offset, channel_bytes);
    stage_assign(stage, id, input_stream, output_stream, &QUANTIZE_OPS, state,
                 sizeof(*state) + 2u * channel_bytes + output_bytes);
    return CP_OK;
}

static void mean_reset(void *opaque, uint32_t reason) {
    mean_state_t *state = (mean_state_t *)opaque;
    (void)reason;
    state->count = 0u;
    state->quality = 0u;
    memset(state->sum, 0, (size_t)state->channels * sizeof(float));
}

static cp_status_t mean_process(void *opaque,
                                const cp_frame_t *input,
                                cp_emitter_t *emitter) {
    mean_state_t *state = (mean_state_t *)opaque;
    const float *source = (const float *)input->data;
    cp_frame_t output;
    uint16_t i;
    if (input->dtype != CP_DTYPE_F32 || input->sample_count != 1u ||
        input->channel_count != state->channels) return CP_EINVAL;
    if (state->count == 0u) state->first_seq = input->seq;
    for (i = 0u; i < state->channels; ++i) state->sum[i] += source[i];
    state->quality |= input->quality_flags;
    state->count++;
    if (state->count < state->factor) return CP_OK;
    for (i = 0u; i < state->channels; ++i) {
        state->output[i] = state->sum[i] / (float)state->factor;
        state->sum[i] = 0.0f;
    }
    init_output(&output, input, state->output_stream, CP_DTYPE_F32, state->channels, 1u,
                state->output, (size_t)state->channels * sizeof(float));
    output.quality_flags = state->quality;
    output.lineage.input_stream_id = input->stream_id;
    output.lineage.first_seq = state->first_seq;
    output.lineage.last_seq = input->seq;
    state->count = 0u;
    state->quality = 0u;
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t MEAN_OPS = {mean_process, mean_reset};

cp_status_t cp_stage_mean_decimate_init(cp_stage_t *stage,
                                        uint32_t id,
                                        uint32_t input_stream,
                                        uint32_t output_stream,
                                        const cp_mean_decimate_config_t *config,
                                        cp_arena_t *arena) {
    mean_state_t *state;
    size_t bytes;
    if (stage == NULL || config == NULL || arena == NULL || config->channels == 0u ||
        config->factor < 2u) return CP_EINVAL;
    bytes = (size_t)config->channels * sizeof(float);
    state = (mean_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    memset(state, 0, sizeof(*state));
    state->sum = (float *)cp_arena_alloc(arena, bytes, CP_STAGE_ALIGNMENT);
    state->output = (float *)cp_arena_alloc(arena, bytes, CP_STAGE_ALIGNMENT);
    if (state->sum == NULL || state->output == NULL) return CP_ENOMEM;
    memset(state->sum, 0, bytes);
    state->output_stream = output_stream;
    state->channels = config->channels;
    state->factor = config->factor;
    stage_assign(stage, id, input_stream, output_stream, &MEAN_OPS, state,
                 sizeof(*state) + 2u * bytes);
    return CP_OK;
}

static void pick_reset(void *opaque, uint32_t reason) {
    pick_state_t *state = (pick_state_t *)opaque;
    (void)reason;
    state->count = 0u;
}

static cp_status_t pick_process(void *opaque,
                                const cp_frame_t *input,
                                cp_emitter_t *emitter) {
    pick_state_t *state = (pick_state_t *)opaque;
    cp_frame_t output;
    uint16_t current;
    if (input->sample_count != 1u || input->dtype != state->dtype ||
        input->channel_count != state->channels) return CP_EINVAL;
    current = state->count;
    state->count = (uint16_t)((state->count + 1u) % state->factor);
    if (current != state->phase) return CP_OK;
    init_output(&output, input, state->output_stream, input->dtype, input->channel_count, 1u,
                input->data, input->data_bytes);
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t PICK_OPS = {pick_process, pick_reset};

cp_status_t cp_stage_pick_decimate_init(cp_stage_t *stage,
                                        uint32_t id,
                                        uint32_t input_stream,
                                        uint32_t output_stream,
                                        const cp_pick_decimate_config_t *config,
                                        cp_arena_t *arena) {
    pick_state_t *state;
    if (stage == NULL || config == NULL || arena == NULL || config->factor < 2u ||
        config->phase >= config->factor || config->channels == 0u ||
        cp_dtype_size(config->dtype) == 0u) return CP_EINVAL;
    state = (pick_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    memset(state, 0, sizeof(*state));
    state->output_stream = output_stream;
    state->channels = config->channels;
    state->factor = config->factor;
    state->phase = config->phase;
    state->dtype = config->dtype;
    stage_assign(stage, id, input_stream, output_stream, &PICK_OPS, state, sizeof(*state));
    return CP_OK;
}

static void moving_reset(void *opaque, uint32_t reason) {
    moving_mean_state_t *state = (moving_mean_state_t *)opaque;
    size_t values = (size_t)state->channels * state->window;
    (void)reason;
    state->head = 0u;
    state->count = 0u;
    memset(state->ring, 0, values * sizeof(float));
    memset(state->sum, 0, (size_t)state->channels * sizeof(float));
    memset(state->quality_ring, 0, (size_t)state->window * sizeof(uint32_t));
}

static cp_status_t moving_process(void *opaque,
                                  const cp_frame_t *input,
                                  cp_emitter_t *emitter) {
    moving_mean_state_t *state = (moving_mean_state_t *)opaque;
    const float *source = (const float *)input->data;
    cp_frame_t output;
    uint16_t i;
    if (input->dtype != CP_DTYPE_F32 || input->sample_count != 1u ||
        input->channel_count != state->channels) return CP_EINVAL;
    for (i = 0u; i < state->channels; ++i) {
        size_t index = (size_t)state->head * state->channels + i;
        if (state->count == state->window) state->sum[i] -= state->ring[index];
        state->ring[index] = source[i];
        state->sum[i] += source[i];
        state->output[i] = state->sum[i] /
                           (float)(state->count < state->window ? state->count + 1u : state->window);
    }
    state->quality_ring[state->head] = input->quality_flags;
    state->head = (uint16_t)((state->head + 1u) % state->window);
    if (state->count < state->window) state->count++;
    init_output(&output, input, state->output_stream, CP_DTYPE_F32, state->channels, 1u,
                state->output, (size_t)state->channels * sizeof(float));
    output.lineage.input_stream_id = input->stream_id;
    output.lineage.first_seq = input->seq - (uint32_t)(state->count - 1u);
    output.lineage.last_seq = input->seq;
    output.quality_flags = 0u;
    for (i = 0u; i < state->count; ++i) output.quality_flags |= state->quality_ring[i];
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t MOVING_OPS = {moving_process, moving_reset};

cp_status_t cp_stage_moving_mean_init(cp_stage_t *stage,
                                      uint32_t id,
                                      uint32_t input_stream,
                                      uint32_t output_stream,
                                      const cp_moving_mean_config_t *config,
                                      cp_arena_t *arena) {
    moving_mean_state_t *state;
    size_t ring_bytes;
    size_t row_bytes;
    if (stage == NULL || config == NULL || arena == NULL || config->channels == 0u ||
        config->window == 0u) return CP_EINVAL;
    row_bytes = (size_t)config->channels * sizeof(float);
    if (!cp_checked_mul_size(row_bytes, config->window, &ring_bytes)) return CP_EBOUNDS;
    state = (moving_mean_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    memset(state, 0, sizeof(*state));
    state->ring = (float *)cp_arena_alloc(arena, ring_bytes, CP_STAGE_ALIGNMENT);
    state->sum = (float *)cp_arena_alloc(arena, row_bytes, CP_STAGE_ALIGNMENT);
    state->output = (float *)cp_arena_alloc(arena, row_bytes, CP_STAGE_ALIGNMENT);
    state->quality_ring = (uint32_t *)cp_arena_alloc(
        arena, (size_t)config->window * sizeof(uint32_t), sizeof(uint32_t));
    if (state->ring == NULL || state->sum == NULL || state->output == NULL ||
        state->quality_ring == NULL) return CP_ENOMEM;
    memset(state->ring, 0, ring_bytes);
    memset(state->sum, 0, row_bytes);
    memset(state->quality_ring, 0, (size_t)config->window * sizeof(uint32_t));
    state->output_stream = output_stream;
    state->channels = config->channels;
    state->window = config->window;
    stage_assign(stage, id, input_stream, output_stream, &MOVING_OPS, state,
                 sizeof(*state) + ring_bytes + 2u * row_bytes +
                 (size_t)config->window * sizeof(uint32_t));
    return CP_OK;
}

static void fir_reset(void *opaque, uint32_t reason) {
    fir_state_t *state = (fir_state_t *)opaque;
    size_t values = (size_t)state->channels * state->tap_count;
    (void)reason;
    state->head = 0u;
    state->count = 0u;
    state->decimation_count = 0u;
    memset(state->ring, 0, values * sizeof(float));
    memset(state->quality_ring, 0, (size_t)state->tap_count * sizeof(uint32_t));
}

static cp_status_t fir_process(void *opaque,
                               const cp_frame_t *input,
                               cp_emitter_t *emitter) {
    fir_state_t *state = (fir_state_t *)opaque;
    const float *source = (const float *)input->data;
    cp_frame_t output;
    uint16_t channel;
    if (input->dtype != CP_DTYPE_F32 || input->sample_count != 1u ||
        input->channel_count != state->channels) return CP_EINVAL;
    for (channel = 0u; channel < state->channels; ++channel) {
        state->ring[(size_t)state->head * state->channels + channel] = source[channel];
    }
    state->quality_ring[state->head] = input->quality_flags;
    state->head = (uint16_t)((state->head + 1u) % state->tap_count);
    if (state->count < state->tap_count) state->count++;
    state->decimation_count++;
    if (state->decimation_count < state->factor) return CP_OK;
    state->decimation_count = 0u;
    for (channel = 0u; channel < state->channels; ++channel) {
        float sum = 0.0f;
        uint16_t tap;
        for (tap = 0u; tap < state->count; ++tap) {
            uint16_t newest = (uint16_t)((state->head + state->tap_count - 1u - tap) % state->tap_count);
            sum += state->coefficients[tap] *
                   state->ring[(size_t)newest * state->channels + channel];
        }
        state->output[channel] = sum;
    }
    init_output(&output, input, state->output_stream, CP_DTYPE_F32, state->channels, 1u,
                state->output, (size_t)state->channels * sizeof(float));
    output.quality_flags = 0u;
    {
        uint16_t tap;
        for (tap = 0u; tap < state->count; ++tap) {
            uint16_t newest = (uint16_t)((state->head + state->tap_count - 1u - tap) % state->tap_count);
            output.quality_flags |= state->quality_ring[newest];
        }
    }
    output.lineage.input_stream_id = input->stream_id;
    output.lineage.first_seq = input->seq - (uint32_t)(state->count - 1u);
    output.lineage.last_seq = input->seq;
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t FIR_OPS = {fir_process, fir_reset};

cp_status_t cp_stage_fir_decimate_init(cp_stage_t *stage,
                                       uint32_t id,
                                       uint32_t input_stream,
                                       uint32_t output_stream,
                                       const cp_fir_decimate_config_t *config,
                                       cp_arena_t *arena) {
    fir_state_t *state;
    size_t ring_bytes;
    size_t row_bytes;
    if (stage == NULL || config == NULL || arena == NULL || config->channels == 0u ||
        config->factor < 2u || config->tap_count == 0u || config->coefficients == NULL) {
        return CP_EINVAL;
    }
    row_bytes = (size_t)config->channels * sizeof(float);
    if (!cp_checked_mul_size(row_bytes, config->tap_count, &ring_bytes)) return CP_EBOUNDS;
    state = (fir_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    memset(state, 0, sizeof(*state));
    state->ring = (float *)cp_arena_alloc(arena, ring_bytes, CP_STAGE_ALIGNMENT);
    state->output = (float *)cp_arena_alloc(arena, row_bytes, CP_STAGE_ALIGNMENT);
    state->quality_ring = (uint32_t *)cp_arena_alloc(
        arena, (size_t)config->tap_count * sizeof(uint32_t), sizeof(uint32_t));
    if (state->ring == NULL || state->output == NULL || state->quality_ring == NULL) return CP_ENOMEM;
    memset(state->ring, 0, ring_bytes);
    memset(state->quality_ring, 0, (size_t)config->tap_count * sizeof(uint32_t));
    state->output_stream = output_stream;
    state->channels = config->channels;
    state->factor = config->factor;
    state->tap_count = config->tap_count;
    state->coefficients = config->coefficients;
    stage_assign(stage, id, input_stream, output_stream, &FIR_OPS, state,
                 sizeof(*state) + ring_bytes + row_bytes +
                 (size_t)config->tap_count * sizeof(uint32_t));
    return CP_OK;
}

static void window_reset(void *opaque, uint32_t reason) {
    window_state_t *state = (window_state_t *)opaque;
    size_t values = (size_t)state->channels * state->length;
    (void)reason;
    state->head = 0u;
    state->count = 0u;
    state->since_emit = 0u;
    memset(state->ring, 0, values * sizeof(float));
    memset(state->quality_ring, 0, (size_t)state->length * sizeof(uint32_t));
}

static cp_status_t window_process(void *opaque,
                                  const cp_frame_t *input,
                                  cp_emitter_t *emitter) {
    window_state_t *state = (window_state_t *)opaque;
    const float *source = (const float *)input->data;
    cp_frame_t output;
    uint16_t row;
    if (input->dtype != CP_DTYPE_F32 || input->sample_count != 1u ||
        input->channel_count != state->channels) return CP_EINVAL;
    memcpy(state->ring + (size_t)state->head * state->channels,
           source, (size_t)state->channels * sizeof(float));
    state->quality_ring[state->head] = input->quality_flags;
    state->head = (uint16_t)((state->head + 1u) % state->length);
    if (state->count < state->length) state->count++;
    if (state->count < state->length) return CP_OK;
    if (state->since_emit != 0u && state->since_emit < state->hop) {
        state->since_emit++;
        return CP_OK;
    }
    for (row = 0u; row < state->length; ++row) {
        uint16_t source_row = (uint16_t)((state->head + row) % state->length);
        memcpy(state->output + (size_t)row * state->channels,
               state->ring + (size_t)source_row * state->channels,
               (size_t)state->channels * sizeof(float));
    }
    init_output(&output, input, state->output_stream, CP_DTYPE_F32, state->channels,
                state->length, state->output,
                (size_t)state->length * state->channels * sizeof(float));
    output.quality_flags = 0u;
    for (row = 0u; row < state->length; ++row) output.quality_flags |= state->quality_ring[row];
    output.lineage.input_stream_id = input->stream_id;
    output.lineage.first_seq = input->seq - (uint32_t)(state->length - 1u);
    output.lineage.last_seq = input->seq;
    state->since_emit = 1u;
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t WINDOW_OPS = {window_process, window_reset};

cp_status_t cp_stage_window_init(cp_stage_t *stage,
                                 uint32_t id,
                                 uint32_t input_stream,
                                 uint32_t output_stream,
                                 const cp_window_config_t *config,
                                 cp_arena_t *arena) {
    window_state_t *state;
    size_t bytes;
    if (stage == NULL || config == NULL || arena == NULL || config->channels == 0u ||
        config->length == 0u || config->hop == 0u) return CP_EINVAL;
    if (!cp_checked_mul_size((size_t)config->channels * sizeof(float),
                             config->length, &bytes)) return CP_EBOUNDS;
    state = (window_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    memset(state, 0, sizeof(*state));
    state->ring = (float *)cp_arena_alloc(arena, bytes, CP_STAGE_ALIGNMENT);
    state->output = (float *)cp_arena_alloc(arena, bytes, CP_STAGE_ALIGNMENT);
    state->quality_ring = (uint32_t *)cp_arena_alloc(
        arena, (size_t)config->length * sizeof(uint32_t), sizeof(uint32_t));
    if (state->ring == NULL || state->output == NULL || state->quality_ring == NULL) return CP_ENOMEM;
    memset(state->ring, 0, bytes);
    memset(state->quality_ring, 0, (size_t)config->length * sizeof(uint32_t));
    state->output_stream = output_stream;
    state->channels = config->channels;
    state->length = config->length;
    state->hop = config->hop;
    stage_assign(stage, id, input_stream, output_stream, &WINDOW_OPS, state,
                 sizeof(*state) + 2u * bytes +
                 (size_t)config->length * sizeof(uint32_t));
    return CP_OK;
}

static cp_status_t spectrum_process(void *opaque,
                                    const cp_frame_t *input,
                                    cp_emitter_t *emitter) {
    spectrum_state_t *state = (spectrum_state_t *)opaque;
    const float *source = (const float *)input->data;
    cp_frame_t output;
    uint16_t bin;
    uint16_t channel;
    const double pi = 3.14159265358979323846;
    if (input->dtype != CP_DTYPE_F32 || input->sample_count != state->length ||
        input->channel_count != state->channels) {
        return CP_EINVAL;
    }
    for (bin = 0u; bin < state->bins; ++bin) {
        for (channel = 0u; channel < state->channels; ++channel) {
            double real = 0.0;
            double imaginary = 0.0;
            uint16_t sample;
            for (sample = 0u; sample < state->length; ++sample) {
                double coefficient = 1.0;
                double angle;
                double value;
                if (state->window == CP_SPECTRUM_HANN && state->length > 1u) {
                    coefficient = 0.5 - 0.5 * cos((2.0 * pi * sample) / (state->length - 1u));
                }
                value = source[(size_t)sample * state->channels + channel] * coefficient;
                angle = (2.0 * pi * bin * sample) / state->length;
                real += value * cos(angle);
                imaginary -= value * sin(angle);
            }
            state->output[(size_t)bin * state->channels + channel] =
                (float)(sqrt(real * real + imaginary * imaginary) / state->length);
        }
    }
    init_output(&output, input, state->output_stream, CP_DTYPE_F32, state->channels,
                state->bins, state->output,
                (size_t)state->bins * state->channels * sizeof(float));
    output.lineage = input->lineage;
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t SPECTRUM_OPS = {spectrum_process, NULL};

cp_status_t cp_stage_spectrum_init(cp_stage_t *stage,
                                   uint32_t id,
                                   uint32_t input_stream,
                                   uint32_t output_stream,
                                   const cp_spectrum_config_t *config,
                                   cp_arena_t *arena) {
    spectrum_state_t *state;
    size_t bytes;
    uint16_t bins;
    if (stage == NULL || config == NULL || arena == NULL || config->channels == 0u ||
        config->length < 2u || config->window > CP_SPECTRUM_HANN) {
        return CP_EINVAL;
    }
    bins = (uint16_t)(config->length / 2u + 1u);
    if (!cp_checked_mul_size((size_t)bins * config->channels, sizeof(float), &bytes)) {
        return CP_EBOUNDS;
    }
    state = (spectrum_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    memset(state, 0, sizeof(*state));
    state->output = (float *)cp_arena_alloc(arena, bytes, CP_STAGE_ALIGNMENT);
    if (state->output == NULL) return CP_ENOMEM;
    state->output_stream = output_stream;
    state->channels = config->channels;
    state->length = config->length;
    state->bins = bins;
    state->window = config->window;
    stage_assign(stage, id, input_stream, output_stream, &SPECTRUM_OPS, state,
                 sizeof(*state) + bytes);
    return CP_OK;
}

static cp_status_t peak_frequency_process(void *opaque,
                                          const cp_frame_t *input,
                                          cp_emitter_t *emitter) {
    peak_frequency_state_t *state = (peak_frequency_state_t *)opaque;
    const float *source = (const float *)input->data;
    cp_frame_t output;
    uint16_t channel;
    uint16_t expected_bins = (uint16_t)(state->fft_length / 2u + 1u);
    if (input->dtype != CP_DTYPE_F32 || input->sample_count != expected_bins ||
        input->channel_count != state->channels) {
        return CP_EINVAL;
    }
    for (channel = 0u; channel < state->channels; ++channel) {
        uint16_t best = state->first_bin;
        uint16_t bin;
        float best_value = source[(size_t)best * state->channels + channel];
        for (bin = (uint16_t)(state->first_bin + 1u); bin <= state->last_bin; ++bin) {
            float value = source[(size_t)bin * state->channels + channel];
            if (value > best_value) {
                best = bin;
                best_value = value;
            }
        }
        state->output[channel] = best * state->bin_hz;
    }
    init_output(&output, input, state->output_stream, CP_DTYPE_F32, state->channels, 1u,
                state->output, (size_t)state->channels * sizeof(float));
    output.lineage = input->lineage;
    return cp_emitter_emit(emitter, &output);
}

static const cp_stage_ops_t PEAK_FREQUENCY_OPS = {peak_frequency_process, NULL};

cp_status_t cp_stage_peak_frequency_init(cp_stage_t *stage,
                                         uint32_t id,
                                         uint32_t input_stream,
                                         uint32_t output_stream,
                                         const cp_peak_frequency_config_t *config,
                                         cp_arena_t *arena) {
    peak_frequency_state_t *state;
    uint16_t bins;
    uint16_t first_bin;
    uint16_t last_bin;
    size_t bytes;
    if (stage == NULL || config == NULL || arena == NULL || config->channels == 0u ||
        config->fft_length < 2u || config->sample_rate_hz <= 0.0f ||
        config->min_hz < 0.0f || config->max_hz < config->min_hz ||
        config->max_hz > config->sample_rate_hz * 0.5f) {
        return CP_EINVAL;
    }
    bins = (uint16_t)(config->fft_length / 2u + 1u);
    first_bin = (uint16_t)ceilf(config->min_hz * config->fft_length / config->sample_rate_hz);
    last_bin = (uint16_t)floorf(config->max_hz * config->fft_length / config->sample_rate_hz);
    if (first_bin >= bins || last_bin >= bins || first_bin > last_bin) return CP_EINVAL;
    bytes = (size_t)config->channels * sizeof(float);
    state = (peak_frequency_state_t *)cp_arena_alloc(arena, sizeof(*state), CP_STAGE_ALIGNMENT);
    if (state == NULL) return CP_ENOMEM;
    memset(state, 0, sizeof(*state));
    state->output = (float *)cp_arena_alloc(arena, bytes, CP_STAGE_ALIGNMENT);
    if (state->output == NULL) return CP_ENOMEM;
    state->output_stream = output_stream;
    state->channels = config->channels;
    state->fft_length = config->fft_length;
    state->first_bin = first_bin;
    state->last_bin = last_bin;
    state->bin_hz = config->sample_rate_hz / config->fft_length;
    stage_assign(stage, id, input_stream, output_stream, &PEAK_FREQUENCY_OPS, state,
                 sizeof(*state) + bytes);
    return CP_OK;
}
