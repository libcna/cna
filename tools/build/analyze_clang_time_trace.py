#!/usr/bin/env python3
"""Rank translation units and headers in a directory of Clang time traces."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
from typing import Any


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Aggregate Clang -ftime-trace JSON files without modifying the build tree."
    )
    parser.add_argument("trace_root", type=Path, help="Build directory containing *.json traces")
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path.cwd(),
        help="Only rank headers below this directory (default: current directory)",
    )
    parser.add_argument("--top", type=int, default=20, help="Number of rows per ranking")
    parser.add_argument(
        "--format", choices=("text", "json"), default="text", help="Output format"
    )
    return parser.parse_args()


def source_durations(events: list[dict[str, Any]]) -> list[tuple[str, int]]:
    durations: list[tuple[str, int]] = []
    stacks: dict[tuple[Any, Any, Any], list[tuple[int, str]]] = defaultdict(list)

    for event in events:
        if event.get("name") != "Source":
            continue
        phase = event.get("ph")
        detail = event.get("args", {}).get("detail", "")
        if phase == "X" and detail:
            durations.append((detail, int(event.get("dur", 0))))
            continue

        key = (event.get("pid"), event.get("tid"), event.get("id"))
        if phase in ("b", "B"):
            stacks[key].append((int(event.get("ts", 0)), detail))
        elif phase in ("e", "E") and stacks[key]:
            start, started_detail = stacks[key].pop()
            if started_detail:
                durations.append((started_detail, int(event.get("ts", 0)) - start))

    return durations


def display_path(path: Path, base: Path) -> str:
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path)


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    trace_root = args.trace_root.resolve()
    project_root = args.project_root.resolve()
    if not trace_root.is_dir():
        raise SystemExit(f"trace root is not a directory: {trace_root}")
    if args.top < 1:
        raise SystemExit("--top must be positive")

    translation_units: list[dict[str, Any]] = []
    headers: dict[str, list[int]] = defaultdict(lambda: [0, 0])

    for trace_path in sorted(trace_root.rglob("*.json")):
        try:
            document = json.loads(trace_path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            continue
        events = document.get("traceEvents")
        if not isinstance(events, list):
            continue

        compiler_duration = max(
            (
                int(event.get("dur", 0))
                for event in events
                if event.get("name") == "Total ExecuteCompiler"
            ),
            default=0,
        )
        if compiler_duration:
            translation_units.append(
                {
                    "trace": display_path(trace_path, trace_root),
                    "duration_ms": round(compiler_duration / 1000.0, 3),
                }
            )

        for source, duration in source_durations(events):
            source_path = Path(source)
            try:
                source_path.resolve().relative_to(project_root)
            except ValueError:
                continue
            headers[str(source_path)][0] += duration
            headers[str(source_path)][1] += 1

    translation_units.sort(key=lambda row: row["duration_ms"], reverse=True)
    header_rows = [
        {
            "header": display_path(Path(path), project_root),
            "inclusive_ms": round(values[0] / 1000.0, 3),
            "parses": values[1],
        }
        for path, values in headers.items()
    ]
    header_rows.sort(key=lambda row: row["inclusive_ms"], reverse=True)

    return {
        "trace_root": str(trace_root),
        "project_root": str(project_root),
        "trace_count": len(translation_units),
        "translation_units": translation_units[: args.top],
        "headers": header_rows[: args.top],
    }


def print_text(report: dict[str, Any]) -> None:
    print(f"Traces: {report['trace_count']} under {report['trace_root']}")
    print("\nTranslation units (Total ExecuteCompiler):")
    for row in report["translation_units"]:
        print(f"{row['duration_ms']:12.3f} ms  {row['trace']}")

    print("\nProject headers (inclusive Source time):")
    for row in report["headers"]:
        print(f"{row['inclusive_ms']:12.3f} ms  {row['parses']:5d} parses  {row['header']}")


def main() -> None:
    args = parse_arguments()
    report = analyze(args)
    if args.format == "json":
        print(json.dumps(report, indent=2))
    else:
        print_text(report)


if __name__ == "__main__":
    main()
