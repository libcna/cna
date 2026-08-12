# SPDX-License-Identifier: MS-PL
"""Command-line entry point for the CNA glTF conformance-fixture generator (`GLTF-003`).

Run from the repository root::

    PYTHONPATH=tools python3 -m gltf_fixtures --out tests/assets/gltf
    PYTHONPATH=tools python3 -m gltf_fixtures --check tests/assets/gltf
    PYTHONPATH=tools python3 -m gltf_fixtures --list

or, for the two ordinary cases, through the wrapper that also verifies the result and reports
whether the working tree changed (`GLTF-020`)::

    scripts/regenerate-gltf-goldens.sh            # regenerate, then verify
    scripts/regenerate-gltf-goldens.sh --check    # verify only -- what CI runs

Generation is deterministic: regenerating an unchanged tree produces a zero diff, which is what
``--check`` asserts and what the corpus tests rely on.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .corpus import all_fixtures, emit
from .manifest import dumps


def _write(out_dir: Path, files: dict[str, bytes]) -> tuple[int, list[str]]:
    out_dir.mkdir(parents=True, exist_ok=True)
    removed: list[str] = []
    generated = set(files)
    for existing in sorted(out_dir.iterdir()):
        if existing.is_file() and existing.name not in generated:
            existing.unlink()
            removed.append(existing.name)
    changed = 0
    for name, data in sorted(files.items()):
        target = out_dir / name
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
            changed += 1
    return changed, removed


def _check(out_dir: Path, files: dict[str, bytes]) -> list[str]:
    problems: list[str] = []
    if not out_dir.is_dir():
        return [f"{out_dir}: directory does not exist"]
    on_disk = {p.name for p in out_dir.iterdir() if p.is_file()}
    for name in sorted(set(files) - on_disk):
        problems.append(f"{name}: missing from {out_dir}")
    for name in sorted(on_disk - set(files)):
        problems.append(f"{name}: present in {out_dir} but not generated (stale)")
    for name in sorted(set(files) & on_disk):
        actual = (out_dir / name).read_bytes()
        if actual != files[name]:
            problems.append(
                f"{name}: differs from the generator output "
                f"({len(actual)} bytes on disk, {len(files[name])} bytes generated)")
    return problems


def main(argv: list[str] | None = None) -> int:
    """Parses arguments and runs the requested action; returns the process exit code."""
    parser = argparse.ArgumentParser(
        prog="gltf_fixtures",
        description="Generate the CNA glTF 2.0 conformance fixture corpus and its manifests.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--out", metavar="DIR", help="write the corpus into DIR")
    group.add_argument("--check", metavar="DIR",
                       help="verify DIR is byte-identical to the generator output")
    group.add_argument("--list", action="store_true",
                       help="print the corpus manifest to stdout without writing anything")
    group.add_argument("--explain", metavar="GOLDEN",
                       help="decode how an L5 golden differs from --against, using the fixture's "
                            "own layout (GLTF-410)")
    parser.add_argument("--against", metavar="FILE",
                        help="the bytes --explain compares GOLDEN with (usually the committed "
                             "version, extracted with 'git show HEAD:<path>')")
    args = parser.parse_args(argv)

    if args.explain:
        if not args.against:
            sys.stderr.write("gltf_fixtures: --explain also needs --against FILE\n")
            return 2
        from .explain import explain
        for line in explain(Path(args.explain), Path(args.against).read_bytes()):
            sys.stdout.write(line + "\n")
        return 0

    fixtures = all_fixtures()
    files = emit(fixtures)

    if args.list:
        import json
        sys.stdout.write(dumps(json.loads(files["manifest.json"].decode("utf-8"))))
        return 0

    if args.check:
        problems = _check(Path(args.check), files)
        for problem in problems:
            sys.stderr.write(f"gltf_fixtures: {problem}\n")
        if problems:
            sys.stderr.write(
                f"gltf_fixtures: {len(problems)} problem(s); regenerate with "
                f"'PYTHONPATH=tools python3 -m gltf_fixtures --out {args.check}'\n")
            return 1
        sys.stdout.write(
            f"gltf_fixtures: {len(fixtures)} assets, {len(files)} files -- byte-identical\n")
        return 0

    changed, removed = _write(Path(args.out), files)
    sys.stdout.write(
        f"gltf_fixtures: {len(fixtures)} assets, {len(files)} files -> {args.out} "
        f"({changed} written, {len(removed)} stale removed)\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
