#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Require every XNA oracle scene to have exactly one same-stem PNG reference."""

from __future__ import annotations

import argparse
from pathlib import Path


def relative_stems(root: Path, suffix: str) -> set[str]:
    return {
        path.relative_to(root).with_suffix("").as_posix()
        for path in root.rglob(f"*{suffix}")
        if path.is_file()
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    repo_root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--scenes", type=Path, default=repo_root / "tools/xna-oracle/scenes"
    )
    parser.add_argument(
        "--references", type=Path, default=repo_root / "tools/xna-oracle/reference"
    )
    args = parser.parse_args()

    scenes = relative_stems(args.scenes, ".scene")
    references = relative_stems(args.references, ".png")
    missing_references = sorted(scenes - references)
    orphan_references = sorted(references - scenes)

    if missing_references:
        print("oracle manifest: scenes missing reference PNGs:")
        for name in missing_references:
            print(f"  {name}.scene")
    if orphan_references:
        print("oracle manifest: reference PNGs missing scenes:")
        for name in orphan_references:
            print(f"  {name}.png")
    if missing_references or orphan_references:
        return 1

    active = sum("/" not in name for name in scenes)
    auxiliary = len(scenes) - active
    print(
        f"oracle manifest: {active} active and {auxiliary} auxiliary scene/reference pairs"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
