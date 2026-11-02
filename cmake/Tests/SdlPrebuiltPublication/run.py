# SPDX-License-Identifier: MS-PL
"""Background cmake -P producers and a polling consumer of their real publication path."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time


NAMES = ("SDL3", "SDL3_mixer")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


class Case:
    def __init__(self, args, name):
        self.args = args
        self.work = args.work / name
        if self.work.exists():
            shutil.rmtree(self.work)
        self.control = self.work / "control"
        self.control.mkdir(parents=True)
        self.root = self.work / "prebuilt"
        self.children = []
        self.observed = set()
        fixture = args.source / "cmake/Tests/SdlPrebuiltPublication"
        for dep in ("SDL", "SDL_mixer"):
            source = self.work / "source/third_party" / dep
            source.mkdir(parents=True)
            for filename in ("CMakeLists.txt", "install.cmake.in"):
                shutil.copyfile(fixture / filename, source / filename)
            (source / "version.txt").write_text("A")
        (self.work / "source/CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(SdlPublicationProducer NONE)\n'
            'include("${CNA_SOURCE_DIR}/cmake/Tests/SdlPrebuiltPublication/producer.cmake")\n')

    def start(self, name):
        command = [self.args.cmake, f"-DCNA_SOURCE_DIR={self.args.source}",
                   f"-DCNA_WORK_DIR={self.work}", f"-DCNA_GENERATOR={self.args.generator}",
                   f"-DCNA_PRODUCER={name}", "-P",
                   str(self.args.source / "cmake/Tests/SdlPrebuiltPublicationCase.cmake")]
        log = self.work / f"{name}.log"
        env = dict(os.environ, CNA_SDL_FIXTURE_CONTROL=str(self.control))
        with log.open("w") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                       env=env, start_new_session=(os.name == "posix"))
        self.children.append(process)
        process.fixture_log = log
        return process

    def observe(self):
        # This consumer is independent of both producers and never takes their lock.
        for stamp in self.root.glob("**/*.cna-build-manifest.txt"):
            name = stamp.name[:-len(".cna-build-manifest.txt")]
            library = stamp.parent / f"install/lib/lib{name}.so"
            package = stamp.parent / f"install/lib/cmake/{name}/{name}Config.cmake"
            require(library.exists(), f"Stamp preceded library: {stamp}")
            require(library.read_bytes() in
                    (f"complete {name} A\n".encode(), f"complete {name} B\n".encode()),
                    f"Consumer saw stamp for incomplete library: {stamp}")
            require(package.exists(), f"Stamp preceded package install: {stamp}")
            self.observed.add((str(stamp.parent), name))

    def until(self, predicate, description, process=None):
        deadline = time.monotonic() + 45
        while True:
            self.observe()
            if predicate():
                return
            if process is not None and process.poll() is not None:
                raise RuntimeError(f"Producer exited while waiting for {description}:\n"
                                   f"{process.fixture_log.read_text()}")
            require(time.monotonic() < deadline, f"Timed out: {description}")
            time.sleep(0.02)

    def finish(self, process, success=True):
        self.until(lambda: process.poll() is not None, "producer exit")
        require((process.returncode == 0) == success,
                f"Unexpected exit {process.returncode}:\n{process.fixture_log.read_text()}")
        with (self.work / "exits.txt").open("a") as exits:
            exits.write(f"{process.fixture_log.name}: {process.returncode}\n")

    def run(self, name):
        self.finish(self.start(name))

    def attempts(self):
        return (self.control / "attempts.txt").read_text().splitlines()

    def complete(self, directory, version="A"):
        for name in NAMES:
            library = directory / f"install/lib/lib{name}.so"
            require(library.read_bytes() == f"complete {name} {version}\n".encode(),
                    f"Wrong installed payload: {library}")
            stamp = directory / f"{name}.cna-build-manifest.txt"
            require(stamp.read_text().startswith("schema cna-sdl-prebuilt-manifest-"),
                    f"Missing complete manifest: {stamp}")
        self.observe()

    def selected(self, producer):
        return Path((self.work / f"{producer}.selected").read_text())

    def state(self, expected):
        self.run("check")
        require((self.work / "check.txt").read_text() == expected,
                f"Production CHECK did not classify SDL3 as {expected}")

    def close(self):
        # Release paused descendants even on assertion failure, then reap every child.
        for name in NAMES:
            (self.control / f"release-{name}").touch()
        for process in self.children:
            if process.poll() is None:
                if os.name == "posix":
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    process.kill()
                process.wait()


def snapshot(root):
    # New manifests/<key> children are allowed; every pre-existing root file is protected,
    # including stamps, package metadata, build-tree files and the lock file itself.
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob("*") if path.is_file()
            and path.relative_to(root).parts[0] != "manifests"}


def concurrent(case):
    for name in NAMES:
        (case.control / f"pause-{name}").touch()
    first = case.start("first")
    case.until(lambda: (case.control / "SDL3.partial").exists(), "partial SDL3", first)
    second = case.start("second")
    case.until(lambda: (case.work / "second.started").exists(), "second producer start", second)
    for name in NAMES:
        case.until(lambda: (case.control / f"{name}.partial").exists(),
                   f"partial {name}", first)
        require(not (case.root / f"{name}.cna-build-manifest.txt").exists(),
                f"Stamp published during partial {name} install")
        case.run("lock-check")
        require(second.poll() is None, "Second producer returned while the first held the lock")
        require(case.attempts() == list(NAMES[:NAMES.index(name) + 1]),
                "Concurrent producer entered an install protected by the root lock")
        (case.control / f"release-{name}").touch()
    case.finish(first)
    case.finish(second)
    case.complete(case.root)
    case.state("current")
    require(case.selected("first") == case.selected("second") == case.root,
            "Same-manifest producers selected different installs")
    require(case.attempts() == list(NAMES), "Same manifest was installed more than once")
    require(all((str(case.root), name) in case.observed for name in NAMES),
            "Consumer did not observe both completed stamps")
    require(not (case.root / "manifests").exists(),
            "Same manifest unexpectedly created a child")


def interrupted(case, name):
    fault = case.control / f"fail-{name}"
    fault.touch()
    failed = case.start("interrupted")
    case.finish(failed, success=False)
    require(f"Injected {name} install interruption" in failed.fixture_log.read_text(),
            "Producer failed for a reason other than the injected install fault")
    require(not (case.root / f"{name}.cna-build-manifest.txt").exists(),
            "Failed install published a stamp")
    require((case.root / f"install/lib/lib{name}.so").read_text() == f"partial {name}\n",
            "Fault did not leave a partial install")
    case.state("absent" if name == "SDL3" else "current")
    preserved = None
    if name == "SDL3_mixer":
        case.run("check-mixer-absent")
        preserved = (case.root / "SDL3.cna-build-manifest.txt").read_bytes()
    fault.unlink()  # The manifest inputs do not change when the fault is removed.
    case.run("recovered")
    case.complete(case.root)
    case.state("current")
    require(case.selected("recovered") == case.root, "Recovery did not reuse the unstamped root")
    expected = (["SDL3", "SDL3", "SDL3_mixer"] if name == "SDL3"
                else ["SDL3", "SDL3_mixer", "SDL3_mixer"])
    require(case.attempts() == expected, "Recovery rebuilt a completed dependency or skipped a partial one")
    if preserved is not None:
        require((case.root / "SDL3.cna-build-manifest.txt").read_bytes() == preserved,
                "Mixer recovery changed the completed SDL3 stamp")


def foreign(case):
    case.run("original")
    case.complete(case.root)
    before = snapshot(case.root)
    for dep in ("SDL", "SDL_mixer"):
        (case.work / "source/third_party" / dep / "version.txt").write_text("B")
    case.state("other")
    case.run("foreign")
    selected = case.selected("foreign")
    require(selected.parent == case.root / "manifests" and len(selected.name) == 16,
            f"Foreign manifest did not select manifests/<key>: {selected}")
    case.complete(selected, "B")
    require(snapshot(case.root) == before, "Foreign manifest changed original root bytes/files")
    child_before = snapshot(selected)
    case.run("foreign-again")
    require(case.selected("foreign-again") == selected, "Foreign manifest did not reuse its child")
    require(snapshot(selected) == child_before and snapshot(case.root) == before,
            "Reusing a child modified a completed install")
    require(case.attempts() == list(NAMES) * 2, "Foreign child was unnecessarily rebuilt")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--generator", required=True)
    args = parser.parse_args()
    args.source = args.source.resolve()
    args.work = args.work.resolve()
    scenarios = [("concurrent", concurrent),
                 ("interrupted-sdl", lambda case: interrupted(case, "SDL3")),
                 ("interrupted-mixer", lambda case: interrupted(case, "SDL3_mixer")),
                 ("foreign-manifest", foreign)]
    passed = 0
    for name, test in scenarios:
        case = Case(args, name)
        try:
            test(case)
            passed += 1
            print(f"PASS: {name}", flush=True)
        except Exception:
            print(f"SdlPrebuiltPublication: {passed} passed, 1 failed, "
                  f"{len(scenarios) - passed - 1} not run, 0 skipped", flush=True)
            raise
        finally:
            case.close()
    print(f"SdlPrebuiltPublication: {passed} passed, 0 failed, 0 skipped", flush=True)


if __name__ == "__main__":
    main()
