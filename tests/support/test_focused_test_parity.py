#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Adversarial process/report controls for the focused parity gate."""
import contextlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import focused_test_parity as parity


class FocusedParityControls(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="cna-parity-controls-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.binary = self.root / "focused"
        self.name = "Suite.Case"

    def report(self, content):
        def write_report(*_):
            (self.root / "focused.xml").write_text(content, encoding="utf-8")
            return ""
        return mock.patch.object(parity, "run", side_effect=write_report)

    def read_report(self, content):
        with self.report(content):
            return parity.outcome(self.binary, self.name, self.root, {}, self.root)

    def test_empty_inventory_is_rejected(self):
        with mock.patch.object(parity, "run", return_value="Running main()\n"):
            with self.assertRaisesRegex(RuntimeError, "registered no tests"):
                parity.inventory(self.binary, self.root, {})

    def test_duplicate_registration_is_rejected(self):
        with mock.patch.object(parity, "run", return_value="Suite.\n  Case\n  Case\n"):
            with self.assertRaisesRegex(RuntimeError, "duplicate registration"):
                parity.inventory(self.binary, self.root, {})

    def test_parameterized_names_retain_their_identity(self):
        with mock.patch.object(parity, "run", return_value=
                "Instantiation/Suite. # TypeParam = int\n  Case/0 # GetParam() = 7\n"):
            self.assertEqual(parity.inventory(self.binary, self.root, {}),
                             {"Instantiation/Suite.Case/0"})

    def test_nonzero_process_exit_never_qualifies(self):
        for exit_code in (1, -11, 77):
            with self.subTest(exit_code=exit_code):
                process = subprocess.CompletedProcess([], exit_code, "test output")
                with mock.patch.object(parity.subprocess, "run", return_value=process):
                    with self.assertRaisesRegex(RuntimeError, f"exited {exit_code}"):
                        parity.run(self.binary, [], self.root, {})

    def test_empty_or_multiple_case_reports_are_rejected(self):
        one = '<testcase classname="Suite" name="Case" status="run" result="completed"/>'
        for cases in ("", one + one):
            with self.subTest(cases=cases):
                with self.assertRaisesRegex(RuntimeError, "exactly one case"):
                    self.read_report("<testsuites>" + cases + "</testsuites>")

    def test_wrong_case_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "missing, unexecuted or failed"):
            self.read_report('<testsuites><testcase classname="Wrong" name="Case" '
                             'status="run" result="completed"/></testsuites>')

    def test_notrun_report_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "missing, unexecuted or failed"):
            self.read_report('<testsuites><testcase classname="Suite" name="Case" '
                             'status="notrun" result="suppressed"/></testsuites>')

    def test_failure_element_cannot_be_a_pass(self):
        with self.assertRaisesRegex(RuntimeError, "missing, unexecuted or failed"):
            self.read_report('<testsuites><testcase classname="Suite" name="Case" '
                             'status="run" result="completed"><failure/></testcase></testsuites>')

    def test_pass_and_skip_are_distinct_outcomes(self):
        self.assertEqual(self.read_report('<testsuites><testcase classname="Suite" name="Case" '
            'status="run" result="completed"/></testsuites>'), "PASS")
        self.assertEqual(self.read_report('<testsuites><testcase classname="Suite" name="Case" '
            'status="run" result="skipped"><skipped/></testcase></testsuites>'), "SKIP")

    def test_aggregate_skip_and_focused_pass_are_rejected(self):
        self.binary.touch()
        aggregate = self.root / "aggregate"
        aggregate.touch()
        arguments = ["parity", "--aggregate", str(aggregate), "--focused", str(self.binary),
                     "--source-root", str(self.root), "--case", self.name]
        with mock.patch("sys.argv", arguments), \
             mock.patch.object(parity, "inventory", return_value={self.name}), \
             mock.patch.object(parity, "outcome", side_effect=["SKIP", "PASS"]), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(parity.main(), 1)

    def test_focused_only_registration_is_rejected(self):
        self.binary.touch()
        aggregate = self.root / "aggregate"
        aggregate.touch()
        arguments = ["parity", "--aggregate", str(aggregate), "--focused", str(self.binary),
                     "--source-root", str(self.root)]
        with mock.patch("sys.argv", arguments), \
             mock.patch.object(parity, "inventory", side_effect=[{"Suite.Case"}, {"Extra.Case"}]), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(parity.main(), 1)


    def test_aggregate_only_registration_is_rejected_by_complete_inventory(self):
        self.binary.touch()
        aggregate = self.root / "aggregate"
        aggregate.touch()
        arguments = ["parity", "--aggregate", str(aggregate), "--focused", str(self.binary),
                     "--source-root", str(self.root), "--complete-inventory"]
        with mock.patch("sys.argv", arguments), \
             mock.patch.object(parity, "inventory", side_effect=[{"Suite.Case", "Missing.Case"}, {"Suite.Case"}]), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(parity.main(), 1)


if __name__ == "__main__":
    unittest.main()
