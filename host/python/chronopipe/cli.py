"""Command-line entry point for capture, inspection, export, replay, and comparison."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import struct
import sys
import time
from typing import BinaryIO, Dict, Iterable, List, Optional, Sequence, Tuple

from .cpb1 import (DATA, DataRecord, FormatError, decode_record,
                   encode_file_header, scan)
from .framing import cobs_decode, cobs_encode


def _data_records(path: str, stream_id: Optional[int] = None) -> List[DataRecord]:
    result = scan(path)
    return [record for record in result.records
            if isinstance(record, DataRecord) and
            (stream_id is None or record.header.stream_id == stream_id)]


def command_inspect(args: argparse.Namespace) -> int:
    result = scan(args.input)
    kinds: Dict[int, int] = {}
    streams: Dict[int, Dict[str, object]] = {}
    session_metadata: List[object] = []
    schema_metadata: List[object] = []
    for record in result.records:
        kinds[record.kind] = kinds.get(record.kind, 0) + 1
        if record.kind in (1, 2):
            try:
                metadata = json.loads(record.payload.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                raise FormatError(f"record at offset {record.offset} has invalid metadata JSON") from error
            (session_metadata if record.kind == 1 else schema_metadata).append(metadata)
        if isinstance(record, DataRecord):
            header = record.header
            entry = streams.setdefault(header.stream_id, {
                "records": 0, "samples": 0, "channels": header.channel_count,
                "dtype": header.dtype, "first_t0_us": header.t0_us,
                "last_t0_us": header.t0_us, "quality_or": 0,
            })
            entry["records"] = int(entry["records"]) + 1
            entry["samples"] = int(entry["samples"]) + header.sample_count
            entry["last_t0_us"] = header.t0_us
            entry["quality_or"] = int(entry["quality_or"]) | header.quality_flags
    report = {
        "format": {"major": 1, "minor": result.minor},
        "record_count": len(result.records),
        "record_kinds": {str(key): value for key, value in sorted(kinds.items())},
        "streams": {str(key): value for key, value in sorted(streams.items())},
        "sessions": session_metadata,
        "schemas": schema_metadata,
        "recovered_tail_bytes": result.recovered_tail_bytes,
    }
    if args.json:
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        print(f"CPB1 1.{result.minor}: {len(result.records)} records")
        for stream_id, entry in sorted(streams.items()):
            print(f"stream {stream_id}: {entry['records']} blocks, {entry['samples']} samples, "
                  f"{entry['channels']} channels, dtype={entry['dtype']}, quality=0x{entry['quality_or']:x}")
        if result.recovered_tail_bytes:
            print(f"recovered incomplete tail: {result.recovered_tail_bytes} bytes")
    return 0


def command_export(args: argparse.Namespace) -> int:
    records = _data_records(args.input, args.stream)
    with Path(args.output).open("w", newline="", encoding="utf-8") as destination:
        writer = csv.writer(destination)
        writer.writerow(["stream_id", "block_seq", "sample_seq", "event_time_us",
                         "channel", "value", "quality_flags", "input_stream_id",
                         "input_first_seq", "input_last_seq"])
        for record in records:
            header = record.header
            for sample_index, values in enumerate(record.values()):
                event_time = header.t0_us + sample_index * header.period_num_us // header.period_den
                for channel, value in enumerate(values):
                    writer.writerow([header.stream_id, header.block_seq,
                                     header.first_sample_seq + sample_index, event_time,
                                     channel, value, header.quality_flags,
                                     header.input_stream_id, header.input_first_seq,
                                     header.input_last_seq])
    print(f"exported {len(records)} DATA records to {args.output}")
    return 0


def _record_key(record: DataRecord) -> Tuple[int, int]:
    return record.header.stream_id, record.header.block_seq


def command_compare(args: argparse.Namespace) -> int:
    expected = {_record_key(record): record for record in _data_records(args.expected, args.stream)}
    actual = {_record_key(record): record for record in _data_records(args.actual, args.stream)}
    errors: List[str] = []
    if expected.keys() != actual.keys():
        missing = sorted(expected.keys() - actual.keys())
        extra = sorted(actual.keys() - expected.keys())
        if missing: errors.append(f"missing blocks: {missing[:10]}")
        if extra: errors.append(f"extra blocks: {extra[:10]}")
    compared_values = 0
    max_abs_error = 0.0
    for key in sorted(expected.keys() & actual.keys()):
        left = expected[key]
        right = actual[key]
        shape_left = (left.header.dtype, left.header.sample_count, left.header.channel_count)
        shape_right = (right.header.dtype, right.header.sample_count, right.header.channel_count)
        if shape_left != shape_right:
            errors.append(f"shape mismatch at {key}: {shape_left} != {shape_right}")
            continue
        for row_index, (left_row, right_row) in enumerate(zip(left.values(), right.values())):
            for channel, (left_value, right_value) in enumerate(zip(left_row, right_row)):
                compared_values += 1
                left_float = float(left_value)
                right_float = float(right_value)
                if not math.isfinite(left_float) or not math.isfinite(right_float):
                    same_nonfinite = (math.isnan(left_float) and math.isnan(right_float)) or left_float == right_float
                    if not same_nonfinite and len(errors) < args.max_errors:
                        errors.append(f"non-finite mismatch at {key}, sample {row_index}, channel {channel}: "
                                      f"{left_value} != {right_value}")
                    continue
                difference = abs(left_float - right_float)
                max_abs_error = max(max_abs_error, difference)
                limit = args.atol + args.rtol * abs(left_float)
                if difference > limit and len(errors) < args.max_errors:
                    errors.append(f"value mismatch at {key}, sample {row_index}, channel {channel}: "
                                  f"{left_value} != {right_value} (limit {limit})")
    report = {"matched": not errors, "blocks": len(expected.keys() & actual.keys()),
              "values": compared_values, "max_abs_error": max_abs_error, "errors": errors}
    print(json.dumps(report, indent=2))
    return 0 if not errors else 1


def _input_stream(path: str) -> Tuple[BinaryIO, bool]:
    if path == "-":
        return sys.stdin.buffer, False
    return Path(path).open("rb"), True


def _output_stream(path: str) -> Tuple[BinaryIO, bool]:
    if path == "-":
        return sys.stdout.buffer, False
    return Path(path).open("wb"), True


def command_capture(args: argparse.Namespace) -> int:
    source, close_source = _input_stream(args.input)
    destination, close_destination = _output_stream(args.output)
    frames = 0
    try:
        destination.write(encode_file_header())
        pending = bytearray()
        while True:
            chunk = source.read(args.chunk_bytes)
            if not chunk:
                break
            pending.extend(chunk)
            while 0 in pending:
                delimiter = pending.index(0)
                encoded = bytes(pending[:delimiter])
                del pending[:delimiter + 1]
                if not encoded:
                    continue
                record = cobs_decode(encoded)
                decode_record(record)
                destination.write(record)
                destination.flush()
                frames += 1
        if pending and not args.allow_incomplete_tail:
            raise FormatError(f"unterminated transport frame ({len(pending)} bytes)")
    finally:
        if close_source: source.close()
        if close_destination: destination.close()
    print(f"captured {frames} validated records", file=sys.stderr)
    return 0


def command_replay(args: argparse.Namespace) -> int:
    result = scan(args.input)
    destination, close_destination = _output_stream(args.output)
    sent = 0
    previous_time: Optional[int] = None
    try:
        for record in result.records:
            if args.stream is not None and (not isinstance(record, DataRecord) or
                                            record.header.stream_id != args.stream):
                continue
            if args.realtime and isinstance(record, DataRecord):
                if previous_time is not None and record.header.t0_us > previous_time:
                    delay = (record.header.t0_us - previous_time) / 1_000_000.0 / args.speed
                    time.sleep(min(delay, args.max_sleep))
                previous_time = record.header.t0_us
            destination.write(cobs_encode(record.raw) + b"\x00")
            destination.flush()
            sent += 1
    finally:
        if close_destination: destination.close()
    print(f"replayed {sent} records", file=sys.stderr)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="chronopipe", description="ChronoPipe evidence tools")
    commands = parser.add_subparsers(dest="command", required=True)
    inspect_parser = commands.add_parser("inspect", help="validate and summarize a CPB1 file")
    inspect_parser.add_argument("input")
    inspect_parser.add_argument("--json", action="store_true")
    inspect_parser.set_defaults(handler=command_inspect)

    export_parser = commands.add_parser("export", help="export DATA records as tidy CSV")
    export_parser.add_argument("input")
    export_parser.add_argument("output")
    export_parser.add_argument("--stream", type=int)
    export_parser.set_defaults(handler=command_export)

    compare_parser = commands.add_parser("compare", help="compare DATA records by stream and block")
    compare_parser.add_argument("expected")
    compare_parser.add_argument("actual")
    compare_parser.add_argument("--stream", type=int)
    compare_parser.add_argument("--atol", type=float, default=1e-6)
    compare_parser.add_argument("--rtol", type=float, default=1e-5)
    compare_parser.add_argument("--max-errors", type=int, default=20)
    compare_parser.set_defaults(handler=command_compare)

    capture_parser = commands.add_parser("capture", help="capture COBS-framed CPB1 records")
    capture_parser.add_argument("input", help="framed input file, or - for stdin")
    capture_parser.add_argument("output", help="CPB1 output file")
    capture_parser.add_argument("--chunk-bytes", type=int, default=4096)
    capture_parser.add_argument("--allow-incomplete-tail", action="store_true")
    capture_parser.set_defaults(handler=command_capture)

    replay_parser = commands.add_parser("replay", help="replay records as COBS frames")
    replay_parser.add_argument("input")
    replay_parser.add_argument("output", help="framed output file, or - for stdout")
    replay_parser.add_argument("--stream", type=int)
    replay_parser.add_argument("--realtime", action="store_true")
    replay_parser.add_argument("--speed", type=float, default=1.0)
    replay_parser.add_argument("--max-sleep", type=float, default=1.0)
    replay_parser.set_defaults(handler=command_replay)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if getattr(args, "speed", 1.0) <= 0.0 or getattr(args, "chunk_bytes", 1) <= 0:
        parser.error("speed and chunk size must be positive")
    try:
        return int(args.handler(args))
    except (FormatError, ValueError, OSError) as error:
        print(f"chronopipe: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
