#!/usr/bin/env python3
"""Run one reproducible CNA clean-build benchmark and emit machine-readable evidence."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any


BUILD_PREFIX = "cmake-build-benchmark-"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Configure and build CNA in a new directory. Existing directories are always refused."
        )
    )
    parser.add_argument("--label", required=True, help="Result label, for example gcc-14")
    parser.add_argument("--cxx-compiler", required=True, type=Path)
    parser.add_argument("--c-compiler", type=Path)
    parser.add_argument("--parallel", type=int, default=12)
    parser.add_argument("--target", default="cna_tool_cnb_info")
    parser.add_argument(
        "--artifact",
        default="cna_tool_cnb_info",
        help="Artifact path relative to the build directory",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        help=f"New directory whose basename starts with {BUILD_PREFIX!r}",
    )
    parser.add_argument(
        "--temp-root", type=Path, default=Path("/tmp"), help="Parent for an automatic build dir"
    )
    parser.add_argument(
        "--linker", choices=("DEFAULT", "LLD", "MOLD"), default="MOLD"
    )
    parser.add_argument(
        "--ccache", choices=("isolated", "off"), default="isolated"
    )
    return parser.parse_args()


def command_version(command: list[str]) -> str:
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    output = result.stdout.strip() or result.stderr.strip()
    return output.splitlines()[0] if output else ""


def create_build_directory(args: argparse.Namespace) -> Path:
    if args.build_dir is None:
        root = args.temp_root.resolve()
        if not root.is_dir():
            raise SystemExit(f"temporary root is not a directory: {root}")
        return Path(tempfile.mkdtemp(prefix=BUILD_PREFIX, dir=root))

    build_dir = args.build_dir.resolve()
    if not build_dir.name.startswith(BUILD_PREFIX):
        raise SystemExit(
            f"explicit build directory basename must start with {BUILD_PREFIX!r}: {build_dir}"
        )
    if build_dir.exists():
        raise SystemExit(f"refusing existing build directory: {build_dir}")
    if not build_dir.parent.is_dir():
        raise SystemExit(f"build-directory parent does not exist: {build_dir.parent}")
    build_dir.mkdir()
    return build_dir


def run_timed(
    name: str, command: list[str], build_dir: Path, environment: dict[str, str]
) -> dict[str, Any]:
    log_path = build_dir / f"benchmark-{name}.log"
    metric_path = build_dir / f"benchmark-{name}.time"
    timed_command = [
        "/usr/bin/time",
        "-f",
        "%e\t%M",
        "-o",
        str(metric_path),
        *command,
    ]
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run(
            timed_command,
            cwd=build_dir,
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
        )
    if result.returncode != 0:
        raise SystemExit(
            f"{name} failed with status {result.returncode}; inspect {log_path}"
        )
    wall, peak = metric_path.read_text(encoding="utf-8").strip().split("\t")
    return {
        "command": command,
        "wall_seconds": float(wall),
        "peak_kib": int(peak),
        "log": str(log_path),
    }


def directory_size(root: Path, excluded_root: Path | None = None) -> int:
    size = 0
    excluded = excluded_root.resolve() if excluded_root else None
    for current, directories, files in os.walk(root):
        current_path = Path(current).resolve()
        if excluded is not None:
            directories[:] = [
                name for name in directories if (current_path / name).resolve() != excluded
            ]
        for name in files:
            try:
                size += (current_path / name).stat().st_size
            except FileNotFoundError:
                pass
    return size


def run_text(command: list[str], environment: dict[str, str] | None = None) -> str:
    return subprocess.run(
        command, check=True, text=True, capture_output=True, env=environment
    ).stdout


def main() -> None:
    args = parse_arguments()
    if args.parallel < 1:
        raise SystemExit("--parallel must be positive")

    source_dir = Path(__file__).resolve().parents[2]
    # Keep the exact driver name: resolving /usr/bin/clang++ can turn it into
    # clang and silently omit the C++ runtime during the final link.
    cxx_compiler = args.cxx_compiler.absolute()
    c_compiler = args.c_compiler.absolute() if args.c_compiler else None
    if not cxx_compiler.is_file():
        raise SystemExit(f"C++ compiler is not a file: {cxx_compiler}")
    if c_compiler is not None and not c_compiler.is_file():
        raise SystemExit(f"C compiler is not a file: {c_compiler}")

    cmake = shutil.which("cmake")
    ninja = shutil.which("ninja")
    ccache = shutil.which("ccache")
    if not cmake or not ninja:
        raise SystemExit("cmake and ninja must be available in PATH")
    if args.ccache == "isolated" and not ccache:
        raise SystemExit("--ccache=isolated requires ccache in PATH")

    linker_executable: str | None = None
    if args.linker == "MOLD":
        linker_executable = shutil.which("mold")
    elif args.linker == "LLD":
        linker_executable = shutil.which("ld.lld") or shutil.which("lld")

    build_dir = create_build_directory(args)

    environment = os.environ.copy()
    cache_dir: Path | None = None
    if args.ccache == "isolated":
        cache_dir = build_dir / ".ccache"
        cache_dir.mkdir()
        (cache_dir / "tmp").mkdir()
        environment.update(
            {
                "CCACHE_DIR": str(cache_dir),
                "CCACHE_TEMPDIR": str(cache_dir / "tmp"),
                "CCACHE_MAXSIZE": "5G",
            }
        )

    configure_command = [
        cmake,
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
        "-DCNA_DEBUG_INFO=FULL",
        "-DCNA_GRAPHICS_RENDERER=STUB",
        "-DCNA_BUILD_TESTS=OFF",
        "-DCNA_BUILD_EXAMPLES=OFF",
        "-DCNA_BUILD_C_API=OFF",
        "-DCNA_ENABLE_NET=OFF",
        "-DCNA_ENABLE_VIDEO=OFF",
        "-DCNA_ENABLE_DRACO=OFF",
        "-DCNA_ENABLE_PCH=OFF",
        "-DCNA_ENABLE_UNITY_BUILD=OFF",
        "-DCNA_ENABLE_IPO=OFF",
        "-DCNA_CONFIGURE_AUDIT_CACHE=OFF",
        f"-DCNA_LINKER={args.linker}",
        f"-DCNA_USE_CCACHE={'ON' if cache_dir else 'OFF'}",
    ]
    if c_compiler is not None:
        configure_command.append(f"-DCMAKE_C_COMPILER={c_compiler}")

    configure = run_timed("configure", configure_command, build_dir, environment)
    if cache_dir is not None:
        subprocess.run([ccache, "--zero-stats"], check=True, env=environment, capture_output=True)

    build_command = [
        cmake,
        "--build",
        str(build_dir),
        "--target",
        args.target,
        "--parallel",
        str(args.parallel),
    ]
    clean_build = run_timed("clean-build", build_command, build_dir, environment)
    no_op = run_timed("no-op", build_command, build_dir, environment)

    graph_commands = run_text(
        [ninja, "-C", str(build_dir), "-t", "commands", args.target], environment
    ).splitlines()
    compile_commands = [line for line in graph_commands if " -c " in line]

    artifact = (build_dir / args.artifact).resolve()
    try:
        artifact.relative_to(build_dir.resolve())
    except ValueError as error:
        raise SystemExit(f"artifact escapes build directory: {artifact}") from error
    if not artifact.is_file():
        raise SystemExit(f"expected artifact does not exist: {artifact}")

    ccache_stats: dict[str, Any] | None = None
    cache_size = 0
    if cache_dir is not None:
        ccache_stats = json.loads(
            run_text([ccache, "--print-stats", "--format=json"], environment)
        )
        cache_size = directory_size(cache_dir)

    report = {
        "schema_version": 1,
        "label": args.label,
        "source_dir": str(source_dir),
        "build_dir": str(build_dir),
        "git_commit": run_text(["git", "-C", str(source_dir), "rev-parse", "HEAD"]).strip(),
        "parallel": args.parallel,
        "target": args.target,
        "artifact": str(artifact),
        "versions": {
            "cxx_compiler": command_version([str(cxx_compiler), "--version"]),
            "c_compiler": (
                command_version([str(c_compiler), "--version"])
                if c_compiler is not None
                else None
            ),
            "linker": (
                command_version([linker_executable, "--version"])
                if linker_executable is not None
                else "toolchain default (not resolved)"
            ),
            "cmake": command_version([cmake, "--version"]),
            "ninja": command_version([ninja, "--version"]),
            "ccache": command_version([ccache, "--version"]) if ccache else None,
        },
        "configuration": {
            "build_type": "Debug",
            "debug_info": "FULL",
            "renderer": "STUB",
            "linker": args.linker,
            "ccache": args.ccache,
            "tests": False,
            "examples": False,
            "c_api": False,
            "net": False,
            "video": False,
            "draco": False,
            "pch": False,
            "unity": False,
            "ipo": False,
        },
        "configure": configure,
        "clean_build": clean_build,
        "no_op": no_op,
        "graph": {
            "commands": len(graph_commands),
            "compile_commands": len(compile_commands),
        },
        "artifact_bytes": artifact.stat().st_size,
        "build_tree_bytes_excluding_ccache": directory_size(build_dir, cache_dir),
        "ccache_bytes": cache_size,
        "ccache_stats": ccache_stats,
    }
    result_path = build_dir / "benchmark-result.json"
    result_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    print(f"Result written to {result_path}")


if __name__ == "__main__":
    main()
