#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Describe one built C ABI artifact precisely enough that a consumer can tell two apart.

plans/plan_cabi.md CABI-11. The language bindings currently pin a hand-retained experimental
library by revision and hash written down in prose. That answers "which file is this" and nothing
else: not which renderer it was built with, not which audio backend, not whether the compiler that
produced it is one anybody else has.

This emits the whole description as JSON, from the artifact and its build directory rather than
from anything a human retyped.

Deliberately separate from ``check_release_gate.py``. That decides whether the ABI *may* be
published; this describes a *file*. A manifest is not a qualification -- see the ``status`` field,
which is ``BUILT`` and nothing more until something else measures otherwise.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def run(args: list[str]) -> str:
    """Return a command's stdout, or an empty string if it is unavailable."""
    try:
        return subprocess.run(
            args, capture_output=True, text=True, check=False, timeout=120
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def build_id(path: Path) -> str | None:
    """The ELF build ID, which is what makes two otherwise identical builds differ."""
    match = re.search(r"Build ID:\s*([0-9a-f]+)", run(["readelf", "-n", str(path)]))
    return match.group(1) if match else None


def exported_routes(path: Path) -> int:
    """Count exported ``cna_*`` routes -- the ABI's actual surface, not every ELF symbol."""
    symbols = run(["nm", "-D", "--defined-only", str(path)])
    return sum(1 for line in symbols.splitlines() if re.search(r"\s[TW]\s+cna_", line))


def cache_values(cache: Path, keys: tuple[str, ...]) -> dict[str, str]:
    """Read named entries out of a CMakeCache.txt."""
    if not cache.is_file():
        return {}
    found: dict[str, str] = {}
    for line in cache.read_text(errors="replace").splitlines():
        match = re.match(r"^([A-Za-z0-9_]+):[A-Z]+=(.*)$", line)
        if match and match.group(1) in keys:
            found[match.group(1)] = match.group(2)
    return found


def abi_version(repo: Path) -> str | None:
    header = repo / "modules/c-api/include/CNA/C/abi.h"
    if not header.is_file():
        return None
    text = header.read_text(errors="replace")
    parts = []
    for component in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(
            rf"#define\s+CNA_ABI_VERSION_{component}\s+UINT32_C\((\d+)\)", text
        )
        if not match:
            return None
        parts.append(match.group(1))
    return ".".join(parts)


def source_revision(repo: Path) -> dict[str, object]:
    head = run(["git", "-C", str(repo), "rev-parse", "HEAD"]).strip()
    # A dirty tree is the difference between a reproducible artifact and an anecdote, so it is
    # recorded rather than left for someone to infer from a mismatched hash.
    dirty = bool(run(["git", "-C", str(repo), "status", "--porcelain"]).strip())
    return {"head": head or None, "dirty": dirty}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path,
                        help="Built C ABI shared library.")
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="Build directory that produced it (for its CMakeCache.txt).")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2],
                        help="Source tree, for revision and ABI version.")
    parser.add_argument("--output", type=Path,
                        help="Write here instead of stdout.")
    arguments = parser.parse_args()

    library: Path = arguments.library
    if not library.is_file():
        print(f"error: no such library: {library}", file=sys.stderr)
        return 2

    cache = cache_values(
        arguments.build_dir / "CMakeCache.txt",
        (
            "CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS",
            "CNA_GRAPHICS_RENDERER", "CNA_GRAPHICS_RENDERERS", "CNA_PLATFORM",
            "CNA_AUDIO_PLATFORM", "CNA_CNAEXT", "CNA_ENABLE_VIDEO", "CNA_BUILD_C_API",
        ),
    )
    compiler = cache.get("CMAKE_CXX_COMPILER", "")

    manifest = {
        "schema": "cna-c-abi-artifact/1",
        # BUILT is the only thing this tool can honestly assert. The ladder above it --
        # ABI_VERIFIED, INTEGRATION_VERIFIED, PLATFORM_QUALIFIED, RELEASED -- is measured
        # elsewhere and must never be stamped here just because the file exists.
        "status": "BUILT",
        "artifact": {
            "name": library.name,
            "size_bytes": library.stat().st_size,
            "sha256": sha256(library),
            "build_id": build_id(library),
            "exported_cna_routes": exported_routes(library),
        },
        "source": source_revision(arguments.repo),
        "abi_version": abi_version(arguments.repo),
        "target": {
            "os": run(["uname", "-s"]).strip() or None,
            "arch": run(["uname", "-m"]).strip() or None,
        },
        "configuration": {
            "build_type": cache.get("CMAKE_BUILD_TYPE"),
            "graphics_renderer": cache.get("CNA_GRAPHICS_RENDERER"),
            "graphics_renderers": cache.get("CNA_GRAPHICS_RENDERERS") or None,
            "platform": cache.get("CNA_PLATFORM"),
            "audio_platform": cache.get("CNA_AUDIO_PLATFORM"),
            "cnaext": cache.get("CNA_CNAEXT"),
            "video": cache.get("CNA_ENABLE_VIDEO"),
        },
        "toolchain": {
            "compiler_path": compiler or None,
            "compiler_version": (run([compiler, "--version"]).splitlines() or [None])[0]
            if compiler else None,
            "cxx_flags": cache.get("CMAKE_CXX_FLAGS") or None,
        },
    }

    text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
