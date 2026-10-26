"""Host-side contract tests that do not need the ESP32 hardware.

Run:
    python tools/validate.py --host
"""

from __future__ import annotations

import importlib.util
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TOOLS = ROOT / "tools"


def load_merge_module():
    spec = importlib.util.spec_from_file_location(
        "merge_firmware", TOOLS / "merge_firmware.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class MergeLayoutTest(unittest.TestCase):
    def test_offsets_are_esp32s3_boot_layout(self):
        module = load_merge_module()
        self.assertEqual(module.OFFSETS["bootloader"], 0x0)
        self.assertEqual(module.OFFSETS["partitions"], 0x8000)
        self.assertEqual(module.OFFSETS["boot_app0"], 0xE000)
        self.assertEqual(module.OFFSETS["firmware"], 0x10000)

    def test_flash_size_is_16mb(self):
        module = load_merge_module()
        self.assertEqual(module.FLASH_SIZE, 16 * 1024 * 1024)


class SingleEnvironmentTest(unittest.TestCase):
    def setUp(self):
        self.text = (ROOT / "platformio.ini").read_text(encoding="utf-8")

    def test_only_one_environment(self):
        envs = re.findall(r"^\[env:([^\]]+)\]", self.text, re.M)
        self.assertEqual(envs, ["guachat_dev"])

    def test_build_points_at_single_entry(self):
        self.assertTrue((ROOT / "src/main.cpp").exists())
        self.assertFalse((ROOT / "src/dev/main_dev.cpp").exists())


class RadioBoundsTest(unittest.TestCase):
    def setUp(self):
        self.text = (ROOT / "src/bsp/bsp_pins.h").read_text(encoding="utf-8")

    def value(self, name: str) -> int:
        match = re.search(rf"#define\s+{name}\s+(-?\d+)", self.text)
        self.assertIsNotNone(match, name)
        return int(match.group(1))

    def test_frequency_range_is_within_ism_band(self):
        low = self.value("BSP_LORA_FREQ_MIN_MHZ")
        high = self.value("BSP_LORA_FREQ_MAX_MHZ")
        self.assertGreaterEqual(low, 2400)
        self.assertLessEqual(high, 2483)

    def test_power_policy_is_warning_only(self):
        self.assertEqual(self.value("BSP_LORA_POWER_RECOMMENDED_DBM"), 7)
        self.assertEqual(self.value("BSP_LORA_POWER_MAX_DBM"), 20)


class EntryContractTest(unittest.TestCase):
    def test_entry_documents_exist(self):
        for name in [
            "AGENTS.md",
            "CLAUDE.md",
            "docs/README.md",
            "docs/bsp-api.md",
        ]:
            self.assertTrue((ROOT / name).exists(), name)

    def test_agents_mentions_safety_rules(self):
        text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
        for phrase in ["bsp_pins.h", "lv_*", "烧录", "Unverified", "src/dev"]:
            self.assertIn(phrase, text)

    def test_merge_docs_warn_about_qio(self):
        text = (ROOT / "docs/build-and-flash.md").read_text(encoding="utf-8")
        self.assertIn("qio", text.lower())
        self.assertIn("DIO", text)


if __name__ == "__main__":
    unittest.main()
