#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""MTH-SAFE-007: the long campaign's accounting and failure rules, against fake_harness.py.

Each case runs the real driver (in-process, or as a subprocess when a signal is involved) over a
fake harness that speaks the seeded protocol and fails in one chosen way on one chosen seed.
"""

import contextlib
import io
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import reproduce  # noqa: E402
import run_long_campaign as campaign  # noqa: E402

FAKE = HERE / "fake_harness.py"
START = 1000


class CampaignTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cna-fuzz-driver-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.corpus = self.root / "corpus"
        self.corpus.mkdir()
        for name, data in (("a.bin", b"alpha"), ("b.bin", b"bravo"), ("ignored.txt", b"not a seed")):
            (self.corpus / name).write_bytes(data)
        self.output = self.root / "out"

    def targets(self, behaviour="ok", seed=None, budget=1.5, extra=None, **fields):
        target = {"name": "fake", "binary": str(FAKE), "launcher": [sys.executable],
                  "corpus": [{"path": str(self.corpus), "suffixes": [".bin"]}],
                  "required_capability": "FakeCapability", "required_counters": ["applies"],
                  "budget_seconds": budget, "candidate_timeout_seconds": 1.5, "startup_timeout_seconds": 20,
                  "rss_limit_mb": 120, "chunk_seconds": 0.4, "initial_count": 8,
                  "env": {"FAKE_BEHAVIOUR": behaviour, "FAKE_SEED": str(START + seed if seed is not None else -1)}}
        target.update(fields)
        targets = [target] + list(extra or [])
        path = self.root / "targets.json"
        path.write_text(json.dumps({"schema_version": 1, "targets": targets}))
        return path

    def run_campaign(self, targets, *arguments):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            code = campaign.main(["--targets-file", str(targets), "--output-dir", str(self.output),
                                  "--seed-start", str(START), *arguments])
        return code, self.summary()

    def summary(self, name="fake"):
        return json.loads((self.output / name / "summary.json").read_text())

    def finding(self, summary, index=0):
        return self.output / "fake" / summary["findings"][index]["record"]

    # -- accounting ----------------------------------------------------------------------------
    def test_clean_run_accounts_every_seed_and_operation(self):
        code, summary = self.run_campaign(self.targets())
        self.assertEqual(code, campaign.EXIT_PASSED)
        self.assertEqual(summary["status"], "passed")
        self.assertEqual(summary["stop_reason"], "budget")
        self.assertGreater(summary["attempts"], 8)
        self.assertGreaterEqual(summary["harness_processes"], 2, "budget must span several harness processes")
        self.assertEqual(summary["operations"], 3 * summary["attempts"])
        self.assertEqual(summary["counters"]["applies"], summary["attempts"])
        self.assertEqual(summary["seed_range"]["first"], START)
        self.assertEqual(summary["seed_range"]["next_unattempted"], START + summary["attempts"])
        self.assertEqual(summary["unaccounted_attempts"], 0)
        self.assertEqual([entry["path"].rsplit("/", 1)[-1] for entry in summary["corpus"]["entries"]],
                         ["a.bin", "b.bin"])
        self.assertEqual(len(summary["corpus"]["entries"][0]["sha256"]), 64)
        self.assertEqual(summary["reported_capabilities"], ["FakeCapability on FAKE"])
        self.assertFalse(summary["coverage"]["available"])
        self.assertEqual(len(summary["binary_sha256"]), 64)
        self.assertGreater(summary["peak_rss_mb"], 0)
        campaign_summary = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(campaign_summary["exit_code"], 0)
        self.assertIsNone(campaign_summary["interrupted_by"])

    def test_seed_range_bounds_the_run(self):
        code, summary = self.run_campaign(self.targets(budget=30), "--seed-count", "20")
        self.assertEqual(code, campaign.EXIT_PASSED)
        self.assertEqual(summary["attempts"], 20)
        self.assertEqual(summary["stop_reason"], "seed_range")
        self.assertEqual(summary["seed_range"]["next_unattempted"], START + 20)

    # -- work that cannot pass -----------------------------------------------------------------
    def test_zero_operations_fails(self):
        code, summary = self.run_campaign(self.targets("zero-ops"))
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertEqual(summary["status"], "failed")
        self.assertGreater(summary["attempts"], 0)
        self.assertEqual(summary["failures"][0], "zero_operations")

    def test_zero_required_counter_fails(self):
        code, summary = self.run_campaign(self.targets("no-applies"))
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertEqual(summary["failures"], ["required_counter_zero: applies"])

    def test_zero_attempts_fails(self):
        code, summary = self.run_campaign(self.targets(budget=30), "--seed-count", "1",
                                          "--budget-seconds", "0.000001")
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertEqual(summary["attempts"], 0)
        self.assertIn("zero_attempts", summary["failures"])

    def test_missing_corpus_fails_before_running(self):
        for path in self.corpus.glob("*.bin"):
            path.unlink()
        code, summary = self.run_campaign(self.targets())
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertTrue(summary["failures"][0].startswith("missing_corpus"))
        self.assertEqual(summary["harness_processes"], 0)

    def test_missing_binary_fails(self):
        code, summary = self.run_campaign(self.targets(binary=str(self.root / "absent")))
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertTrue(summary["failures"][0].startswith("missing_binary"))

    def test_unavailable_capability_fails(self):
        code, summary = self.run_campaign(self.targets("capability"))
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertTrue(summary["failures"][0].startswith("capability_unavailable"))
        self.assertIn("not qualified", summary["failures"][0])

    def test_other_capability_fails(self):
        code, summary = self.run_campaign(self.targets("wrong-capability"))
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertTrue(summary["failures"][0].startswith("capability_mismatch"))
        self.assertEqual(summary["attempts"], 0)

    def test_startup_failure_is_a_harness_error_not_a_finding(self):
        code, summary = self.run_campaign(self.targets("startup-fail"))
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertTrue(summary["failures"][0].startswith("harness_error: exit 2"))
        self.assertEqual(summary["findings"], [])

    def test_attempt_count_disagreement_fails(self):
        code, summary = self.run_campaign(self.targets("mismatch"))
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertTrue(summary["failures"][0].startswith("accounting_mismatch"))

    def test_missing_instrumentation_fails(self):
        build = self.root / "build"
        build.mkdir()
        (build / "CMakeCache.txt").write_text("CNA_SANITIZE:STRING=address,undefined\nCMAKE_BUILD_TYPE:STRING=Debug\n")
        source = "tools/fuzz/fake_harness.py"
        commands = [{"directory": str(build), "file": str(campaign.REPO_ROOT / source),
                     "command": "c++ -g -fsanitize=address -O0 -c fake.cpp"}]
        (build / "compile_commands.json").write_text(json.dumps(commands))
        targets = self.targets(source=source)
        code, summary = self.run_campaign(targets, "--build-dir", str(build), "--require-sanitizer", "address")
        self.assertEqual(code, campaign.EXIT_PASSED)
        self.assertEqual(summary["instrumentation"]["sanitizers"], ["address"])
        self.assertEqual(summary["instrumentation"]["cmake_cache"]["CNA_SANITIZE"], "address,undefined")
        code, summary = self.run_campaign(targets, "--build-dir", str(build), "--require-sanitizer", "address",
                                          "--require-sanitizer", "undefined")
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertEqual(summary["failures"], ["instrumentation_missing: undefined"])
        (build / "compile_commands.json").unlink()
        code, summary = self.run_campaign(targets, "--build-dir", str(build), "--require-sanitizer", "address")
        self.assertEqual(code, campaign.EXIT_FAILED)
        self.assertIn("no compile_commands.json entry", summary["failures"][0])

    # -- findings ------------------------------------------------------------------------------
    def test_crash_on_known_seed_is_kept_with_its_exact_seed(self):
        code, summary = self.run_campaign(self.targets("crash", seed=13, budget=2.0, initial_count=32))
        self.assertEqual(code, campaign.EXIT_FINDINGS)
        self.assertEqual(summary["status"], "findings")
        self.assertEqual(summary["crashes"], 1)
        finding = summary["findings"][0]
        self.assertEqual((finding["kind"], finding["seed"]), ("crash", START + 13))
        self.assertTrue(finding["reproduces_in_isolation"])
        self.assertGreater(summary["attempts"], 14, "the campaign continues after a finding")
        # Progress snapshots account for seeds 0..12 of the crashed process; only the crash is lost.
        self.assertEqual(summary["unaccounted_attempts"], 1)
        self.assertEqual(summary["operations"], 3 * (summary["attempts"] - 1))
        record = json.loads(self.finding(summary).read_text())
        kept = self.finding(summary).parent / record["input"]
        self.assertEqual(kept.read_bytes(), b"bravo|seed=%d" % (START + 13))
        self.assertEqual(record["signal"], "SIGABRT")
        self.assertEqual(record["previous_seed_in_process"], START + 12)
        self.assertIn("heap-buffer-overflow", record["signature"])
        self.assertEqual(record["environment"]["FAKE_BEHAVIOUR"], "crash")

        quiet = contextlib.redirect_stdout(io.StringIO())
        with quiet, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(reproduce.main([str(self.finding(summary)), "--regenerate"]), reproduce.REPRODUCES)
            os.environ["FAKE_FIXED"] = "1"
            try:
                self.assertEqual(reproduce.main([str(self.finding(summary))]), reproduce.JUDGED)
            finally:
                del os.environ["FAKE_FIXED"]
        # The rerun after the repair passes the same seed range.
        os.environ["FAKE_FIXED"] = "1"
        try:
            self.output = self.root / "after-repair"
            code, summary = self.run_campaign(self.targets("crash", seed=13), "--seed-count", "30")
        finally:
            del os.environ["FAKE_FIXED"]
        self.assertEqual(code, campaign.EXIT_PASSED)
        self.assertEqual(summary["attempts"], 30)

    def test_reproduce_refuses_a_changed_corpus_and_minimizes(self):
        _, summary = self.run_campaign(self.targets("crash", seed=4))
        (self.corpus / "a.bin").write_bytes(b"changed")
        smaller = self.root / "minimized.bin"
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(reproduce.main([str(self.finding(summary)), "--regenerate"]), reproduce.UNJUDGED)
            self.assertEqual(reproduce.main([str(self.finding(summary)), "--minimize", str(smaller)]),
                             reproduce.REPRODUCES)
        self.assertEqual(smaller.read_bytes(), b"|seed=%d" % (START + 4))

    def test_timeout_on_known_seed(self):
        code, summary = self.run_campaign(self.targets("timeout", seed=5, budget=1.0, candidate_timeout_seconds=1.0),
                                          "--no-isolation-check")
        self.assertEqual(code, campaign.EXIT_FINDINGS)
        self.assertEqual(summary["timeouts"], 1)
        self.assertEqual(summary["findings"][0]["seed"], START + 5)
        record = json.loads(self.finding(summary).read_text())
        self.assertEqual(record["signal"], "SIGKILL")

    def test_rss_limit_on_known_seed(self):
        code, summary = self.run_campaign(self.targets("rss", seed=3, budget=1.0, rss_limit_mb=100),
                                          "--no-isolation-check")
        self.assertEqual(code, campaign.EXIT_FINDINGS)
        self.assertEqual(summary["rss_failures"], 1)
        self.assertEqual(summary["findings"][0]["seed"], START + 3)
        record = json.loads(self.finding(summary).read_text())
        self.assertGreater(record["rss_at_kill_mb"], 100)

    def test_failure_at_exit_bisects_to_one_seed(self):
        code, summary = self.run_campaign(self.targets("leak", seed=37, initial_count=64))
        self.assertEqual(code, campaign.EXIT_FINDINGS)
        self.assertEqual(summary["exit_failures"], 1)
        finding = summary["findings"][0]
        self.assertEqual((finding["seed"], finding["sanitizer"]), (START + 37, "LeakSanitizer"))
        self.assertTrue(finding["reproduces_in_isolation"])
        self.assertGreater(summary["bisection_runs"], 0)
        self.assertEqual(summary["operations"], 3 * summary["attempts"], "a failure at exit keeps its counts")

    def test_failure_at_exit_without_any_input_is_not_blamed_on_a_seed(self):
        code, summary = self.run_campaign(self.targets("leak-always", budget=1.0))
        self.assertEqual(code, campaign.EXIT_FINDINGS)
        self.assertEqual(len(summary["findings"]), 1, "an input-independent failure is kept once")
        finding = summary["findings"][0]
        self.assertTrue(finding["input_independent"])
        self.assertIsNone(finding["seed"])
        self.assertEqual(finding["sanitizer"], "LeakSanitizer")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(reproduce.main([str(self.finding(summary))]), reproduce.REPRODUCES)

    def test_a_repeated_signature_keeps_three_inputs_and_counts_the_rest(self):
        code, summary = self.run_campaign(self.targets("every-fifth", budget=30), "--seed-count", "60",
                                          "--no-isolation-check")
        self.assertEqual(code, campaign.EXIT_FINDINGS)
        self.assertEqual(summary["stop_reason"], "seed_range", "a known signature does not end the search")
        self.assertEqual(summary["attempts"], 60)
        self.assertEqual([item["seed"] for item in summary["findings"]], [START, START + 5, START + 10])
        self.assertEqual(summary["distinct_signatures"], 1)
        repeats = summary["repeated_findings"]
        self.assertEqual(len(repeats), 1)
        self.assertEqual(repeats[0]["count"], 9)
        self.assertEqual(repeats[0]["seeds"], [START + 5 * n for n in range(3, 12)])
        self.assertEqual(summary["crashes"], 12)
        kept = list((self.output / "fake" / "findings").glob("crash-seed-*.bin"))
        self.assertEqual(len(kept), 3)

    def test_a_batch_stopped_before_its_start_line_is_stopped_not_a_startup_failure(self):
        stopped = campaign.Outcome()
        stopped.stop_sent = True
        stopped.returncode = -signal.SIGTERM
        self.assertIsNone(stopped.start)
        self.assertEqual(stopped.kind, "stopped")
        # Without a stop from the driver the same exit is still a startup failure.
        unstopped = campaign.Outcome()
        unstopped.returncode = -signal.SIGTERM
        self.assertEqual(unstopped.kind, "startup")

    def test_a_signature_ignores_what_varies_between_hits_of_one_defect(self):
        def outcome(stderr):
            result = campaign.Outcome()
            result.stderr_tail = stderr
            return result
        leak = ("==1==ERROR: LeakSanitizer: detected memory leaks\n\n"
                "Direct leak of {0} byte(s) in 1 object(s) allocated from:\n"
                "    #0 0x7f186acf4c57 in operator new(unsigned long) asan_new_delete.cpp:95\n"
                "    #1 0x55d1 in std::make_unique<Shader>() unique_ptr.h:1077\n"
                "    #1 0x55d1 in std::__new_allocator<unsigned int>::allocate(unsigned long) new_allocator.h:151\n"
                "    #2 0x55d2 in ParserContext::CompileShader(void const*) SoftwareCompiledEffect.cpp:473\n"
                "    #3 0x55d3 in readsmallobjects mojoshader_effects.c:862\n"
                "    #4 0x55d4 in MOJOSHADER_compileEffect mojoshader_effects.c:1423\n\n"
                "SUMMARY: AddressSanitizer: {0} byte(s) leaked in 41 allocation(s).\n")
        first, second = outcome(leak.format(144)), outcome(leak.format(208))
        self.assertNotEqual(first.signature, second.signature)
        self.assertEqual(first.dedup_signature, second.dedup_signature)
        self.assertEqual(first.dedup_signature, "LeakSanitizer: ParserContext::CompileShader(void const*) <- "
                                                "readsmallobjects <- MOJOSHADER_compileEffect")
        ubsan = "../src/EffectParameter.cpp:400:52: runtime error: {0} is outside the range of type 'int'\n"
        self.assertEqual(outcome(ubsan.format("-nan")).dedup_signature,
                         outcome(ubsan.format("7.55556e+31")).dedup_signature)
        self.assertEqual(outcome(ubsan.format("-nan")).dedup_signature,
                         "../src/EffectParameter.cpp:400:52: runtime error")

    def test_max_findings_stops_the_target(self):
        code, summary = self.run_campaign(self.targets("every-crash", budget=30, max_findings=3),
                                          "--no-isolation-check")
        self.assertEqual(code, campaign.EXIT_FINDINGS)
        self.assertEqual(len(summary["findings"]), 3)
        self.assertEqual(summary["stop_reason"], "max_findings")
        self.assertEqual([item["seed"] for item in summary["findings"]], [START, START + 1, START + 2])

    # -- interruption --------------------------------------------------------------------------
    def interrupt(self, targets, number, *arguments):
        command = [sys.executable, str(HERE / "run_long_campaign.py"), "--targets-file", str(targets),
                   "--output-dir", str(self.output), "--seed-start", str(START), *arguments]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        summary_path = self.output / "fake" / "summary.json"
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if summary_path.is_file() and json.loads(summary_path.read_text())["attempts"] > 0:
                break
            time.sleep(0.1)
        process.send_signal(number)
        output, _ = process.communicate(timeout=60)
        return process.returncode, output.decode(errors="replace")

    def test_interrupted_run_stops_cleanly_with_a_partial_summary(self):
        second = {"name": "second", "binary": str(FAKE), "launcher": [sys.executable],
                  "corpus": [{"path": str(self.corpus), "suffixes": [".bin"]}], "budget_seconds": 1}
        targets = self.targets(budget=60, extra=[second], env={"FAKE_DELAY": "0.01"})
        code, output = self.interrupt(targets, signal.SIGINT, "--jobs", "1")
        self.assertEqual(code, campaign.EXIT_PASSED, output)
        summary = self.summary()
        self.assertEqual(summary["status"], "interrupted")
        self.assertEqual(summary["stop_reason"], "interrupted")
        self.assertGreater(summary["attempts"], 0)
        self.assertEqual(summary["seed_range"]["next_unattempted"], START + summary["attempts"])
        self.assertEqual(summary["operations"], 3 * summary["attempts"], "the harness finished its candidate")
        self.assertLess(summary["elapsed_seconds"], 30)
        self.assertEqual(self.summary("second")["status"], "not_run")
        campaign_summary = json.loads((self.output / "campaign.json").read_text())
        self.assertEqual(campaign_summary["interrupted_by"], "SIGINT")

    def test_interrupted_run_with_a_finding_exits_nonzero(self):
        targets = self.targets("crash", seed=2, budget=60, env={"FAKE_DELAY": "0.01", "FAKE_BEHAVIOUR": "crash",
                                                                 "FAKE_SEED": str(START + 2)})
        code, output = self.interrupt(targets, signal.SIGTERM, "--no-isolation-check")
        self.assertEqual(code, campaign.EXIT_FINDINGS, output)
        summary = self.summary()
        self.assertEqual(summary["status"], "findings")
        self.assertEqual(summary["findings"][0]["seed"], START + 2)
        self.assertEqual(json.loads((self.output / "campaign.json").read_text())["interrupted_by"], "SIGTERM")

    # -- planning ------------------------------------------------------------------------------
    def test_plan_refuses_a_budget_longer_than_the_wall_limit(self):
        targets = self.targets(budget=600)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(campaign.main(["--targets-file", str(targets), "--output-dir", str(self.output),
                                            "--plan", "--max-wall-seconds", "300"]), campaign.EXIT_FAILED)
            self.assertEqual(campaign.main(["--targets-file", str(targets), "--output-dir", str(self.output),
                                            "--plan", "--max-wall-seconds", "900"]), campaign.EXIT_PASSED)
        self.assertFalse(self.output.exists(), "a plan runs nothing")
        self.assertEqual(campaign.plan_wall_seconds([100, 100, 50], 2), 100 + 120 + 50 + 120)


if __name__ == "__main__":
    unittest.main()
