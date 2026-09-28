# CPB2 Binary Format

This document is normative for ChronoPipe Block Format major version 2.
CPB1 remains valid for existing major-1 files. A CPB2 reader must not accept a
CPB1 file header, and a CPB1 reader must not accept a CPB2 file header.

CPB2 keeps the same evidence model: streams, events, and time. Hot fixed-rate
DATA blocks use a 17-byte card. Session identity, period, and the time origin
live in SESSION and SCHEMA. Sparse facts live in EVENT records. Shapes that do
not fit the compact card use a self-describing wide DATA record.

## Encoding rules

- Byte order is little-endian.
- Integers use exact widths; IEEE-754 binary32 represents `f32`.
- Readers must reject lengths or shape products that overflow `size_t` or exceed input.
- CRC32C uses polynomial `0x1EDC6F41` in its reflected Castagnoli form.
- A C struct layout is never the wire layout; encoders write each field explicitly.
- Encoders reject values that do not fit a field. They must not truncate or wrap them.
- Stream and block sequences do not wrap within a session; start a new session before
  a `u32` sequence would overflow.
- A reader stops at an incomplete tail. It must not scan payload bytes for a magic value.

## File header

The 16-byte header is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `CPB2` |
| 4 | 2 | major = 2 |
| 6 | 2 | minor |
| 8 | 4 | flags, zero in this major version |
| 12 | 4 | header CRC32C over bytes 0..11 |

## Record classes

A record is compact DATA when `(first_byte & 7) == 3` and the record is not a
CPB1 DATA prefix (`first_byte == 3` and `second_byte == 0`). Every other record
begins with the 8-byte length prefix `kind:u16`, `flags:u16`, `record_bytes:u32`.
The length includes the prefix. Length-delimited kinds must satisfy
`(kind & 7) != 3`, so they cannot be mistaken for a compact card.

| Kind | Value | Framing | Purpose |
|---|---:|---|---|
| SESSION | 1 | length prefix | session, build, and graph identity |
| SCHEMA | 2 | length prefix | one stream's shape, period, and time origin |
| DATA compact | 3 | 17-byte card | hot fixed-rate sample block inside the compact profile |
| EVENT | 4 | length prefix | sparse diagnostic or time-mapping fact |
| SESSION_END | 5 | length prefix | optional clean close |
| INDEX | 6 | length prefix | optional host-generated random-access index |
| DATA wide | 7 | length prefix | self-describing block outside the compact profile |

SESSION, SCHEMA, EVENT, SESSION_END, and INDEX end with a four-byte CRC32C over
the payload, excluding the prefix and the CRC itself. Unknown length-delimited
kinds that obey the rule above may be skipped using `record_bytes`.

SESSION and SCHEMA payloads are UTF-8 JSON. Implementations may preserve unknown
keys. A SCHEMA object that describes a stream recorded with compact DATA must
include:

| Key | Meaning |
|---|---|
| `stream_id` | numeric stream id |
| `period_num_us` | rational period numerator, microseconds |
| `period_den` | rational period denominator, never zero |
| `time_origin_us` | origin used by the sample-time formula |

The sample time of compact DATA is:

```text
t(i) = time_origin_us + (first_sample_seq + i) * period_num_us / period_den
```

Division is integer division. `period_num_us == 0` is an event-driven stream and
must use wide DATA, which carries its own `t0_us`. When a later measurement does
not match the formula, the recorder appends `CP_EVENT_TIME_MAPPING` before the
affected block. Readers apply that event, in file order, by replacing the origin:

```text
time_origin_us = event_time_us - sequence * period_num_us / period_den
```

The affected compact block also sets the discontinuity quality bit. Sequence gaps
are `CP_EVENT_SEQUENCE_GAP` records plus the same quality bit. Events do not
consume a stream id.

EVENT content before its CRC is the following 36-byte structure:

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
`reason:u32`, and `last_block_seq:u32`. INDEX content is versioned by its first
`u16`; readers may skip it using `record_bytes`.

## Compact DATA

The card is 17 bytes. Payload bytes follow immediately. There is no length prefix;
the payload size is `sample_count * channel_count * dtype_size`.

| Offset | Field |
|---:|---|
| 0..2 | 24-bit little-endian bitfield, see below |
| 3..4 | `sample_count` as `u16` |
| 5..8 | `block_seq` as `u32` |
| 9..12 | `first_sample_seq` as `u32` |
| 13..16 | CRC32C over bytes 0..12 and the payload |

Bitfield, least significant bit first:

| Bits | Width | Field |
|---:|---:|---|
| 0..2 | 3 | kind = 3 |
| 3..7 | 5 | stream id, 0..31 |
| 8..10 | 3 | dtype code |
| 11 | 1 | layout, 0 = sample-major |
| 12..15 | 4 | `channel_count - 1` |
| 16..21 | 6 | quality flags |
| 22..23 | 2 | zero |

Legal ranges:

| Field | Range |
|---|---|
| stream id | 0..31 |
| channels | 1..16 |
| samples per block | 1..65535 |
| quality | the six flags 0..63 |
| dtype | `f32`, `i16`, `u16`, `i32`, `u32` |
| layout | 0 or 1 |
| period numerator | nonzero; the period itself is not stored on the card |

Zero samples, zero channels, an unknown dtype, a nonzero pad, or a payload length
that disagrees with the card is invalid. Values above the ranges above are
rejected at encode time. They are not masked into the field.

Layout 0 and layout 1 are sample-major: all channels of sample 0, then all
channels of sample 1. One CRC32C covers the card prefix and the payload.

These widths are the compact profile. They are the default storage shape, not a
ceiling on the product. A session may use more than 32 streams, more than 16
channels, or more than 65535 samples in a block by writing wide DATA for the
streams that do not fit. Raising a compile-time stream limit does not widen this
card and does not create a second compact layout.

## Wide DATA

Wide DATA is kind 7. After the 8-byte prefix, the fixed header is:

| Offset | Size | Field |
|---:|---:|---|
| 8 | 4 | stream_id |
| 12 | 4 | schema_id |
| 16 | 4 | block_seq |
| 20 | 4 | first_sample_seq |
| 24 | 8 | t0_us |
| 32 | 4 | period_num_us |
| 36 | 4 | period_den |
| 40 | 4 | sample_count |
| 44 | 2 | channel_count |
| 46 | 1 | dtype |
| 47 | 1 | layout |
| 48 | 2 | quality_flags |
| 50 | 4 | payload_bytes |
| 54 | 4 | payload_crc32c |
| 58 | 4 | input_stream_id |
| 62 | 4 | input_first_seq |
| 66 | 4 | input_last_seq |
| 70 | 4 | header_crc32c |

The header is 74 bytes including the prefix. `header_crc32c` covers those 74
bytes with its own field treated as zero. `payload_crc32c` covers the payload
only. `period_den` must not be zero. `period_num_us` may be zero for an
event-driven block such as a spectrum frame. Sample time is
`t0_us + i * period_num_us / period_den`. The payload size must equal
`sample_count * channel_count * dtype_size`.

Use wide DATA when any compact limit would be exceeded, when the dtype has no
width, when the period numerator is zero, or when the block's period differs
from the stream SCHEMA. Static graph lineage stays in SESSION and SCHEMA.
Per-block parent ranges are stored only on wide DATA.

## Store slots

A RAM or raw-flash slot must be at least as large as the sealed record it
holds. The compact record size is 17 plus the payload. Slots are not required
to be 4096 bytes. The flash commit marker remains outside the CPB2 record, as
it was outside CPB1.

## Compatibility

Minor versions may add record kinds that keep `(kind & 7) != 3`, flags, or
trailing fields while preserving existing field meaning. Removing fields,
changing numeric meaning, changing which bits are kind 3, or changing checksum
coverage requires a new major version.
