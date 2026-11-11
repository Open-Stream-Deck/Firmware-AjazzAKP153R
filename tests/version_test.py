import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import version


class VersionTests(unittest.TestCase):
    def test_hid_version_range(self):
        for value in ("0.0.0", "1.2.3", "255.255.255"):
            with self.subTest(value=value), patch.object(version, "FIRMWARE_VERSION", value):
                self.assertEqual(version.firmware_version(), value)
        for value in ("256.0.0", "1.256.0", "1.0.256", "01.0.0", "1.0", "-1.0.0", "1.0.0\n"):
            with self.subTest(value=value), patch.object(version, "FIRMWARE_VERSION", value):
                with self.assertRaises(ValueError):
                    version.firmware_version()

    def test_generated_header_matches_manifest_version(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "firmware_version.h"
            tool = Path(__file__).resolve().parents[1] / "tools" / "version.py"
            subprocess.run([sys.executable, str(tool), "--header", str(header)], check=True)
            text = header.read_text(encoding="utf-8")
            for name, value in zip(("MAJOR", "MINOR", "PATCH"), version.firmware_version().split(".")):
                self.assertIn(f"#define FIRMWARE_VERSION_{name} {value}u\n", text)


if __name__ == "__main__":
    unittest.main()
