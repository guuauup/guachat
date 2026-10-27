---
name: guachat-build
description: Build GUACHAT firmware, check memory budgets, and produce a 0x0 merged image with checksum. Use when compiling, validating size, or preparing a flashable deliverable.
---

# GUACHAT build

## When to use

- The user asks to compile or produce firmware.
- You need to check RAM/Flash/DROM usage.
- You need a merged image flashable from `0x0`.

## Steps

1. Run the complete gate before delivery:
   ```bash
   python tools/validate.py --all
   ```
   It runs static checks, host tests, the build, the 0x0 merge, verifies
   `full.bin` against `manifest.json` (size, SHA256, matching ELF), and runs
   `tools/verify_firmware.py` to parse the image independently.
2. For a build-only check:
   ```bash
   pio run -e guachat_dev
   ```
   `main` has only this one environment; build only the application you are
   developing.
3. Read the size report:
   - `RAM` and `Flash` lines
   - `drom0_0_seg` line
   - the dev baseline is small, but fonts and constant tables grow DROM quickly.
4. Inspect the artifacts:
   ```bash
   cat build/guachat-dev-full.bin.sha256
   cat build/guachat-dev-full.manifest.json
   ```
   The manifest binds the merged image, application, bootloader, partitions,
   ELF, and MAP with their SHA256.
5. Verify independently (the merge tool already does this unless `--no-verify`):
   ```bash
   python tools/verify_firmware.py
   python tools/verify_firmware.py --image-only --image build/guachat-dev-full.bin
   ```
   It checks chip/flash size, a DIO bootloader (QIO fails), partition table
   magic and MD5, range/overlap, the app partition, the app descriptor, the
   embedded ELF SHA256, and component bytes at `0x0/0x8000/0xE000/0x10000`.
   A matching manifest SHA256 is not a substitute.
6. Create a release archive when delivering:
   ```bash
   python tools/archive_firmware.py create --env guachat_dev
   python tools/archive_firmware.py verify dist/firmware/<sha256>
   ```
   The directory is named by the merged-image SHA256 and contains the image,
   checksum, manifest, `firmware.elf`, `firmware.map`, `README.txt`, and
   `archive.json`. It is about 32 MB; `dist/` is gitignored.
7. Report:
   - Build status (`PASS / FAIL / NOT RUN`)
   - memory percentages
   - artifact path, size, SHA256, manifest path, archive path
   - warnings (RF/wireless notices are not failures)

## Manual merge

If you only need to re-merge an existing build:

```bash
python tools/merge_firmware.py --no-build
```

Without `--no-build`, the tool builds first and then merges.

## CI

- `.github/workflows/validate.yml` runs `validate.py --static --host`.
- `.github/workflows/firmware.yml` builds `guachat_dev`, merges, verifies,
  archives, and uploads the image, checksum, manifest, restore zip, and
  `dist/firmware/**` (14-day retention).
- Gitee does not run GitHub Actions. Run the same gate locally before
  delivering from a Gitee mirror.

## Hard rules

- Flashing `full.bin` from `0x0` writes the NVS region (`0x9000`) and otadata
  (`0xE000`) but does **not** reliably erase NVS: the merged image is 0xFF-filled
  there, and a 2026-10-27 flash kept the DRV2605L calibration. To keep NVS, use
  `pio run -e guachat_dev -t upload` or write only `firmware.bin` at `0x10000`;
  to start from scratch, erase NVS explicitly first.
- Do not force `--flash_mode qio`. The bootloader header is DIO; QIO causes a
  `TG0WDT_SYS_RST` boot loop.
- Do not run `pio pkg update`; it may replace the vendored LVGL.
- Do not reintroduce UI-generator output or application business code into `main`.

## Outputs

```text
build/guachat-dev-full.bin
build/guachat-dev-full.bin.sha256
build/guachat-dev-full.manifest.json
dist/firmware/<sha256>/   # only when archiving
```

## Fallbacks

- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
