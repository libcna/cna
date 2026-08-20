#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""
PLAT-9 - keep platform calls out of hot loops.

What this enforces
------------------
plans/plan_platform.md design decision 4, verbatim: *no platform call may appear inside a per-pixel,
per-vertex, per-fragment, per-sample or per-event loop*. The contract is coarse-grained by
construction -- events arrive as a batch, audio is filled a buffer at a time, input is a per-frame
snapshot, capabilities are read once and cached -- and every one of those guarantees is undone by
a single call moved inside a loop. That move is invisible in review (the code still reads
correctly and still passes every test) and shows up only as a frame-time regression on someone
else's machine.

How a violation is found
------------------------
Two independent judgements, and a line must satisfy both:

  1. **It is a platform call.** The method names are read out of the contract headers themselves,
     so a method added to `IPlatform` tomorrow is covered without editing this script. A bare name
     match would be far too noisy (`Update`, `GetName` and `Clear` appear everywhere), so the call
     must additionally be *reached through a platform receiver*: a variable whose declaration
     names a platform interface type, or one of the ambient accessors.

  2. **It is inside a hot loop.** A `for`/`while` whose header mentions one of the tokens below.
     This is a heuristic and is meant to be: it recognises the loops the design decision names,
     and stays quiet about the game loop itself -- which is per-frame, and where a platform call
     is exactly right.

Both judgements under-report rather than over-report. A pass is therefore not proof that no hot
path calls the platform; a failure is always a real one.

Suppression
-----------
A genuine exception is annotated on the offending line or the line above it:

    // CNA_PLATFORM_HOT_PATH_OK: <reason>

The reason is mandatory -- an unexplained suppression is rejected the same as a violation, because
a bare marker is indistinguishable from someone silencing the tool to get a build through.

Usage
-----
    python3 tools/platform/hot_path_lint.py            # report violations, exit non-zero on any
    python3 tools/platform/hot_path_lint.py --list     # print what it considers a platform call
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

CONTRACT_INCLUDE = Path("modules/platform/include")
SOURCE_ROOTS = [Path("modules")]

# Directories whose contents are not production code. A test may legitimately hammer the platform
# in a loop -- that is often the point of the test.
EXCLUDED_DIR_PARTS = {"tests", "test", "examples", "benchmarks"}

# Tokens that make a loop header count as per-pixel / per-vertex / per-fragment / per-sample /
# per-event. Deliberately concrete: these are the loops design decision 4 names, plus the raster
# dimensions that in practice *are* the per-pixel loop (`for (int y = 0; y < height; ++y)`).
#
# `frame` is absent on purpose. The game loop is per-frame and a platform call there is correct;
# flagging it would train everyone to suppress the tool.
HOT_LOOP_TOKENS = (
    "pixel",
    "texel",
    "vertex",
    "vertices",
    "fragment",
    "sample",
    "samples",
    "event",
    "width",
    "height",
    "stride",
    "pitch",
    "scanline",
    "row",
    "column",
)

# Ambient ways to reach the platform that are not a variable of platform type.
AMBIENT_RECEIVERS = ("GetCurrentPlatform()", "GetPlatformEXT()")

SUPPRESSION = re.compile(r"//\s*CNA_PLATFORM_HOT_PATH_OK\s*:\s*(?P<reason>\S.*)$")
BARE_SUPPRESSION = re.compile(r"//\s*CNA_PLATFORM_HOT_PATH_OK\s*:?\s*$")

# `IPlatformMouse* mouse` / `IPlatformMouse& mouse` / `CNA::Platform::IPlatform& platform`
RECEIVER_DECLARATION = re.compile(
    r"\b(?:CNA::Platform::)?(?P<type>IPlatform\w*)\s*[*&]?\s*(?P<name>[a-z]\w*)\b"
)

LOOP_HEADER = re.compile(r"^\s*(?:\}\s*)?(?:for|while)\s*\(")

METHOD_DECLARATION = re.compile(
    r"virtual\s+[\w:<>,\s*&\[\]]*?\b(?P<name>[A-Z]\w+)\s*\("
)


@dataclass
class OpenLoop:
    entry_depth: int
    opened: bool
    line: int
    text: str


@dataclass
class Violation:
    path: str
    line: int
    text: str
    method: str
    loop_line: int
    loop_text: str
    reason: str


def contract_method_names(repo_root: Path) -> set[str]:
    """Every virtual method declared anywhere in the platform contract."""
    names: set[str] = set()
    for header in (repo_root / CONTRACT_INCLUDE).rglob("*.hpp"):
        for match in METHOD_DECLARATION.finditer(header.read_text(encoding="utf-8")):
            names.add(match.group("name"))
    return names


def production_sources(repo_root: Path) -> list[Path]:
    files: list[Path] = []
    for root in SOURCE_ROOTS:
        for path in (repo_root / root).rglob("*"):
            if path.suffix not in (".cpp", ".hpp", ".mm"):
                continue
            if EXCLUDED_DIR_PARTS & set(part.lower() for part in path.parts):
                continue
            files.append(path)
    return sorted(files)


def strip_comment(line: str) -> str:
    """Removes a trailing line comment so a method named in prose is not read as a call."""
    index = line.find("//")
    return line if index < 0 else line[:index]


def is_hot_loop(header: str) -> bool:
    lowered = header.lower()
    return any(token in lowered for token in HOT_LOOP_TOKENS)


def scan_file(path: Path, repo_root: Path, methods: set[str]) -> tuple[list[Violation], list[Violation]]:
    """Returns (violations, bare-suppressions) for one file."""
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    relative = path.relative_to(repo_root).as_posix()

    # Receiver names are collected over the whole file rather than per scope. That over-accepts
    # (a name reused for something else elsewhere still counts) in the direction of finding more
    # calls, which is the safe direction for a lint whose other half is already conservative.
    receivers: set[str] = set()
    for match in RECEIVER_DECLARATION.finditer(text):
        receivers.add(match.group("name"))
    receivers.update({"platform_", "platform"})

    call_patterns = [
        re.compile(rf"\b{re.escape(name)}\s*(?:->|\.)\s*(?P<method>[A-Z]\w+)\s*\(")
        for name in sorted(receivers)
    ]
    call_patterns += [
        re.compile(rf"{re.escape(name)}\s*(?:->|\.)\s*(?P<method>[A-Z]\w+)\s*\(")
        for name in AMBIENT_RECEIVERS
    ]

    violations: list[Violation] = []
    bare: list[Violation] = []

    # Brace-depth tracking of hot loops. A loop is open from its header until the depth falls back
    # to where it started. The `opened` flag exists because the body's `{` is usually on the line
    # *after* the header in this codebase's style -- without it, every loop would look like it had
    # already closed on its own header line. A loop with no braces at all closes after exactly one
    # statement, which is also handled here.
    hot_stack: list[OpenLoop] = []
    depth = 0

    for index, raw in enumerate(lines):
        line = strip_comment(raw)
        is_header = bool(LOOP_HEADER.match(line)) and is_hot_loop(line)

        if hot_stack and not is_header:
            for pattern in call_patterns:
                match = pattern.search(line)
                if not match or match.group("method") not in methods:
                    continue

                annotation = SUPPRESSION.search(raw) or (
                    SUPPRESSION.search(lines[index - 1]) if index > 0 else None
                )
                bare_annotation = BARE_SUPPRESSION.search(raw) or (
                    BARE_SUPPRESSION.search(lines[index - 1]) if index > 0 else None
                )
                loop = hot_stack[-1]
                record = Violation(
                    path=relative,
                    line=index + 1,
                    text=raw.strip(),
                    method=match.group("method"),
                    loop_line=loop.line,
                    loop_text=loop.text,
                    reason=annotation.group("reason").strip() if annotation else "",
                )
                if annotation:
                    pass
                elif bare_annotation:
                    bare.append(record)
                else:
                    violations.append(record)
                break

        entry_depth = depth
        depth += line.count("{") - line.count("}")

        if is_header:
            hot_stack.append(
                OpenLoop(entry_depth=entry_depth, opened="{" in line, line=index + 1,
                         text=line.strip())
            )
            continue

        if hot_stack and not hot_stack[-1].opened:
            if "{" in line:
                hot_stack[-1].opened = True
            else:
                # A brace-less body is exactly one statement, and it was this line.
                hot_stack.pop()

        while hot_stack and hot_stack[-1].opened and depth <= hot_stack[-1].entry_depth:
            hot_stack.pop()

    return violations, bare


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument(
        "--list", action="store_true", help="print the contract methods this lint watches for"
    )
    args = parser.parse_args(argv)

    repo_root: Path = args.repo.resolve()
    methods = contract_method_names(repo_root)

    if not methods:
        print(
            f"error: no virtual methods found under {CONTRACT_INCLUDE}. The lint would pass\n"
            f"       vacuously, which is worse than failing.",
            file=sys.stderr,
        )
        return 1

    if args.list:
        print(f"{len(methods)} contract methods watched:")
        for name in sorted(methods):
            print(f"  {name}")
        return 0

    violations: list[Violation] = []
    bare: list[Violation] = []
    for path in production_sources(repo_root):
        found, unexplained = scan_file(path, repo_root, methods)
        violations.extend(found)
        bare.extend(unexplained)

    if bare:
        print(
            f"error: {len(bare)} hot-path suppression(s) carry no reason. A bare marker is\n"
            f"       indistinguishable from silencing the tool to get a build through:",
            file=sys.stderr,
        )
        for item in bare:
            print(f"  {item.path}:{item.line}  {item.text}", file=sys.stderr)

    if violations:
        print(
            f"error: {len(violations)} platform call(s) inside a hot loop. Design decision 4 says\n"
            f"       no platform call may appear inside a per-pixel, per-vertex, per-fragment,\n"
            f"       per-sample or per-event loop -- hoist the call out, or annotate the line\n"
            f"       with `// CNA_PLATFORM_HOT_PATH_OK: <reason>` if it genuinely belongs there:",
            file=sys.stderr,
        )
        for item in violations:
            print(f"  {item.path}:{item.line}  {item.method}()", file=sys.stderr)
            print(f"      in loop at line {item.loop_line}: {item.loop_text}", file=sys.stderr)
            print(f"      {item.text}", file=sys.stderr)

    if violations or bare:
        return 1

    print(
        f"hot-path: no platform call appears inside a hot loop "
        f"({len(methods)} contract methods watched across "
        f"{len(production_sources(repo_root))} production sources)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
