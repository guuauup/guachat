"""Host tests for the firmware debug archive tool."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(TOOLS))

from firmware_fixture import make_fixture  # noqa: E402


def load_archive():
    spec = importlib.util.spec_from_file_location(
        "archive_firmware", TOOLS / "archive_firmware.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ArchiveFirmwareTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="guachat-archive-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.files = make_fixture(self.root)
        self.archive = load_archive()
        self.archive_root = self.root / "dist"

    def create(self) -> Path:
        return self.archive.create_archive(
            "guachat_dev",
            self.files["image"],
            self.files["checksum"],
            self.files["manifest"],
            self.files["build_dir"],
            self.archive_root,
        )

    def test_create_and_verify(self) -> None:
        destination = self.create()
        self.assertTrue(destination.name)
        self.assertTrue((destination / "firmware.elf").is_file())
        self.assertTrue((destination / "firmware.map").is_file())
        self.assertTrue((destination / "archive.json").is_file())
        info = self.archive.verify_archive(destination)
        self.assertEqual(info["descriptor"]["project_name"], "fixture-app")

    def test_create_is_idempotent(self) -> None:
        first = self.create()
        second = self.create()
        self.assertEqual(first, second)

    def test_tampered_elf_is_rejected(self) -> None:
        destination = self.create()
        elf = destination / "firmware.elf"
        data = bytearray(elf.read_bytes())
        data[-1] ^= 0xFF
        elf.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "SHA256"):
            self.archive.verify_archive(destination)

    def test_extra_file_is_rejected(self) -> None:
        destination = self.create()
        (destination / "extra.bin").write_bytes(b"x")
        with self.assertRaisesRegex(ValueError, "unexpected"):
            self.archive.verify_archive(destination)

    def test_wrong_directory_name_is_rejected(self) -> None:
        destination = self.create()
        renamed = destination.with_name("0" * 64)
        shutil.move(str(destination), str(renamed))
        with self.assertRaisesRegex(ValueError, "directory name"):
            self.archive.verify_archive(renamed)

    def test_source_manifest_mismatch_is_rejected(self) -> None:
        destination = self.create()
        manifest = destination / "guachat-dev-full.manifest.json"
        data = json.loads(manifest.read_text(encoding="utf-8"))
        data["merged_image"]["sha256"] = "0" * 64
        manifest.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

        archive_json = destination / "archive.json"
        archive_data = json.loads(archive_json.read_text(encoding="utf-8"))
        entry = archive_data["artifacts"][manifest.name]
        entry["size"] = manifest.stat().st_size
        entry["sha256"] = hashlib.sha256(manifest.read_bytes()).hexdigest()
        archive_json.write_text(
            json.dumps(archive_data, indent=2) + "\n", encoding="utf-8"
        )

        with self.assertRaisesRegex(ValueError, "source manifest"):
            self.archive.verify_archive(destination)

    def test_protected_archive_root_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "archive root"):
            self.archive.create_archive(
                "guachat_dev",
                self.files["image"],
                self.files["checksum"],
                self.files["manifest"],
                self.files["build_dir"],
                TOOLS.parent,
            )


if __name__ == "__main__":
    unittest.main()
