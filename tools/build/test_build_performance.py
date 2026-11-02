#!/usr/bin/env python3
"""Exercise benchmark/report provenance with real, compilation-free CMake/Ninja builds."""

from __future__ import annotations

import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import benchmark_clean_build as benchmark
import report_build_performance as reporter


def git(root: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", "-C", str(root), "-c", "commit.gpgsign=false", "-c", "core.hooksPath=/dev/null",
         *arguments], check=True, capture_output=True, text=True
    ).stdout.strip()


def init_repository(root: Path) -> None:
    root.mkdir(parents=True, exist_ok=True)
    git(root, "init", "--quiet")
    (root / "tracked.txt").write_text("original\n", encoding="utf-8")
    git(root, "add", "tracked.txt")
    git(root, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
        "commit", "--quiet", "-m", "fixture")


@unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja") and shutil.which("git"),
                     "requires CMake, Ninja and Git")
class BuildPerformanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cna benchmark evidence ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        init_repository(self.source)
        (self.source / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.20)
project(BenchmarkFixture NONE)
option(CNA_SHARED_LIBRARY "Fixture deliberately defaults to shared" ON)
set(CNA_PLATFORM TERMINAL CACHE STRING "")
set(CNA_AUDIO_PLATFORM NULL CACHE STRING "")
set(CNA_FUTURE_OPTION "unanticipated=value;with spaces" CACHE STRING "")
add_custom_command(OUTPUT cna_tool_cnb_info
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/cna_tool_cnb_info")
add_custom_target(fixture ALL DEPENDS cna_tool_cnb_info)
''', encoding="utf-8")
        git(self.source, "add", "CMakeLists.txt")
        git(self.source, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
            "commit", "--quiet", "-m", "project")

    def run_benchmark(self, mode="static", platform="SDL3", audio="SDL3", *, explicit=True):
        build = self.root / f"build-{mode}-{platform}-{audio}"
        build.mkdir()
        arguments = [
            "benchmark_clean_build.py", "--owner-approved", "--label", "fixture",
            "--cxx-compiler", sys.executable, "--linker", "DEFAULT", "--parallel", "1",
        ]
        if explicit:
            arguments += ["--cna-library", mode, "--platform", platform, "--audio-platform", audio]
        with patch.object(benchmark, "SOURCE_DIR", self.source), \
             patch.object(benchmark, "create_build_directory", return_value=build), \
             patch.object(sys, "argv", arguments), contextlib.redirect_stdout(io.StringIO()):
            benchmark.main()
        return build, json.loads((build / "benchmark-result.json").read_text())

    @unittest.skipUnless(Path("/proc/self/status").is_file(), "benchmark RSS needs Linux")
    def test_clean_driver_pins_runtime_and_platform_and_keeps_future_options(self):
        for mode, platform, audio in (("static", "SDL3", "SDL3"),
                                      ("shared", "HEADLESS", "NULL")):
            with self.subTest(mode=mode):
                _, report = self.run_benchmark(mode, platform, audio)
                cache = report["cmake_cache"]
                self.assertEqual(cache["CNA_SHARED_LIBRARY"], "ON" if mode == "shared" else "OFF")
                self.assertEqual(cache["CNA_PLATFORM"], platform)
                self.assertEqual(cache["CNA_AUDIO_PLATFORM"], audio)
                self.assertEqual(cache["CNA_FUTURE_OPTION"], "unanticipated=value;with spaces")
                self.assertEqual(report["configuration"]["cna_library"], mode)
                self.assertEqual(report["schema_version"], 3)
                self.assertTrue(report["inputs_stable_between_snapshots"])

    @unittest.skipUnless(Path("/proc/self/status").is_file(), "benchmark RSS needs Linux")
    def test_clean_defaults_choose_static_cna_independently_of_cmake_default(self):
        from build_provenance import read_cache

        build, report = self.run_benchmark(explicit=False)
        self.assertEqual(read_cache(build / "CMakeCache.txt")["CNA_SHARED_LIBRARY"]["value"], "OFF")
        self.assertEqual(report["cmake_cache"]["CNA_SHARED_LIBRARY"], "OFF")

    @unittest.skipUnless(Path("/proc/self/status").is_file(), "benchmark RSS needs Linux")
    def test_forced_effective_mode_mismatch_is_refused_before_build(self):
        with (self.source / "CMakeLists.txt").open("a") as stream:
            stream.write('set(CNA_SHARED_LIBRARY ON CACHE BOOL "" FORCE)\n')
        with self.assertRaisesRegex(SystemExit, "configured CNA_SHARED_LIBRARY.*build refused"):
            self.run_benchmark()
        self.assertFalse((self.root / "build-static-SDL3-SDL3" / "cna_tool_cnb_info").exists())

    @unittest.skipUnless(Path("/proc/self/status").is_file(), "benchmark RSS needs Linux")
    def test_source_change_during_build_retains_evidence_and_fails(self):
        original = benchmark.run_timed

        def change_after_phase(name, *arguments):
            result = original(name, *arguments)
            if name == "no-op":
                (self.source / "tracked.txt").write_text("changed while measuring\n")
            return result

        with patch.object(benchmark, "run_timed", side_effect=change_after_phase), \
             self.assertRaisesRegex(SystemExit, "inputs changed.*invalid"):
            self.run_benchmark()
        report = json.loads((self.root / "build-static-SDL3-SDL3" / "benchmark-result.json").read_text())
        self.assertFalse(report["inputs_stable_between_snapshots"])
        root = str(self.source)
        self.assertFalse(report["provenance_before_build"]["repositories"][root]["dirty"])
        self.assertTrue(report["provenance"]["repositories"][root]["dirty"])

    def test_owner_parallel_and_directory_guards_precede_any_build(self):
        for arguments, diagnostic in (([], "owner's approval"),
                (["--owner-approved", "--parallel", "0"], "must be positive"),
                (["--owner-approved", "--parallel", str(benchmark.MAX_PARALLEL_JOBS + 1)],
                 "must not exceed")):
            with self.subTest(arguments=arguments), \
                 patch.object(sys, "argv", ["benchmark", "--label", "guard",
                                           "--cxx-compiler", sys.executable, *arguments]), \
                 patch.object(benchmark, "create_build_directory") as create, \
                 self.assertRaisesRegex(SystemExit, diagnostic):
                benchmark.main()
            create.assert_not_called()
        with self.assertRaisesRegex(SystemExit, "must not be under /tmp"):
            benchmark.refuse_tmp(Path("/tmp/cmake-build-benchmark-guard"), "build")
        existing = self.root / "cmake-build-benchmark-existing"
        existing.mkdir()
        (existing / "marker").write_text("keep")
        with patch.object(benchmark, "refuse_tmp"), \
             self.assertRaisesRegex(SystemExit, "refusing existing build directory"):
            benchmark.create_build_directory(type("Args", (), {"build_dir": existing})())
        self.assertEqual((existing / "marker").read_text(), "keep")

    def test_existing_build_report_uses_effective_cache_and_its_real_source(self):
        build = self.root / "existing"
        subprocess.run(["cmake", "-S", str(self.source), "-B", str(build), "-G", "Ninja",
                        "-DCNA_SHARED_LIBRARY=OFF", "-DCNA_PLATFORM=HEADLESS"],
                       check=True, capture_output=True)
        output = self.root / "report.json"
        with patch.object(sys, "argv", ["report_build_performance.py", "--build-dir", str(build),
                                        "--output", str(output)]), \
             patch.object(reporter, "ccache_summary", return_value={"available": False}), \
             contextlib.redirect_stdout(io.StringIO()):
            reporter.main()
        report = json.loads(output.read_text())
        self.assertEqual(report["git_commit"], git(self.source, "rev-parse", "HEAD"))
        provenance = report["provenance"]
        self.assertEqual(provenance["cmake_cache"]["CNA_SHARED_LIBRARY"], "OFF")
        self.assertEqual(provenance["cmake_cache"]["CNA_PLATFORM"], "HEADLESS")
        self.assertEqual(provenance["cmake_cache"]["CNA_FUTURE_OPTION"],
                         "unanticipated=value;with spaces")
        self.assertEqual(report["schema_version"], 2)

    def test_dependency_revisions_modifications_untracked_inputs_and_binary_hashes(self):
        from build_provenance import capture_provenance, file_sha256

        sibling = self.root / "sibling with spaces"
        child = sibling / "nested"
        init_repository(sibling)
        init_repository(child)
        (sibling / ".gitignore").write_text("/nested/\n")
        (sibling / ".gitmodules").write_text('[submodule "nested"]\n path = nested\n')
        git(sibling, "add", ".gitignore", ".gitmodules")
        child_head = git(child, "rev-parse", "HEAD")
        git(sibling, "update-index", "--add", "--cacheinfo", f"160000,{child_head},nested")
        git(sibling, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
            "commit", "--quiet", "-m", "record nested dependency")
        library = self.root / "libfixture.so"
        library.write_bytes(b"binary-identity\0")
        build = self.root / "cache"
        build.mkdir()
        (build / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.source}\n"
            f"dependency_SOURCE_DIR:STATIC={sibling}\n"
            f"FETCHCONTENT_SOURCE_DIR_FIXTURE:PATH={sibling}\n"
            f"RESOLVED_LIBRARY:INTERNAL={library}\n"
            "CNA_SHARED_LIBRARY:BOOL=ON\nCNA_FUTURE_OPTION:STRING=x=y;z\n"
            '"name:with:colons":STRING=preserved\n')
        first = capture_provenance(build, environment={"CCACHE_DIR": "/fixture/private"})
        self.assertEqual(first["cmake_cache"]["CNA_FUTURE_OPTION"], "x=y;z")
        self.assertEqual(first["cmake_cache"]["name:with:colons"], "preserved")
        self.assertEqual(first["cmake_cache_types"]["RESOLVED_LIBRARY"], "INTERNAL")
        self.assertEqual(first["environment"], {"CCACHE_DIR": "/fixture/private"})
        self.assertEqual(first["repositories"][str(child)]["git_commit"], git(child, "rev-parse", "HEAD"))
        self.assertEqual(first["repositories"][str(sibling)]["recorded_gitlinks"], {"nested": child_head})
        self.assertEqual(first["file_inputs"][str(library)]["sha256"], file_sha256(library))
        (sibling / "tracked.txt").write_text("modified dependency\n")
        (child / "untracked.txt").write_text("new child source\n")
        second = capture_provenance(build)
        self.assertNotEqual(first["repositories"][str(sibling)]["tracked_diff_sha256"],
                            second["repositories"][str(sibling)]["tracked_diff_sha256"])
        self.assertTrue(second["repositories"][str(child)]["dirty"])
        untracked = second["repositories"][str(child)]["untracked_files"]
        self.assertEqual(untracked["untracked.txt"]["sha256"], file_sha256(child / "untracked.txt"))
        library.write_bytes(b"different library")
        self.assertNotEqual(second["file_inputs"], capture_provenance(build)["file_inputs"])

    def test_unversioned_source_export_is_explicit_and_has_no_invented_git_commit(self):
        from build_provenance import capture_provenance

        export = self.root / "source-export"
        export.mkdir()
        (export / "code.txt").write_text("source export\n")
        build = self.root / "export-build"
        build.mkdir()
        (build / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={export}\n")
        evidence = capture_provenance(build)
        self.assertIsNone(evidence["source_repository"])
        self.assertEqual(evidence["unversioned_source_dirs"], [str(export)])
        self.assertEqual(evidence["repositories"], {})
        self.assertEqual(evidence["unversioned_source_trees"][str(export)]["files"], 1)
        (export / "code.txt").write_text("modified export\n")
        changed = capture_provenance(build)
        self.assertNotEqual(evidence["unversioned_source_trees"], changed["unversioned_source_trees"])


if __name__ == "__main__":
    unittest.main()
