#!/usr/bin/env python3
"""Verify a merged ESP32-S3 image and its matching ELF.

The checks are intentionally independent from ``merge_firmware.py``: they read
the final image and confirm that the bootloader, partition table, boot_app0 and
application really appear at the expected offsets, that the partition table is
well formed, and that the application descriptor embeds the SHA256 of the ELF.

Usage:
    python tools/verify_firmware.py
    python tools/verify_firmware.py --env guachat_dev --image build/guachat-dev-full.bin
    python tools/verify_firmware.py --image-only
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FLASH_SIZE = 16 * 1024 * 1024
BOOTLOADER_OFFSET = 0x0
PARTITION_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xE000
APP_OFFSET = 0x10000
PARTITION_TABLE_SIZE = 0xC00
PARTITION_ENTRY = struct.Struct("<HBBII16sI")
PARTITION_MAGIC = 0x50AA
PARTITION_MD5_MAGIC = 0xEBEB
PARTITION_END_MAGIC = 0xFFFF
OTADATA_MIN_SIZE = 0x2000

ESP_IMAGE_MAGIC = 0xE9
ESP_IMAGE_HEADER_SIZE = 24
ESP_SEGMENT_HEADER_SIZE = 8
APP_DESCRIPTOR_SIZE = 256
APP_DESCRIPTOR_MAGIC = 0xABCD5432
APP_DESCRIPTOR_OFFSET = ESP_IMAGE_HEADER_SIZE + ESP_SEGMENT_HEADER_SIZE
APP_ELF_SHA_OFFSET = 144
ESP32S3_CHIP_ID = 9
FLASH_SIZE_16MB = 4
FLASH_MODE_QIO = 0
FLASH_MODE_DIO = 2

ELF_MACHINE_XTENSA = 94


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_image_header(data: bytes, label: str) -> dict[str, object]:
    """Validate the common ESP32-S3 image header fields."""
    if len(data) < ESP_IMAGE_HEADER_SIZE:
        raise ValueError(f"{label}: image header is truncated")
    if data[0] != ESP_IMAGE_MAGIC:
        raise ValueError(f"{label}: invalid image magic 0x{data[0]:02x}")
    segment_count = data[1]
    if not 1 <= segment_count <= 16:
        raise ValueError(f"{label}: invalid segment count {segment_count}")
    flash_mode = data[2]
    flash_speed = data[3] & 0x0F
    flash_size = data[3] >> 4
    chip_id = struct.unpack_from("<H", data, 12)[0]
    if chip_id != ESP32S3_CHIP_ID:
        raise ValueError(
            f"{label}: chip id {chip_id} is not ESP32-S3 ({ESP32S3_CHIP_ID})"
        )
    if flash_size != FLASH_SIZE_16MB:
        raise ValueError(
            f"{label}: flash size code {flash_size} is not 16 MB "
            f"({FLASH_SIZE_16MB})"
        )
    return {
        "segment_count": segment_count,
        "flash_mode": flash_mode,
        "flash_speed": flash_speed,
        "flash_size": flash_size,
        "chip_id": chip_id,
    }


def parse_partition_table(
    raw: bytes, table_offset: int = PARTITION_OFFSET
) -> tuple[list[dict[str, object]], bool]:
    """Parse an ESP partition table and verify its optional MD5 marker."""
    if len(raw) < PARTITION_ENTRY.size:
        raise ValueError("partition table is truncated")
    if len(raw) > 0x1000:
        raise ValueError("partition table is larger than one 4 KB sector")

    partitions: list[dict[str, object]] = []
    found_md5 = False
    for cursor in range(0, len(raw) - PARTITION_ENTRY.size + 1, PARTITION_ENTRY.size):
        magic = struct.unpack_from("<H", raw, cursor)[0]
        if magic == PARTITION_END_MAGIC:
            break
        if magic == PARTITION_MD5_MAGIC:
            expected = hashlib.md5(raw[:cursor]).digest()
            actual = raw[cursor + 16 : cursor + 32]
            if actual != expected:
                raise ValueError("partition table MD5 marker does not match")
            found_md5 = True
            break
        if magic != PARTITION_MAGIC:
            raise ValueError(f"invalid partition entry at table offset 0x{cursor:x}")

        _, kind, subtype, offset, size, label_raw, _ = PARTITION_ENTRY.unpack_from(
            raw, cursor
        )
        label = label_raw.split(b"\0", 1)[0].decode("ascii", "strict")
        minimum_offset = table_offset + 0x1000
        if not label:
            raise ValueError(f"partition entry at 0x{cursor:x} has no label")
        if size == 0:
            raise ValueError(f"partition {label!r} has zero size")
        if offset < minimum_offset:
            raise ValueError(
                f"partition {label!r} starts at 0x{offset:x}, inside the table sector"
            )
        if offset + size > FLASH_SIZE:
            raise ValueError(
                f"partition {label!r} ends outside the 16 MB flash: "
                f"0x{offset:x}+0x{size:x}"
            )
        partitions.append(
            {
                "kind": kind,
                "subtype": subtype,
                "offset": offset,
                "size": size,
                "label": label,
            }
        )

    if not partitions:
        raise ValueError("partition table is empty")
    if not found_md5:
        raise ValueError("partition table has no MD5 marker")

    labels = [item["label"] for item in partitions]
    if len(labels) != len(set(labels)):
        raise ValueError("partition labels must be unique")

    ordered = sorted(partitions, key=lambda item: item["offset"])
    for left, right in zip(ordered, ordered[1:]):
        if left["offset"] + left["size"] > right["offset"]:
            raise ValueError(
                f"partitions {left['label']!r} and {right['label']!r} overlap"
            )
    return partitions, found_md5


def app_descriptor(app: bytes) -> dict[str, str]:
    """Read the ESP application descriptor and its embedded ELF SHA256."""
    if len(app) < APP_DESCRIPTOR_OFFSET + APP_DESCRIPTOR_SIZE:
        raise ValueError("application image is too short for its descriptor")
    segment_length = struct.unpack_from("<I", app, ESP_IMAGE_HEADER_SIZE + 4)[0]
    if segment_length < APP_DESCRIPTOR_SIZE:
        raise ValueError("application descriptor segment is truncated")
    if APP_DESCRIPTOR_OFFSET + segment_length > len(app):
        raise ValueError("application descriptor segment exceeds the image")

    descriptor = app[APP_DESCRIPTOR_OFFSET : APP_DESCRIPTOR_OFFSET + APP_DESCRIPTOR_SIZE]
    if struct.unpack_from("<I", descriptor, 0)[0] != APP_DESCRIPTOR_MAGIC:
        raise ValueError("invalid application descriptor magic")

    def text_field(offset: int, size: int) -> str:
        value = descriptor[offset : offset + size].split(b"\0", 1)[0]
        try:
            return value.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError("application descriptor contains invalid text") from exc

    return {
        "version": text_field(16, 32),
        "project_name": text_field(48, 32),
        "idf_version": text_field(112, 32),
        "elf_sha256": descriptor[
            APP_ELF_SHA_OFFSET : APP_ELF_SHA_OFFSET + 32
        ].hex(),
    }


def verify_elf(elf_path: Path, embedded_sha256: str) -> dict[str, object]:
    """Check the ELF identity and compare it with the descriptor's SHA256."""
    if not elf_path.is_file():
        raise ValueError(f"ELF not found: {elf_path}")
    data = elf_path.read_bytes()
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("ELF must be a 32-bit little-endian file")
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != ELF_MACHINE_XTENSA:
        raise ValueError(f"ELF machine {machine} is not Xtensa ({ELF_MACHINE_XTENSA})")
    actual = sha256_bytes(data)
    if actual != embedded_sha256:
        raise ValueError(
            "application descriptor ELF SHA256 does not match firmware.elf"
        )
    return {"path": str(elf_path), "size": len(data), "sha256": actual}


def verify_merged_image(
    image_path: Path,
    elf_path: Path | None = None,
) -> dict[str, object]:
    """Validate the merged image structure, optionally against a matching ELF."""
    image = image_path.read_bytes()
    if len(image) < APP_OFFSET + APP_DESCRIPTOR_OFFSET + APP_DESCRIPTOR_SIZE:
        raise ValueError("merged image is too short")
    if len(image) > FLASH_SIZE:
        raise ValueError("merged image exceeds the 16 MB flash size")

    bootloader_header = read_image_header(image, "bootloader")
    if bootloader_header["flash_mode"] == FLASH_MODE_QIO:
        raise ValueError(
            "bootloader uses QIO; this board requires the PlatformIO DIO header "
            "(forcing QIO causes a TG0WDT_SYS_RST boot loop)"
        )

    partition_raw = image[PARTITION_OFFSET : PARTITION_OFFSET + PARTITION_TABLE_SIZE]
    partitions, _ = parse_partition_table(partition_raw, PARTITION_OFFSET)

    if len(image) < APP_OFFSET + 1:
        raise ValueError("merged image has no application at 0x10000")
    app_header = read_image_header(image[APP_OFFSET:], "application")
    descriptor = app_descriptor(image[APP_OFFSET:])

    app_partitions = [
        item
        for item in partitions
        if item["offset"] == APP_OFFSET and item["kind"] == 0
    ]
    if len(app_partitions) != 1:
        raise ValueError(
            f"expected exactly one app partition at 0x{APP_OFFSET:x}, "
            f"found {len(app_partitions)}"
        )
    app_partition = app_partitions[0]
    app_size = len(image) - APP_OFFSET
    if app_size > app_partition["size"]:
        raise ValueError(
            f"application is {app_size} bytes; partition limit is "
            f"{app_partition['size']}"
        )

    otadata = [
        item
        for item in partitions
        if item["offset"] == BOOT_APP0_OFFSET
        and item["kind"] == 1
        and item["size"] >= OTADATA_MIN_SIZE
    ]
    if not otadata:
        raise ValueError(
            f"no data/otadata partition covers 0x{BOOT_APP0_OFFSET:x} "
            f"with at least 0x{OTADATA_MIN_SIZE:x} bytes"
        )

    elf_info = None
    if elf_path is not None:
        elf_info = verify_elf(elf_path, descriptor["elf_sha256"])

    return {
        "image": str(image_path),
        "size": len(image),
        "bootloader_header": bootloader_header,
        "app_header": app_header,
        "descriptor": descriptor,
        "partitions": partitions,
        "app_partition": app_partition,
        "elf": elf_info,
    }


def find_boot_app0() -> Path:
    packages = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio")) / "packages"
    candidates = sorted(
        packages.glob("framework-arduinoespressif32*/tools/partitions/boot_app0.bin")
    )
    if not candidates:
        raise ValueError(
            f"boot_app0.bin not found under {packages}; run a PlatformIO build first"
        )
    return candidates[0]


def verify_build(
    build_dir: Path,
    image_path: Path,
    boot_app0_path: Path | None = None,
) -> dict[str, object]:
    """Compare every build component with the merged image, then verify layout."""
    local_boot_app0 = build_dir / "boot_app0.bin"
    components = {
        "bootloader": build_dir / "bootloader.bin",
        "partitions": build_dir / "partitions.bin",
        "boot_app0": boot_app0_path
        or (local_boot_app0 if local_boot_app0.is_file() else find_boot_app0()),
        "application": build_dir / "firmware.bin",
    }
    missing = [str(path) for path in components.values() if not path.is_file()]
    if missing:
        raise ValueError("missing build component(s): " + ", ".join(missing))

    image = image_path.read_bytes()
    placements = {
        "bootloader": BOOTLOADER_OFFSET,
        "partitions": PARTITION_OFFSET,
        "boot_app0": BOOT_APP0_OFFSET,
        "application": APP_OFFSET,
    }
    sizes: dict[str, int] = {}
    for name, path in components.items():
        data = path.read_bytes()
        offset = placements[name]
        if not data:
            raise ValueError(f"{name} image is empty")
        if offset + len(data) > len(image):
            raise ValueError(f"{name} image is outside the merged image")
        if image[offset : offset + len(data)] != data:
            raise ValueError(
                f"{name} image differs from the merged image at 0x{offset:x}"
            )
        sizes[name] = len(data)

    info = verify_merged_image(image_path, build_dir / "firmware.elf")
    info["component_sizes"] = sizes
    return info


def default_image(env: str) -> Path:
    name = "guachat-dev-full.bin" if env == "guachat_dev" else f"{env}-full.bin"
    return ROOT / "build" / name


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", default="guachat_dev", help="PlatformIO environment")
    parser.add_argument("--image", type=Path, help="merged image (default: build/)")
    parser.add_argument("--build-dir", type=Path, help="build directory (default: .pio/build/<env>)")
    parser.add_argument("--boot-app0", type=Path, help="boot_app0.bin path")
    parser.add_argument(
        "--image-only",
        action="store_true",
        help="verify the image structure without comparing build components",
    )
    args = parser.parse_args(argv)

    image = args.image or default_image(args.env)
    build_dir = args.build_dir or ROOT / ".pio" / "build" / args.env
    if not image.is_absolute():
        image = ROOT / image
    if not build_dir.is_absolute():
        build_dir = ROOT / build_dir
    if args.boot_app0 and not args.boot_app0.is_absolute():
        args.boot_app0 = ROOT / args.boot_app0

    try:
        if args.image_only:
            info = verify_merged_image(image, build_dir / "firmware.elf")
        else:
            info = verify_build(build_dir, image, args.boot_app0)
    except (OSError, ValueError, struct.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    descriptor = info["descriptor"]
    print(f"Image:        {image} ({info['size']} bytes)")
    print(
        "Bootloader:   "
        f"{info['bootloader_header']['segment_count']} segments, "
        f"{info['bootloader_header']['flash_mode']} flash mode"
    )
    print(
        "Partitions:   "
        f"{len(info['partitions'])} entries, "
        f"app {info['app_partition']['label']} at "
        f"0x{info['app_partition']['offset']:x} "
        f"({info['app_partition']['size']} bytes)"
    )
    print(
        "Application:  "
        f"{descriptor['project_name'] or 'unknown'} / "
        f"{descriptor['version'] or 'unknown'}"
    )
    print(f"ELF:          {info['elf']['sha256']}")
    print("Merged firmware: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
