---
name: guachat-debug
description: Diagnose GUACHAT boot loops, panics, display corruption, I2C/SD failures, and radio/audio problems. Use when the device misbehaves or a build boots incorrectly.
---

# GUACHAT debug

## When to use

- Reboot loop, `TG0WDT_SYS_RST`, Guru Meditation, panic.
- Black screen, snow, stripes, touch not working.
- I2C devices disappear; SD card crashes; LoRa/audio fails.
- A merged image behaves differently from a `pio upload`.

## First: get the boot log

1. Open the serial port first (the firmware uses `Serial.setTxTimeoutMs(0)`).
2. Toggle DTR/RTS to reset.
3. Capture 15–30 seconds.
4. Look for:
   - `rst:0x...` and `mode:DIO` / `mode:QIO`
   - `Saved PC:`
   - the last `[BSP] ...` line before silence
   - `[BSP] hardware init done`

## Match the ELF before decoding addresses

1. Read `build/guachat-dev-full.manifest.json` and confirm the flashed image's
   SHA256 matches `merged_image.sha256`.
2. Use `.pio/build/guachat_dev/firmware.elf` only if its SHA256 matches
   `artifacts.elf.sha256` in the same manifest.
3. Decode with the matching file:
   ```bash
   ~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line \
     -pfiaC -e .pio/build/guachat_dev/firmware.elf <addresses>
   ```
4. If no matching ELF exists, report only what the log establishes and list the
   missing artifact in `Unverified`. Do not rebuild the current source and
   pretend it matches the old firmware.

## Decision table

| Symptom | Likely cause | Action |
| --- | --- | --- |
| Repeated ROM log, no app | merged image forced QIO | re-merge without `--flash_mode` |
| Boot loop after a code change | crash in init/task | read PC, bisect init order |
| Black screen, serial OK | backlight / TCA9535 / LVGL buffer | check logs, `bsp_backlight_set` |
| Snow/stripes | LoRa on SPI2 | move LoRa to SPI3 |
| Random assert in LVGL | non-UI task calls `lv_*` | queue/`lv_async_call` |
| UI stutters on USB | periodic `Serial` in `uiTask` | remove logging |
| I2C all dead | direct `Wire` or stuck bus | use Manager / `i2cBusLock` |
| TF card crash | missing `SdLockGuard` | add lock around file access |
| `QMC5883P NOT FOUND` | hardware missing / address | scan I2C, check power/solder |
| BMP581 `10.2 hPa` | hPa/Pa mix-up | BSP should multiply by 100 |
| LoRa RX never fires | RX not re-armed | call `bsp_radio_start_rx()` after read/TX |

## Rules

- Do not "fix" a failure by deleting checks or disabling the watchdog first.
- Do not change RF power/frequency unless the user asks; if you do, report it.
- Keep a minimal reproduction; remove one variable at a time.

## Fallbacks

- `docs/troubleshooting.md`
- `docs/concurrency.md`
- `docs/build-and-flash.md`
- `docs/testing.md`
