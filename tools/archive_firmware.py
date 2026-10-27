#!/usr/bin/env python3
"""Create and verify a self-contained firmware debug archive.

The archive keeps the merged image, matching ELF/MAP, checksum, manifest and an
archive manifest together under ``dist/firmware/<sha256>/``. Release artifacts
can attach this directory (or a zip of it) without committing large binaries.

Usage:
    python tools/archive_firmware.py create --env guachat_dev
    python tools/archive_firmware.py verify dist/firmware/<sha256>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from verify_firmware import sha256_file, verify_build, verify_merged_image  # noqa: E402

ROOT = TOOLS.parent
DEFAULT_ARCHIVE_ROOT = ROOT / "dist" / "firmware"
SHA256_RE = re.compile(r"[0-9a-f]{64}")
SAFE_NAME_RE = re.compile(r"[A-Za-z0-9._-]+")


def safe_name(name: str) -> None:
    if not SAFE_NAME_RE.fullmatch(name) or name in (".", ".."):
        raise ValueError(f"unsafe archive file name: {name!r}")


def artifact(path: Path, role: str) -> dict[str, object]:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"archive source is not a regular file: {path}")
    return {
        "role": role,
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate archive manifest key: {key}")
        result[key] = value
    return result


def read_manifest(path: Path) -> dict[str, object]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"archive manifest is unreadable: {path} ({exc})") from exc
    if not isinstance(data, dict):
        raise ValueError("archive manifest must be a JSON object")
    if data.get("schema_version") != 1:
        raise ValueError("unsupported archive manifest schema")
    return data


def check_archive_entries(directory: Path, allowed: set[str]) -> None:
    for entry in directory.iterdir():
        if entry.is_symlink():
            raise ValueError(f"archive entry is a symlink: {entry.name}")
        if entry.is_dir():
            raise ValueError(f"unexpected archive subdirectory: {entry.name}")
        if not entry.is_file() or entry.name not in allowed:
            raise ValueError(f"unexpected archive entry: {entry.name}")


def verify_archive(directory: Path) -> dict[str, object]:
    """Verify every archived file and the merged image against its ELF."""
    directory = Path(directory).resolve()
    if not directory.is_dir():
        raise ValueError(f"archive directory does not exist: {directory}")
    if not SHA256_RE.fullmatch(directory.name):
        raise ValueError("archive directory name must be the merged image SHA256")

    data = read_manifest(directory / "archive.json")
    merged = data.get("merged_image") or {}
    if not isinstance(merged, dict):
        raise ValueError("archive manifest merged_image must be an object")
    if merged.get("sha256") != directory.name:
        raise ValueError("archive directory name does not match merged_image.sha256")

    artifacts = data.get("artifacts") or {}
    if not isinstance(artifacts, dict):
        raise ValueError("archive manifest artifacts must be an object")
    required = {"merged", "elf", "map", "manifest", "checksum"}
    roles = {item.get("role") for item in artifacts.values() if isinstance(item, dict)}
    missing_roles = required - roles
    if missing_roles:
        raise ValueError("archive is missing role(s): " + ", ".join(sorted(missing_roles)))

    allowed = set(artifacts) | {"archive.json"}
    check_archive_entries(directory, allowed)

    for name, expected in artifacts.items():
        if not isinstance(name, str):
            raise ValueError("archive artifact names must be strings")
        safe_name(name)
        if not isinstance(expected, dict):
            raise ValueError(f"archive artifact entry is not an object: {name}")
        path = directory / name
        if not path.is_file() or path.is_symlink():
            raise ValueError(f"archive file is missing: {name}")
        if path.stat().st_size != expected.get("size"):
            raise ValueError(f"archive file size mismatch: {name}")
        if sha256_file(path) != expected.get("sha256"):
            raise ValueError(f"archive file SHA256 mismatch: {name}")

    # The artifact key is the real file name; find the merged role explicitly.
    image_name = next(
        name
        for name, item in artifacts.items()
        if isinstance(item, dict) and item.get("role") == "merged"
    )
    image_path = directory / image_name
    elf_name = next(
        name
        for name, item in artifacts.items()
        if isinstance(item, dict) and item.get("role") == "elf"
    )
    elf_path = directory / elf_name
    manifest_name = next(
        name
        for name, item in artifacts.items()
        if isinstance(item, dict) and item.get("role") == "manifest"
    )
    checksum_name = next(
        name
        for name, item in artifacts.items()
        if isinstance(item, dict) and item.get("role") == "checksum"
    )

    if merged.get("size") != image_path.stat().st_size:
        raise ValueError("archive merged_image.size does not match the image")
    if merged.get("sha256") != sha256_file(image_path):
        raise ValueError("archive merged_image.sha256 does not match the image")

    source_manifest = read_json(directory / manifest_name)
    source_merged = source_manifest.get("merged_image") or {}
    if (
        source_merged.get("sha256") != merged.get("sha256")
        or source_merged.get("size") != merged.get("size")
    ):
        raise ValueError("source manifest does not describe the archived image")

    checksum_text = (directory / checksum_name).read_text(encoding="utf-8").split()
    if not checksum_text or checksum_text[0] != merged.get("sha256"):
        raise ValueError("checksum file does not match the archived image")

    info = verify_merged_image(image_path, elf_path)
    return info


def read_json(path: Path) -> dict[str, object]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"JSON file is unreadable: {path} ({exc})") from exc
    if not isinstance(data, dict):
        raise ValueError(f"JSON file must be an object: {path}")
    return data


def default_paths(env: str) -> tuple[Path, Path, Path, Path]:
    name = "guachat-dev-full.bin" if env == "guachat_dev" else f"{env}-full.bin"
    image = ROOT / "build" / name
    checksum = image.with_suffix(image.suffix + ".sha256")
    manifest = image.with_suffix(".manifest.json")
    build_dir = ROOT / ".pio" / "build" / env
    return image, checksum, manifest, build_dir


def safe_archive_root(root: Path) -> Path:
    root = root.resolve()
    protected = {Path(root.anchor), Path.home(), ROOT}
    if root in protected:
        raise ValueError("archive root must be a dedicated directory below the repository")
    return root


def create_archive(
    env: str,
    image: Path,
    checksum: Path,
    manifest: Path,
    build_dir: Path,
    archive_root: Path,
) -> Path:
    """Verify the build, then publish one immutable archive directory."""
    for path, label in (
        (image, "merged image"),
        (checksum, "checksum"),
        (manifest, "manifest"),
        (build_dir / "firmware.elf", "ELF"),
        (build_dir / "firmware.map", "MAP"),
    ):
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"{label} is not a regular file: {path}")

    info = verify_build(build_dir, image)
    source_manifest = read_json(manifest)
    digest = sha256_file(image)
    archive_root = safe_archive_root(archive_root)
    destination = archive_root / digest

    if destination.exists():
        verify_archive(destination)
        print(f"archive already exists and verifies: {destination}")
        return destination

    archive_root.mkdir(parents=True, exist_ok=True)
    destination.mkdir()
    try:
        sources = {
            "merged": (image, image.name),
            "checksum": (checksum, checksum.name),
            "manifest": (manifest, manifest.name),
            "elf": (build_dir / "firmware.elf", "firmware.elf"),
            "map": (build_dir / "firmware.map", "firmware.map"),
        }
        files: dict[str, dict[str, object]] = {}
        for role, (source, name) in sources.items():
            safe_name(name)
            if name in files:
                raise ValueError(f"duplicate archive file name: {name}")
            shutil.copyfile(source, destination / name)
            files[name] = artifact(destination / name, role)

        readme = (
            "GUACHAT firmware debug archive\n"
            f"Environment: {env}\n"
            f"Merged image: {image.name} ({digest})\n\n"
            "This directory contains the matching ELF and MAP for crash\n"
            "symbolization. Keep it with the release; it is not required to flash.\n"
            "WARNING: flashing the merged image at 0x0 overwrites NVS (0x9000)\n"
            "and otadata (0xE000); device settings and calibration are lost.\n"
            f"Verify with: python tools/archive_firmware.py verify {destination}\n"
        )
        (destination / "README.txt").write_text(readme, encoding="utf-8")
        files["README.txt"] = artifact(destination / "README.txt", "readme")

        archive_manifest = {
            "schema_version": 1,
            "created_at": datetime.now(timezone.utc)
            .isoformat(timespec="seconds")
            .replace("+00:00", "Z"),
            "environment": env,
            "git": source_manifest.get("git"),
            "merged_image": {
                "name": image.name,
                "size": image.stat().st_size,
                "sha256": digest,
            },
            "elf_sha256": files["firmware.elf"]["sha256"],
            "map_sha256": files["firmware.map"]["sha256"],
            "artifacts": files,
        }
        (destination / "archive.json").write_text(
            json.dumps(archive_manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        verify_archive(destination)
    except Exception:
        shutil.rmtree(destination, ignore_errors=True)
        raise

    print(f"archive: {destination}")
    return destination


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create an archive")
    create.add_argument("--env", default="guachat_dev")
    create.add_argument("--image", type=Path)
    create.add_argument("--checksum", type=Path)
    create.add_argument("--manifest", type=Path)
    create.add_argument("--build-dir", type=Path)
    create.add_argument("--archive-root", type=Path, default=DEFAULT_ARCHIVE_ROOT)

    verify = subparsers.add_parser("verify", help="verify an archive directory")
    verify.add_argument("directory", type=Path)

    args = parser.parse_args(argv)
    try:
        if args.command == "create":
            default_image, default_checksum, default_manifest, default_build_dir = (
                default_paths(args.env)
            )
            image = args.image or default_image
            checksum = args.checksum or default_checksum
            manifest = args.manifest or default_manifest
            build_dir = args.build_dir or default_build_dir
            archive_root = args.archive_root
            if not archive_root.is_absolute():
                archive_root = ROOT / archive_root
            destination = create_archive(
                args.env, image, checksum, manifest, build_dir, archive_root
            )
            print("Archive: PASS")
            print(f"path: {destination}")
        else:
            info = verify_archive(args.directory)
            print(f"Archive: PASS ({info['size']} byte image)")
        return 0
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
