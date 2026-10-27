"""Host tests for the project skill installer."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
INSTALLER = ROOT / "tools" / "install_skills.py"
SKILL_NAMES = [
    "guachat-build",
    "guachat-debug",
    "guachat-develop",
    "guachat-device-test",
    "guachat-setup",
]


class InstallSkillsTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="guachat-skills-")
        self.addCleanup(temporary.cleanup)
        self.target = Path(temporary.name) / "skills"

    def run_installer(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(INSTALLER), "--target", str(self.target), *args],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_check_without_installation_does_not_write(self) -> None:
        result = self.run_installer("--check")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout.count("[MISSING]"), len(SKILL_NAMES))
        self.assertFalse(self.target.exists())

    def test_install_then_check(self) -> None:
        install = self.run_installer()
        self.assertEqual(install.returncode, 0, install.stderr)
        for name in SKILL_NAMES:
            self.assertTrue((self.target / name / "SKILL.md").is_file(), name)

        check = self.run_installer("--check")
        self.assertEqual(check.returncode, 0, check.stderr)
        self.assertEqual(check.stdout.count("[OK]"), len(SKILL_NAMES))

    def test_repeated_install_is_idempotent(self) -> None:
        self.assertEqual(self.run_installer().returncode, 0)
        second = self.run_installer()
        self.assertEqual(second.returncode, 0)
        self.assertIn("[SKIP]", second.stdout)
        self.assertEqual(self.run_installer("--check").returncode, 0)

    def test_force_restores_modified_skill(self) -> None:
        self.assertEqual(self.run_installer().returncode, 0)
        installed = self.target / "guachat-build" / "SKILL.md"
        installed.write_text("modified\n", encoding="utf-8")
        self.assertEqual(self.run_installer("--force").returncode, 0)
        self.assertNotEqual(installed.read_text(encoding="utf-8"), "modified\n")

    def test_target_that_is_a_file_is_rejected(self) -> None:
        self.target.parent.mkdir(parents=True, exist_ok=True)
        self.target.write_text("not a directory\n", encoding="utf-8")
        result = self.run_installer("--check")
        self.assertEqual(result.returncode, 1)
        self.assertIn("not a directory", result.stdout)


if __name__ == "__main__":
    unittest.main()
