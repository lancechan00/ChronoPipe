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

- [ ] Decide whether bit-align-17 becomes a CPB2 minor packing or a major format.
- [ ] Keep SESSION/SCHEMA owning session, period, and shape so those fields stay
      out of every DATA card.
- [ ] Size RAM/flash slots to the sealed record, not a default 4096 B page.
- [ ] Reject out-of-range fields instead of silent truncation before any wire use.
- [ ] Port the packing into shared C encode/decode with host and device tests.
- [ ] Keep window/spectrum/event records on self-describing headers; only hot
      fixed-rate streams use the compact card.

## Later releases

- Compact DATA header (bit-align-17 or successor) after the gates above.
- Rational resampling and multiple clock domains.
- Multi-parent lineage for sensor fusion graphs.
- Planar payloads and optional LZ4/Zstd compression.
- Triggered pre/post incident bundles.
- Additional RTOS, storage, and transport ports.
- Optional MCAP and Parquet export plugins.
