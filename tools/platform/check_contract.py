#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""
PLAT-26 / PLAT-27 - keep the platform contract complete and documented.

Purpose
-------
Two mechanical checks over modules/platform/include, both guarding properties that are easy to
state and easy to let slip:

  headers   Every contract header is listed in the SDL-free probe translation unit
            (ContractIsSdlFreeTests.cpp). The probe is what proves the contract pulls in no SDL,
            Vulkan or GL header, and a header that is not listed is simply not checked -- so the
            probe silently weakens every time someone adds a file and forgets. This makes that a
            test failure rather than an invisible gap.

  doxygen   Every public member of every contract header carries a Doxygen block. CLAUDE.md
            requires this repo-wide; for this module it matters more than usual, because the
            documentation IS the specification a second implementation is written against. A
            future SDL2, terminal or Win32 platform author has nothing else to work from.

Both are heuristic in the same direction: they under-report rather than over-report, so a pass is
not a proof of quality, but a failure is always a real omission.

Usage
-----
    python3 tools/platform/check_contract.py            # run both checks
    python3 tools/platform/check_contract.py --headers  # probe completeness only
    python3 tools/platform/check_contract.py --doxygen  # documentation coverage only
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

CONTRACT_INCLUDE = Path("modules/platform/include")
PROBE = Path("modules/platform/tests/CNA/Platform/ContractIsSdlFreeTests.cpp")

# Headers exempt from the SDL-free probe, each for a stated reason. This list must stay tiny: it
# is the one way a header can legitimately escape the guarantee, so an addition to it is a
# deliberate decision that shows up in review, not an oversight.
SDL_FREE_EXEMPT = {
    # Entrypoint.hpp's entire job is to conditionally include <SDL3/SDL_main.h> on Android under
    # the SDL3 platform -- that rename is what makes the app start at all. It is included by the
    # translation unit defining main(), never by the contract's consumers, so it cannot drag SDL
    # into anything the probe is protecting. See docs/platform-entrypoint-audit.md.
    "CNA/Platform/Entrypoint.hpp",
}

# A declaration that needs a Doxygen block: a class/struct/enum, or a member function or field
# declared at class scope. Deliberately narrow -- this is a lint, not a parser.
DECLARATION = re.compile(
    r"^\s{0,8}(?:"
    r"(?P<type>class|struct|enum\s+class|enum)\s+(?P<typename>[A-Z]\w*)"
    r"|(?P<member>(?:\[\[nodiscard\]\]\s*)?(?:static\s+|virtual\s+|constexpr\s+|inline\s+)*"
    r"[A-Za-z_][\w:<>,\s\*&]*\s[\*&]?(?P<name>\w+)\s*\([^;]*\)\s*(?:const\s*)?"
    r"(?:=\s*0\s*)?(?:override\s*)?;)"
    r")"
)

FIELD = re.compile(r"^\s{4,8}(?:[A-Za-z_][\w:<>,\s\*&]*\s)(?P<name>\w+)\s*(?:=[^;]*)?;\s*$")

DOC_END = re.compile(r"\*/\s*$")
DOC_LINE = re.compile(r"^\s*(/\*\*|\*|\*/|/\*\*.*\*/)")


def contract_headers(repo_root: Path) -> list[Path]:
    return sorted((repo_root / CONTRACT_INCLUDE).rglob("*.hpp"))


def check_headers(repo_root: Path) -> int:
    probe_path = repo_root / PROBE
    if not probe_path.is_file():
        print(f"error: SDL-free probe not found at {PROBE}", file=sys.stderr)
        return 1

    probe = probe_path.read_text(encoding="utf-8")
    included = set(re.findall(r'#include\s+"(CNA/Platform/[^"]+)"', probe))

    expected = {
        h.relative_to(repo_root / CONTRACT_INCLUDE).as_posix() for h in contract_headers(repo_root)
    }
    expected = {f"CNA/Platform/{p}" if not p.startswith("CNA/") else p for p in expected}
    expected -= SDL_FREE_EXEMPT

    missing = sorted(expected - included)
    extra = sorted(included - expected)

    if missing:
        print(
            f"error: {len(missing)} contract header(s) are not included by the SDL-free probe\n"
            f"       ({PROBE}). A header the probe does not include is a header the SDL-free\n"
            f"       guarantee does not cover:",
            file=sys.stderr,
        )
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return 1

    extra = [path for path in extra if path not in SDL_FREE_EXEMPT]
    if extra:
        print(f"error: the probe includes {len(extra)} header(s) that no longer exist:", file=sys.stderr)
        for path in extra:
            print(f"  {path}", file=sys.stderr)
        return 1

    print(f"headers: all {len(expected)} contract headers are covered by the SDL-free probe "
          f"({len(SDL_FREE_EXEMPT)} exempt, see SDL_FREE_EXEMPT).")
    return 0


def check_doxygen(repo_root: Path) -> int:
    undocumented: list[tuple[str, int, str]] = []
    documented = 0

    for header in contract_headers(repo_root):
        lines = header.read_text(encoding="utf-8").splitlines()
        relative = header.relative_to(repo_root).as_posix()

        in_private = False
        for index, line in enumerate(lines):
            stripped = line.strip()

            if stripped.startswith("private:"):
                in_private = True
                continue
            if stripped.startswith(("public:", "struct ", "class ")):
                in_private = stripped.startswith(("public:",)) and False or in_private
            if stripped in ("public:",):
                in_private = False
            if in_private:
                continue
            if stripped.startswith(("//", "*", "/*")) or not stripped:
                continue

            # A forward declaration (`class IPlatformWindow;`) introduces no API surface -- it
            # is documented where the type is actually defined. Requiring a Doxygen block here
            # would mean documenting the same type once per header that mentions it.
            if re.match(r"^\s*(class|struct)\s+\w+\s*;\s*$", line):
                continue

            match = DECLARATION.match(line) or FIELD.match(line)
            if not match:
                continue

            name = match.groupdict().get("typename") or match.groupdict().get("name")
            if not name or name in ("return", "if", "for", "while", "switch", "using", "namespace"):
                continue

            # Walk back over blank lines to the previous non-blank line; it must close a
            # Doxygen block.
            previous = index - 1
            while previous >= 0 and not lines[previous].strip():
                previous -= 1
            if previous >= 0 and DOC_END.search(lines[previous]):
                documented += 1
            elif previous >= 0 and DOC_LINE.match(lines[previous]):
                documented += 1
            else:
                undocumented.append((relative, index + 1, name))

    if undocumented:
        print(
            f"error: {len(undocumented)} public declaration(s) in the platform contract have no\n"
            f"       Doxygen block. The contract's documentation is the specification a second\n"
            f"       implementation is written from, so this is a real gap, not a formality:",
            file=sys.stderr,
        )
        for path, line, name in undocumented[:40]:
            print(f"  {path}:{line}  {name}", file=sys.stderr)
        if len(undocumented) > 40:
            print(f"  … and {len(undocumented) - 40} more", file=sys.stderr)
        return 1

    print(f"doxygen: all {documented} public declarations in the contract are documented.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--headers", action="store_true", help="run only the probe-completeness check")
    parser.add_argument("--doxygen", action="store_true", help="run only the documentation check")
    args = parser.parse_args(argv)

    repo_root: Path = args.repo.resolve()
    run_all = not (args.headers or args.doxygen)

    status = 0
    if run_all or args.headers:
        status |= check_headers(repo_root)
    if run_all or args.doxygen:
        status |= check_doxygen(repo_root)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
