#!/usr/bin/env python3
"""Merge a PlatformIO ESP32-S3 build into a 0x0 full-flash image.

Usage:
    python tools/merge_firmware.py
    python tools/merge_firmware.py --env guachat_dev -o build/full.bin
    python tools/merge_firmware.py --no-build

The bootloader header produced by PlatformIO is DIO. Do NOT force
``--flash_mode qio`` here: doing so causes a TG0WDT_SYS_RST boot loop on
this board. We deliberately leave the flash mode unchanged.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
BUILD_DIR = ROOT / "build"

OFFSETS = {
    "bootloader": 0x0,
    "partitions": 0x8000,
    "boot_app0": 0xE000,
    "firmware": 0x10000,
}

FLASH_SIZE = 16 * 1024 * 1024


def core_dir() -> Path:
    return Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))


def pio_python() -> str:
    core = core_dir()
    candidate = core / "penv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    return str(candidate) if candidate.exists() else sys.executable


def find_pio() -> str:
    exe = shutil.which("pio")
    if exe:
        return exe
    core = core_dir()
    candidate = core / "penv" / ("Scripts/pio.exe" if os.name == "nt" else "bin/pio")
    if candidate.exists():
        return str(candidate)
    raise SystemExit("pio not found; install PlatformIO Core first")


def find_esptool() -> str:
    packages = core_dir() / "packages"
    candidates = sorted(packages.glob("tool-esptoolpy*/esptool.py"))
    if candidates:
        return str(candidates[0])
    raise SystemExit(
        f"esptool.py not found under {packages}; run a PlatformIO build first"
    )


def find_boot_app0() -> Path:
    packages = core_dir() / "packages"
    candidates = sorted(
        packages.glob("framework-arduinoespressif32*/tools/partitions/boot_app0.bin")
    )
    if candidates:
        return candidates[0]
    raise SystemExit(
        f"boot_app0.bin not found under {packages}; install the Arduino framework first"
    )


def default_output(env: str) -> Path:
    name = "guachat-dev-full.bin" if env == "guachat_dev" else f"{env}-full.bin"
    return BUILD_DIR / name


def run(cmd: list[str]) -> None:
    print("$ " + " ".join(str(c) for c in cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def verify_image(output: Path, env: str) -> None:
    """Run the independent image verifier on the merged result."""
    cmd = [
        sys.executable,
        str(TOOLS / "verify_firmware.py"),
        "--env",
        env,
        "--image",
        str(output),
        "--build-dir",
        str(ROOT / ".pio" / "build" / env),
        "--boot-app0",
        str(find_boot_app0()),
    ]
    run(cmd)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_state() -> dict[str, object]:
    def git(*args: str) -> str:
        try:
            proc = subprocess.run(
                ["git", *args],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError:
            return ""
        return proc.stdout.strip() if proc.returncode == 0 else ""

    return {
        "commit": git("rev-parse", "HEAD") or None,
        "dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
    }


def display_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def artifact(path: Path, role: str) -> dict[str, object]:
    return {
        "role": role,
        "path": display_path(path),
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def optional_artifact(path: Path, role: str) -> dict[str, object] | None:
    return artifact(path, role) if path.exists() else None


def write_manifest(
    output: Path,
    checksum: Path,
    env: str,
    files: dict[str, Path],
) -> Path:
    env_build = ROOT / ".pio" / "build" / env
    manifest = {
        "schema": 1,
        "generated_at": datetime.now(timezone.utc)
        .isoformat(timespec="seconds")
        .replace("+00:00", "Z"),
        "environment": env,
        "git": git_state(),
        "flash": {
            "chip": "esp32s3",
            "size_bytes": FLASH_SIZE,
            "freq": "80m",
            "mode": "unchanged (bootloader DIO)",
        },
        "offsets": {key: hex(value) for key, value in OFFSETS.items()},
        "merged_image": {
            **artifact(output, "merged"),
            "checksum_path": display_path(checksum),
        },
        "artifacts": {
            "application": artifact(files["firmware"], "application"),
            "bootloader": artifact(files["bootloader"], "bootloader"),
            "partitions": artifact(files["partitions"], "partitions"),
            "boot_app0": artifact(files["boot_app0"], "boot_app0"),
            "elf": optional_artifact(env_build / "firmware.elf", "elf"),
            "map": optional_artifact(env_build / "firmware.map", "map"),
        },
    }
    path = output.with_suffix(".manifest.json")
    path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return path


def make_zip(
    image: Path,
    checksum: Path,
    manifest: Path,
    env: str,
) -> Path:
    restore = BUILD_DIR / f"{image.stem}-restore.zip"
    readme = (
        f"GUACHAT AI firmware restore package\n"
        f"Environment: {env}\n"
        f"Image: {image.name}\n"
        f"SHA256: {checksum.read_text(encoding='utf-8').split()[0]}\n\n"
        "Flash from offset 0x0:\n"
        f"  python tools/flash.py {image.name}\n\n"
        "WARNING: flashing at 0x0 writes the NVS partition (0x9000) and\n"
        "otadata (0xE000), but does not reliably erase NVS: the merged image\n"
        "is 0xFF-filled there. Existing settings/calibration may survive.\n"
        "To keep NVS, upload only the application (pio run -t upload);\n"
        "to start clean, erase NVS explicitly first.\n\n"
        "Artifacts and hashes are recorded in the included manifest JSON.\n"
        "Do not force --flash_mode qio when re-merging; the bootloader header is DIO.\n"
    )
    with zipfile.ZipFile(restore, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(image, image.name)
        archive.write(checksum, checksum.name)
        archive.write(manifest, manifest.name)
        archive.writestr("README.txt", readme)
    return restore


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", default="guachat_dev", help="PlatformIO environment")
    parser.add_argument("-o", "--output", type=Path, help="output merged image")
    parser.add_argument("--no-build", action="store_true", help="use existing build output")
    parser.add_argument("--no-verify", action="store_true", help="skip independent image verification")
    parser.add_argument("--zip", action="store_true", help="also create a restore zip")
    args = parser.parse_args()

    env = args.env
    env_build = ROOT / ".pio" / "build" / env

    if not args.no_build:
        run([find_pio(), "run", "-e", env])

    files = {
        "bootloader": env_build / "bootloader.bin",
        "partitions": env_build / "partitions.bin",
        "firmware": env_build / "firmware.bin",
        "boot_app0": find_boot_app0(),
    }
    missing = [str(p) for p in files.values() if not p.exists()]
    if missing:
        raise SystemExit("missing build artifact(s):\n  " + "\n  ".join(missing))

    output = args.output or default_output(env)
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        pio_python(),
        find_esptool(),
        "--chip",
        "esp32s3",
        "merge_bin",
        "-o",
        str(output),
        "--flash_freq",
        "80m",
        "--flash_size",
        "16MB",
    ]
    for key in ("bootloader", "partitions", "boot_app0", "firmware"):
        cmd.extend([hex(OFFSETS[key]), str(files[key])])

    run(cmd)

    size = output.stat().st_size
    if size > FLASH_SIZE:
        raise SystemExit(f"merged image is {size} bytes, larger than 16 MB flash")
    if size < 0x20000:
        raise SystemExit(f"merged image is suspiciously small: {size} bytes")

    digest = sha256_file(output)
    checksum = output.with_suffix(output.suffix + ".sha256")
    checksum.write_text(f"{digest}  {output.name}\n", encoding="utf-8")

    manifest = write_manifest(output, checksum, env, files)

    if not args.no_verify:
        verify_image(output, env)

    print()
    print(f"image   : {output}")
    print(f"size    : {size} bytes")
    print(f"sha256  : {digest}")
    print(f"manifest: {manifest}")

    if args.zip:
        restore = make_zip(output, checksum, manifest, env)
        print(f"zip     : {restore}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
