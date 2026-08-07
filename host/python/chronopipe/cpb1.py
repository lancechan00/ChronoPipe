"""Strict, dependency-free reader and writer for CPB1 major version 1."""

from __future__ import annotations

from dataclasses import dataclass
import io
import struct
from pathlib import Path
from typing import BinaryIO, Iterable, Iterator, Optional, Tuple, Union

FILE_HEADER_BYTES = 16
RECORD_PREFIX_BYTES = 8
DATA_RECORD_HEADER_BYTES = 88

SESSION = 1
SCHEMA = 2
DATA = 3
EVENT = 4
SESSION_END = 5
INDEX = 6

DTYPE_FORMAT = {1: "f", 2: "h", 3: "H", 4: "i", 5: "I"}
DTYPE_SIZE = {kind: struct.calcsize("<" + fmt) for kind, fmt in DTYPE_FORMAT.items()}


class FormatError(ValueError):
    """Raised when bytes violate the normative CPB1 contract."""


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1))
    return (~crc) & 0xFFFFFFFF


def encode_file_header(minor: int = 0) -> bytes:
    prefix = struct.pack("<4sHHI", b"CPB1", 1, minor, 0)
    return prefix + struct.pack("<I", crc32c(prefix))


def decode_file_header(data: bytes) -> int:
    if len(data) != FILE_HEADER_BYTES:
        raise FormatError("incomplete CPB1 file header")
    magic, major, minor, flags, checksum = struct.unpack("<4sHHII", data)
    if magic != b"CPB1" or major != 1 or flags != 0:
        raise FormatError("unsupported CPB1 file header")
    if checksum != crc32c(data[:12]):
        raise FormatError("file header CRC32C mismatch")
    return minor


def encode_record(kind: int, payload: bytes = b"", flags: int = 0) -> bytes:
    if not 0 < kind <= 0xFFFF or kind == DATA:
        raise ValueError("kind must be a nonzero non-DATA u16")
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
        return self.raw[RECORD_PREFIX_BYTES:] if self.kind == DATA else self.raw[RECORD_PREFIX_BYTES:-4]


@dataclass(frozen=True)
class DataHeader:
    session_id: int
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
class DataRecord(Record):
    header: DataHeader = None  # type: ignore[assignment]
    sample_payload: bytes = b""

    def values(self) -> Iterator[Tuple[Union[int, float], ...]]:
        fmt = DTYPE_FORMAT[self.header.dtype]
        row = struct.Struct("<" + fmt * self.header.channel_count)
        for sample in range(self.header.sample_count):
            start = sample * row.size
            yield row.unpack_from(self.sample_payload, start)


def _shape_bytes(header: DataHeader) -> int:
    try:
        width = DTYPE_SIZE[header.dtype]
    except KeyError as exc:
        raise FormatError(f"unsupported dtype {header.dtype}") from exc
    if header.sample_count <= 0 or header.channel_count <= 0:
        raise FormatError("DATA shape dimensions must be positive")
    return header.sample_count * header.channel_count * width


def encode_data(header: DataHeader, sample_payload: bytes) -> bytes:
    expected = _shape_bytes(header)
    if header.period_den == 0 or header.layout != 0 or len(sample_payload) != expected:
        raise ValueError("invalid DATA period, layout, or payload shape")
    output = bytearray(DATA_RECORD_HEADER_BYTES)
    struct.pack_into("<HHI", output, 0, DATA, 0, DATA_RECORD_HEADER_BYTES + expected)
    struct.pack_into("<QIIIIQIIHHBBHIIIIII", output, 8,
                     header.session_id, header.stream_id, header.schema_id,
                     header.block_seq, header.first_sample_seq, header.t0_us,
                     header.period_num_us, header.period_den,
                     header.sample_count, header.channel_count,
                     header.dtype, header.layout, header.quality_flags,
                     expected, crc32c(sample_payload), header.input_stream_id,
                     header.input_first_seq, header.input_last_seq, 0)
    struct.pack_into("<I", output, 76, crc32c(bytes(output)))
    return bytes(output) + sample_payload


def decode_record(raw: bytes, offset: int = 0) -> Record:
    if len(raw) < RECORD_PREFIX_BYTES:
        raise FormatError("incomplete record prefix")
    kind, flags, record_bytes = struct.unpack_from("<HHI", raw)
    if kind == 0 or record_bytes != len(raw):
        raise FormatError("invalid record kind or length")
    if kind != DATA:
        if len(raw) < RECORD_PREFIX_BYTES + 4:
            raise FormatError("non-DATA record lacks payload CRC32C")
        payload = raw[RECORD_PREFIX_BYTES:-4]
        if struct.unpack_from("<I", raw, len(raw) - 4)[0] != crc32c(payload):
            raise FormatError("record payload CRC32C mismatch")
        return Record(kind, flags, raw, offset)
    if len(raw) < DATA_RECORD_HEADER_BYTES:
        raise FormatError("incomplete DATA header")
    header_copy = bytearray(raw[:DATA_RECORD_HEADER_BYTES])
    stored_header_crc = struct.unpack_from("<I", raw, 76)[0]
    struct.pack_into("<I", header_copy, 76, 0)
    if stored_header_crc != crc32c(bytes(header_copy)):
        raise FormatError("DATA header CRC32C mismatch")
    fields = struct.unpack_from("<QIIIIQIIHHBBHIIIIII", raw, 8)
    header = DataHeader(
        session_id=fields[0], stream_id=fields[1], schema_id=fields[2],
        block_seq=fields[3], first_sample_seq=fields[4], t0_us=fields[5],
        period_num_us=fields[6], period_den=fields[7], sample_count=fields[8],
        channel_count=fields[9], dtype=fields[10], layout=fields[11],
        quality_flags=fields[12], input_stream_id=fields[15],
        input_first_seq=fields[16], input_last_seq=fields[17],
    )
    expected = _shape_bytes(header)
    if header.period_den == 0 or header.layout != 0 or fields[13] != expected or \
            DATA_RECORD_HEADER_BYTES + expected != len(raw):
        raise FormatError("DATA shape, layout, period, or length mismatch")
    payload = raw[DATA_RECORD_HEADER_BYTES:]
    if fields[14] != crc32c(payload):
        raise FormatError("DATA payload CRC32C mismatch")
    return DataRecord(kind, flags, raw, offset, header, payload)


@dataclass(frozen=True)
class ScanResult:
    minor: int
    records: Tuple[Record, ...]
    recovered_tail_bytes: int = 0


def _open(source: Union[str, Path, BinaryIO]) -> Tuple[BinaryIO, bool]:
    if hasattr(source, "read"):
        return source, False  # type: ignore[return-value]
    return Path(source).open("rb"), True


def scan(source: Union[str, Path, BinaryIO], recover_tail: bool = True) -> ScanResult:
    stream, close = _open(source)
    try:
        minor = decode_file_header(stream.read(FILE_HEADER_BYTES))
        records = []
        offset = FILE_HEADER_BYTES
        recovered = 0
        while True:
            prefix = stream.read(RECORD_PREFIX_BYTES)
            if not prefix:
                break
            if len(prefix) < RECORD_PREFIX_BYTES:
                if not recover_tail:
                    raise FormatError("incomplete record prefix at file tail")
                recovered = len(prefix)
                break
            kind, flags, record_bytes = struct.unpack("<HHI", prefix)
            if kind == 0 or record_bytes < RECORD_PREFIX_BYTES:
                raise FormatError(f"invalid record prefix at offset {offset}")
            payload = stream.read(record_bytes - RECORD_PREFIX_BYTES)
            if len(payload) != record_bytes - RECORD_PREFIX_BYTES:
                if not recover_tail:
                    raise FormatError("incomplete record at file tail")
                recovered = RECORD_PREFIX_BYTES + len(payload)
                break
            records.append(decode_record(prefix + payload, offset))
            offset += record_bytes
        return ScanResult(minor, tuple(records), recovered)
    finally:
        if close:
            stream.close()


def write_file(destination: BinaryIO, records: Iterable[bytes], minor: int = 0) -> int:
    written = destination.write(encode_file_header(minor))
    for record in records:
        decode_record(record)
        written += destination.write(record)
    return written
