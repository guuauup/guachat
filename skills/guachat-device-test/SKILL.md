---
name: guachat-device-test
description: Flash and test GUACHAT firmware on real hardware after explicit user confirmation. Use when the user asks to test on device, capture serial logs, or verify sensors/radio/audio.
---

# GUACHAT device test

## When to use

- The user explicitly asks to flash or test on hardware.
- You need serial logs to diagnose a device-only issue.
- You need to verify a merged image boots.

## Consent rule

**Never flash without explicit confirmation in this session.** A successful
build or a connected USB cable is not consent. Ask first, then wait.

## Steps

1. Confirm the target:
   - board and flash size (16 MB)
   - which environment/image
   - whether NVS may be erased (do not erase by default)
   - if flashing `build/guachat-dev-full.bin`, verify it against
     `build/guachat-dev-full.manifest.json` (size and SHA256)
2. List ports:
   ```bash
   pio device list
   ```
3. Flash with confirmation:
   ```bash
   python tools/flash.py --env guachat_dev
   # or
   python tools/flash.py build/guachat-dev-full.bin --port COM9
   ```
   `tools/flash.py` asks for `yes` unless `--yes` is passed. Only use `--yes`
   when the user already authorized this exact flash.
4. Capture boot logs correctly:
   - open the serial port **before** resetting
   - toggle DTR/RTS to reset
   - read for 15–30 seconds
   - check for `rst:` loops and `[BSP] hardware init done`
5. Run the device checklist in `docs/testing.md`. Record actual readings:
   - display/touch
   - power and TF card
   - SHT30/BMP581/IMU/magnetometer/RTC
   - LoRa TX (and RX only if a second device exists)
   - GPS (outdoor; indoor may be no-fix)
   - speaker beep and mic peak
6. Report:
   ```text
   Device tests: PASS / FAIL / NOT RUN
   Unverified: ...
   ```
   Use `NOT RUN` when the device is inaccessible. Include the reason for every
   unverified item. Flashing success and clean logs do not prove display,
   touch, audio, radio, or power behavior.

## Known baseline facts

- QMC5883P may be absent on some boards; check I2C0 for 0x2C before blaming code.
- LoRa RX requires a second device; otherwise mark it unverified.
- GPS indoors usually has no fix.
- `Serial.setTxTimeoutMs(0)` drops boot output if no host is reading.

## Fallbacks

- `docs/testing.md`
- `docs/build-and-flash.md`
- `docs/troubleshooting.md`
