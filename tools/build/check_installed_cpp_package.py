#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Build, relocate and exercise the configured native CNA C++ SDK (UP-001).

Run against an existing producer with CNA_INSTALL_CXX=ON. This does not change
producer options. Shared/static configurations are separate invocations. Only
self-created successful artifacts are removed; logs and failed runs survive.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


def read_cache(build):
    values = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        match = re.match(r"([^/#][^:]*):[^=]+=(.*)", line)
        if match:
            values[match[1]] = match[2]
    return values


def enabled(value):
    return value.upper() in {"1", "ON", "TRUE", "YES"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--config", default="")
    parser.add_argument("--check-pkg-dependency", action="store_true")
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    cache = read_cache(build)
    if not enabled(cache.get("CNA_INSTALL_CXX", "OFF")):
        parser.error("the existing producer must enable CNA_INSTALL_CXX")
    source = Path(__file__).resolve().parents[2]
    producer_roots = {source, build}
    producer_roots.update(Path(value).resolve() for key, value in cache.items()
                          if key.endswith("_SOURCE_DIR") and value and Path(value).is_dir())
    fixture = source / "cmake/Tests/InstalledRuntimeProbe"
    parent = build / "installed-cpp-probe"
    parent.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    stage = work / "original"
    relocated = work / "relocated"
    audit = {"source": str(source), "producer": str(build), "steps": [], "passed": False}
    config = args.config or cache.get("CMAKE_BUILD_TYPE", "")
    config_args = ["--config", config] if config else []
    test_config_args = ["-C", config] if config else []
    env = os.environ.copy()
    # The private runner supplies its own display. Never inherit the live desktop.
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
        env.pop(key, None)

    def run(name, command):
        log = work / f"{name}.log"
        started = time.monotonic()
        with log.open("w") as output:
            output.write(shlex.join(map(str, command)) + "\n")
            output.flush()
            result = subprocess.run(list(map(str, command)), stdout=output,
                                    stderr=subprocess.STDOUT, env=env, cwd=source)
        audit["steps"].append({"name": name, "command": list(map(str, command)),
                               "code": result.returncode, "log": str(log),
                               "seconds": time.monotonic() - started})
        print(f"{name}: {result.returncode} ({log})", flush=True)
        if result.returncode:
            raise RuntimeError(f"{name} failed; see {log}")

    def configure(name, case=None, c_only=False):
        consumer = work / name
        command = [args.cmake, "-S", fixture, "-B", consumer,
                   "-G", cache["CMAKE_GENERATOR"],
                   f"-DCMAKE_PREFIX_PATH={relocated}",
                   f"-DCNA_DIR={relocated / cache.get('CMAKE_INSTALL_LIBDIR', 'lib') / 'cmake/CNA'}",
                   "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
                   "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
                   "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
        for key in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_BUILD_TYPE",
                    "CMAKE_C_COMPILER_LAUNCHER", "CMAKE_CXX_COMPILER_LAUNCHER"):
            if cache.get(key):
                command.append(f"-D{key}={cache[key]}")
        if case:
            command.append(f"-DPROBE_CASE={case}")
        if c_only:
            command.append("-DPROBE_C_ONLY=ON")
        run(f"{name}-configure", command)
        return consumer

    def build_and_test(name, consumer):
        run(f"{name}-build", [args.cmake, "--build", consumer, "--parallel", "1", *config_args])
        commands = consumer / "compile_commands.json"
        if commands.exists():
            for entry in json.loads(commands.read_text()):
                command = entry.get("arguments") or shlex.split(entry["command"])
                for index, argument in enumerate(command):
                    # Test source is allowed; framework headers must come only
                    # from the relocated SDK, not the source or producer build.
                    include = None
                    for flag in ("-isystem", "-I", "/I"):
                        if argument == flag:
                            include = command[index + 1]
                            break
                        if argument.startswith(flag):
                            include = argument[len(flag):]
                            break
                    if include:
                        root = Path(include).resolve()
                        if root.is_relative_to(relocated):
                            continue
                        if any(root.is_relative_to(parent) for parent in producer_roots | {stage}):
                            raise RuntimeError(f"source/build include leaked: {include}")
        if sys.platform == "linux":
            run(f"{name}-test", [source / "tools/platform/run_gpu_tests_private.sh",
                                consumer, "--output-on-failure", "-j1", *test_config_args])
        else:
            run(f"{name}-test", [args.ctest, "--test-dir", consumer,
                                "--output-on-failure", "-j1", *test_config_args])

    try:
        run("sdk-build", [args.cmake, "--build", build, "--parallel", "1",
                          "--target", "cna_cpp_sdk", *config_args])
        components = ["SharpRuntime", "CNACpp"]
        has_c_api = enabled(cache.get("CNA_BUILD_C_API", "OFF"))
        if has_c_api:
            components.append("CNACApi")
        for component in components:
            run(f"install-{component}", [args.cmake, "--install", build, "--prefix", stage,
                                         "--component", component, *config_args])
        stage.rename(relocated)
        for file in relocated.rglob("*"):
            if file.is_symlink() and not file.resolve().is_relative_to(relocated):
                raise RuntimeError(f"installed symlink escapes the SDK: {file}")
        for file in relocated.rglob("*.cmake"):
            text = file.read_text()
            if any(str(root) + "/" in text for root in producer_roots | {stage}):
                raise RuntimeError(f"producer path leaked into {file}")
        build_and_test("runtime", configure("runtime"))
        cases = ["Unknown", "Version", "MissingSharp"]
        if args.check_pkg_dependency:
            cases.append("MissingPkg")
        for case in cases:
            configure(case, case)
        if has_c_api:
            for case in ("CApiOnly", "CppApiOnly", "ApiThenCpp"):
                build_and_test(case, configure(case, case, case == "CApiOnly"))
        audit["passed"] = True
        if not args.keep_artifacts:
            # Each path is a direct child of this invocation's mkdtemp directory.
            # Never delete a caller-supplied producer/prefix or a previous run.
            for child in work.iterdir():
                if child.is_dir() and not child.is_symlink():
                    shutil.rmtree(child)
    finally:
        if sys.platform == "linux":
            for line in Path("/proc/self/cgroup").read_text().splitlines():
                if line.startswith("0::"):
                    group = Path("/sys/fs/cgroup") / line[3:].lstrip("/")
                    for name in ("memory.peak", "memory.events"):
                        try:
                            audit[name] = (group / name).read_text().strip()
                        except OSError:
                            pass
        (work / "audit.json").write_text(json.dumps(audit, indent=2) + "\n")
        print(f"SDK audit: {work / 'audit.json'}", flush=True)


if __name__ == "__main__":
    main()
