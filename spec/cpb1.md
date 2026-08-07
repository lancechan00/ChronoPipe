# CPB1 Binary Format

This document is normative for ChronoPipe Block Format major version 1.

## Encoding rules

- Byte order is little-endian.
- Integers use exact widths; IEEE-754 binary32 represents `f32`.
- Readers must reject lengths or shape products that overflow `size_t` or exceed input.
- CRC32C uses polynomial `0x1EDC6F41` in its reflected Castagnoli form.
- Records are length-delimited and unknown record kinds may be skipped.
- A C struct layout is never the wire layout; encoders write each field explicitly.
- Stream and block sequences do not wrap within a session; start a new session before
  a `u32` sequence would overflow.

## File header

The 16-byte header is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `CPB1` |
| 4 | 2 | major = 1 |
| 6 | 2 | minor |
| 8 | 4 | flags, zero in V1 |
| 12 | 4 | header CRC32C over bytes 0..11 |

It is followed by records. Every record begins with `kind:u16`, `flags:u16`, and
`record_bytes:u32`; the length includes the 8-byte prefix. A record ending beyond the
available bytes is an incomplete tail, not a valid record.

Every non-DATA record ends with a four-byte CRC32C over its payload content, excluding
the common prefix and the CRC field itself. DATA uses its fixed header and payload CRCs
described below. Thus every complete CPB1 record has independent integrity protection.

## Record kinds

| Kind | Value | Purpose |
|---|---:|---|
| SESSION | 1 | session/build/graph identity |
| SCHEMA | 2 | stream shape, channels, units and quantization |
| DATA | 3 | fixed-shape sample block |
| EVENT | 4 | sparse diagnostics or decisions |
| SESSION_END | 5 | optional clean close |
| INDEX | 6 | optional host-generated random-access index |

SESSION and SCHEMA content is UTF-8 JSON followed by the common payload CRC. SESSION
identifies the build, graph, and contract fingerprint. SCHEMA defines one stream's
name, dtype, shape, rational period, channels, units, physical ranges, quantization,
producer, and configuration hash. Implementations may preserve unknown JSON keys.

EVENT content before its CRC is the following fixed 36-byte structure:

| Field | Size |
|---|---:|
| session_id | 8 |
| event_type | 2 |
| flags | 2 |
| stream_id | 4 |
| sequence | 4 |
| event_time_us | 8 |
| detail_i32 | 4 |
| value_f32 | 4 |

SESSION_END content before its CRC contains `session_id:u64`, `end_time_us:u64`,
`reason:u32`, and `last_block_seq:u32`. INDEX is host-generated and its content is
versioned by the first `u16`; readers may skip it using `record_bytes`.

## DATA record

After the common prefix, DATA uses this fixed 80-byte header:

| Field | Size |
|---|---:|
| session_id | 8 |
| stream_id | 4 |
| schema_id | 4 |
| block_seq | 4 |
| first_sample_seq | 4 |
| t0_us | 8 |
| period_num_us | 4 |
| period_den | 4 |
| sample_count | 2 |
| channel_count | 2 |
| dtype | 1 |
| layout | 1 |
| quality_flags | 2 |
| payload_bytes | 4 |
| payload_crc32c | 4 |
| input_stream_id | 4 |
| input_first_seq | 4 |
| input_last_seq | 4 |
| header_crc32c | 4 |
| reserved | 8 |

`header_crc32c` covers the common record prefix and DATA header with the CRC field
treated as zero. `period_den` must not be zero. Event-driven streams set period numerator
to zero and use one sample per DATA record or an EVENT record.

V1 layout 0 is sample-major: all channels for sample 0, then all channels for sample 1.
The expected payload size is `sample_count * channel_count * dtype_size`. A mismatch is
invalid even if CRC passes.

## Append and recovery

Files are append-only. A reader stops at an incomplete record tail and reports recovery;
it must not scan arbitrary payload bytes for a magic value. Raw-flash backends wrap each
record in a fixed slot and program a backend-specific commit marker after the complete
record. The commit marker is not part of CPB1 files or network messages.

## Compatibility

Minor versions may add record kinds, flags, or trailing fields while preserving existing
field meaning. A reader skips unknown records using `record_bytes`. Removing fields,
changing numeric meaning, or changing checksum coverage requires a new major version.
