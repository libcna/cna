#!/usr/bin/env python3
"""Create a lightweight machine-readable report from an existing Ninja build."""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--target", default="all")
    parser.add_argument(
        "--artifact",
        action="append",
        default=[],
        type=Path,
        help="Artifact path relative to the build directory; may be repeated",
    )
    parser.add_argument("--configure-timing", type=Path)
    parser.add_argument("--build-timing", type=Path)
    parser.add_argument("--build-log", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def run_text(command: list[str]) -> str:
    return subprocess.run(command, check=True, text=True, capture_output=True).stdout


def read_timing(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    fields = path.read_text(encoding="utf-8").strip().split()
    if len(fields) != 2:
        raise SystemExit(f"expected '<wall-seconds> <peak-kib>' in {path}")
    return {"wall_seconds": float(fields[0]), "max_rss_kib": int(fields[1])}


def executed_edges(path: Path | None) -> dict[str, int] | None:
    if path is None:
        return None
    compile_edges = 0
    link_edges = 0
    progress = re.compile(r"^\[\d+/\d+\]\s+(.*)$")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = progress.match(line)
        if not match:
            continue
        description = match.group(1)
        if description.startswith("Building ") and " object " in description:
            compile_edges += 1
        elif description.startswith("Linking ") or description.startswith(
            "Combining the C API"
        ):
            link_edges += 1
    return {"compile": compile_edges, "link": link_edges}


def ccache_summary() -> dict[str, Any]:
    ccache = shutil.which("ccache")
    if not ccache:
        return {"available": False}
    try:
        stats = json.loads(run_text([ccache, "--print-stats", "--format=json"]))
    except (subprocess.CalledProcessError, json.JSONDecodeError) as error:
        return {"available": True, "error": str(error)}
    uncacheable_names = (
        "autoconf_test",
        "bad_compiler_arguments",
        "bad_input_file",
        "bad_output_file",
        "called_for_link",
        "called_for_preprocessing",
        "compile_failed",
        "compiler_check_failed",
        "compiler_produced_empty_output",
        "compiler_produced_no_output",
        "compiler_produced_stdout",
        "could_not_find_compiler",
        "could_not_use_modules",
        "could_not_use_precompiled_header",
        "error_hashing_extra_file",
        "internal_error",
        "modified_input_file",
        "multiple_source_files",
        "no_input_file",
        "output_to_stdout",
        "preprocessor_error",
        "unsupported_code_directive",
        "unsupported_compiler_option",
        "unsupported_environment_variable",
        "unsupported_source_language",
    )
    uncacheable_reasons = {
        name: stats.get(name, 0) for name in uncacheable_names if stats.get(name, 0)
    }
    return {
        "available": True,
        "version": run_text([ccache, "--version"]).splitlines()[0],
        "direct_hits": stats.get("direct_cache_hit", 0),
        "preprocessed_hits": stats.get("preprocessed_cache_hit", 0),
        "misses": stats.get("cache_miss", 0),
        "uncacheable": sum(uncacheable_reasons.values()),
        "uncacheable_reasons": uncacheable_reasons,
        "cleanups": stats.get("cleanups_performed", 0),
        "cache_size_kib": stats.get("cache_size_kibibyte", 0),
    }


def main() -> None:
    args = parse_arguments()
    source_dir = Path(__file__).resolve().parents[2]
    build_dir = args.build_dir.resolve()
    if not (build_dir / "build.ninja").is_file():
        raise SystemExit(f"not an existing Ninja build directory: {build_dir}")

    ninja = shutil.which("ninja")
    if not ninja:
        raise SystemExit("ninja is not available in PATH")
    graph_commands = run_text(
        [ninja, "-C", str(build_dir), "-t", "commands", args.target]
    ).splitlines()
    compile_pattern = re.compile(r"(?:^|\s)(?:-c|/c)(?:\s|$)")

    artifacts: dict[str, int] = {}
    for relative in args.artifact:
        artifact = (build_dir / relative).resolve()
        try:
            artifact.relative_to(build_dir)
        except ValueError as error:
            raise SystemExit(f"artifact escapes build directory: {relative}") from error
        if not artifact.is_file():
            raise SystemExit(f"artifact does not exist: {artifact}")
        artifacts[relative.as_posix()] = artifact.stat().st_size

    report = {
        "schema_version": 1,
        "git_commit": run_text(["git", "-C", str(source_dir), "rev-parse", "HEAD"]).strip(),
        "host": {
            "platform": platform.platform(),
            "logical_cpus": os.cpu_count(),
        },
        "build_dir": str(build_dir),
        "target": args.target,
        "timing": {
            "configure": read_timing(args.configure_timing),
            "build": read_timing(args.build_timing),
        },
        "graph": {
            "commands": len(graph_commands),
            "compile_commands": sum(
                1 for command in graph_commands if compile_pattern.search(command)
            ),
        },
        "executed_edges": executed_edges(args.build_log),
        "artifacts_bytes": artifacts,
        "ccache": ccache_summary(),
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
