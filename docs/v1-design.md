# ChronoPipe V1 Design

Status: implementation target
Audience: embedded, algorithm, tooling, and maintainers

## Product definition

ChronoPipe is the bounded data plane between sensor acquisition and algorithm evidence.
It standardizes streams, time, types, transforms, derivation, recording, transport,
and replay without owning sensor drivers, DSP kernels, product algorithms, or cloud
infrastructure.

V1 targets one device and one monotonic clock domain. Typical sources contain 1–8
numeric channels sampled at 50–200 Hz. A graph may derive lower-rate filtered streams,
fixed windows, high-dimensional spectra, scalar features, decisions, and diagnostic
events.

## Reference pipeline

```text
8 × f32 @ 100 Hz
  ├─ quantize(per-channel scale/offset) ── 8 × i16 @ 100 Hz ── recorder
  └─ FIR decimate ×4 ── 8 × f32 @ 25 Hz
       ├─ moving mean ── filtered features
       └─ window(length=512, hop=102)
            └─ spectrum stage or optimized FFT adapter ── 257 × f32 spectrum @ event-driven
                 └─ user detector ── value + confidence
```

At 25 Hz, a 512-sample window covers 20.48 seconds and hop 102 produces a new
window about every 4.08 seconds. The spectrum and scalar result are normal streams
with their own schema, sequence, event time, emit time, and parent range.

## Core invariants

1. Sequence is ordering truth; monotonic time is timing truth; wall time is metadata.
2. A stream has stable shape, dtype, unit semantics, period, and producer identity.
3. A data block contains one stream and one schema only.
4. Fixed-rate blocks store one time origin and period, not a timestamp per sample.
5. Every derived result identifies the input sequence range that produced it.
6. Every loss, reset, saturation, discontinuity, and recovery is observable.
7. The real-time path performs no heap allocation, blocking I/O, flash erase, or network operation.
8. Algorithms publish through an emitter and never call storage or transport directly.
9. Equivalent device and host replay use the same portable C algorithm core.
10. Wire data is explicitly little-endian; in-memory C structs are never a wire contract.

## Public model

### Session

A boot or explicit restart creates a session. Its metadata contains a numeric session
ID, device-provided public label, firmware/build ID, contract fingerprint, graph hash,
monotonic start, optional wall-clock mapping, and the stream/schema directory.

### Stream

A stream descriptor defines:

- numeric ID and stable name;
- dtype, channel count, optional logical shape;
- fixed rational period or event-driven timing;
- channel names, units, physical ranges, scale, and offset;
- producer stage ID, version, and configuration hash;
- optional direct parent stream.

V1 uses one dtype per stream. Mixed types are represented as separate streams.

### Frame

A frame is one complete sample row. It carries stream ID, stream-local sequence,
event time, emit time, quality flags, dtype, channel count, and a borrowed data view.

Event time describes the represented phenomenon. Emit time describes when the result
became available. For a window result, event time is the window end by default and
lineage records the full input range.

### Event

Events are sparse facts: sequence gap, late frame, non-finite input, saturation,
buffer overflow, recorder stop, algorithm reset, configuration change, transport
failure, store recovery, or time mapping update.

### Block

A block is the atomic recording and batch-transfer unit. It has a checked CPB1 header,
contiguous payload, parent range, and CRC. Blocks are sealed before leaving the
real-time plane.

## Time and discontinuity

For a fixed-rate block:

```text
t(i) = t0_us + i * period_num_us / period_den
```

Conversion preserves event time. Causal filters use the triggering input time.
Decimators use the configured output phase time. Windows use the last included input
time and retain first/last parent sequence. A stage encountering a gap must choose
reset, mark-and-continue, hold, or fail; reset is the default and hold must be explicit.

## Types and quantization

V1 supports `f32`, `i16`, `u16`, `i32`, and `u32`. Each channel may use distinct
physical metadata while sharing the stream dtype.

```text
physical = stored * scale + offset
stored = round_half_away_from_zero((physical - offset) / scale)
```

Quantization checks NaN/Inf, applies deterministic rounding, clamps before casting,
sets saturation flags, and increments diagnostics. Stage order, coefficients, scale,
offset, rounding, overflow behavior, and dtype are part of the graph hash.

## Execution architecture

V1 builds a static acyclic plan at initialization. A source push synchronously walks
matching stages in deterministic registration order. Stage outputs are dispatched
through the same runtime without recursion beyond a validated depth limit.

Memory comes from caller-owned arenas. Every stage reports persistent state, scratch,
and alignment for audit, while the target build exposes exact arena use after graph
construction. Per-stream caller-owned recorder buffers connect the data plane to stores;
applications that require asynchronous flash or network work place sealed buffers into
their own fixed queue.

Built-in stages:

| Stage | Purpose |
|---|---|
| Select | channel selection and reorder |
| Affine | per-channel gain and bias |
| Quantize | deterministic numeric conversion |
| FIR Decimate | anti-alias filtering and integer-factor decimation |
| Mean Decimate | explicit block averaging |
| Pick Decimate | explicit phase selection; never the analog default |
| Moving Mean | rolling mean with bounded state |
| Window | fixed length and hop aggregation |
| Spectrum | portable reference DFT magnitude for validation and small workloads |
| Peak Frequency | bounded-band scalar feature from a spectrum |
| Algorithm | user callback with declared state/scratch |
| Tap | select streams for evidence sinks |

The portable spectrum stage favors a dependency-free reference result over speed.
Production targets should adapt CMSIS-DSP, ESP-DSP, or another optimized kernel behind
the same stream contract. ChronoPipe owns scheduling and evidence semantics; it does
not attempt to out-optimize dedicated DSP libraries.

## Recording and storage

CPB1 is the device binary format. Debug JSON uses the same logical model but is not
used on high-frequency device paths.

The default RAM store uses 4096-byte blocks and a configurable fixed count. At 200 Hz,
8 × i16 consumes about 3.2 kB/s before small block overhead; a 32 KiB ring retains
about ten seconds. At 200 Hz, 8 × f32 consumes 6.4 kB/s and retains about five seconds.

Store backends expose append, read, oldest, newest, and acknowledge operations.
V1 includes a RAM ring, host file store, and raw-flash fixed-slot ring. Flash commits
data before a final marker, validates headers and payload CRCs at boot, skips partial
slots, and rotates erase sectors. Overflow policy is explicit: drop oldest, drop
newest, or stop recording. Blocking is forbidden on the device real-time path.

## Transport

Live mode prioritizes latency: select streams and output rate, frame serial packets,
and permit detectable loss without retransmission. Batch mode transfers sealed blocks,
accepts `ack_through`/`next_expected`, resumes from the oldest unacknowledged block,
and records any permanent gap caused by storage overwrite.

UART/USB/TCP/HTTP/MQTT/BLE are adapters. The core owns sequencing, retry state,
backpressure, and acknowledgement semantics but contains no socket or credential code.
CRC detects corruption; production authentication and confidentiality belong to the
transport adapter.

## Host replay

The host CLI captures framed CPB1, safely stops at incomplete append-only tails,
inspects sessions, exports CSV, replays records, and compares device output with a
golden or host-produced session. The portable C graph can also be linked into a host
test executable when algorithm re-execution is required.
Integer/state outputs are strict by default. Floating fields use per-field absolute
and relative tolerances. Reports identify the first differing stream/sequence, parent
range, graph hash, and nearby diagnostics.

## Scope

V1 includes one clock domain, fixed and event-driven streams, static graphs, integer
factor decimation, fixed windows, bounded stores, live/batch transport contracts, and
host replay. It excludes hard real-time control scheduling, multiple-device clock
sync, dynamic plugin loading, arbitrary graph mutation, cloud fleet management, GUI,
compression, encryption, and generic rational resampling.

## Release evidence

- One-hour 200 Hz × 8 f32 run without unexplained gaps.
- Zero heap allocations after runtime initialization.
- Portable core overhead target below 5% average CPU on the reference MCU, excluding user algorithms.
- Interrupted flash writes recover all previously committed blocks.
- Disconnect/reconnect resumes batch upload from the expected block.
- Integer device/host paths are bit-exact; floating paths pass declared tolerance.
- An engineer unfamiliar with the implementation completes the reference example in one day.

Architecture diagrams: [context](architecture/c4-context.md),
[containers](architecture/c4-containers.md), and
[device components](architecture/c4-components-device-runtime.md).
