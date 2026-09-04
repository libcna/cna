#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Checks that build-time-only dependencies never reach a runtime link closure.

plans/plan_xnapipeline.md XNAP-90. `cna_content` is linked by every game that loads content at run
time. A font rasterizer, a block-compression encoder and an external-process effect compiler are
needed only while content is being *built*. The boundary between them exists -- `cna_content_pipeline`
is linked by `cna_content_compiler` and by nothing else -- but it was held up by convention and by
one comment. This makes it checkable.

Two independent layers, because either alone can be fooled:

  * **Target graph.** CMake's own `--graphviz` dependency graph is walked from the declared runtime
    roots and from the declared build-time roots. Anything reachable from a build-time root and not
    from any runtime root *is* build-time-only -- derived, not listed -- and the policy file records
    what that set is expected to contain. A dependency added to the build-time module tomorrow
    appears as an unrecorded member and fails the gate until somebody classifies it, exactly as a
    new glTF fixture does in XNAP-59.

  * **Symbols.** The graph says what CMake was *told*; `nm` says what is actually in the archives.
    Every strong defined symbol in the build-time libraries is looked for in the runtime ones. This
    is what catches a translation unit moved across the boundary -- the target graph would still
    look right, because the target it moved into is a runtime target either way.

Usage:
    python3 tools/xnb/dependency_boundary.py --build-dir <dir> [--update] [--cmake cmake]

Exit code 0 when the boundary holds, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
POLICY = REPOSITORY / "tools" / "xnb" / "dependency-boundary.json"

# "node12" -> "node34" [ ... ] // cna_content_pipeline -> Freetype::Freetype
EDGE = re.compile(r'^\s*"node\d+"\s*->\s*"node\d+".*//\s*(?P<from>\S+)\s*->\s*(?P<to>\S+)\s*$')


def read_policy() -> dict:
    return json.loads(POLICY.read_text(encoding="utf-8"))


def write_policy(policy: dict, buildTimeOnly: list[str]) -> None:
    policy["buildTimeOnly"] = sorted(buildTimeOnly)
    POLICY.write_text(json.dumps(policy, indent=2) + "\n", encoding="utf-8")


def target_graph(cmake: str, buildDirectory: Path) -> dict[str, set[str]]:
    """Returns CMake's own link dependency graph as {target: {dependencies}}."""
    with tempfile.TemporaryDirectory(prefix="cna_depgraph_") as scratch:
        destination = Path(scratch) / "cna.dot"
        completed = subprocess.run(
            [cmake, f"--graphviz={destination}", "."],
            cwd=buildDirectory, capture_output=True, text=True, check=False)
        if completed.returncode != 0 or not destination.is_file():
            raise RuntimeError(
                f"cmake --graphviz failed in {buildDirectory}:\n"
                f"{completed.stdout}{completed.stderr}")
        graph: dict[str, set[str]] = {}
        for line in destination.read_text(encoding="utf-8").splitlines():
            edge = EDGE.match(line)
            if edge is None:
                continue
            graph.setdefault(edge.group("from"), set()).add(edge.group("to"))
            graph.setdefault(edge.group("to"), set())
        return graph


def closure(graph: dict[str, set[str]], roots: list[str]) -> set[str]:
    """Every node reachable from @p roots, the roots themselves included."""
    reached: set[str] = set()
    pending = [root for root in roots if root in graph]
    while pending:
        node = pending.pop()
        if node in reached:
            continue
        reached.add(node)
        pending.extend(graph.get(node, set()))
    return reached


def strong_symbols(archive: Path) -> set[str]:
    """Strong, defined symbols in @p archive.

    Weak and vague-linkage symbols are excluded on purpose: an inline function or a template
    instantiation legitimately appears in every archive that used it, and flagging those would make
    the check unusable rather than strict.
    """
    completed = subprocess.run(
        ["nm", "--defined-only", "-g", str(archive)],
        capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        return set()
    symbols = set()
    for line in completed.stdout.splitlines():
        fields = line.split()
        if len(fields) != 3 or fields[1] not in ("T", "D", "B", "R"):
            continue
        symbols.add(fields[2])
    return symbols


def find_archive(buildDirectory: Path, target: str) -> Path | None:
    matches = sorted(buildDirectory.rglob(f"lib{target}.a"))
    return matches[0] if matches else None


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake")
    parser.add_argument("--update", action="store_true",
                        help="record the derived build-time-only set as the new expectation")
    arguments = parser.parse_args(argv[1:])

    if not (arguments.build_dir / "CMakeCache.txt").is_file():
        print(f"{arguments.build_dir}: not a configured CMake build directory.", file=sys.stderr)
        return 1

    policy = read_policy()
    runtimeRoots = policy["runtimeRoots"]
    buildTimeRoots = policy["buildTimeRoots"]

    graph = target_graph(arguments.cmake, arguments.build_dir)
    problems: list[str] = []

    missingRoots = [root for root in runtimeRoots + buildTimeRoots if root not in graph]
    if missingRoots:
        print("these roots are not targets in this configuration, so the gate would pass "
              "vacuously: " + ", ".join(sorted(missingRoots)) +
              ". Either the target names changed or this configuration cannot check the boundary.",
              file=sys.stderr)
        return 1

    runtimeClosure = closure(graph, runtimeRoots)
    buildTimeClosure = closure(graph, buildTimeRoots)
    derived = sorted(buildTimeClosure - runtimeClosure)

    if arguments.update:
        write_policy(policy, derived)
        print(f"{POLICY}: recorded {len(derived)} build-time-only node(s).")
        return 0

    expected = set(policy["buildTimeOnly"])
    for node in derived:
        if node not in expected:
            problems.append(
                f"{node} is reachable from the build-time libraries and from no runtime target, "
                f"so it is a new build-time-only dependency. That is not itself wrong -- it is "
                f"unreviewed. Re-run with --update and read the diff, which is also the moment to "
                f"confirm it must never reach a game.")
    for node in sorted(expected - set(derived)):
        if node in runtimeClosure:
            problems.append(
                f"{node} is recorded as build-time-only and is now reachable from a runtime root. "
                f"A game that links CNA would link it. This is the leak this gate exists for.")
        else:
            problems.append(
                f"{node} is recorded as build-time-only and is no longer in the build-time "
                f"closure at all, so nothing here is checking it. If the dependency was removed, "
                f"re-run with --update; if it was merely not detected by this configuration, the "
                f"gate is passing vacuously.")

    # The boundary's whole point, stated directly rather than inferred from the two sets above.
    for node in policy["neverInRuntime"]:
        if node in runtimeClosure:
            problems.append(f"{node} is in the runtime link closure and must never be.")

    # Layer two: the archives themselves.
    for buildTimeTarget in policy["buildTimeArchives"]:
        archive = find_archive(arguments.build_dir, buildTimeTarget)
        if archive is None:
            problems.append(f"lib{buildTimeTarget}.a was not built, so no symbol check ran. "
                            f"Build it before running this gate.")
            continue
        buildTimeSymbols = strong_symbols(archive)
        if not buildTimeSymbols:
            problems.append(f"{archive} defines no strong symbols; `nm` is unusable here and the "
                            f"symbol layer would pass vacuously.")
            continue
        for runtimeTarget in policy["runtimeArchives"]:
            runtimeArchive = find_archive(arguments.build_dir, runtimeTarget)
            if runtimeArchive is None:
                continue
            shared = sorted(buildTimeSymbols & strong_symbols(runtimeArchive))
            for symbol in shared[:8]:
                problems.append(
                    f"{symbol} is defined in both lib{buildTimeTarget}.a and "
                    f"lib{runtimeTarget}.a. A build-time translation unit has moved into a "
                    f"runtime library, which the target graph cannot see.")
            if len(shared) > 8:
                problems.append(f"...and {len(shared) - 8} more symbols shared between "
                                f"lib{buildTimeTarget}.a and lib{runtimeTarget}.a.")

    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print(f"{len(problems)} dependency-boundary problem(s).", file=sys.stderr)
        return 1
    print(f"dependency boundary holds: {len(derived)} build-time-only node(s) "
          f"({', '.join(derived)}), none reachable from {', '.join(runtimeRoots)}; no strong "
          f"symbol is shared between the build-time and runtime archives.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
