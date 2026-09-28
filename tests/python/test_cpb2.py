import contextlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

from chronopipe.cli import main
from chronopipe.cpb2 import (COMPACT_HEADER_BYTES, CompactHeader, FormatError, WideHeader,
                             apply_time_mapping, compact_record_bytes, decode_compact,
                             decode_event, decode_wide, encode_compact, encode_event,
                             encode_file_header, encode_record, encode_wide, scan)


def compact_block(block_seq=3, first_seq=6, value=1.25):
    header = CompactHeader(
        stream_id=1, dtype=1, layout=0, channel_count=1, quality_flags=0,
        sample_count=2, block_seq=block_seq, first_sample_seq=first_seq,
    )
    return encode_compact(header, struct.pack("<ff", value, value + 1.0))


class Cpb2Tests(unittest.TestCase):
    def test_compact_layout_and_crc(self):
        record = compact_block()
        self.assertEqual(compact_record_bytes(2, 1, 1), COMPACT_HEADER_BYTES + 8)
        self.assertEqual(record[0], 0x0B)
        self.assertEqual(record[1], 0x01)
        self.assertEqual(record[2], 0x00)
        self.assertEqual(record[3:6], bytes((2, 0, 3)))
        decoded = decode_compact(record)
        self.assertEqual(decoded.header.block_seq, 3)
        self.assertEqual(decoded.header.first_sample_seq, 6)
        self.assertEqual(list(decoded.values()), [(1.25,), (2.25,)])
        broken = bytearray(record)
        broken[-1] ^= 1
        with self.assertRaises(FormatError):
            decode_compact(bytes(broken))

    def test_compact_profile_rejects_out_of_range_fields(self):
        payload = struct.pack("<f", 1.0)
        base = dict(stream_id=1, dtype=1, layout=0, channel_count=1, quality_flags=0,
                    sample_count=1, block_seq=0, first_sample_seq=0)
        with self.assertRaises(ValueError):
            encode_compact(CompactHeader(**{**base, "channel_count": 17}), payload * 17)
        with self.assertRaises(ValueError):
            encode_compact(CompactHeader(**{**base, "stream_id": 32}), payload)
        with self.assertRaises(ValueError):
            encode_compact(CompactHeader(**{**base, "sample_count": 0}), b"")
        with self.assertRaises(ValueError):
            encode_compact(CompactHeader(**{**base, "sample_count": 65536}), payload * 65536)
        with self.assertRaises(ValueError):
            encode_compact(CompactHeader(**{**base, "dtype": 0}), payload)

    def test_wide_round_trip(self):
        payload = struct.pack("<" + "f" * 17, *range(17))
        header = WideHeader(
            stream_id=32, schema_id=9, block_seq=4, first_sample_seq=1, t0_us=50,
            period_num_us=0, period_den=1, sample_count=1, channel_count=17, dtype=1,
            input_stream_id=2, input_first_seq=8, input_last_seq=8,
        )
        record = encode_wide(header, payload)
        decoded = decode_wide(record)
        self.assertEqual(decoded.header.channel_count, 17)
        self.assertEqual(decoded.header.stream_id, 32)
        self.assertEqual(decoded.header.t0_us, 50)
        self.assertEqual(struct.unpack_from("<I", record, 8)[0], 32)
        self.assertEqual(struct.unpack_from("<Q", record, 24)[0], 50)
        self.assertEqual(struct.unpack_from("<I", record, 40)[0], 1)
        self.assertEqual(struct.unpack_from("<H", record, 44)[0], 17)

    def test_scan_recovers_short_tail_and_reads_time(self):
        schema = json.dumps({
            "stream_id": 1, "period_num_us": 10000, "period_den": 1, "time_origin_us": 1000,
        }).encode("utf-8")
        first = compact_block(1, 0, 1.0)
        mapping = encode_event(7, 11, 1, 2, 50000)
        second_header = CompactHeader(
            stream_id=1, dtype=1, layout=0, channel_count=1, quality_flags=1,
            sample_count=1, block_seq=2, first_sample_seq=2,
        )
        second = encode_compact(second_header, struct.pack("<f", 4.0))
        blob = encode_file_header() + encode_record(2, schema) + first + mapping + second + first[:9]
        result = scan(io.BytesIO(blob))
        self.assertEqual(result.minor, 0)
        self.assertEqual(len(result.records), 4)
        self.assertEqual(result.recovered_tail_bytes, 9)
        self.assertEqual(decode_event(mapping).event_time_us, 50000)
        self.assertEqual(apply_time_mapping(decode_event(mapping), 10000, 1), 30000)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.cpb"
            exported = Path(directory) / "export.csv"
            framed = Path(directory) / "framed.bin"
            captured = Path(directory) / "captured.cpb"
            path.write_bytes(blob[:-9])
            report_out = io.StringIO()
            with contextlib.redirect_stdout(report_out):
                self.assertEqual(main(["inspect", str(path), "--json"]), 0)
            report = json.loads(report_out.getvalue())
            self.assertEqual(report["format"]["major"], 2)
            self.assertEqual(report["streams"]["1"]["first_t0_us"], 1000)
            self.assertEqual(report["streams"]["1"]["last_t0_us"], 50000)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(["export", str(path), str(exported)]), 0)
            lines = exported.read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines[1].split(",")[3], "1000")
            self.assertEqual(lines[2].split(",")[3], "11000")
            self.assertEqual(lines[3].split(",")[3], "50000")
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(main(["replay", str(path), str(framed)]), 0)
                self.assertEqual(main(["capture", str(framed), str(captured)]), 0)
            self.assertTrue(captured.read_bytes().startswith(b"CPB2"))


if __name__ == "__main__":
    unittest.main()
