#ifndef CHRONOPIPE_GRAPH_H
#define CHRONOPIPE_GRAPH_H

#include "chronopipe/types.h"

#ifdef __cplusplus
extern "C" {
#endif
struct cp_emitter;

typedef cp_status_t (*cp_stage_process_fn)(void *state,
                                           const cp_frame_t *input,
                                           struct cp_emitter *emitter);
typedef void (*cp_stage_reset_fn)(void *state, uint32_t reason);

typedef struct {
    cp_stage_process_fn process;
    cp_stage_reset_fn reset;
} cp_stage_ops_t;

typedef struct {
    uint32_t id;
    uint32_t input_stream_id;
    uint32_t output_stream_id;
    const cp_stage_ops_t *ops;
    void *state;
    void *scratch;
    size_t state_bytes;
    size_t scratch_bytes;
    size_t alignment;
} cp_stage_t;

typedef struct {
    size_t state_bytes;
    size_t scratch_bytes;
    size_t max_alignment;
} cp_memory_plan_t;

typedef void (*cp_frame_sink_fn)(void *user, const cp_frame_t *frame);
typedef void (*cp_event_sink_fn)(void *user, const cp_event_t *event);

typedef struct cp_runtime cp_runtime_t;

typedef struct cp_emitter {
    cp_runtime_t *runtime;
    const cp_frame_t *parent;
    uint32_t depth;
} cp_emitter_t;

struct cp_runtime {
    const cp_stream_desc_t *streams;
    size_t stream_count;
    cp_stage_t *stages;
    size_t stage_count;
    uint32_t next_seq[CP_MAX_STREAMS];
    uint32_t expected_source_seq[CP_MAX_STREAMS];
    uint8_t source_seen[CP_MAX_STREAMS];
    cp_frame_sink_fn frame_sink;
    cp_event_sink_fn event_sink;
    void *sink_user;
    uint32_t dispatch_depth;
};

cp_status_t cp_plan_memory(const cp_stage_t *stages,
                           size_t stage_count,
                           cp_memory_plan_t *out);

cp_status_t cp_runtime_init(cp_runtime_t *runtime,
                            const cp_stream_desc_t *streams,
                            size_t stream_count,
                            cp_stage_t *stages,
                            size_t stage_count,
                            cp_frame_sink_fn frame_sink,
                            cp_event_sink_fn event_sink,
                            void *sink_user);

cp_status_t cp_runtime_validate(const cp_runtime_t *runtime);
cp_status_t cp_runtime_push(cp_runtime_t *runtime, const cp_frame_t *frame);
cp_status_t cp_emitter_emit(cp_emitter_t *emitter, cp_frame_t *frame);
void cp_runtime_event(cp_runtime_t *runtime, const cp_event_t *event);
void cp_runtime_reset(cp_runtime_t *runtime, uint32_t reason);
const cp_stream_desc_t *cp_runtime_stream(const cp_runtime_t *runtime, uint32_t stream_id);

#ifdef __cplusplus
}
#endif

#endif
