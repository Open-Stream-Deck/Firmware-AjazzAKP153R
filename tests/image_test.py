import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from image import APPLICATION_BASE, RAM_END, crc16_arc, read_image
from version import firmware_version


def application():
    vectors = [APPLICATION_BASE + 321] * 80
    vectors[0] = RAM_END
    for index in (7, 8, 9, 10, 13):
        vectors[index] = 0
    return struct.pack("<80I", *vectors) + b"\x00\xbf\x00\xbf"


def elf(segments):
    header = struct.pack(
        "<16sHHIIIIIHHHHHH",
        b"\x7fELF\x01\x01\x01" + bytes(9),
        2,
        40,
        1,
        APPLICATION_BASE + 321,
        52,
        0,
        0,
        52,
        32,
        len(segments),
        0,
        0,
        0,
    )
    headers = bytearray()
    payload = bytearray()
    offset = 52 + 32 * len(segments)
    for address, content in segments:
        headers.extend(
            struct.pack("<8I", 1, offset, address, address, len(content), len(content), 5, 4)
        )
        payload.extend(content)
        offset += len(content)
    return header + headers + payload


class ImageTests(unittest.TestCase):
    def test_exported_manifest_matches_source_version_and_image(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "firmware.elf"
            output = Path(directory) / "firmware.bin"
            source.write_bytes(elf([(APPLICATION_BASE, application())]))
            tool = Path(__file__).resolve().parents[1] / "tools" / "image.py"
            subprocess.run(
                [sys.executable, str(tool), str(source), "--output", str(output)],
                capture_output=True,
                check=True,
            )
            manifest = json.loads(output.with_name("firmware.json").read_text())
            self.assertEqual(manifest["firmware_version"], firmware_version())
            self.assertEqual(output.read_bytes(), application())
            self.assertEqual(manifest["bytes"], len(application()))
            self.assertEqual(manifest["sha256"], hashlib.sha256(application()).hexdigest())

    def read(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.elf"
            path.write_bytes(data)
            return read_image(path)

    def test_image_round_trip_and_crc(self):
        expected = application()
        image, stack, entry = self.read(elf([(APPLICATION_BASE, expected)]))
        self.assertEqual(image, expected)
        self.assertEqual(stack, RAM_END)
        self.assertEqual(entry, APPLICATION_BASE + 321)
        self.assertEqual(crc16_arc(b"123456789"), 0xBB3D)

    def test_invalid_vectors(self):
        for index, value in [(0, RAM_END - 4), (1, APPLICATION_BASE + 320), (7, 1), (2, 0)]:
            with self.subTest(index=index):
                data = bytearray(application())
                struct.pack_into("<I", data, index * 4, value)
                with self.assertRaises(ValueError):
                    self.read(elf([(APPLICATION_BASE, data)]))

    def test_truncated_and_wrong_architecture(self):
        valid = elf([(APPLICATION_BASE, application())])
        wrong_machine = bytearray(valid)
        struct.pack_into("<H", wrong_machine, 18, 62)
        for data in [valid[:20], valid[:-1], b"INVALID" + valid[7:], wrong_machine]:
            with self.subTest(size=len(data)):
                with self.assertRaises(ValueError):
                    self.read(data)

    def test_overlap_and_application_bounds(self):
        for segments in [
            [(APPLICATION_BASE - 4, application())],
            [(APPLICATION_BASE, application()), (APPLICATION_BASE + 320, bytes(4))],
            [(APPLICATION_BASE, application()), (APPLICATION_BASE + 96496, bytes(4))],
        ]:
            with self.subTest(addresses=[address for address, _ in segments]):
                with self.assertRaises(ValueError):
                    self.read(elf(segments))

    def test_invalid_input_does_not_emit_image(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid.elf"
            output = Path(directory) / "firmware.bin"
            source.write_bytes(b"invalid")
            tool = Path(__file__).resolve().parents[1] / "tools" / "image.py"
            result = subprocess.run(
                [sys.executable, str(tool), str(source), "--output", str(output)],
                capture_output=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
