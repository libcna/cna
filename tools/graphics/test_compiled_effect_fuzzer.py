#!/usr/bin/env python3
"""Replay-tool controls using real capable/unsupported renderers and parser-independent fixtures."""

import argparse
import hashlib
import json
import os
import signal
import subprocess
import tempfile
import unittest
from pathlib import Path


class CompiledEffectFuzzerCliTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="cna fx replay ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def run_tool(self, *arguments, renderer=None, stdout=subprocess.PIPE):
        environment = dict(os.environ)
        if renderer is not None:
            environment["CNA_GRAPHICS_RENDERER"] = renderer
        return subprocess.run([str(ARGS.binary), *map(str, arguments)], env=environment,
                              text=True, stdout=stdout, stderr=subprocess.PIPE, timeout=45)

    def summary(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = [line for line in result.stdout.splitlines() if line.startswith("fx_fuzz_summary ")]
        self.assertEqual(len(lines), 1, result.stdout)
        return json.loads(lines[0].split(" ", 1)[1])

    def test_known_shader_and_annotated_fixture_reach_clone_typed_apply_draw_readback(self):
        fixture = self.root / "annotated.fxb"
        subprocess.run([str(ARGS.support), "--write-fixture", str(fixture)], check=True)
        result = self.run_tool(ARGS.stock, fixture)
        report = self.summary(result)
        self.assertEqual(report["renderer"], "FNA3D")
        self.assertEqual(report["candidates"], 2)
        self.assertEqual(report["constructed"], 2)
        self.assertEqual(report["clones"], 2)
        self.assertGreater(report["parameters"], 0)
        self.assertGreater(report["annotations"], 0)
        self.assertGreater(report["applies"], 0)
        self.assertGreater(report["draws"], 0)
        self.assertGreater(report["readbacks"], 0)
        self.assertIn(hashlib.sha256(ARGS.stock.read_bytes()).hexdigest(), result.stdout)

    def test_unsupported_route_cannot_report_success(self):
        # An unavailable identity also fails in a single-renderer build; in the multi tree this
        # constructs the real STUB renderer and verifies its missing capability is fatal.
        result = self.run_tool(ARGS.stock, renderer="STUB")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertNotIn("fx_fuzz_summary ", result.stdout)

    def test_invalid_campaign_arguments_are_refused_without_running_candidates(self):
        for arguments in (("--campaign",), ("--campaign", self.root, "0"),
                ("--campaign", self.root, "-1"), ("--campaign", self.root, "2junk"),
                ("--campaign", self.root, "18446744073709551616"),
                ("--campaign", self.root, "1", "0x"),
                ("--campaign", self.root, "1", "7", "extra")):
            with self.subTest(arguments=arguments):
                result = self.run_tool(*arguments)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn("fx_fuzz_summary ", result.stdout)
        self.assertEqual(self.run_tool("--help").returncode, 0)

    def test_empty_missing_oversized_and_mixed_inputs_fail(self):
        empty = self.root / "empty.bin"
        empty.touch()
        large = self.root / "large.bin"
        with large.open("wb") as stream:
            stream.seek(64 * 1024 * 1024)
            stream.write(b"x")
        empty_dir = self.root / "empty dir"
        empty_dir.mkdir()
        for paths in ((empty,), (large,), (empty_dir,), (self.root / "missing",),
                      (ARGS.stock, self.root / "missing")):
            with self.subTest(paths=paths):
                result = self.run_tool(*paths)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertNotIn("fx_fuzz_summary ", result.stdout)

    def test_short_campaigns_are_sorted_repeatable_and_report_rejections(self):
        (self.root / "z.bin").write_bytes(b"zz")
        (self.root / "a.bin").write_bytes(b"a")
        results = [self.run_tool("--campaign", self.root, 40, "0x123") for _ in range(2)]
        reports = [self.summary(result) for result in results]
        self.assertEqual(reports[0], reports[1])
        self.assertEqual(reports[0]["candidates"], 40)
        self.assertEqual(reports[0]["constructed"], 0)
        self.assertEqual(reports[0]["rejections"], 40)
        evidence = lambda result: [line for line in result.stdout.splitlines()
                                  if line.startswith(("seed ", "campaign-v2 ", "iteration "))]
        self.assertEqual(evidence(results[0]), evidence(results[1]))
        self.assertIn('a.bin', evidence(results[0])[0])
        self.assertIn('z.bin', evidence(results[0])[1])

    @unittest.skipUnless(Path("/dev/full").exists(), "requires an output-failure device")
    def test_result_output_failure_is_unsuccessful(self):
        with open("/dev/full", "w") as output:
            result = self.run_tool(ARGS.stock, stdout=output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot write", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--support", type=Path, required=True)
    parser.add_argument("--stock", type=Path, required=True)
    ARGS, remaining = parser.parse_known_args()
    ARGS.binary = ARGS.binary.resolve()
    ARGS.support = ARGS.support.resolve()
    ARGS.stock = ARGS.stock.resolve()
    unittest.main(argv=[__file__, *remaining])
