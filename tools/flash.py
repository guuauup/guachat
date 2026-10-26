#!/usr/bin/env python3
"""Flash a merged image or upload a PlatformIO environment, with confirmation.

Usage:
    python tools/flash.py                         # upload guachat_dev via pio
    python tools/flash.py --env guachat_dev
    python tools/flash.py build/guachat-dev-full.bin
    python tools/flash.py build/guachat-dev-full.bin --port COM9

The confirmation prompt is intentional. Do not add --yes to agent scripts unless
the user has explicitly authorized flashing in this session.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


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
    candidate = core_dir() / "packages" / "tool-esptoolpy" / "esptool.py"
    if candidate.exists():
        return str(candidate)
    raise SystemExit(f"esptool.py not found at {candidate}")


def list_ports() -> list[str]:
    try:
        from serial.tools import list_ports as lp

        return [p.device for p in lp.comports()]
    except Exception:
        return []


def confirm(message: str, assume_yes: bool) -> None:
    if assume_yes:
        return
    print(message)
    answer = input("Type 'yes' to continue: ").strip().lower()
    if answer != "yes":
        raise SystemExit("aborted")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", nargs="?", type=Path, help="merged 0x0 image")
    parser.add_argument("--env", default="guachat_dev", help="PlatformIO environment")
    parser.add_argument("--port", help="serial port, e.g. COM9 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--yes", action="store_true", help="skip confirmation")
    args = parser.parse_args()

    ports = list_ports()
    if ports:
        print("serial ports: " + ", ".join(ports))

    if args.image:
        image = args.image
        if not image.is_absolute():
            image = ROOT / image
        if not image.exists():
            raise SystemExit(f"image not found: {image}")

        confirm(
            f"About to flash {image} at offset 0x0 "
            f"(port={args.port or 'auto'}, baud={args.baud}).",
            args.yes,
        )
        cmd = [
            pio_python(),
            find_esptool(),
            "--chip",
            "esp32s3",
            "--baud",
            str(args.baud),
            "write_flash",
            "0x0",
            str(image),
        ]
        if args.port:
            cmd[2:2] = ["--port", args.port]
        print("$ " + " ".join(cmd))
        return subprocess.run(cmd, cwd=ROOT).returncode

    confirm(
        f"About to run 'pio run -e {args.env} -t upload' "
        f"(port={args.port or 'auto'}).",
        args.yes,
    )
    cmd = [find_pio(), "run", "-e", args.env, "-t", "upload"]
    if args.port:
        cmd.extend(["--upload-port", args.port])
    print("$ " + " ".join(cmd))
    return subprocess.run(cmd, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
