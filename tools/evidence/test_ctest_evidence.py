#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Regression fixtures exercise claims, not source-presence approximations."""

import argparse
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import ctest_evidence as evidence

FIXTURES = Path(__file__).resolve().parent / "fixtures"
TOOL = Path(evidence.__file__)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cna-evidence-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def evaluate(self, name, code=0, log=None, selection="selected"):
        return evidence.evaluate(FIXTURES / "selected.json", FIXTURES / f"{selection}.json",
                                 FIXTURES / f"{name}.xml", log or FIXTURES / f"{name}.log", code)

    def test_pass_has_reconciled_counts(self):
        result = self.evaluate("pass")
        self.assertEqual(result["outcome"], "pass")
        self.assertEqual([result[key] for key in ("configured", "selected", "executed", "passed",
                                                "failed", "skipped", "not_run")], [2, 2, 2, 2, 0, 0, 0])
        self.assertEqual(result["ctest_wall_seconds"], 0.02)

    def test_failure_is_not_a_skip(self):
        result = self.evaluate("fail", 8)
        self.assertEqual(result["outcome"], "fail")
        self.assertEqual(result["failed"], 1)
        self.assertEqual(result["executed"], 2)
        self.assertIn("expected 1", result["tests"][1]["output"])

    def test_required_skip_is_not_pass_even_with_exit_zero(self):
        result = self.evaluate("skip")
        self.assertEqual(result["outcome"], "required-skip")
        self.assertEqual(result["skipped"], 1)
        self.assertEqual(result["executed"], 1)
        self.assertEqual(result["tests"][1]["reason"], "SKIP_RETURN_CODE=77")
        self.assertIn("device unavailable", result["tests"][1]["output"])

    def test_zero_tests_is_distinct_even_with_exit_zero(self):
        result = self.evaluate("zero", selection="empty")
        self.assertEqual(result["outcome"], "zero-tests-selected")
        self.assertEqual(result["selected"], 0)
        self.assertEqual(result["configured"], 2)
        self.assertEqual(result["executed"], 0)

    def test_not_run_is_failure_with_no_executed_claim(self):
        result = self.evaluate("not-run", 8)
        self.assertEqual(result["outcome"], "fail")
        self.assertEqual(result["not_run"], 1)
        self.assertEqual(result["executed"], 1)
        self.assertEqual(result["tests"][1]["reason"], "Unable to find executable")

    def test_missing_and_truncated_logs_never_pass(self):
        for log in (self.directory / "missing.log", FIXTURES / "truncated.log"):
            with self.subTest(log=log):
                result = self.evaluate("pass", log=log)
                self.assertEqual(result["outcome"], "incomplete")
                self.assertTrue(result["validation_errors"])

    def test_corrupt_xml_and_each_incorrect_suite_count(self):
        xml = self.directory / "test.xml"
        for text in ("<testsuite", (FIXTURES / "bad-count.xml").read_text(), *(
                (FIXTURES / "pass.xml").read_text().replace(f'{key}="0"', f'{key}="1"')
                for key in ("failures", "skipped", "disabled"))):
            with self.subTest(text=text):
                xml.write_text(text)
                result = evidence.evaluate(FIXTURES / "selected.json", FIXTURES / "selected.json",
                                           xml, FIXTURES / "pass.log", 0)
                self.assertEqual(result["outcome"], "incomplete")

    def test_missing_test_case_and_wrong_names_never_pass(self):
        for replacement in ("other.test", "fast.first"):
            xml = self.directory / "test.xml"
            xml.write_text((FIXTURES / "pass.xml").read_text().replace("fast.second", replacement))
            result = evidence.evaluate(FIXTURES / "selected.json", FIXTURES / "selected.json",
                                       xml, FIXTURES / "pass.log", 0)
            self.assertEqual(result["outcome"], "incomplete")

    def test_log_summary_and_result_rows_must_agree(self):
        original = (FIXTURES / "pass.log").read_text()
        for text in (original.replace("out of 2", "out of 3"),
                     original.replace("2/2 Test", "1/2 Test"),
                     original.replace("fast.second", "other.test"),
                     original.replace("Passed", "***Failed", 1),
                     "\n".join(line for line in original.splitlines() if "1/2 Test" not in line)):
            log = self.directory / "test.log"
            log.write_text(text)
            self.assertEqual(self.evaluate("pass", log=log)["outcome"], "incomplete")

    def test_nonzero_exit_and_interruption_never_pass(self):
        self.assertEqual(self.evaluate("pass", 8)["outcome"], "fail")
        self.assertEqual(self.evaluate("pass", None)["outcome"], "incomplete")

    def test_disabled_test_is_not_execution(self):
        xml = self.directory / "disabled.xml"
        xml.write_text((FIXTURES / "skip.xml").read_text().replace('disabled="0" skipped="1"',
                      'disabled="1" skipped="0"').replace('status="notrun"', 'status="disabled"'))
        log = self.directory / "disabled.log"
        log.write_text((FIXTURES / "skip.log").read_text().replace("***Skipped", "***Not Run (Disabled)"))
        result = evidence.evaluate(FIXTURES / "selected.json", FIXTURES / "selected.json", xml, log, 0)
        self.assertEqual(result["outcome"], "fail")
        self.assertEqual((result["executed"], result["not_run"]), (1, 1))

    def record(self, name="pass", code=0, selection="selected"):
        paths = {"configured": FIXTURES / "selected.json", "selected": FIXTURES / f"{selection}.json",
                 "junit": FIXTURES / f"{name}.xml", "log": FIXTURES / f"{name}.log"}
        artifacts = {}
        for key, path in paths.items():
            destination = self.directory / path.name
            shutil.copyfile(path, destination)
            artifacts[key] = evidence.artifact(destination)
        source = {"path": "/source", "git_head": "a" * 40, "dirty": True}
        return {"schema_version": 1, "artifacts": artifacts, "ctest_returncode": code,
                "capture_errors": [], "result": self.evaluate(name, code, selection=selection),
                "source": source, "source_after": source, "build": self.build_facts(),
                "wall_seconds": 0.02, "started_utc": "2026-11-02T00:00:00+00:00", "evidence_kind": "metadata",
                "selection": {"regex": None, "label": None, "config": None},
                "configured_returncode": 0, "selected_returncode": 0,
                "commands": {key: ["ctest"] for key in ("configured", "selected", "run")}}

    def build_facts(self):
        return {"path": "/existing-build", "cache_sha256": "b" * 64,
                "compiler": {"path": "/compiler", "id": "GNU", "version": "14.2.0"},
                "cmake_cache": {"CMAKE_HOME_DIRECTORY": "/source", "CMAKE_GENERATOR": "Ninja",
                    "CMAKE_BUILD_TYPE": "Release", "CNA_GRAPHICS_RENDERER": "SOFTWARE",
                    "CNA_GRAPHICS_RENDERERS": "", "CNA_PLATFORM": "SDL3"}}

    def test_retained_record_rechecks_hashes_and_claims(self):
        record = self.record()
        self.assertEqual(evidence.validate(record)["outcome"], "pass")
        forged = copy.deepcopy(record)
        forged["result"]["passed"] = 999
        self.assertEqual(evidence.validate(forged)["outcome"], "incomplete")
        log = Path(record["artifacts"]["log"]["path"])
        log.write_text(log.read_text() + "changed after capture\n")
        self.assertEqual(evidence.validate(record)["outcome"], "incomplete")
        log.unlink()
        self.assertEqual(evidence.validate(record)["outcome"], "incomplete")

    def test_pass_claim_needs_provenance_selection_and_wall_time(self):
        record = self.record()
        for key in ("source", "build", "selection", "wall_seconds", "commands"):
            with self.subTest(key=key):
                missing = copy.deepcopy(record)
                del missing[key]
                self.assertEqual(evidence.validate(missing)["outcome"], "incomplete")

    def test_malformed_json_structures_are_incomplete(self):
        record = self.record()
        record["artifacts"] = []
        self.assertEqual(evidence.validate(record)["outcome"], "incomplete")
        malformed = self.directory / "inventory.json"
        for data in ([], {"kind": "ctestInfo", "version": []}):
            malformed.write_text(json.dumps(data))
            result = evidence.evaluate(malformed, FIXTURES / "selected.json", FIXTURES / "pass.xml",
                                       FIXTURES / "pass.log", 0)
            self.assertEqual(result["outcome"], "incomplete")

    def test_cli_validates_all_acceptance_outcomes_with_exit_codes(self):
        for fixture, code, selection, outcome in (("pass", 0, "selected", "pass"),
                ("fail", 8, "selected", "fail"), ("skip", 0, "selected", "required-skip"),
                ("zero", 0, "empty", "zero-tests-selected")):
            with self.subTest(outcome=outcome):
                record = self.record(fixture, code, selection)
                path = self.directory / "record.json"
                path.write_text(json.dumps(record))
                result = subprocess.run([sys.executable, str(TOOL), "validate", str(path)],
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode, 0 if outcome == "pass" else 1, result.stderr)
                self.assertEqual(json.loads(result.stdout)["outcome"], outcome)

    def test_cli_invalid_or_missing_record_is_incomplete(self):
        path = self.directory / "record.json"
        for contents in (None, "{", "{}"):
            if contents is not None:
                path.write_text(contents)
            result = subprocess.run([sys.executable, str(TOOL), "validate", str(path)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(json.loads(result.stdout or result.stderr)["outcome"], "incomplete")

    def test_capture_retains_selection_commands_and_refuses_stale_output(self):
        # Fake the CTest child only: no new build/configure tree is made by this unit test.
        args = argparse.Namespace(build_dir=self.directory / "existing-build", output_dir=self.directory / "run",
                ctest="ctest", config="Release", regex="^fast", label="tools", evidence_kind="metadata", context=[])
        cache = args.build_dir / "CMakeCache.txt"
        facts = self.build_facts()

        def child(command, stdout, **kwargs):
            if "--show-only=json-v1" in command:
                stdout.write((FIXTURES / "selected.json").read_bytes())
            else:
                shutil.copyfile(FIXTURES / "pass.xml", args.output_dir / "ctest.xml")
                shutil.copyfile(FIXTURES / "pass.log", args.output_dir / "ctest.log")
            return subprocess.CompletedProcess(command, 0, stderr=b"")

        actual_hash = evidence.file_sha256
        with patch.object(evidence, "build_identity", return_value=facts), \
                patch.object(evidence, "source_identity", return_value={"path": "/source", "git_head": "a" * 40, "dirty": True}), \
                patch.object(evidence.subprocess, "run", side_effect=child), \
                patch.object(evidence, "file_sha256", side_effect=lambda p: "b" * 64 if p == cache else actual_hash(p)):
            path = evidence.run(args)
            record = json.loads(path.read_text())
            self.assertEqual(evidence.validate(record)["outcome"], "pass")
            self.assertEqual(record["selection"], {"regex": "^fast", "label": "tools", "config": "Release"})
            self.assertNotIn("-R", record["commands"]["configured"])
            self.assertIn("-R", record["commands"]["selected"])
            self.assertIn("-L", record["commands"]["run"])
            self.assertGreaterEqual(record["wall_seconds"], 0)
            with self.assertRaises(FileExistsError):
                evidence.run(args)


if __name__ == "__main__":
    unittest.main()
