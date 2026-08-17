# SPDX-License-Identifier: MS-PL
"""Unit tests for the GLTF-015 Validator gate; no validator binary or network required."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path
from unittest import mock

from .corpus import all_fixtures
from .validator import ValidatorError, load_validator_pin, validate_emission


def _report(code: str | None) -> tuple[subprocess.CompletedProcess[str], dict]:
    messages = [] if code is None else [{"code": code, "severity": 0}]
    errors = 0 if code is None else 1
    return subprocess.CompletedProcess([], errors, "", ""), {
        "validatorVersion": "2.0.0-dev.3.10",
        "issues": {
            "numErrors": errors,
            "numWarnings": 0,
            "messages": messages,
        },
    }


class ValidatorGateTests(unittest.TestCase):
    @staticmethod
    def _fixture(fixture_id: str):
        return next(fixture for fixture in all_fixtures() if fixture.id == fixture_id)

    @staticmethod
    def _files(fixture_id: str) -> dict[str, bytes]:
        return {f"{fixture_id}.gltf": b"{}", f"{fixture_id}.glb": b"glTF"}

    @staticmethod
    def _version_result() -> subprocess.CompletedProcess[str]:
        return subprocess.CompletedProcess(
            [], 64, "glTF 2.0 Validator, version 2.0.0-dev.3.10\n", "")

    def test_pin_names_an_exact_release_and_digest(self):
        pin = load_validator_pin()
        self.assertEqual("2.0.0-dev.3.10", pin["releaseVersion"])
        self.assertEqual(64, len(pin["ciArtifact"]["sha256"]))

    def test_undeclared_malformed_fixture_fails_generation(self):
        fixture = self._fixture("glb-basic")
        with mock.patch("gltf_fixtures.validator.subprocess.run",
                        return_value=self._version_result()), \
             mock.patch("gltf_fixtures.validator._report",
                        return_value=_report("UNDECLARED_TEST_ERROR")):
            with self.assertRaisesRegex(ValidatorError, "UNDECLARED_TEST_ERROR"):
                validate_emission([fixture], self._files(fixture.id), Path("validator"))

    def test_deliberate_bad_fixture_requires_its_exact_error(self):
        fixture = self._fixture("bad-index-out-of-range")
        with mock.patch("gltf_fixtures.validator.subprocess.run",
                        return_value=self._version_result()), \
             mock.patch("gltf_fixtures.validator._report",
                        return_value=_report("ACCESSOR_INDEX_OOB")):
            summary = validate_emission(
                [fixture], self._files(fixture.id), Path("validator"))
        self.assertEqual(0, summary["validContainers"])
        self.assertEqual(2, summary["expectedInvalidContainers"])

    def test_bad_fixture_with_a_different_error_fails(self):
        fixture = self._fixture("bad-index-out-of-range")
        with mock.patch("gltf_fixtures.validator.subprocess.run",
                        return_value=self._version_result()), \
             mock.patch("gltf_fixtures.validator._report",
                        return_value=_report("SOME_OTHER_ERROR")):
            with self.assertRaisesRegex(ValidatorError, "SOME_OTHER_ERROR"):
                validate_emission([fixture], self._files(fixture.id), Path("validator"))


if __name__ == "__main__":
    unittest.main()
