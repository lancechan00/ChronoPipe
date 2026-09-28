# Roadmap

## V1 release gates

- [x] Public Stream, Frame, Event, lineage, and CPB1 contracts are frozen for 0.1.x.
- [x] Portable C runtime executes a validated static graph with bounded memory.
- [x] Built-in conversion, quantization, decimation, moving mean, window, and spectrum paths pass tests.
- [x] RAM and raw-flash circular stores recover from overflow and interrupted writes.
- [ ] Live framing and resumable batch transfer expose every permanent gap.
- [x] Host CLI captures, inspects, exports, replays, and compares CPB1 sessions.
- [x] A 200 Hz × 8 channel reference pipeline simulates one hour without unexplained gaps.
- [ ] Device and host execute the same C algorithm core for integer bit-exact replay.
- [x] Documentation, security guidance, CI, and contribution workflows are complete.

## Compact DATA header track

V1 CPB1 keeps the 88-byte DATA header for 0.1.x. Host experiments under
`experiments/header_budget/` compared field packing without dropping the card's
core fields. The recommended candidate is **bit-align-17**:

- Card contents: kind, stream, dtype, layout, channels, six quality bits,
  sample count, block sequence, sample sequence, and one CRC32C.
- Layout: 22 packed bits in the first three bytes; `sample_count`, sequences,
  and CRC remain on byte boundaries and are read byte-wise.
- At 50 Hz × 2 × f32 with 50 samples per block: 417 B/s versus 400 B raw
  (1.04×). The current 88-byte header is 1.22× under the same packing.
- Fixed 4096-byte store slots remain a separate tax; a tight 17-byte card still
  wastes the slot if the ring keeps oversized empty pages.

Stable domain measured by `sweep17.c`:

| Field | Stable range | Cliff |
|---|---|---|
| channels | 1–16 | 17 or 0 decode as 1 |
| stream | 0–31 | 32 wraps to 0 |
| quality | existing 6 flags (0–63) | bit 6 and above are dropped |
| samples/block | 1–65535 | 65536 wraps to 0; 0 yields an empty block |
| dtype | the five sized types | codes without a width cannot size the payload |
| sequences | full `u32` | wrap ends the session; at 200 Hz ≈ 249 days |

Expansion is only `1 + 17 / payload_bytes`. Keep payload ≥ 340 B for ≤ 1.05×
(e.g. f32 × 2 needs ≥ 43 samples/block). Worst in-range case is one i16 sample
per block (9.5×). Frequency is not encoded in the card; it only changes bytes/s
and `u32` lifetime. Max in-range block is 65535 × 16 × f32 ≈ 4 MiB + 17 B.

### Next gates for this track

- [x] bit-align-17 is CPB2, a new major format. CPB1 stays valid for existing files.
- [x] SESSION/SCHEMA own session, period, and time origin. Compact DATA does not
      repeat them. A broken formula is a `CP_EVENT_TIME_MAPPING` record.
- [x] Callers size RAM/flash slots from `cp_cpb2_compact_record_bytes()`, not from
      a default 4096 B page.
- [x] Out-of-range compact fields return an error instead of being truncated.
- [x] Shared C encode/decode and the host tools cover the compact card, wide
      DATA, and time reconstruction.
- [x] Window frames, spectra, and other shapes outside the compact profile use
      wide DATA. Events stay on their own records.

The compact widths (32 streams, 16 channels, 65535 samples, six quality bits)
are the default profile. Streams that need more use wide DATA in the same CPB2
file. Raising `CP_MAX_STREAMS` does not widen the 17-byte card.

## Later releases

- Rational resampling and multiple clock domains.
- Multi-parent lineage for sensor fusion graphs.
- Planar payloads and optional LZ4/Zstd compression.
- Triggered pre/post incident bundles.
- Additional RTOS, storage, and transport ports.
- Optional MCAP and Parquet export plugins.
