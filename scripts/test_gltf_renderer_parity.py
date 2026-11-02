#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Exercise the real parity wrapper with isolated stand-in test executables."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
SCRIPT = REPO / "scripts/gltf-renderer-parity.sh"


def result(name="GltfConformanceL1.Sample", status="       OK ", timing=3):
    return (f"[{status}] {name} ({timing} ms)\n"
            f"[==========] 1 tests from 1 test suite ran. ({timing} ms total)\n")


class RendererParityScriptTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.builds = [Path(self.scratch.name) / "renderer A", Path(self.scratch.name) / "renderer B"]
        self.configs = [{"layers": [0, result()], "whole": [0, result("RuntimeGltfModel.Sample")]}
                        for _ in self.builds]

    def run_script(self):
        for directory, config in zip(self.builds, self.configs):
            directory.mkdir(exist_ok=True)
            executable = directory / "CnaTests"
            executable.write_text(f"#!{sys.executable}\n" + r'''
import json
import sys
from pathlib import Path
config = json.loads(Path(__file__).with_suffix(".json").read_text())
phase = "layers" if any(arg.startswith("--gtest_filter=GltfConformanceL1") for arg in sys.argv) else "whole"
status, output = config[phase]
sys.stdout.write(output)
sys.exit(status)
''', encoding="utf-8")
            executable.chmod(0o755)
            executable.with_suffix(".json").write_text(json.dumps(config), encoding="utf-8")
        return subprocess.run(["bash", str(SCRIPT), *map(str, self.builds)], cwd=REPO,
                              capture_output=True, text=True, timeout=10)

    def test_matching_successes_ignore_timing_and_support_spaces_in_paths(self):
        self.configs[1]["layers"][1] = result(timing=91)
        self.configs[1]["whole"][1] = result("RuntimeGltfModel.Sample", timing=87)
        self.assertEqual(self.run_script().returncode, 0)

    def test_identical_failed_gtest_results_are_not_success(self):
        for config in self.configs:
            config["layers"] = [1, result(status="  FAILED  ")]
        self.assertEqual(self.run_script().returncode, 1)

    def test_identical_abrupt_process_failures_are_not_success(self):
        for config in self.configs:
            config["layers"] = [42, "fatal: process stopped before a result\n"]
        self.assertEqual(self.run_script().returncode, 1)

    def test_nonzero_exit_after_matching_success_lines_is_not_success(self):
        self.configs[0]["layers"][0] = 42
        self.assertEqual(self.run_script().returncode, 1)

    def test_whole_selection_failure_is_not_success(self):
        for config in self.configs:
            config["whole"][0] = 42
        self.assertEqual(self.run_script().returncode, 1)

    def test_empty_successful_output_is_not_a_completed_selection(self):
        for config in self.configs:
            config["layers"] = [0, ""]
        self.assertEqual(self.run_script().returncode, 1)

    def test_zero_selected_tests_are_not_a_completed_selection(self):
        for config in self.configs:
            config["layers"] = [0, "[==========] 0 tests from 0 test suites ran. (0 ms total)\n"]
        self.assertEqual(self.run_script().returncode, 1)

    def test_real_lower_layer_differences_still_fail(self):
        self.configs[1]["layers"][1] = result("GltfConformanceL1.Different")
        self.assertEqual(self.run_script().returncode, 1)

    def test_whole_selection_capability_skips_remain_allowed(self):
        self.configs[0]["whole"][1] = result("RuntimeGltfModel.Sample", status="  SKIPPED ")
        self.assertEqual(self.run_script().returncode, 0)

    def test_missing_executable_remains_an_environment_failure(self):
        outcome = subprocess.run(["bash", str(SCRIPT), *map(str, self.builds)], cwd=REPO,
                                 capture_output=True, text=True, timeout=10)
        self.assertEqual(outcome.returncode, 2)


if __name__ == "__main__":
    unittest.main()
