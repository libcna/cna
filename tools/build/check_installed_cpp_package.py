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


def check_path(path, forbidden, allowed=()):
    """Reject producer dependencies while allowing the moved SDK inside a checkout."""
    path = Path(path).resolve()
    if any(path.is_relative_to(root) for root in allowed):
        return
    if any(path.is_relative_to(root) for root in forbidden):
        raise RuntimeError(f"source/build path leaked: {path}")


def check_metadata(text, forbidden):
    for root in forbidden:
        if re.search(re.escape(str(root)) + r'(?=$|[/;"\s)])', text):
            raise RuntimeError(f"producer path leaked into package metadata: {root}")


def check_link(command, forbidden, allowed, directory):
    # CMake's file API supplies generator-independent link fragments. Inspect
    # direct library inputs, -L, and linker options such as -Wl,-rpath,<path>.
    for argument in shlex.split(command):
        for token in argument.removeprefix("-Wl,").split(","):
            token = token.removeprefix("-L").removeprefix("-F")
            if "=" in token:
                token = token.split("=", 1)[1]
            for path in token.split(":"):
                if Path(path).is_absolute():
                    check_path(path, forbidden, allowed)
                elif "/" in path and not path.startswith(("-", "$")):
                    check_path(directory / path, forbidden, allowed)


def check_dynamic(dynamic, file, forbidden, allowed):
    for line in dynamic.splitlines():
        if not any(tag in line for tag in ("(NEEDED)", "(RPATH)", "(RUNPATH)")):
            continue
        for value in re.findall(r"\[(.*?)\]", line):
            for path in value.split(":"):
                if "PATH)" in line:
                    if not path.startswith(("/", "$ORIGIN", "${ORIGIN}")):
                        raise RuntimeError(f"cwd-dependent runtime search path in {file}: {path}")
                    path = path.replace("${ORIGIN}", str(file.parent)).replace("$ORIGIN", str(file.parent))
                if Path(path).is_absolute():
                    check_path(path, forbidden, allowed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--config", default="")
    parser.add_argument("--check-pkg-dependency", action="store_true")
    parser.add_argument("--keep-artifacts", action="store_true")
    parser.add_argument("--probe-static-mixed", action="store_true",
                        help="attempt unsupported localized static C/C++ composition (may fail to link)")
    parser.add_argument("--host-only", action="store_true",
                        help="run these window-free probes without a compositor, using SDL dummy drivers")
    parser.add_argument("--work-dir", type=Path,
                        help="fresh artifact directory (must not already exist)")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    cache = read_cache(build)
    if not enabled(cache.get("CNA_INSTALL_CXX", "OFF")):
        parser.error("the existing producer must enable CNA_INSTALL_CXX")
    source = Path(__file__).resolve().parents[2]
    producer_roots = {source, build}
    producer_roots.update(Path(value).resolve() for key, value in cache.items()
                          if (key.endswith(("_SOURCE_DIR", "_BINARY_DIR")) or
                              key in {"CNA_SDL_PREBUILT_DIR", "CNA_SDL_PREBUILT_ROOT", "CNA_WEBGPU_ROOT"})
                          and value and Path(value).is_dir())
    if args.work_dir:
        work = args.work_dir.resolve()
        work.mkdir(parents=True, exist_ok=False)
    else:
        parent = build / "installed-cpp-probe"
        parent.mkdir(exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    fixture = work / "fixture"
    shutil.copytree(source / "cmake/Tests/InstalledRuntimeProbe", fixture)
    stage = work / "original"
    relocated = work / "relocated"
    audit = {"source": str(source), "producer": str(build), "steps": [], "passed": False,
             "configuration": {key: cache.get(key) for key in
                               ("CNA_SHARED_LIBRARY", "CNA_BUILD_C_API", "CNA_C_API_BUILD_STATIC",
                                "CNA_ENABLE_VIDEO", "CNA_GRAPHICS_RENDERER")},
             "elf": [], "test_mode": "host-only" if args.host_only else "private-display"}
    config = args.config or cache.get("CMAKE_BUILD_TYPE", "")
    config_args = ["--config", config] if config else []
    test_config_args = ["-C", config] if config else []
    env = os.environ.copy()
    # The private runner supplies its own display. Never inherit the live desktop.
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH",
                "LD_PRELOAD", "LIBRARY_PATH", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
                "CMAKE_PREFIX_PATH", "CNA_DIR", "SharpRuntime_DIR", "SDL3_DIR", "SDL3_mixer_DIR"):
        env.pop(key, None)
    if args.host_only:
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", WAYLAND_DISPLAY="")

    def run(name, command):
        log = work / f"{name}.log"
        started = time.monotonic()
        with log.open("w") as output:
            output.write(shlex.join(map(str, command)) + "\n")
            output.flush()
            result = subprocess.run(list(map(str, command)), stdout=output,
                                    stderr=subprocess.STDOUT, env=env, cwd=work)
        audit["steps"].append({"name": name, "command": list(map(str, command)),
                               "code": result.returncode, "log": str(log),
                               "seconds": time.monotonic() - started})
        print(f"{name}: {result.returncode} ({log})", flush=True)
        if result.returncode:
            raise RuntimeError(f"{name} failed; see {log}")
        return log.read_text()

    def inspect_elf(name, root):
        if sys.platform != "linux":
            return
        seen = set()
        for file in sorted(root.rglob("*")):
            if not file.is_file() or file.resolve() in seen:
                continue
            with file.open("rb") as stream:
                header = stream.read(18)
            if header[:4] != b"\x7fELF" or len(header) < 18:
                continue
            if int.from_bytes(header[16:18], "little" if header[5] == 1 else "big") not in (2, 3):
                continue
            seen.add(file.resolve())
            tag = f"{name}-{len(seen)}-{file.name}"
            dynamic = run(f"{tag}-readelf", ["readelf", "-d", file])
            check_dynamic(dynamic, file, producer_roots | {stage}, (relocated, root))
            record = {"file": str(file), "dynamic": dynamic}
            # A fully static executable has no dynamic section and ldd refuses it.
            if "(NEEDED)" in dynamic:
                loaded = run(f"{tag}-ldd", ["ldd", file])
                if "not found" in loaded:
                    raise RuntimeError(f"unresolved installed dependency: {file}\n{loaded}")
                for path in re.findall(r"(?:=>\s*|^\s*)(/\S+)", loaded, re.MULTILINE):
                    check_path(path, producer_roots | {stage}, (relocated, root))
                    if Path(path).name.startswith("libcna") or Path(path).name in packaged_libraries:
                        if not Path(path).resolve().is_relative_to(relocated):
                            raise RuntimeError(f"package runtime resolved outside moved SDK: {path}")
                record["ldd"] = loaded
            audit["elf"].append(record)

    def configure(name, case=None, c_only=False):
        consumer = work / name
        query = consumer / ".cmake/api/v1/query"
        query.mkdir(parents=True)
        (query / "codemodel-v2").touch()
        command = [args.cmake, "-S", fixture, "-B", consumer,
                   "-G", cache["CMAKE_GENERATOR"],
                   f"-DCMAKE_PREFIX_PATH={relocated}",
                   f"-DCNA_DIR={relocated / cache.get('CMAKE_INSTALL_LIBDIR', 'lib') / 'cmake/CNA'}",
                   "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
                   "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
                   "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                   f"-DPROBE_STATIC_MIXED={'ON' if args.probe_static_mixed else 'OFF'}",
                   f"-DPROBE_EXPECT_STATIC={'ON' if has_static_c_api else 'OFF'}"]
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
                        include = Path(include)
                        if not include.is_absolute():
                            include = Path(entry["directory"]) / include
                        check_path(include, producer_roots | {stage}, (relocated,))
        reply = consumer / ".cmake/api/v1/reply"
        for target in reply.glob("target-*.json"):
            data = json.loads(target.read_text())
            for fragment in data.get("link", {}).get("commandFragments", []):
                check_link(fragment["fragment"], producer_roots | {stage}, (relocated, consumer), consumer)
        inspect_elf(name, consumer)
        if sys.platform == "linux" and not args.host_only:
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
        has_static_c_api = (has_c_api and os.name == "posix" and
                            enabled(cache.get("CNA_C_API_BUILD_STATIC", "OFF")))
        audit["static_mixed_contract"] = (
            ("unsupported localized static C/C++ composition: diagnostic requested; no shared-state claim"
             if args.probe_static_mixed else
             "unsupported localized static C/C++ composition: diagnostic not requested; no shared-state claim")
            if has_static_c_api else "not run: producer has no standalone static C archive")
        if has_c_api:
            if has_static_c_api:
                run("static-c-api-build", [args.cmake, "--build", build, "--parallel", "1",
                                           "--target", "cna_c_api_static", *config_args])
            components.append("CNACApi")
        for component in components:
            run(f"install-{component}", [args.cmake, "--install", build, "--prefix", stage,
                                         "--component", component, *config_args])
        stage.rename(relocated)
        for file in relocated.rglob("*"):
            if file.is_symlink() and not file.resolve().is_relative_to(relocated):
                raise RuntimeError(f"installed symlink escapes the SDK: {file}")
        for file in relocated.rglob("*"):
            if file.suffix not in {".cmake", ".pc"}:
                continue
            try:
                check_metadata(file.read_text(), producer_roots | {stage})
            except RuntimeError as error:
                raise RuntimeError(f"{file}: {error}") from error
        packaged_libraries = {file.name for file in relocated.rglob("*")
                              if file.is_file() and ".so" in file.name}
        inspect_elf("sdk", relocated)
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
