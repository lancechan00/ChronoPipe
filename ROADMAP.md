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

## Later releases

- Rational resampling and multiple clock domains.
- Multi-parent lineage for sensor fusion graphs.
- Planar payloads and optional LZ4/Zstd compression.
- Triggered pre/post incident bundles.
- Additional RTOS, storage, and transport ports.
- Optional MCAP and Parquet export plugins.
