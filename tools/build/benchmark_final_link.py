#!/usr/bin/env python3
"""Benchmark one existing Ninja final-link command without recompiling its inputs."""

from __future__ import annotations

import argparse
import json
import platform
import shlex
import shutil
import statistics
import subprocess
import time
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run an existing Ninja target's final-link command directly."
    )
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("--artifact", required=True, type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def command_version(command: list[str]) -> str:
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    output = result.stdout.strip() or result.stderr.strip()
    return output.splitlines()[0] if output else ""


def final_link_command(build_dir: Path, target: str, artifact: Path) -> list[str]:
    ninja = shutil.which("ninja")
    if ninja is None:
        raise SystemExit("ninja is not available in PATH")
    output = subprocess.run(
        [ninja, "-C", str(build_dir), "-t", "commands", target],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    matches: list[list[str]] = []
    for line in output.splitlines():
        tokens = shlex.split(line)
        if tokens[:2] == [":", "&&"]:
            tokens = tokens[2:]
        if tokens[-2:] == ["&&", ":"]:
            tokens = tokens[:-2]
        for index, token in enumerate(tokens[:-1]):
            if token == "-o" and Path(tokens[index + 1]) == artifact:
                matches.append(tokens)
                break
    if len(matches) != 1:
        raise SystemExit(
            f"expected one final-link command for {artifact}, found {len(matches)}"
        )
    return matches[0]


def process_tree_rss_kib(root_pid: int) -> int:
    total = 0
    pending = [root_pid]
    visited: set[int] = set()
    while pending:
        pid = pending.pop()
        if pid in visited:
            continue
        visited.add(pid)
        proc = Path("/proc") / str(pid)
        try:
            for line in (proc / "status").read_text(encoding="utf-8").splitlines():
                if line.startswith("VmRSS:"):
                    total += int(line.split()[1])
                    break
            children = (proc / "task" / str(pid) / "children").read_text(
                encoding="utf-8"
            )
            pending.extend(int(child) for child in children.split())
        except (FileNotFoundError, ProcessLookupError):
            continue
    return total


def main() -> None:
    args = parse_arguments()
    if args.iterations < 1:
        raise SystemExit("--iterations must be positive")
    if not Path("/proc/self/status").is_file():
        raise SystemExit("process-tree RSS measurement requires Linux /proc")
    build_dir = args.build_dir.resolve()
    if not (build_dir / "build.ninja").is_file():
        raise SystemExit(f"not an existing Ninja build directory: {build_dir}")
    if args.artifact.is_absolute() or ".." in args.artifact.parts:
        raise SystemExit("--artifact must be a relative path inside the build directory")

    artifact = build_dir / args.artifact
    command = final_link_command(build_dir, args.target, args.artifact)
    linker_flag = next(
        (token for token in command if token.startswith("-fuse-ld=")), None
    )
    linker_name = linker_flag.split("=", 1)[1] if linker_flag else "ld.bfd"
    linker_executable = (
        shutil.which("ld.lld") or shutil.which("lld")
        if linker_name == "lld"
        else shutil.which(linker_name)
    )
    measurements = []
    for iteration in range(1, args.iterations + 1):
        log_path = build_dir / f"benchmark-final-link-{args.label}-{iteration}.log"
        with log_path.open("w", encoding="utf-8") as log:
            started = time.perf_counter()
            process = subprocess.Popen(
                command,
                cwd=build_dir,
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
            )
            peak_kib = 0
            while process.poll() is None:
                peak_kib = max(peak_kib, process_tree_rss_kib(process.pid))
                time.sleep(0.005)
            wall_seconds = time.perf_counter() - started
        if process.returncode != 0:
            raise SystemExit(
                f"link iteration {iteration} failed with status {process.returncode}; "
                f"inspect {log_path}"
            )
        measurements.append(
            {
                "iteration": iteration,
                "wall_seconds": wall_seconds,
                "peak_kib": peak_kib,
                "log": str(log_path),
            }
        )

    if not artifact.is_file():
        raise SystemExit(f"link command produced no artifact: {artifact}")
    walls = [item["wall_seconds"] for item in measurements]
    peaks = [item["peak_kib"] for item in measurements]
    report = {
        "schema_version": 1,
        "label": args.label,
        "build_dir": str(build_dir),
        "target": args.target,
        "artifact": str(artifact),
        "artifact_bytes": artifact.stat().st_size,
        "git_commit": subprocess.run(
            ["git", "-C", str(Path(__file__).resolve().parents[2]), "rev-parse", "HEAD"],
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip(),
        "platform": platform.platform(),
        "versions": {
            "compiler_driver": command_version([command[0], "--version"]),
            "linker": (
                command_version([linker_executable, "--version"])
                if linker_executable is not None
                else None
            ),
            "ninja": command_version(["ninja", "--version"]),
        },
        "command": command,
        "measurements": measurements,
        "summary": {
            "wall_median_seconds": statistics.median(walls),
            "wall_mean_seconds": statistics.mean(walls),
            "peak_kib_max": max(peaks),
        },
    }
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
