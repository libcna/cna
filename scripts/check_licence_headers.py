#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Every tracked C/C++/Objective-C source file of CNA carries an MS-PL SPDX header.

The owner's rule (handoff section 6, decided 2026-11-02): files written for CNA are MS-PL. The only
other licence allowed is MIT, and only for the files named in MIT_EXCEPTIONS below, which are
copies or adaptations of other projects that keep their own licence:

  * DirectXMeshOptimizeFaces.cpp adapts Microsoft's DirectXMesh (THIRD_PARTY_NOTICES.md);
  * the CnaService protocol files are verbatim copies of cna-gamer-services-server's
    protocol sources, which ship with that project's MIT LICENCE next to them.

The SPDX line must be among the first five lines. Vendored trees (third_party/, vendor/) are not
CNA's files and are skipped. Exit 0 when every file complies, 1 with the offending files listed.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
EXTENSIONS = {".c", ".cc", ".cpp", ".h", ".hpp", ".inl", ".m", ".mm"}
SKIPPED_ROOTS = ("third_party/", "vendor/")
MIT_EXCEPTIONS = {
    "modules/content-pipeline/src/Internal/DirectXMeshOptimizeFaces.cpp",
    "modules/gamer-services/src/Internal/Protocol/CnaService/Protocol.hpp",
    "modules/gamer-services/src/Internal/Protocol/CnaService/RelayProtocol.hpp",
    "modules/gamer-services/src/Internal/ServiceProtocol.cpp",
    "modules/net/src/Internal/RelayProtocol.cpp",
}
MARKER = "SPDX-License-Identifier:"


def tracked_sources() -> list[str]:
    output = subprocess.run(["git", "ls-files", "-z"], cwd=REPO, check=True,
                            capture_output=True).stdout.decode("utf-8")
    return sorted(path for path in output.split("\0")
                  if path and Path(path).suffix in EXTENSIONS
                  and not path.startswith(SKIPPED_ROOTS))


def licence_of(path: Path) -> str | None:
    with path.open(encoding="utf-8", errors="replace") as handle:
        for _ in range(5):
            line = handle.readline()
            if MARKER in line:
                return line.split(MARKER, 1)[1].strip().rstrip("*/").strip()
    return None


def main() -> int:
    problems = []
    files = tracked_sources()
    for relative in files:
        licence = licence_of(REPO / relative)
        expected = "MIT" if relative in MIT_EXCEPTIONS else "MS-PL"
        if licence is None:
            problems.append(f"  {relative}: no SPDX-License-Identifier in its first five lines")
        elif licence != expected:
            problems.append(f"  {relative}: {licence}, expected {expected}")
    missing = sorted(path for path in MIT_EXCEPTIONS if path not in files)
    problems += [f"  {path}: listed as an MIT exception but no longer tracked" for path in missing]
    if problems:
        print(f"licence headers: {len(problems)} problem(s) in {len(files)} source files:")
        print("\n".join(problems))
        print("CNA's own files carry `// SPDX-License-Identifier: MS-PL` (C files: `/* ... */`).")
        return 1
    print(f"licence headers: {len(files)} source files, all MS-PL except "
          f"{len(MIT_EXCEPTIONS)} named MIT exceptions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
