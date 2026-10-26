#!/usr/bin/env python3
"""Install the repository's Agent Skills into a skills directory.

Usage:
    python tools/install_skills.py                    # -> .claude/skills
    python tools/install_skills.py --target ~/.claude/skills
    python tools/install_skills.py --print            # only print paths

Claude Code loads project skills from `.claude/skills`. Other agents may use a
different directory; point --target at whatever they support, or copy the
folders manually.
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKILLS = ROOT / "skills"


def skill_dirs() -> list[Path]:
    if not SKILLS.exists():
        return []
    return sorted(p for p in SKILLS.iterdir() if p.is_dir() and (p / "SKILL.md").exists())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--target",
        default=".claude/skills",
        help="destination directory (relative paths are from the repo root)",
    )
    parser.add_argument(
        "--print",
        action="store_true",
        help="only print the source and destination paths",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="overwrite existing skill directories",
    )
    args = parser.parse_args()

    dirs = skill_dirs()
    if not dirs:
        print(f"no skills found under {SKILLS}")
        return 1

    target = Path(args.target).expanduser()
    if not target.is_absolute():
        target = ROOT / target

    for source in dirs:
        destination = target / source.name
        if args.print:
            print(f"{source} -> {destination}")
            continue
        if destination.exists():
            if not args.force:
                print(f"[SKIP] {destination} exists (use --force to overwrite)")
                continue
            shutil.rmtree(destination)
        shutil.copytree(source, destination)
        print(f"[OK]   {source.name} -> {destination}")

    if not args.print:
        print()
        print(f"Installed {len(dirs)} skill(s) into {target}")
        print("Restart or reload the agent so it picks up the new skills.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
