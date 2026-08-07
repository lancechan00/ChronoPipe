import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest

from chronopipe.cli import main
from chronopipe.cpb1 import (DATA, DataHeader, DataRecord, FormatError, crc32c,
                             encode_data, encode_file_header, encode_record, scan)
from chronopipe.framing import cobs_decode, cobs_encode


def data_record(block_seq=1, value=1.25):
    header = DataHeader(
        session_id=7, stream_id=1, schema_id=1, block_seq=block_seq,
        first_sample_seq=block_seq * 2, t0_us=1000, period_num_us=10000,
        period_den=1, sample_count=2, channel_count=1, dtype=1,
    )
    return encode_data(header, struct.pack("<ff", value, value + 1.0))


class Cpb1Tests(unittest.TestCase):
    def test_standard_crc_vector(self):
        self.assertEqual(crc32c(b"123456789"), 0xE3069283)

    def test_round_trip_and_offsets(self):
        record = data_record(3)
        result = scan(io.BytesIO(encode_file_header() + record))
        self.assertEqual(result.minor, 0)
        self.assertEqual(len(result.records), 1)
        decoded = result.records[0]
        self.assertIsInstance(decoded, DataRecord)
        self.assertEqual(decoded.kind, DATA)
        self.assertEqual(decoded.header.block_seq, 3)
        self.assertEqual(list(decoded.values()), [(1.25,), (2.25,)])
        self.assertEqual(struct.unpack_from("<I", record, 4)[0], len(record))
        self.assertEqual(struct.unpack_from("<I", record, 24)[0], 3)

    def test_corruption_is_rejected(self):
        broken = bytearray(data_record())
        broken[-1] ^= 1
        with self.assertRaises(FormatError):
            scan(io.BytesIO(encode_file_header() + broken))

    def test_metadata_crc_is_checked(self):
        record = bytearray(encode_record(1, b"{}"))
        result = scan(io.BytesIO(encode_file_header() + record))
        self.assertEqual(result.records[0].payload, b"{}")
        record[-1] ^= 1
        with self.assertRaises(FormatError):
            scan(io.BytesIO(encode_file_header() + record))

    def test_incomplete_tail_is_reported(self):
        record = data_record()
        result = scan(io.BytesIO(encode_file_header() + record + record[:13]))
        self.assertEqual(len(result.records), 1)
        self.assertEqual(result.recovered_tail_bytes, 13)

    def test_cobs_round_trip(self):
        payload = b"\x00\x01\x02\x00\x03\x00"
        self.assertEqual(cobs_decode(cobs_encode(payload)), payload)

    def test_cli_replay_capture_compare_export(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / "original.cpb"
            framed = root / "framed.bin"
            captured = root / "captured.cpb"
            exported = root / "export.csv"
            original.write_bytes(encode_file_header() + data_record(1) + data_record(2, 2.0))
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(main(["replay", str(original), str(framed)]), 0)
                self.assertEqual(main(["capture", str(framed), str(captured)]), 0)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(main(["compare", str(original), str(captured)]), 0)
                self.assertEqual(main(["export", str(captured), str(exported)]), 0)
            lines = exported.read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(lines), 5)


if __name__ == "__main__":
    unittest.main()
