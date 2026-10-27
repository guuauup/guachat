"""Synthetic ESP32-S3 build fixtures for host-side tool tests."""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path

FLASH_SIZE = 16 * 1024 * 1024
PARTITION_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xE000
APP_OFFSET = 0x10000
PARTITION_TABLE_SIZE = 0xC00
ENTRY = struct.Struct("<HBBII16sI")


def image_header(segment_count: int = 3, flash_mode: int = 2) -> bytes:
    header = bytearray(24)
    header[0] = 0xE9
    header[1] = segment_count
    header[2] = flash_mode
    header[3] = 4 << 4  # 16 MB
    struct.pack_into("<I", header, 4, 0x40380000)
    struct.pack_into("<H", header, 12, 9)  # ESP32-S3
    return bytes(header)


def partition_table(app_size: int = 0x10000) -> bytes:
    entries = [
        (1, 2, 0x9000, 0x5000, "nvs"),
        (1, 1, 0xE000, 0x2000, "otadata"),
        (0, 0x10, APP_OFFSET, app_size, "app0"),
    ]
    raw = bytearray()
    for kind, subtype, offset, size, label in entries:
        raw += ENTRY.pack(
            0x50AA,
            kind,
            subtype,
            offset,
            size,
            label.encode("ascii").ljust(16, b"\0"),
            0,
        )
    raw += struct.pack("<H", 0xEBEB) + b"\0" * 14 + hashlib.md5(raw).digest()
    return bytes(raw).ljust(PARTITION_TABLE_SIZE, b"\xff")


def app_image(elf_sha256: str, app_size: int = 0x1000) -> bytes:
    app = bytearray(app_size)
    app[:24] = image_header(segment_count=5)
    struct.pack_into("<I", app, 24, 0x3C000000)
    struct.pack_into("<I", app, 28, 256)
    struct.pack_into("<I", app, 32, 0xABCD5432)
    fields = {
        16: b"fixture-version",
        48: b"fixture-app",
        80: b"12:00:00",
        96: b"Jan 1 2026",
        112: b"esp-idf: fixture",
    }
    for offset, value in fields.items():
        app[32 + offset : 32 + offset + len(value)] = value
    app[32 + 144 : 32 + 176] = bytes.fromhex(elf_sha256)
    return bytes(app)


def elf_bytes(seed: bytes = b"fixture") -> bytes:
    data = bytearray(64)
    data[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<H", data, 16, 2)
    struct.pack_into("<H", data, 18, 94)  # Xtensa
    data[32:40] = seed[:8].ljust(8, b"\0")
    return bytes(data)


def make_fixture(root: Path) -> dict[str, Path]:
    """Create a small but structurally valid build and merged image."""
    build = root / "build"
    build.mkdir(parents=True)
    elf = elf_bytes()
    elf_sha = hashlib.sha256(elf).hexdigest()
    app = app_image(elf_sha)
    bootloader = image_header()
    partitions = partition_table()
    boot_app0 = b"\1\0\0\0" + b"\xff" * (0x2000 - 4)

    merged = bytearray(b"\xff" * (APP_OFFSET + len(app)))
    merged[0 : len(bootloader)] = bootloader
    merged[PARTITION_OFFSET : PARTITION_OFFSET + len(partitions)] = partitions
    merged[BOOT_APP0_OFFSET : BOOT_APP0_OFFSET + len(boot_app0)] = boot_app0
    merged[APP_OFFSET : APP_OFFSET + len(app)] = app

    image = root / "guachat-dev-full.bin"
    image.write_bytes(merged)
    digest = hashlib.sha256(merged).hexdigest()
    (root / "guachat-dev-full.bin.sha256").write_text(
        f"{digest}  {image.name}\n", encoding="utf-8"
    )

    manifest = {
        "schema": 1,
        "environment": "guachat_dev",
        "git": {"commit": "0" * 40, "dirty": False},
        "merged_image": {
            "role": "merged",
            "path": image.name,
            "size": len(merged),
            "sha256": digest,
        },
        "artifacts": {
            "elf": {"role": "elf", "path": "firmware.elf", "size": len(elf), "sha256": elf_sha}
        },
    }
    manifest_path = root / "guachat-dev-full.manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    files = {
        "build_dir": build,
        "image": image,
        "checksum": root / "guachat-dev-full.bin.sha256",
        "manifest": manifest_path,
        "elf": build / "firmware.elf",
        "map": build / "firmware.map",
        "bootloader": build / "bootloader.bin",
        "partitions": build / "partitions.bin",
        "boot_app0": build / "boot_app0.bin",
        "application": build / "firmware.bin",
    }
    files["elf"].write_bytes(elf)
    files["map"].write_text("fixture map\n", encoding="utf-8")
    files["bootloader"].write_bytes(bootloader)
    files["partitions"].write_bytes(partitions)
    files["boot_app0"].write_bytes(boot_app0)
    files["application"].write_bytes(app)
    return files
