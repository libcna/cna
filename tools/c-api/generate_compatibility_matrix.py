#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Compile the public C API headers across the declared toolchain matrix.

The matrix in ``compatibility_matrix.json`` declares which compilers and language modes the C ABI
claims to support, and which renderer configurations it is exercised in.  Two things keep that
declaration honest:

``--run``
    Compiles every public header against every declared toolchain that is actually installed.  A
    toolchain that is present and rejects a header fails the run; a toolchain that is absent is
    reported as skipped, with its reason, rather than quietly counted as passing.  A ``required``
    toolchain that is absent fails too, because the host compilers always exist.

``--write`` / ``--check``
    Regenerates or verifies ``docs/c-api/COMPATIBILITY.md`` from the declaration, so the published
    matrix cannot drift away from the one the gate runs.

Two probes are compiled for every cell: each public header **on its own**, which is what proves a
header is self-contained, and the umbrella, which is what proves they compose.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MATRIX_PATH = Path(__file__).resolve().parent / "compatibility_matrix.json"
INCLUDE_DIR = REPO_ROOT / "modules" / "c-api" / "include"
HEADER_DIR = INCLUDE_DIR / "CNA" / "C"
DOC_PATH = REPO_ROOT / "docs" / "c-api" / "COMPATIBILITY.md"

# The same warning wall the C API's own targets build under. `-pedantic` is the point of the
# exercise: it is what turns a C11-only construct in a header into an error under C99.
WARNING_FLAGS = ["-Wall", "-Wextra", "-Werror", "-pedantic"]


@dataclass(frozen=True)
class CellResult:
    toolchain: str
    standard: str
    status: str
    detail: str


def public_headers() -> list[str]:
    names = sorted(path.name for path in HEADER_DIR.glob("*.h"))
    if not names:
        raise SystemExit(f"No public headers were found under {HEADER_DIR}.")
    return names


def resolve(toolchain_id: str) -> str | None:
    """Find a compiler by id, honouring the environment CMake would have used."""
    override = {"cc": os.environ.get("CC"), "c++": os.environ.get("CXX")}.get(toolchain_id)
    if override:
        found = shutil.which(override)
        if found:
            return found
    return shutil.which(toolchain_id)


def compile_probe(compiler: str, standard: str, source: Path) -> tuple[bool, str]:
    command = [compiler, f"-std={standard}", *WARNING_FLAGS, "-fsyntax-only", "-I", str(INCLUDE_DIR), str(source)]
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.returncode == 0:
        return True, ""
    output = (completed.stderr or completed.stdout).strip().splitlines()
    return False, "\n".join(output[:6])


def run_matrix(matrix: dict) -> int:
    headers = public_headers()
    results: list[CellResult] = []
    failures = 0

    with tempfile.TemporaryDirectory() as workspace:
        work = Path(workspace)
        for toolchain in matrix["toolchains"]:
            suffix = ".c" if toolchain["language"] == "c" else ".cpp"
            compiler = resolve(toolchain["id"])
            if compiler is None:
                status = "missing" if toolchain["role"] == "required" else "skipped"
                if toolchain["role"] == "required":
                    failures += 1
                for standard in toolchain["standards"]:
                    results.append(
                        CellResult(toolchain["id"], standard, status, "compiler not installed"))
                continue

            # One translation unit per header proves self-containment; the umbrella proves they
            # compose. Both are compiled in every declared language mode.
            probes = []
            for header in headers:
                probe = work / f"leaf_{header.replace('.', '_')}{suffix}"
                probe.write_text(f'#include <CNA/C/{header}>\n', encoding="utf-8")
                probes.append((header, probe))
            umbrella = work / f"umbrella{suffix}"
            umbrella.write_text('#include <CNA/C/cna.h>\n#include <CNA/C/cna.h>\n', encoding="utf-8")
            probes.append(("cna.h (twice, for the include guards)", umbrella))

            for standard in toolchain["standards"]:
                failed_probe = None
                detail = ""
                for name, probe in probes:
                    ok, message = compile_probe(compiler, standard, probe)
                    if not ok:
                        failed_probe = name
                        detail = message
                        break
                if failed_probe is None:
                    results.append(
                        CellResult(toolchain["id"], standard, "passed", f"{len(probes)} translation units"))
                else:
                    failures += 1
                    results.append(
                        CellResult(toolchain["id"], standard, "failed", f"{failed_probe}: {detail}"))

    width = max(len(result.toolchain) for result in results) if results else 10
    for result in results:
        print(f"{result.toolchain:<{width}}  {result.standard:<7}  {result.status:<8}  {result.detail}")

    passed = sum(1 for result in results if result.status == "passed")
    skipped = sum(1 for result in results if result.status == "skipped")
    print(
        f"\nC API header compatibility: {passed} passed, {skipped} skipped, "
        f"{failures} failed, across {len(public_headers())} public headers.")
    if failures:
        print("A toolchain that is installed must accept every public header.", file=sys.stderr)
    return 1 if failures else 0


def render_doc(matrix: dict) -> str:
    headers = public_headers()
    lines: list[str] = []
    lines.append("# C API compatibility matrix")
    lines.append("")
    lines.append(
        "This file is generated from `tools/c-api/compatibility_matrix.json` by")
    lines.append(
        "`tools/c-api/generate_compatibility_matrix.py`. It records what the C ABI is **actually**")
    lines.append(
        "compiled and run against, and — just as deliberately — what it is not.")
    lines.append("")
    lines.append("## How a cell is decided")
    lines.append("")
    lines.append(
        "The `CApi_HeaderCompatibility` test compiles every public header **on its own** and the")
    lines.append(
        f"umbrella twice, in every declared language mode: {len(headers) + 1} translation units per cell.")
    lines.append("Two rules keep the result honest:")
    lines.append("")
    lines.append(
        "- **A toolchain that is installed is binding.** If it is present and rejects a header, the")
    lines.append("  gate fails, whether the row is required or optional.")
    lines.append(
        "- **A toolchain that is absent is skipped, not passed.** The run says so by name, so a")
    lines.append("  machine without Clang cannot look like a machine where Clang agreed.")
    lines.append("")
    lines.append(
        "A `required` row is one the host toolchain always provides, so its absence is a broken")
    lines.append("environment rather than a narrower matrix, and fails.")
    lines.append("")
    lines.append("## Compilers and language modes")
    lines.append("")
    lines.append("| Toolchain | Language | Modes | Role | Why |")
    lines.append("|---|---|---|---|---|")
    for toolchain in matrix["toolchains"]:
        modes = ", ".join(f"`{standard}`" for standard in toolchain["standards"])
        lines.append(
            f"| `{toolchain['id']}` | {toolchain['language'].upper()} | {modes} | "
            f"{toolchain['role']} | {toolchain['note']} |")
    lines.append("")
    lines.append("## Build and run configurations")
    lines.append("")
    lines.append(
        "Compiling a header proves it parses. Running the C smoke programs is what proves the ABI")
    lines.append(
        "behaves, and every one of them runs in all four configurations below — the same source, four")
    lines.append("different answers from the runtime underneath it.")
    lines.append("")
    lines.append("| Configuration | Renderer | `CNA_DEVICES` | Role | What it is for |")
    lines.append("|---|---|---|---|---|")
    for configuration in matrix["configurations"]:
        lines.append(
            f"| `{configuration['id']}` | `{configuration['renderer']}` | {configuration['devices']} | "
            f"{configuration['role']} | {configuration['note']} |")
    lines.append("")
    lines.append("## What is not covered")
    lines.append("")
    lines.append(
        "A matrix that only lists successes is not a matrix. These are the combinations this")
    lines.append("repository does **not** exercise, and the reason each one is absent:")
    lines.append("")
    for entry in matrix["unsupported"]:
        lines.append(f"- **{entry['subject']}.** {entry['note']}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--run", action="store_true", help="compile the matrix against this machine")
    group.add_argument("--write", action="store_true", help="regenerate the published matrix")
    group.add_argument("--check", action="store_true", help="verify the published matrix is current")
    arguments = parser.parse_args()

    matrix = json.loads(MATRIX_PATH.read_text(encoding="utf-8"))

    if arguments.run:
        return run_matrix(matrix)

    rendered = render_doc(matrix)
    if arguments.write:
        DOC_PATH.write_text(rendered, encoding="utf-8")
        print(f"wrote {DOC_PATH.relative_to(REPO_ROOT)}")
        return 0

    current = DOC_PATH.read_text(encoding="utf-8") if DOC_PATH.exists() else ""
    if current != rendered:
        print(
            f"{DOC_PATH.relative_to(REPO_ROOT)} is out of date; "
            "run generate_compatibility_matrix.py --write",
            file=sys.stderr)
        return 1
    print(f"{DOC_PATH.relative_to(REPO_ROOT)} is current")
    return 0


if __name__ == "__main__":
    sys.exit(main())
