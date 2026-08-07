#include "chronopipe/graph.h"

#include <string.h>

static int stream_index(const cp_runtime_t *runtime, uint32_t id) {
    size_t i;
    if (runtime == NULL) return -1;
    for (i = 0u; i < runtime->stream_count; ++i) {
        if (runtime->streams[i].id == id) return (int)i;
    }
    return -1;
}

const cp_stream_desc_t *cp_runtime_stream(const cp_runtime_t *runtime, uint32_t stream_id) {
    int index = stream_index(runtime, stream_id);
    return index >= 0 ? &runtime->streams[(size_t)index] : NULL;
}

static size_t align_up(size_t value, size_t alignment, bool *ok) {
    size_t mask;
    if (ok == NULL || alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        if (ok != NULL) *ok = false;
        return 0u;
    }
    mask = alignment - 1u;
    if (value > SIZE_MAX - mask) {
        *ok = false;
        return 0u;
    }
    return (value + mask) & ~mask;
}

cp_status_t cp_plan_memory(const cp_stage_t *stages,
                           size_t stage_count,
                           cp_memory_plan_t *out) {
    size_t i;
    size_t state = 0u;
    size_t scratch = 0u;
    size_t max_alignment = 1u;
    bool ok = true;
    if (out == NULL || (stage_count > 0u && stages == NULL) || stage_count > CP_MAX_STAGES) {
        return CP_EINVAL;
    }
    for (i = 0u; i < stage_count; ++i) {
        size_t alignment = stages[i].alignment == 0u ? 1u : stages[i].alignment;
        state = align_up(state, alignment, &ok);
        if (!ok || stages[i].state_bytes > SIZE_MAX - state) return CP_EBOUNDS;
        state += stages[i].state_bytes;
        if (stages[i].scratch_bytes > scratch) scratch = stages[i].scratch_bytes;
        if (alignment > max_alignment) max_alignment = alignment;
    }
    out->state_bytes = state;
    out->scratch_bytes = scratch;
    out->max_alignment = max_alignment;
    return CP_OK;
}

static cp_status_t visit_stream(const cp_runtime_t *runtime,
                                size_t index,
                                uint8_t *color,
                                uint32_t depth) {
    size_t i;
    if (depth > CP_MAX_GRAPH_DEPTH) return CP_EBOUNDS;
    if (color[index] == 1u) return CP_ECYCLE;
    if (color[index] == 2u) return CP_OK;
    color[index] = 1u;
    for (i = 0u; i < runtime->stage_count; ++i) {
        int output_index;
        if (runtime->stages[i].input_stream_id != runtime->streams[index].id) continue;
        output_index = stream_index(runtime, runtime->stages[i].output_stream_id);
        if (output_index < 0) return CP_ENOTFOUND;
        {
            cp_status_t status = visit_stream(runtime, (size_t)output_index, color, depth + 1u);
            if (status != CP_OK) return status;
        }
    }
    color[index] = 2u;
    return CP_OK;
}

cp_status_t cp_runtime_validate(const cp_runtime_t *runtime) {
    uint8_t color[CP_MAX_STREAMS];
    size_t i;
    size_t j;
    if (runtime == NULL || runtime->streams == NULL || runtime->stream_count == 0u ||
        runtime->stream_count > CP_MAX_STREAMS || runtime->stage_count > CP_MAX_STAGES ||
        (runtime->stage_count > 0u && runtime->stages == NULL)) {
        return CP_EINVAL;
    }
    for (i = 0u; i < runtime->stream_count; ++i) {
        const cp_stream_desc_t *stream = &runtime->streams[i];
        if (stream->id == CP_STREAM_SOURCE || stream->name == NULL ||
            cp_dtype_size(stream->dtype) == 0u || stream->channel_count == 0u ||
            stream->period_den == 0u) {
            return CP_EINVAL;
        }
        for (j = i + 1u; j < runtime->stream_count; ++j) {
            if (runtime->streams[j].id == stream->id ||
                runtime->streams[j].schema_id == stream->schema_id) {
                return CP_EINVAL;
            }
        }
    }
    for (i = 0u; i < runtime->stage_count; ++i) {
        const cp_stage_t *stage = &runtime->stages[i];
        if (stage->ops == NULL || stage->ops->process == NULL ||
            stream_index(runtime, stage->input_stream_id) < 0 ||
            stream_index(runtime, stage->output_stream_id) < 0 ||
            stage->input_stream_id == stage->output_stream_id) {
            return CP_EINVAL;
        }
        for (j = i + 1u; j < runtime->stage_count; ++j) {
            if (runtime->stages[j].id == stage->id) return CP_EINVAL;
        }
    }
    memset(color, 0, sizeof(color));
    for (i = 0u; i < runtime->stream_count; ++i) {
        cp_status_t status = visit_stream(runtime, i, color, 0u);
        if (status != CP_OK) return status;
    }
    return CP_OK;
}

cp_status_t cp_runtime_init(cp_runtime_t *runtime,
                            const cp_stream_desc_t *streams,
                            size_t stream_count,
                            cp_stage_t *stages,
                            size_t stage_count,
                            cp_frame_sink_fn frame_sink,
                            cp_event_sink_fn event_sink,
                            void *sink_user) {
    cp_status_t status;
    if (runtime == NULL) return CP_EINVAL;
    memset(runtime, 0, sizeof(*runtime));
    runtime->streams = streams;
    runtime->stream_count = stream_count;
    runtime->stages = stages;
    runtime->stage_count = stage_count;
    runtime->frame_sink = frame_sink;
    runtime->event_sink = event_sink;
    runtime->sink_user = sink_user;
    status = cp_runtime_validate(runtime);
    if (status != CP_OK) memset(runtime, 0, sizeof(*runtime));
    return status;
}

void cp_runtime_event(cp_runtime_t *runtime, const cp_event_t *event) {
    if (runtime != NULL && event != NULL && runtime->event_sink != NULL) {
        runtime->event_sink(runtime->sink_user, event);
    }
}

static cp_status_t validate_frame(const cp_runtime_t *runtime, const cp_frame_t *frame) {
    const cp_stream_desc_t *stream;
    size_t expected;
    if (runtime == NULL || frame == NULL || frame->data == NULL) return CP_EINVAL;
    stream = cp_runtime_stream(runtime, frame->stream_id);
    if (stream == NULL || stream->dtype != frame->dtype ||
        stream->channel_count != frame->channel_count || frame->period_den == 0u ||
        !cp_frame_shape_bytes(frame, &expected) || expected != frame->data_bytes) {
        return CP_EINVAL;
    }
    return CP_OK;
}

static cp_status_t dispatch(cp_runtime_t *runtime, const cp_frame_t *frame, uint32_t depth) {
    size_t i;
    if (depth > CP_MAX_GRAPH_DEPTH) return CP_EBOUNDS;
    if (runtime->frame_sink != NULL) runtime->frame_sink(runtime->sink_user, frame);
    for (i = 0u; i < runtime->stage_count; ++i) {
        cp_stage_t *stage = &runtime->stages[i];
        if (stage->input_stream_id == frame->stream_id) {
            cp_emitter_t emitter;
            cp_status_t status;
            emitter.runtime = runtime;
            emitter.parent = frame;
            emitter.depth = depth + 1u;
            status = stage->ops->process(stage->state, frame, &emitter);
            if (status != CP_OK) return status;
        }
    }
    return CP_OK;
}

static void reset_downstream(cp_runtime_t *runtime,
                             uint32_t stream_id,
                             uint32_t reason,
                             uint8_t *stage_reset,
                             uint32_t depth) {
    size_t i;
    if (depth > CP_MAX_GRAPH_DEPTH) return;
    for (i = 0u; i < runtime->stage_count; ++i) {
        cp_stage_t *stage = &runtime->stages[i];
        if (stage->input_stream_id == stream_id && stage_reset[i] == 0u) {
            stage_reset[i] = 1u;
            if (stage->ops != NULL && stage->ops->reset != NULL) {
                stage->ops->reset(stage->state, reason);
            }
            reset_downstream(runtime, stage->output_stream_id, reason, stage_reset, depth + 1u);
        }
    }
}

cp_status_t cp_runtime_push(cp_runtime_t *runtime, const cp_frame_t *frame) {
    int index;
    bool discontinuity = false;
    cp_status_t status = validate_frame(runtime, frame);
    if (status != CP_OK) return status;
    index = stream_index(runtime, frame->stream_id);
    if (index < 0) return CP_ENOTFOUND;
    if (runtime->source_seen[(size_t)index] != 0u &&
        frame->seq != runtime->expected_source_seq[(size_t)index]) {
        cp_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = CP_EVENT_SEQUENCE_GAP;
        event.stream_id = frame->stream_id;
        event.seq = frame->seq;
        event.event_time_us = frame->event_time_us;
        event.detail = (int32_t)(frame->seq - runtime->expected_source_seq[(size_t)index]);
        cp_runtime_event(runtime, &event);
        {
            uint8_t stage_reset[CP_MAX_STAGES];
            memset(stage_reset, 0, sizeof(stage_reset));
            reset_downstream(runtime, frame->stream_id, CP_EVENT_SEQUENCE_GAP, stage_reset, 0u);
        }
        discontinuity = true;
    }
    runtime->source_seen[(size_t)index] = 1u;
    runtime->expected_source_seq[(size_t)index] = frame->seq + 1u;
    if (discontinuity) {
        cp_frame_t marked = *frame;
        marked.quality_flags |= CP_Q_DISCONTINUITY;
        return dispatch(runtime, &marked, 0u);
    }
    return dispatch(runtime, frame, 0u);
}

cp_status_t cp_emitter_emit(cp_emitter_t *emitter, cp_frame_t *frame) {
    int index;
    const cp_stream_desc_t *stream;
    cp_status_t status;
    if (emitter == NULL || emitter->runtime == NULL || emitter->parent == NULL || frame == NULL) {
        return CP_EINVAL;
    }
    index = stream_index(emitter->runtime, frame->stream_id);
    if (index < 0) return CP_ENOTFOUND;
    stream = &emitter->runtime->streams[(size_t)index];
    if (frame->seq == CP_SEQ_AUTO) frame->seq = emitter->runtime->next_seq[(size_t)index]++;
    if (frame->period_den == 0u) {
        frame->period_num_us = stream->period_num_us;
        frame->period_den = stream->period_den;
    }
    if (frame->dtype == 0) frame->dtype = stream->dtype;
    if (frame->lineage.input_stream_id == 0u) {
        frame->lineage.input_stream_id = emitter->parent->stream_id;
        frame->lineage.first_seq = emitter->parent->seq;
        frame->lineage.last_seq = emitter->parent->seq;
    }
    frame->quality_flags |= emitter->parent->quality_flags;
    status = validate_frame(emitter->runtime, frame);
    if (status != CP_OK) return status;
    return dispatch(emitter->runtime, frame, emitter->depth);
}

void cp_runtime_reset(cp_runtime_t *runtime, uint32_t reason) {
    size_t i;
    if (runtime == NULL) return;
    memset(runtime->next_seq, 0, sizeof(runtime->next_seq));
    memset(runtime->expected_source_seq, 0, sizeof(runtime->expected_source_seq));
    memset(runtime->source_seen, 0, sizeof(runtime->source_seen));
    for (i = 0u; i < runtime->stage_count; ++i) {
        if (runtime->stages[i].ops != NULL && runtime->stages[i].ops->reset != NULL) {
            runtime->stages[i].ops->reset(runtime->stages[i].state, reason);
        }
    }
}
