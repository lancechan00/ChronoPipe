# Integration guide

## 1. Define streams before runtime start

Give every raw and derived stream a unique numeric ID and schema ID. Keep one dtype and
channel count per stream. Use a rational microsecond period so rates such as 44.1 kHz
do not require floating timestamps. Split mixed-rate or mixed-type values into separate
streams.

## 2. Build a static graph in caller-owned memory

Initialize a `cp_arena_t`, construct stages in registration order, then call
`cp_runtime_init`. Constructors copy small configuration arrays except FIR coefficients,
which must remain valid for the stage lifetime. No runtime path calls `malloc`.

The dependency-free spectrum stage is a correctness reference with quadratic cost.
Replace it on production MCUs with an optimized application stage while preserving the
input/output stream descriptors and lineage.

Custom algorithms implement `cp_stage_ops_t`:

```c
static cp_status_t process(void *state,
                           const cp_frame_t *input,
                           cp_emitter_t *emitter) {
    cp_frame_t output = {0};
    /* Validate input, run the bounded algorithm, fill a borrowed output view. */
    output.stream_id = 20;
    output.seq = CP_SEQ_AUTO;
    output.event_time_us = input->event_time_us;
    output.dtype = CP_DTYPE_F32;
    output.channel_count = 1;
    output.sample_count = 1;
    output.data = state;
    output.data_bytes = sizeof(float);
    return cp_emitter_emit(emitter, &output);
}
```

Populate `cp_stage_t` with a stable ID, one input and output stream, state pointer,
state/scratch byte counts, alignment, and process/reset operations. A stage never writes
flash or calls a transport.

## 3. Push acquisition rows

Drivers produce a complete `cp_frame_t` with source sequence, monotonic event time,
emit time, dtype, shape, period, quality, and borrowed data. The runtime walks the graph
synchronously. A sequence gap emits an event, resets reachable stateful stages, and
marks the first post-gap path discontinuous.

Do not call a long graph directly in a highest-priority ISR. Fill a preallocated queue
or slot and run the graph in a bounded application task.

## 4. Tap streams into recorders

Create one `cp_recorder_t` for each evidence stream. The recorder batches scalar rows
up to `samples_per_block` and seals CPB2. Hot fixed-rate blocks use the 17-byte card.
Set `time_origin_us` to the SCHEMA origin; sample time is
`origin + seq * period_num_us / period_den`. A break flushes the open block, appends
`CP_EVENT_TIME_MAPPING`, and marks the next block discontinuous. Shapes outside the
compact profile, including spectra, are wide records that carry `t0_us`, period, and
the direct parent range. `cp_recorder_flush` seals a partial final block.

Recorders write through `cp_store_t`. Use the RAM store when data may be overwritten
after a short retention window. Use the flash store when committed blocks must survive
restart. Flash slot size and offset must satisfy the adapter's program and erase
alignment. The adapter owns any background erase scheduling.

## 5. Transfer without hiding loss

Use COBS for a simple live byte stream. Live mode is allowed to lose frames, but block
and source sequences make loss detectable. Use `cp_batch_sender_t` for retained
evidence: it sends the oldest block, waits for `ack_through`, retries within policy, and
only then releases acknowledged storage.

Authentication, encryption, socket ownership, credentials, and remote authorization
belong to the platform transport. Never put secrets into SESSION or SCHEMA metadata.

## 6. Capture and compare on a host

```sh
python -m pip install -e .
chronopipe capture framed.bin session.cpb
chronopipe inspect session.cpb --json
chronopipe export session.cpb stream.csv --stream 1
chronopipe replay session.cpb replay.bin --realtime --speed 4
chronopipe compare golden.cpb replayed.cpb --atol 1e-6 --rtol 1e-5
```

`capture` validates each decoded record before appending it. File scans stop at an
incomplete tail and report its size; they never search for magic bytes inside payloads.

## Buffer sizing checklist

- Stage arena: use the target build's final `cp_arena_t.used` plus deliberate margin.
- Recorder buffer: `88 + samples_per_block * channels * dtype_bytes`.
- RAM store: `slot_count * slot_bytes`; metadata is bounded by `CP_STORE_MAX_SLOTS`.
- Flash read buffer: at least `slot_bytes - 24` with the current fixed-slot wrapper.
- COBS output: at least `input_bytes + input_bytes / 254 + 1`.
- Retention seconds: approximately `usable_payload_bytes / encoded_bytes_per_second`.

The one-hour stress executable reports the constructed arena use for the reference
path, making changes to static memory visible in CI.
