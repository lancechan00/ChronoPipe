"""Strict, dependency-free reader and writer for CPB2 major version 2."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from pathlib import Path
from typing import BinaryIO, Iterator, List, Tuple, Union

FILE_HEADER_BYTES = 16
RECORD_PREFIX_BYTES = 8
COMPACT_HEADER_BYTES = 17
WIDE_RECORD_HEADER_BYTES = 74
EVENT_PAYLOAD_BYTES = 36

SESSION = 1
SCHEMA = 2
DATA_COMPACT = 3
EVENT = 4
SESSION_END = 5
INDEX = 6
DATA_WIDE = 7

TIME_MAPPING = 11
COMPACT_MAX_STREAMS = 32
COMPACT_MAX_CHANNELS = 16
COMPACT_MAX_SAMPLES = 65535
COMPACT_MAX_QUALITY = 63

DTYPE_FORMAT = {1: "f", 2: "h", 3: "H", 4: "i", 5: "I"}
DTYPE_SIZE = {kind: struct.calcsize("<" + fmt) for kind, fmt in DTYPE_FORMAT.items()}


class FormatError(ValueError):
    """Raised when bytes violate the normative CPB2 contract."""


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF


def _crc_parts(head: bytes, payload: bytes) -> int:
    crc = 0xFFFFFFFF
    for blob in (head, payload):
        for byte in blob:
            crc ^= byte
            for _ in range(8):
                crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF


def shape_fits_compact(stream_id: int, channel_count: int, sample_count: int, quality_flags: int,
                       layout: int, dtype: int, period_num_us: int) -> bool:
    return (0 <= stream_id < COMPACT_MAX_STREAMS and
            1 <= channel_count <= COMPACT_MAX_CHANNELS and
            1 <= sample_count <= COMPACT_MAX_SAMPLES and
            0 <= quality_flags <= COMPACT_MAX_QUALITY and
            0 <= layout <= 1 and dtype in DTYPE_SIZE and period_num_us != 0)


def compact_record_bytes(sample_count: int, channel_count: int, dtype: int) -> int:
    if sample_count <= 0 or channel_count <= 0 or dtype not in DTYPE_SIZE:
        raise ValueError("compact shape dimensions and dtype must be usable")
    if sample_count > COMPACT_MAX_SAMPLES or channel_count > COMPACT_MAX_CHANNELS:
        raise ValueError("shape exceeds the compact profile")
    return COMPACT_HEADER_BYTES + sample_count * channel_count * DTYPE_SIZE[dtype]


def encode_file_header(minor: int = 0) -> bytes:
    prefix = struct.pack("<4sHHI", b"CPB2", 2, minor, 0)
    return prefix + struct.pack("<I", crc32c(prefix))


def decode_file_header(data: bytes) -> int:
    if len(data) != FILE_HEADER_BYTES:
        raise FormatError("incomplete CPB2 file header")
    magic, major, minor, flags, checksum = struct.unpack("<4sHHII", data)
    if magic != b"CPB2" or major != 2 or flags != 0:
        raise FormatError("unsupported CPB2 file header")
    if checksum != crc32c(data[:12]):
        raise FormatError("file header CRC32C mismatch")
    return minor


def _length_delimited_kind_ok(kind: int) -> bool:
    return 0 < kind <= 0xFFFF and kind != DATA_WIDE and (kind & 7) != 3


def encode_record(kind: int, payload: bytes = b"", flags: int = 0) -> bytes:
    if not _length_delimited_kind_ok(kind):
        raise ValueError("kind must be a length-delimited non-wide record")
    total = RECORD_PREFIX_BYTES + len(payload) + 4
    if total > 0xFFFFFFFF:
        raise ValueError("record is too large")
    return struct.pack("<HHI", kind, flags, total) + payload + struct.pack("<I", crc32c(payload))


@dataclass(frozen=True)
class Record:
    kind: int
    flags: int
    raw: bytes
    offset: int = 0

    @property
    def payload(self) -> bytes:
        if self.kind == DATA_COMPACT:
            return self.raw[COMPACT_HEADER_BYTES:]
        if self.kind == DATA_WIDE:
            return self.raw[WIDE_RECORD_HEADER_BYTES:]
        return self.raw[RECORD_PREFIX_BYTES:-4]


@dataclass(frozen=True)
class CompactHeader:
    stream_id: int
    dtype: int
    layout: int
    channel_count: int
    quality_flags: int
    sample_count: int
    block_seq: int
    first_sample_seq: int


@dataclass(frozen=True)
class WideHeader:
    stream_id: int
    schema_id: int
    block_seq: int
    first_sample_seq: int
    t0_us: int
    period_num_us: int
    period_den: int
    sample_count: int
    channel_count: int
    dtype: int
    layout: int = 0
    quality_flags: int = 0
    input_stream_id: int = 0
    input_first_seq: int = 0
    input_last_seq: int = 0


@dataclass(frozen=True)
class EventRecord(Record):
    session_id: int = 0
    event_type: int = 0
    event_flags: int = 0
    stream_id: int = 0
    sequence: int = 0
    event_time_us: int = 0
    detail: int = 0
    value: float = 0.0


def _sample_rows(dtype: int, channel_count: int, sample_count: int, payload: bytes) -> Iterator[Tuple]:
    fmt = DTYPE_FORMAT[dtype]
    row = struct.Struct("<" + fmt * channel_count)
    for sample in range(sample_count):
        yield row.unpack_from(payload, sample * row.size)


def _shape_bytes(sample_count: int, channel_count: int, dtype: int) -> int:
    try:
        width = DTYPE_SIZE[dtype]
    except KeyError as exc:
        raise FormatError(f"unsupported dtype {dtype}") from exc
    if sample_count <= 0 or channel_count <= 0:
        raise FormatError("DATA shape dimensions must be positive")
    return sample_count * channel_count * width


def encode_compact(header: CompactHeader, sample_payload: bytes) -> bytes:
    if header.sample_count <= 0 or header.channel_count <= 0 or header.dtype not in DTYPE_SIZE:
        raise ValueError("invalid compact dtype or empty shape")
    if (header.stream_id >= COMPACT_MAX_STREAMS or header.channel_count > COMPACT_MAX_CHANNELS or
            header.sample_count > COMPACT_MAX_SAMPLES or header.quality_flags > COMPACT_MAX_QUALITY or
            header.layout > 1 or header.dtype > 7):
        raise ValueError("compact field is outside the stable profile")
    expected = _shape_bytes(header.sample_count, header.channel_count, header.dtype)
    if len(sample_payload) != expected:
        raise ValueError("compact payload does not match the card shape")
    packed = DATA_COMPACT
    packed |= (header.stream_id & 31) << 3
    packed |= (header.dtype & 7) << 8
    packed |= (header.layout & 1) << 11
    packed |= ((header.channel_count - 1) & 15) << 12
    packed |= (header.quality_flags & 63) << 16
    output = bytearray(COMPACT_HEADER_BYTES + expected)
    output[0] = packed & 0xFF
    output[1] = (packed >> 8) & 0xFF
    output[2] = (packed >> 16) & 0xFF
    struct.pack_into("<HII", output, 3, header.sample_count, header.block_seq, header.first_sample_seq)
    output[COMPACT_HEADER_BYTES:] = sample_payload
    struct.pack_into("<I", output, 13, _crc_parts(bytes(output[:13]), sample_payload))
    return bytes(output)


def decode_compact(raw: bytes, offset: int = 0) -> "CompactRecord":
    if len(raw) < COMPACT_HEADER_BYTES:
        raise FormatError("incomplete compact DATA card")
    packed = raw[0] | (raw[1] << 8) | (raw[2] << 16)
    if (packed & 7) != DATA_COMPACT or ((packed >> 22) & 3) != 0:
        raise FormatError("compact card kind or pad is invalid")
    dtype = (packed >> 8) & 7
    channel_count = ((packed >> 12) & 15) + 1
    sample_count = struct.unpack_from("<H", raw, 3)[0]
    block_seq, first_sample_seq = struct.unpack_from("<II", raw, 5)
    expected = _shape_bytes(sample_count, channel_count, dtype)
    if COMPACT_HEADER_BYTES + expected != len(raw):
        raise FormatError("compact DATA length does not match its shape")
    payload = raw[COMPACT_HEADER_BYTES:]
    if struct.unpack_from("<I", raw, 13)[0] != _crc_parts(raw[:13], payload):
        raise FormatError("compact DATA CRC32C mismatch")
    header = CompactHeader(
        stream_id=(packed >> 3) & 31, dtype=dtype, layout=(packed >> 11) & 1,
        channel_count=channel_count, quality_flags=(packed >> 16) & 63,
        sample_count=sample_count, block_seq=block_seq, first_sample_seq=first_sample_seq,
    )
    return CompactRecord(DATA_COMPACT, 0, raw, offset, header, payload)


@dataclass(frozen=True)
class CompactRecord(Record):
    header: CompactHeader = None  # type: ignore[assignment]
    sample_payload: bytes = b""

    def values(self) -> Iterator[Tuple]:
        return _sample_rows(self.header.dtype, self.header.channel_count,
                            self.header.sample_count, self.sample_payload)


def encode_wide(header: WideHeader, sample_payload: bytes) -> bytes:
    if header.period_den == 0 or header.sample_count <= 0 or header.channel_count <= 0 or header.layout > 1:
        raise ValueError("invalid wide period, layout, or empty shape")
    if header.channel_count > 0xFFFF:
        raise ValueError("wide channel count does not fit u16")
    expected = _shape_bytes(header.sample_count, header.channel_count, header.dtype)
    if len(sample_payload) != expected:
        raise ValueError("wide payload does not match the header shape")
    output = bytearray(WIDE_RECORD_HEADER_BYTES + expected)
    struct.pack_into("<HHI", output, 0, DATA_WIDE, 0, len(output))
    struct.pack_into("<IIIIQIIIHBBHIIIII", output, 8,
                     header.stream_id, header.schema_id, header.block_seq, header.first_sample_seq,
                     header.t0_us, header.period_num_us, header.period_den, header.sample_count,
                     header.channel_count, header.dtype, header.layout, header.quality_flags,
                     expected, crc32c(sample_payload), header.input_stream_id,
                     header.input_first_seq, header.input_last_seq)
    struct.pack_into("<I", output, 70, crc32c(bytes(output[:WIDE_RECORD_HEADER_BYTES])))
    output[WIDE_RECORD_HEADER_BYTES:] = sample_payload
    return bytes(output)


def decode_wide(raw: bytes, offset: int = 0) -> "WideRecord":
    if len(raw) < WIDE_RECORD_HEADER_BYTES:
        raise FormatError("incomplete wide DATA header")
    kind, flags, record_bytes = struct.unpack_from("<HHI", raw)
    if kind != DATA_WIDE or record_bytes != len(raw):
        raise FormatError("wide DATA kind or length mismatch")
    header_copy = bytearray(raw[:WIDE_RECORD_HEADER_BYTES])
    stored_header_crc = struct.unpack_from("<I", raw, 70)[0]
    struct.pack_into("<I", header_copy, 70, 0)
    if stored_header_crc != crc32c(bytes(header_copy)):
        raise FormatError("wide DATA header CRC32C mismatch")
    fields = struct.unpack_from("<IIIIQIIIHBBHIIIII", raw, 8)
    header = WideHeader(
        stream_id=fields[0], schema_id=fields[1], block_seq=fields[2], first_sample_seq=fields[3],
        t0_us=fields[4], period_num_us=fields[5], period_den=fields[6], sample_count=fields[7],
        channel_count=fields[8], dtype=fields[9], layout=fields[10], quality_flags=fields[11],
        input_stream_id=fields[14], input_first_seq=fields[15], input_last_seq=fields[16],
    )
    expected = _shape_bytes(header.sample_count, header.channel_count, header.dtype)
    if header.period_den == 0 or header.layout > 1 or fields[12] != expected or \
            WIDE_RECORD_HEADER_BYTES + expected != len(raw):
        raise FormatError("wide DATA shape, layout, period, or length mismatch")
    payload = raw[WIDE_RECORD_HEADER_BYTES:]
    if fields[13] != crc32c(payload):
        raise FormatError("wide DATA payload CRC32C mismatch")
    return WideRecord(kind, flags, raw, offset, header, payload)


@dataclass(frozen=True)
class WideRecord(Record):
    header: WideHeader = None  # type: ignore[assignment]
    sample_payload: bytes = b""

    def values(self) -> Iterator[Tuple]:
        return _sample_rows(self.header.dtype, self.header.channel_count,
                            self.header.sample_count, self.sample_payload)


def encode_event(session_id: int, event_type: int, stream_id: int, sequence: int, event_time_us: int,
                 detail: int = 0, value: float = 0.0, flags: int = 0) -> bytes:
    payload = struct.pack("<QHHIIQif", session_id, event_type, flags, stream_id, sequence,
                          event_time_us, detail, value)
    return encode_record(EVENT, payload, flags)


def decode_event(raw: bytes, offset: int = 0) -> EventRecord:
    if len(raw) != RECORD_PREFIX_BYTES + EVENT_PAYLOAD_BYTES + 4:
        raise FormatError("EVENT record has the wrong length")
    kind, prefix_flags, record_bytes = struct.unpack_from("<HHI", raw)
    payload = raw[RECORD_PREFIX_BYTES:-4]
    if kind != EVENT or record_bytes != len(raw) or struct.unpack_from("<I", raw, len(raw) - 4)[0] != crc32c(payload):
        raise FormatError("EVENT kind, length, or CRC32C mismatch")
    fields = struct.unpack("<QHHIIQif", payload)
    return EventRecord(kind, prefix_flags, raw, offset, fields[0], fields[1], fields[2], fields[3],
                       fields[4], fields[5], fields[6], fields[7])


def decode_record(raw: bytes, offset: int = 0) -> Record:
    if len(raw) >= 2 and (raw[0] & 7) == DATA_COMPACT and not (raw[0] == DATA_COMPACT and raw[1] == 0):
        return decode_compact(raw, offset)
    if len(raw) < RECORD_PREFIX_BYTES:
        raise FormatError("incomplete record prefix")
    kind, flags, record_bytes = struct.unpack_from("<HHI", raw)
    if kind == 0 or (kind & 7) == 3 or record_bytes != len(raw):
        raise FormatError("invalid record kind or length")
    if kind == DATA_WIDE:
        return decode_wide(raw, offset)
    if kind == EVENT and record_bytes == RECORD_PREFIX_BYTES + EVENT_PAYLOAD_BYTES + 4:
        return decode_event(raw, offset)
    if len(raw) < RECORD_PREFIX_BYTES + 4:
        raise FormatError("record lacks payload CRC32C")
    payload = raw[RECORD_PREFIX_BYTES:-4]
    if struct.unpack_from("<I", raw, len(raw) - 4)[0] != crc32c(payload):
        raise FormatError("record payload CRC32C mismatch")
    return Record(kind, flags, raw, offset)


@dataclass(frozen=True)
class ScanResult:
    minor: int
    records: Tuple[Record, ...]
    recovered_tail_bytes: int = 0


def _open(source: Union[str, Path, BinaryIO]) -> Tuple[BinaryIO, bool]:
    if hasattr(source, "read"):
        return source, False  # type: ignore[return-value]
    return Path(source).open("rb"), True


def _take(stream: BinaryIO, count: int) -> bytes:
    return stream.read(count)


def scan(source: Union[str, Path, BinaryIO], recover_tail: bool = True) -> ScanResult:
    stream, close = _open(source)
    try:
        header = _take(stream, FILE_HEADER_BYTES)
        if len(header) < FILE_HEADER_BYTES:
            raise FormatError("incomplete CPB2 file header")
        minor = decode_file_header(header)
        records: List[Record] = []
        offset = FILE_HEADER_BYTES
        recovered = 0
        while True:
            first = _take(stream, 1)
            if not first:
                break
            if (first[0] & 7) == DATA_COMPACT:
                second = _take(stream, 1)
                if len(second) != 1:
                    if not recover_tail:
                        raise FormatError("incomplete compact card at file tail")
                    recovered = 1
                    break
                if first[0] == DATA_COMPACT and second[0] == 0:
                    raise FormatError("CPB1 DATA prefix is not a CPB2 compact card")
                rest = _take(stream, COMPACT_HEADER_BYTES - 2)
                card = first + second + rest
                if len(card) != COMPACT_HEADER_BYTES:
                    if not recover_tail:
                        raise FormatError("incomplete compact card at file tail")
                    recovered = len(card)
                    break
                try:
                    expected = _shape_bytes(
                        struct.unpack_from("<H", card, 3)[0],
                        ((card[1] >> 4) & 15) + 1,
                        card[1] & 7,
                    )
                except FormatError:
                    raise
                payload = _take(stream, expected)
                raw = card + payload
                if len(payload) != expected:
                    if not recover_tail:
                        raise FormatError("incomplete compact payload at file tail")
                    recovered = len(raw)
                    break
                records.append(decode_compact(raw, offset))
                offset += len(raw)
                continue
            prefix_rest = _take(stream, RECORD_PREFIX_BYTES - 1)
            prefix = first + prefix_rest
            if len(prefix) < RECORD_PREFIX_BYTES:
                if not recover_tail:
                    raise FormatError("incomplete record prefix at file tail")
                recovered = len(prefix)
                break
            kind, _flags, record_bytes = struct.unpack("<HHI", prefix)
            if kind == 0 or (kind & 7) == 3 or record_bytes < RECORD_PREFIX_BYTES:
                raise FormatError(f"invalid record prefix at offset {offset}")
            body = _take(stream, record_bytes - RECORD_PREFIX_BYTES)
            if len(body) != record_bytes - RECORD_PREFIX_BYTES:
                if not recover_tail:
                    raise FormatError("incomplete record at file tail")
                recovered = RECORD_PREFIX_BYTES + len(body)
                break
            records.append(decode_record(prefix + body, offset))
            offset += record_bytes
        return ScanResult(minor, tuple(records), recovered)
    finally:
        if close:
            stream.close()


def sample_time(origin_us: int, sample_seq: int, period_num_us: int, period_den: int) -> int:
    if period_den == 0:
        raise FormatError("period denominator must not be zero")
    return origin_us + sample_seq * period_num_us // period_den


def apply_time_mapping(event: EventRecord, period_num_us: int, period_den: int) -> int:
    if period_den == 0:
        raise FormatError("period denominator must not be zero")
    delta = event.sequence * period_num_us // period_den
    if event.event_time_us < delta:
        raise FormatError("time mapping is earlier than the sequence formula")
    return event.event_time_us - delta
