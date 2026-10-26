---
name: guachat-setup
description: Prepare a clean machine to build GUACHAT firmware. Use when PlatformIO, Python, toolchains, or first build are missing.
---

# GUACHAT setup

## When to use

- Fresh clone, new machine, or CI runner.
- `pio` is missing or the first build fails on toolchain download.
- The user asks "how do I build this".

## Steps

1. Check tools:
   ```bash
   python --version
   pio --version
   git --version
   ```
2. Install PlatformIO Core if needed:
   ```bash
   python -m pip install -U platformio
   ```
3. Build the development baseline:
   ```bash
   pio run -e guachat_dev
   ```
   First run downloads `platform-espressif32 @ 6.5.0`, the Arduino framework,
   esptool, and `lib_deps`.
4. Verify the entry contract is present:
   ```bash
   ls AGENTS.md docs/README.md src/bsp/bsp_pins.h
   ```
5. Report:
   - PlatformIO / Python versions
   - build result and RAM/Flash usage
   - what was downloaded
   - any missing USB driver

## Notes

- `lib/lvgl` is a vendored, customized LVGL 8.3. Do **not** run
  `pio pkg update` or replace it from the registry.
- The project has no fixed `upload_port`; detect ports with `pio device list`.
- On Windows, ESP32-S3 native USB usually needs no driver.

## Fallbacks

- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
