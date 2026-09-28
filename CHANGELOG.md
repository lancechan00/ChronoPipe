# Changelog

All notable changes will be documented in this file.

The format follows Keep a Changelog and the project uses Semantic Versioning.

## [Unreleased]

### Added

- CPB2, a new major block format. Hot fixed-rate DATA uses a 17-byte card.
  SCHEMA holds the period and time origin. Time breaks and sequence gaps are
  EVENT records. Shapes outside the compact profile use self-describing wide DATA.
- Host inspect, export, replay, and capture read CPB2 and reconstruct sample time
  from SCHEMA plus `CP_EVENT_TIME_MAPPING`. CPB1 files still read and write as before.

## [0.1.0] - 2026-08-07

### Added

- Portable static C99 stream graph and bounded arena allocation.
- Conversion, quantization, decimation, moving mean, window, reference spectrum,
  and peak-frequency stages.
- CPB1 with per-record CRC32C, lineage, Python reader/writer, and recovery rules.
- Per-stream aggregation recorders, RAM and recoverable fixed-slot flash rings.
- COBS live framing and acknowledged retained-block transfer.
- Host capture, inspect, CSV export, replay, and tolerant comparison commands.
- Fault-injection, protocol, graph, and one-hour 200 Hz × 8-channel stress tests.
