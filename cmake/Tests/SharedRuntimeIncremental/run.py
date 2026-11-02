# SPDX-License-Identifier: MS-PL
"""Incrementally mutate a tiny consumer of CNA's real shared-runtime policy."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


CONSUMERS = ("data", "layout", "virtual", "opaque", "static")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


class Case:
    def __init__(self, args):
        self.args = args
        self.work = args.work.resolve()
        self.source = self.work / "source"
        self.build = self.work / "build"
        self.work.mkdir(parents=True, exist_ok=True)
        self.source.mkdir(exist_ok=True)
        self.commands = []
        self.failures = []
        self.measurements = []
        self.consumers = CONSUMERS if args.policy_only else (*CONSUMERS, "mixed")
        self.env = dict(os.environ, LC_ALL="C")
        fixture = args.source / "cmake/Tests/SharedRuntimeIncremental"
        for path in fixture.iterdir():
            if path.suffix in (".cpp", ".hpp") or path.name == "CMakeLists.txt":
                write_changed(self.source / path.name, path.read_text())

    def command(self, name, command, success=True):
        start = time.perf_counter()
        result = subprocess.run([str(arg) for arg in command], env=self.env,
                                capture_output=True, text=True, timeout=60)
        seconds = time.perf_counter() - start
        (self.work / f"{name}.log").write_text(result.stdout + result.stderr)
        self.commands.append(dict(name=name, command=[str(arg) for arg in command],
                                  exit_code=result.returncode, seconds=seconds))
        (self.work / "commands.json").write_text(json.dumps(self.commands, indent=2) + "\n")
        if success:
            require(result.returncode == 0,
                    f"{name}: exit {result.returncode}\n{result.stdout}{result.stderr}")
        return result, seconds

    def snapshot(self):
        files = list(self.build.rglob("*.o")) + list(self.build.glob("*.a"))
        files += [self.build / "libcna.so"]
        files += [self.build / f"{name}_consumer" for name in self.consumers]
        return {str(path.relative_to(self.build)): (path.stat().st_mtime_ns, path.stat().st_size)
                for path in files if path.exists()}

    def build_stage(self, name, expected):
        before = self.snapshot() if (self.build / "data_consumer").exists() else {}
        _, seconds = self.command(name, [self.args.cmake, "--build", self.build, "--parallel", "4"])
        after = self.snapshot()
        linked = {key[:-len("_consumer")] for key in after if key.endswith("_consumer")
                  and before.get(key) != after[key]}
        if expected is not None:
            if linked != set(expected):
                self.failures.append(f"{name}: relinked {linked}, expected {set(expected)}")
        changed = sorted(key for key in after if before.get(key) != after[key])
        if name in ("implementation", "data-size"):
            require(not any("_consumer.dir/" in key for key in changed),
                    f"{name}: unexpectedly recompiled consumer objects")
        if expected == ():
            require(not changed, f"{name}: no-op changed outputs: {changed}")
        self.measurements.append(dict(stage=name, seconds=seconds,
                                      relinked=sorted(linked), changed=changed,
                                      output_bytes=sum(after[key][1] for key in changed)))
        (self.work / "measurements.json").write_text(json.dumps(self.measurements, indent=2) + "\n")
        print(f"{name}: {seconds:.3f}s, relinked {sorted(linked)}, changed {len(changed)} files", flush=True)

    def run_consumers(self, stage):
        for name in self.consumers:
            result, _ = self.command(f"{stage}-run-{name}", [self.build / f"{name}_consumer"],
                                     success=False)
            if result.returncode or "different size" in result.stderr:
                self.failures.append(f"{stage}/{name}: exit {result.returncode}: {result.stderr.strip()}")

    def edit(self, filename, old, new):
        path = self.source / filename
        text = path.read_text()
        require(old in text, f"Missing edit anchor in {path}")
        path.write_text(text.replace(old, new))

    def run(self):
        ccache = shutil.which("ccache")
        require(ccache, "ccache is required; use the shared cache")
        linker_flags = f"-fuse-ld={self.args.linker}" if self.args.linker else ""
        self.command("configure", [self.args.cmake, "-S", self.source, "-B", self.build,
                     "-G", self.args.generator, f"-DCNA_SOURCE_DIR={self.args.policy_source or self.args.source}",
                     f"-DCNA_FIXTURE_POLICY_ONLY={'ON' if self.args.policy_only else 'OFF'}",
                     "-DCNA_SHARED_LIBRARY=ON", f"-DCMAKE_CXX_COMPILER={self.args.compiler}",
                     f"-DCMAKE_EXE_LINKER_FLAGS={linker_flags}",
                     f"-DCMAKE_SHARED_LINKER_FLAGS={linker_flags}",
                     f"-DCMAKE_CXX_COMPILER_LAUNCHER={ccache}", "-DCMAKE_BUILD_TYPE=Debug"])
        self.build_stage("initial", None)
        self.run_consumers("initial")
        result, _ = self.command("initial-relocations", ["readelf", "-Wr", self.build / "data_consumer"])
        print(f"initial exported-data COPY relocation: {'COPY' in result.stdout}", flush=True)
        if "COPY" in result.stdout:
            self.failures.append("data consumer contains a COPY relocation")
        self.build_stage("initial-noop", ())
        self.edit("impl.cpp", "return 1;", "return 2;")
        self.build_stage("implementation", ("static",))
        self.run_consumers("implementation")
        self.build_stage("implementation-noop", ())
        self.edit("data.cpp", "{11}", "{11, 22}")
        self.edit("data.cpp", "ExportedData[0] == 11;", "ExportedData[0] == 11 && ExportedData[1] == 22;")
        self.build_stage("data-size", ("static",))
        self.run_consumers("data-size")
        self.edit("layout.hpp", "    int value = 11;", "    int padding = 7;\n    int value = 11;")
        self.build_stage("inline-layout", ("layout", "static"))
        self.run_consumers("inline-layout")
        self.edit("virtual.hpp", "    virtual int value() const;",
                  "    virtual int extra() const { return -1; }\n    virtual int value() const;")
        self.build_stage("vtable", ("virtual", "static"))
        self.run_consumers("vtable")
        self.build_stage("final-noop", ())
        if not self.args.policy_only:
            result, _ = self.command("mixed-symbols", ["nm", "--defined-only", "-C",
                                                     self.build / "mixed_consumer"])
            require("Opaque()" not in result.stdout, "Mixed consumer extracted a second runtime")
        if (self.build / "has-nonpic-check").exists():
            result, _ = self.command("nonpic-link", [self.args.cmake, "--build", self.build,
                                     "--parallel", "4", "--target", "nonpic_consumer"], success=False)
            diagnostic = result.stdout + result.stderr
            if result.returncode == 0:
                self.failures.append("Non-PIC input was accepted instead of refusing unsafe relocations")
            else:
                require(any(word in diagnostic for word in ("read-only", "recompile with -fPIC")),
                        f"Non-PIC input failed for an unexpected reason: {diagnostic}")
        else:
            print("SKIP: forced non-PIC link rejection requires x86-64", flush=True)
        if not self.args.policy_only:
            prefix = self.work / "install"
            self.command("install", [self.args.cmake, "--install", self.build, "--prefix", prefix])
            installed = self.work / "installed-build"
            self.command("installed-configure", [self.args.cmake, "-S",
                         self.args.source / "cmake/Tests/SharedRuntimeIncremental/InstalledConsumer",
                         "-B", installed, "-G", self.args.generator, f"-DCNA_PREFIX={prefix}",
                         f"-DCMAKE_CXX_COMPILER={self.args.compiler}",
                         f"-DCMAKE_EXE_LINKER_FLAGS={linker_flags}",
                         f"-DCMAKE_CXX_COMPILER_LAUNCHER={ccache}"])
            self.command("installed-build", [self.args.cmake, "--build", installed, "--parallel", "4"])
            self.command("installed-run", [installed / "installed_consumer"])
            result, _ = self.command("installed-relocations", ["readelf", "-Wr",
                                                               installed / "installed_consumer"])
            if "COPY" in result.stdout:
                self.failures.append("Installed consumer contains a COPY relocation")
        for failure in self.failures:
            print(f"FAIL: {failure}", flush=True)
        require(not self.failures, f"{len(self.failures)} regression failures")
        print("PASS: implementation, data size, inline layout, vtable, opaque consumer and no-op checks", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--linker", choices=("bfd", "lld", "mold"))
    parser.add_argument("--policy-source", type=Path)
    parser.add_argument("--policy-only", action="store_true")
    args = parser.parse_args()
    Case(args).run()


if __name__ == "__main__":
    main()
