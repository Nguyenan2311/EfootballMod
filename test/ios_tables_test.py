import struct
import sys
import unittest
import zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from repair_ios_tables import compact_table, unpack_wesys


class IosTablesTest(unittest.TestCase):
    def test_legacy_records_keep_attribute_and_unlock_bits(self):
        values = [i | ((i % 3) << 6) | 256 for i in range(36)]
        raw = b"".join(struct.pack("<I", n) + "Tên phong cách".encode().ljust(164, b"\0") for n in values)
        fixed = compact_table(raw)
        self.assertEqual(len(fixed), 36 * 4)
        self.assertEqual(struct.unpack("<36I", fixed), tuple(values))
        self.assertEqual(compact_table(fixed), fixed)

    def test_player_skill_104_byte_stride(self):
        values = [i | ((i % 5) << 7) | 1024 for i in range(72)]
        raw = b"".join(struct.pack("<I", n) + "Kỹ năng".encode().ljust(100, b"\0") for n in values)
        fixed = compact_table(raw)
        self.assertEqual(len(fixed), 72 * 4)
        self.assertEqual(struct.unpack("<72I", fixed), tuple(values))

    def test_unknown_legacy_ids_are_rejected(self):
        with self.assertRaises(ValueError):
            compact_table(b"\0" * (168 * 36))

    def test_native_unencrypted_wesys_payload(self):
        raw = struct.pack("<36I", *range(36))
        compressed = zlib.compress(raw)
        packed = b"\0\1\1WESYS" + struct.pack("<II", len(compressed), len(raw)) + compressed
        self.assertEqual(unpack_wesys(packed + b"\0" * 50), raw)
        with self.assertRaises(ValueError):
            unpack_wesys(packed[:-5])


if __name__ == "__main__":
    unittest.main()
