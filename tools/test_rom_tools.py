import struct
import unittest
from extract_segments import decode_mio0
from prepare_rom import normalize


class RomToolsTests(unittest.TestCase):
    def test_all_three_byte_orders(self):
        canonical = bytes.fromhex("80371240") + bytes(12*1024*1024-4)
        swapped = bytearray(len(canonical))
        swapped[0::2], swapped[1::2] = canonical[1::2], canonical[0::2]
        little = bytearray(len(canonical))
        for i in range(4):
            little[i::4] = canonical[3-i::4]
        for source in (canonical, swapped, little):
            self.assertEqual(normalize(source)[0], canonical)

    def test_reject_truncated_rom(self):
        with self.assertRaises(ValueError):
            normalize(bytes.fromhex("80371240"))

    def test_mio0_overlapping_back_reference(self):
        encoded = b"MIO0" + struct.pack(">III", 9, 17, 19) + b"\xe0\x30\x02ABC"
        self.assertEqual(decode_mio0(encoded), b"ABCABCABC")

    def test_mio0_literal_only(self):
        encoded = b"MIO0" + struct.pack(">III", 3, 17, 17) + b"\xe0ABC"
        self.assertEqual(decode_mio0(encoded), b"ABC")

    def test_mio0_invalid_distance(self):
        encoded = b"MIO0" + struct.pack(">III", 3, 17, 19) + b"\0\0\0"
        with self.assertRaises(ValueError):
            decode_mio0(encoded)

    def test_mio0_truncated_literals(self):
        encoded = b"MIO0" + struct.pack(">III", 3, 17, 17) + b"\xe0AB"
        with self.assertRaises(ValueError):
            decode_mio0(encoded)


if __name__ == "__main__":
    unittest.main()
