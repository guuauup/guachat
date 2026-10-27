#!/usr/bin/env python3
"""Project validation gate for GUACHAT AI.

Usage:
    python tools/validate.py --static
    python tools/validate.py --host
    python tools/validate.py --firmware
    python tools/validate.py --all

Exit code is non-zero only for real failures. RF and wireless checks are
notices (warnings), because users may deliberately enable WiFi/BT/ESP-NOW on
their own hardware and are responsible for compliance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"


class Report:
    def __init__(self) -> None:
        self.failures = 0
        self.warnings = 0
        self.checks = 0

    def ok(self, message: str) -> None:
        self.checks += 1
        print(f"[OK]   {message}")

    def warn(self, message: str) -> None:
        self.checks += 1
        self.warnings += 1
        print(f"[WARN] {message}")

    def fail(self, message: str) -> None:
        self.checks += 1
        self.failures += 1
        print(f"[FAIL] {message}")

    def skip(self, message: str) -> None:
        self.checks += 1
        print(f"[SKIP] {message}")

    def summary(self, title: str) -> int:
        print()
        print(f"{title}: {self.checks} checks, "
              f"{self.failures} failure(s), {self.warnings} warning(s)")
        return 1 if self.failures else 0


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


# ---------------------------------------------------------------- static checks

REQUIRED_PATHS = [
    "AGENTS.md",
    "CLAUDE.md",
    "README.md",
    "docs/README.md",
    "docs/bsp-api.md",
    "docs/build-and-flash.md",
    "docs/testing.md",
    "src/bsp/bsp_pins.h",
    "src/bsp/bsp_haptic.h",
    "src/bsp/bsp_haptic.cpp",
    "src/main.cpp",
    "platformio.ini",
    "partitions_16MB_app.csv",
    "skills/README.md",
    "tools/install_skills.py",
]

REQUIRED_PINS = [
    "BSP_LCD_WIDTH",
    "BSP_LCD_HEIGHT",
    "BSP_I2C0_SDA",
    "BSP_I2C0_SCL",
    "BSP_SD_PIN_CLK",
    "BSP_LORA_PIN_NSS",
    "BSP_GPS_BAUD",
    "BSP_LORA_FREQ_MIN_MHZ",
    "BSP_LORA_FREQ_MAX_MHZ",
    "BSP_LORA_POWER_RECOMMENDED_DBM",
    "BSP_LORA_POWER_MAX_DBM",
    "BSP_I2C_ADDR_DRV2605L",
    "BSP_HAPTIC_BOOT_EFFECT",
]


def check_required_files(report: Report) -> None:
    missing = [p for p in REQUIRED_PATHS if not (ROOT / p).exists()]
    if missing:
        report.fail("missing required file(s): " + ", ".join(missing))
    else:
        report.ok("required entry files and BSP sources exist")


def check_build_filters(report: Report) -> None:
    text = read(ROOT / "platformio.ini")
    envs = re.findall(r"^\[env:([^\]]+)\]", text, re.M)
    if envs == ["guachat_dev"]:
        report.ok("platformio.ini has exactly one environment: guachat_dev")
    else:
        report.fail(
            "platformio.ini must contain only [env:guachat_dev]; found: "
            + (", ".join(envs) if envs else "none")
        )


def check_pins(report: Report) -> None:
    text = read(ROOT / "src/bsp/bsp_pins.h")
    missing = [name for name in REQUIRED_PINS if name not in text]
    if missing:
        report.fail("bsp_pins.h missing constants: " + ", ".join(missing))
    else:
        report.ok("bsp_pins.h contains required hardware constants")


APP_SCAN_PATHS = ["src/dev", "src/main.cpp"]
DIRECT_HARDWARE_PATTERNS = [
    (r"\bdigitalWrite\s*\(", "digitalWrite"),
    (r"\bpinMode\s*\(", "pinMode"),
    (r"\bWire\s*\.", "Wire."),
    (r"\bSPI\s*\.", "SPI."),
    (r"\bSD\s*\.\s*begin", "SD.begin"),
    (r"\bi2s_[a-z_]+\s*\(", "i2s_*"),
]


def check_app_boundaries(report: Report) -> None:
    hits: list[str] = []
    paths: list[Path] = []
    for item in APP_SCAN_PATHS:
        target = ROOT / item
        if target.is_file():
            paths.append(target)
        elif target.is_dir():
            paths.extend(target.rglob("*"))

    for path in paths:
        if not path.is_file():
            continue
        if path.suffix.lower() not in {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}:
            continue
        text = read(path)
        for pattern, label in DIRECT_HARDWARE_PATTERNS:
            if re.search(pattern, text):
                hits.append(f"{path.relative_to(ROOT)}: {label}")
    if hits:
        report.warn(
            "application code touches hardware directly (prefer BSP): "
            + "; ".join(hits[:8])
        )
    else:
        report.ok("application code uses BSP APIs (no direct GPIO/Wire/SPI/SD/I2S)")


def parse_define_int(text: str, name: str) -> int | None:
    match = re.search(rf"#define\s+{re.escape(name)}\s+(-?\d+)", text)
    return int(match.group(1)) if match else None


def check_rf(report: Report) -> None:
    text = read(ROOT / "src/bsp/bsp_pins.h")
    fmin = parse_define_int(text, "BSP_LORA_FREQ_MIN_MHZ")
    fmax = parse_define_int(text, "BSP_LORA_FREQ_MAX_MHZ")
    power_max = parse_define_int(text, "BSP_LORA_POWER_MAX_DBM")
    power_rec = parse_define_int(text, "BSP_LORA_POWER_RECOMMENDED_DBM")

    if None in (fmin, fmax, power_max, power_rec):
        report.warn("cannot parse LoRa frequency/power constants from bsp_pins.h")
        return

    if not (2400 <= fmin <= fmax <= 2483):
        report.warn(
            f"LoRa frequency range {fmin}-{fmax} MHz is outside 2400-2483.5 MHz; "
            "user modifications are allowed but compliance is the user's responsibility"
        )
    else:
        report.ok(f"LoRa frequency range {fmin}-{fmax} MHz is within 2.4 GHz ISM")

    if power_max > 20:
        report.warn(
            f"LoRa hardware power ceiling {power_max} dBm exceeds the "
            "E28-2G4M20S 20 dBm module rating; check the hardware and regulations"
        )
    else:
        report.ok(
            f"LoRa hardware power ceiling {power_max} dBm is within the module rating"
        )

    if power_rec > 7:
        report.warn(
            f"LoRa recommended power {power_rec} dBm exceeds the only WiFi-coexistence "
            "value tested (7 dBm); firmware will warn but not block the user"
        )
    elif power_rec > power_max:
        report.warn(
            f"LoRa recommended power {power_rec} dBm exceeds hardware ceiling "
            f"{power_max} dBm"
        )
    else:
        report.ok(
            f"LoRa recommended power {power_rec} dBm; higher values are warning-only"
        )


WIRELESS_PATTERNS = [
    r"#\s*include\s*[<\"]WiFi\.h[>\"]",
    r"\besp_wifi\b",
    r"\besp_now\b",
    r"\bWebServer\b",
    r"\bHTTPClient\b",
    r"#\s*include\s*[<\"]BluetoothSerial\.h[>\"]",
    r"#\s*include\s*[<\"]BLEDevice\.h[>\"]",
    r"#\s*include\s*[<\"]BLE\.h[>\"]",
    r"\bBLEDevice\b",
    r"\bBLEServer\b",
    r"\besp_bt\b",
    r"\besp_ble\b",
    r"\bNimBLEDevice\b",
]


def check_wireless_notice(report: Report) -> None:
    hits: list[str] = []
    for directory in ["src"]:
        for path in (ROOT / directory).rglob("*"):
            if path.suffix.lower() not in {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}:
                continue
            text = read(path)
            for pattern in WIRELESS_PATTERNS:
                if re.search(pattern, text):
                    hits.append(str(path.relative_to(ROOT)))
                    break
    if hits:
        report.warn(
            "wireless feature detected in: " + ", ".join(sorted(set(hits)))
            + " — this is allowed, but you are responsible for RF compliance"
        )
    else:
        report.ok("no WiFi/BLE/ESP-NOW usage detected (stock configuration)")


LINK_RE = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
FENCE_RE = re.compile(r"```.*?```", re.S)
PROJECT_SCAN_PATHS = [
    "README.md",
    "AGENTS.md",
    "CLAUDE.md",
    "THIRD_PARTY_NOTICES.md",
    "platformio.ini",
    "partitions_16MB_app.csv",
    "docs",
    "skills",
    ".github",
    "src",
    "tools",
]
SECRET_PATTERNS = [
    ("GitHub token", re.compile(r"(?:ghp_|github_pat_)[A-Za-z0-9_]{20,}")),
    ("AWS access key", re.compile(r"AKIA[0-9A-Z]{16}")),
    ("private key", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----")),
]
CONFLICT_RE = re.compile(r"(?m)^(?:<<<<<<< |=======\s*$|>>>>>>> )")


def project_files() -> list[Path]:
    """Return project-owned text files without scanning vendored lib/ directories."""
    files: list[Path] = []
    for item in PROJECT_SCAN_PATHS:
        target = ROOT / item
        if target.is_file():
            files.append(target)
        elif target.is_dir():
            files.extend(
                path
                for path in sorted(target.rglob("*"))
                if path.is_file()
                and "__pycache__" not in path.parts
                and path.stat().st_size <= 2 * 1024 * 1024
            )
    return files


def check_doc_links(report: Report) -> None:
    files = [ROOT / "README.md", ROOT / "AGENTS.md", ROOT / "CLAUDE.md"]
    for directory in ("docs", "skills", ".github"):
        base = ROOT / directory
        if base.is_dir():
            files.extend(sorted(base.rglob("*.md")))
    broken: list[str] = []
    for path in files:
        if not path.exists():
            continue
        text = FENCE_RE.sub("", read(path))
        for raw_target in LINK_RE.findall(text):
            target = raw_target.strip()
            if target.startswith("<") and target.endswith(">"):
                target = target[1:-1].strip()
            else:
                target = target.split(maxsplit=1)[0] if target.split() else ""
            if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            if target.startswith("/"):
                continue
            local = target.split("#", 1)[0]
            if not local or "*" in local or "{" in local:
                continue
            resolved = (path.parent / local).resolve()
            if not resolved.exists():
                broken.append(f"{path.relative_to(ROOT)} -> {target}")
    if broken:
        report.fail("broken documentation link(s):\n  " + "\n  ".join(broken))
    else:
        report.ok("documentation links resolve")


def check_secrets(report: Report) -> None:
    hits: list[str] = []
    for path in project_files():
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for label, pattern in SECRET_PATTERNS:
            if pattern.search(text):
                hits.append(f"{path.relative_to(ROOT)}: possible {label}")
    if hits:
        report.fail("possible secret(s):\n  " + "\n  ".join(hits))
    else:
        report.ok("no high-confidence secret patterns found")


def check_conflict_markers(report: Report) -> None:
    hits: list[str] = []
    for path in project_files():
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        if CONFLICT_RE.search(text):
            hits.append(str(path.relative_to(ROOT)))
    if hits:
        report.fail("unresolved merge conflict marker(s): " + ", ".join(hits))
    else:
        report.ok("no unresolved merge conflict markers")


def check_action_pins(report: Report) -> None:
    workflow_dir = ROOT / ".github" / "workflows"
    problems: list[str] = []
    workflows = sorted(workflow_dir.glob("*.y*ml")) if workflow_dir.is_dir() else []
    for path in workflows:
        for number, line in enumerate(read(path).splitlines(), 1):
            match = re.match(r"\s*-?\s*uses:\s*['\"]?([^'\"\s#]+)", line)
            if not match:
                continue
            action = match.group(1)
            if action.startswith("./"):
                continue
            if action.startswith("docker://"):
                if "@sha256:" not in action:
                    problems.append(f"{path.name}:{number}: unpinned Docker action")
                continue
            if not re.search(r"@[0-9a-f]{40}$", action):
                problems.append(f"{path.name}:{number}: action must use a full commit SHA")
    if problems:
        report.fail("unpinned GitHub Actions: " + "; ".join(problems))
    elif not workflows:
        report.warn("no GitHub workflow files found")
    else:
        report.ok(f"all GitHub Actions in {len(workflows)} workflow(s) are SHA-pinned")


def check_dependency_pins(report: Report) -> None:
    text = read(ROOT / "platformio.ini")
    platform = re.search(r"^platform\s*=\s*(\S+)(?:\s*@\s*(\S+))?", text, re.M)
    if not platform or not platform.group(2):
        report.fail("platformio.ini platform must pin an exact version")
        return

    entries: list[str] = []
    in_deps = False
    for raw in text.splitlines():
        stripped = raw.strip()
        if stripped.startswith("lib_deps"):
            in_deps = True
            continue
        if not in_deps:
            continue
        if not stripped or stripped.startswith(";"):
            continue
        if raw[0] not in " \t":
            break
        entries.append(stripped)

    problems = [
        item
        for item in entries
        if not re.search(r"@\d+\.\d+\.\d+(?:\+[A-Za-z0-9._-]+)?$", item)
        or re.search(r"@[\^~<>=]", item)
    ]
    if not entries:
        report.fail("platformio.ini has no lib_deps entries")
    elif problems:
        report.fail(
            "lib_deps must pin exact versions (no ^/~ ranges): "
            + "; ".join(problems)
        )
    else:
        report.ok(f"{len(entries)} direct librar(ies) pin exact versions")


CJK_RE = re.compile(r"[㐀-䶿一-鿿　-〿＀-￯]")
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def check_fonts(report: Report) -> bool:
    used: set[str] = set()
    for path in (ROOT / "src/dev").rglob("*"):
        if path.suffix.lower() not in {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}:
            continue
        if path.name.startswith(("font_", "ui_font_")):
            continue
        used |= set(CJK_RE.findall(COMMENT_RE.sub("", read(path))))
    if not used:
        report.ok("no CJK strings in src/dev (default UI is English)")
        return True

    fonts: list[Path] = []
    for directory in ("src/dev", "src/bsp", "src/fonts"):
        base = ROOT / directory
        if not base.is_dir():
            continue
        fonts.extend(base.glob("font_*.c"))
        fonts.extend(base.glob("ui_font_*.c"))
    if not fonts:
        report.fail(
            f"{len(used)} CJK character(s) used in src/dev but no font file found; "
            "see docs/fonts.md"
        )
        return False

    covered: set[str] = set()
    for font in fonts:
        covered |= {chr(int(m, 16)) for m in re.findall(r"/\*\s*U\+([0-9A-Fa-f]{4,6})", read(font))}
    if not covered:
        report.warn("CJK text found, but font glyph comments could not be parsed")
        return True

    missing = sorted(used - covered)
    if missing:
        report.fail(
            "CJK character(s) missing from font: "
            + " ".join(missing)
            + " — regenerate the subset (docs/fonts.md)"
        )
        return False
    report.ok(f"all {len(used)} CJK characters are covered by {len(fonts)} font file(s)")
    return True


FRONTMATTER_RE = re.compile(r"\A---\s*\n(.*?)\n---", re.S)


def check_skills(report: Report) -> None:
    root = ROOT / "skills"
    dirs = sorted(p for p in root.iterdir() if p.is_dir()) if root.exists() else []
    if not dirs:
        report.fail("skills/ has no skill directories")
        return

    problems: list[str] = []
    for directory in dirs:
        for filename in ("SKILL.md", "SKILL.zh_CN.md"):
            if not (directory / filename).exists():
                problems.append(f"{directory.name}: missing {filename}")
        main = directory / "SKILL.md"
        if not main.exists():
            continue
        match = FRONTMATTER_RE.match(read(main))
        if not match:
            problems.append(f"{directory.name}: SKILL.md has no frontmatter")
            continue
        name = re.search(r"^name:\s*(\S+)\s*$", match.group(1), re.M)
        if not name:
            problems.append(f"{directory.name}: frontmatter missing name")
        elif name.group(1) != directory.name:
            problems.append(
                f"{directory.name}: frontmatter name is '{name.group(1)}'"
            )

    if problems:
        report.fail("skill package problem(s): " + "; ".join(problems))
    else:
        report.ok(f"{len(dirs)} skill package(s) have frontmatter and bilingual files")


def check_gitignore(report: Report) -> None:
    path = ROOT / ".gitignore"
    if not path.exists():
        report.fail(".gitignore is missing")
        return
    text = read(path)
    expected = [".pio", "build", "dist"]
    missing = [item for item in expected if item not in text]
    if missing:
        report.warn(".gitignore does not mention: " + ", ".join(missing))
    else:
        report.ok(".gitignore covers build artifacts")


def static_checks() -> int:
    report = Report()
    print("== static checks ==")
    check_required_files(report)
    check_build_filters(report)
    check_pins(report)
    check_skills(report)
    check_app_boundaries(report)
    check_gitignore(report)
    check_rf(report)
    check_wireless_notice(report)
    check_fonts(report)
    check_doc_links(report)
    check_secrets(report)
    check_conflict_markers(report)
    check_action_pins(report)
    check_dependency_pins(report)
    return report.summary("static")


# ---------------------------------------------------------------- host tests

def host_checks() -> int:
    print("== host tests ==")
    tests = ROOT / "tools" / "host_tests"
    if not tests.exists() or not any(tests.glob("test_*.py")):
        print("[SKIP] no host tests under tools/host_tests")
        return 0
    cmd = [sys.executable, "-m", "unittest", "discover", "-s", str(tests), "-p", "test_*.py", "-v"]
    print("$ " + " ".join(cmd))
    proc = subprocess.run(cmd, cwd=ROOT)
    return proc.returncode


# ---------------------------------------------------------------- firmware

def firmware_checks(env: str) -> int:
    print("== firmware build + merge ==")
    merge = TOOLS / "merge_firmware.py"
    if not merge.exists():
        print("[FAIL] tools/merge_firmware.py is missing")
        return 1
    cmd = [sys.executable, str(merge), "--env", env]
    print("$ " + " ".join(cmd))
    proc = subprocess.run(cmd, cwd=ROOT)
    if proc.returncode != 0:
        return proc.returncode

    name = "guachat-dev-full.bin" if env == "guachat_dev" else f"{env}-full.bin"
    image = ROOT / "build" / name
    checksum = image.with_suffix(image.suffix + ".sha256")
    manifest = image.with_suffix(".manifest.json")
    if not image.exists() or not checksum.exists() or not manifest.exists():
        print(
            "[FAIL] expected artifacts missing: "
            f"{image}, {checksum}, {manifest}"
        )
        return 1

    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"[FAIL] manifest is unreadable: {manifest} ({exc})")
        return 1

    merged = data.get("merged_image") or {}
    if merged.get("sha256") != sha256_file(image):
        print(f"[FAIL] manifest SHA256 does not match {image}")
        return 1
    if merged.get("size") != image.stat().st_size:
        print(f"[FAIL] manifest size does not match {image}")
        return 1

    elf = (data.get("artifacts") or {}).get("elf")
    if not elf or not elf.get("sha256"):
        print("[FAIL] manifest has no ELF; crash symbolization is unavailable")
        return 1

    print(f"[OK]   merged image: {image} ({image.stat().st_size} bytes)")
    print(f"[OK]   manifest: {manifest}")
    print(f"[OK]   matching ELF: {elf.get('path')}")
    return 0


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--static", action="store_true", help="run static checks")
    parser.add_argument("--host", action="store_true", help="run host tests")
    parser.add_argument("--firmware", action="store_true", help="build merged firmware")
    parser.add_argument("--all", action="store_true", help="static + host + firmware")
    parser.add_argument("--env", default="guachat_dev", help="environment for --firmware")
    args = parser.parse_args()

    if not any([args.static, args.host, args.firmware, args.all]):
        args.static = True
        args.host = True

    status = 0
    if args.static or args.all:
        status |= static_checks()
    if args.host or args.all:
        status |= host_checks()
    if args.firmware or args.all:
        status |= firmware_checks(args.env)

    print()
    print("RESULT:", "PASS" if status == 0 else "FAIL")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
