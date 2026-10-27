"""Host tests for the independent merged-image verifier."""

from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TOOLS))

from firmware_fixture import APP_OFFSET, make_fixture  # noqa: E402


def load_verifier():
    spec = importlib.util.spec_from_file_location(
        "verify_firmware", TOOLS / "verify_firmware.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class VerifyFirmwareTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="guachat-verify-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.files = make_fixture(self.root)
        self.verifier = load_verifier()

    def test_valid_build(self) -> None:
        info = self.verifier.verify_build(
            self.files["build_dir"],
            self.files["image"],
            self.files["boot_app0"],
        )
        self.assertEqual(info["size"], 0x11000)
        self.assertEqual(info["app_partition"]["label"], "app0")
        self.assertEqual(info["descriptor"]["project_name"], "fixture-app")

    def test_image_only(self) -> None:
        info = self.verifier.verify_merged_image(
            self.files["image"], self.files["elf"]
        )
        self.assertEqual(info["elf"]["sha256"], info["descriptor"]["elf_sha256"])

    def test_component_mismatch(self) -> None:
        data = self.files["bootloader"].read_bytes()
        self.files["bootloader"].write_bytes(b"\0" + data[1:])
        with self.assertRaisesRegex(ValueError, "differs"):
            self.verifier.verify_build(
                self.files["build_dir"],
                self.files["image"],
                self.files["boot_app0"],
            )

    def test_partition_md5_mismatch(self) -> None:
        image = bytearray(self.files["image"].read_bytes())
        image[0x8000 + 2 * 32 + 8] ^= 0x01
        self.files["image"].write_bytes(image)
        with self.assertRaisesRegex(ValueError, "MD5"):
            self.verifier.verify_merged_image(self.files["image"], self.files["elf"])

    def test_elf_mismatch(self) -> None:
        elf = bytearray(self.files["elf"].read_bytes())
        elf[-1] ^= 0xFF
        self.files["elf"].write_bytes(elf)
        with self.assertRaisesRegex(ValueError, "ELF SHA256"):
            self.verifier.verify_merged_image(self.files["image"], self.files["elf"])

    def test_wrong_chip_is_rejected(self) -> None:
        image = bytearray(self.files["image"].read_bytes())
        struct.pack_into("<H", image, 12, 5)
        self.files["image"].write_bytes(image)
        with self.assertRaisesRegex(ValueError, "chip id"):
            self.verifier.verify_merged_image(self.files["image"])

    def test_qio_bootloader_is_rejected(self) -> None:
        image = bytearray(self.files["image"].read_bytes())
        image[2] = 0
        self.files["image"].write_bytes(image)
        with self.assertRaisesRegex(ValueError, "QIO"):
            self.verifier.verify_merged_image(self.files["image"])

    def test_truncated_image_is_rejected(self) -> None:
        self.files["image"].write_bytes(b"\xe9")
        with self.assertRaises(ValueError):
            self.verifier.verify_merged_image(self.files["image"])


if __name__ == "__main__":
    unittest.main()
