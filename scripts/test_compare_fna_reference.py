#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""CPU-only CLI regressions for the FNA/CNA reference comparator (MTH-CONF-001)."""

import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("compare-fna-reference.py")
ROOT = SCRIPT.parent.parent


class CompareFnaReferenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="compare-fna-reference-")
        self.addCleanup(self.directory.cleanup)
        self.fna = Path(self.directory.name) / "fna.json"
        self.cna = Path(self.directory.name) / "cna.json"

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(SCRIPT), str(self.fna), str(self.cna), *args],
                              capture_output=True, text=True, timeout=10)

    def check_raw(self, fna, cna, expected=0, args=("--category", "Probe")):
        self.fna.write_text(fna, encoding="utf-8")
        self.cna.write_text(cna, encoding="utf-8")
        result = self.run_cli(*args)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        if expected == 0:
            self.assertIn("PASS:", result.stdout)
        else:
            self.assertNotIn("PASS:", result.stdout)
        return result

    def check_documents(self, fna, cna, expected=0, args=("--category", "Probe")):
        return self.check_raw(json.dumps(fna), json.dumps(cna), expected, args)

    def check_values(self, fna, cna, expected=0, args=("--category", "Probe")):
        return self.check_documents({"Probe": {"value": fna}}, {"Probe": {"value": cna}},
                                    expected, args)

    def test_equal_integers_remain_exact_even_beyond_binary64_range(self):
        for value in (0, 2**53 + 1, -(2**63), 2**64 - 1, 10**400):
            with self.subTest(value=value):
                self.check_values(value, value)

    def test_adjacent_large_integers_differ_in_both_directions(self):
        for a, b in ((2**53, 2**53 + 1), (-(2**53), -(2**53) - 1), (10**400, 10**400 + 1)):
            for left, right in ((a, b), (b, a)):
                with self.subTest(left=left, right=right):
                    self.check_values(left, right, 1)

    def test_tolerance_never_applies_to_integer_pairs(self):
        self.check_values(1, 2, 1, ("--category", "Probe", "--tolerance", "100"))

    def test_mixed_numeric_tokens_support_integral_float_serialization(self):
        for left, right in ((1, 1.0), (1.0, 1), (2**53, float(2**53))):
            with self.subTest(left=left, right=right):
                self.check_values(left, right)

    def test_mixed_numeric_tokens_do_not_round_large_integers(self):
        for integer, floating in ((2**53 + 1, float(2**53)), (10**400, 1.0)):
            for left, right in ((integer, floating), (floating, integer)):
                with self.subTest(left=left, right=right):
                    self.check_values(left, right, 1)

    def test_default_tolerance_is_per_float_field(self):
        self.check_values({"x": 0, "y": 2.0}, {"x": 0.00009, "y": 2.00009})
        self.check_values({"x": 0, "y": 2.0}, {"x": 0.00009, "y": 2.0002}, 1)

    def test_default_tolerance_boundary_is_inclusive(self):
        self.check_values(0.0, 1e-4)
        self.check_values(0.0, math.nextafter(1e-4, math.inf), 1)

    def test_explicit_tolerance_is_absolute_not_relative(self):
        args = ("--category", "Probe", "--tolerance", "0.125")
        self.check_values(0.0, 0.125, args=args)
        self.check_values(0.0, math.nextafter(0.125, math.inf), 1, args)
        self.check_values(1e12, 1e12 + 1, 1, args)

    def test_zero_tolerance_requires_equal_numeric_values(self):
        args = ("--category", "Probe", "--tolerance", "0")
        self.check_values(1, 1.0, args=args)
        self.check_values(1.0, math.nextafter(1.0, math.inf), 1, args)

    def test_signed_zero_is_equivalent(self):
        for left, right in ((0.0, -0.0), (-0.0, 0.0), (0, -0.0), (-0.0, 0)):
            with self.subTest(left=left, right=right):
                self.check_values(left, right, args=("--category", "Probe", "--tolerance", "0"))
        self.check_raw('{"Probe":{"value":-0}}', '{"Probe":{"value":0}}')

    def test_nonfinite_numbers_are_unsupported_even_when_identical(self):
        for token in ("NaN", "Infinity", "-Infinity", "1e999", "-1e999"):
            bad = '{"Probe":{"value":' + token + '}}'
            good = '{"Probe":{"value":1.0}}'
            for left, right in ((good, bad), (bad, good), (bad, bad)):
                with self.subTest(token=token, left=left, right=right):
                    result = self.check_raw(left, right, 1)
                    self.assertIn("unsupported non-finite", result.stderr)

    def test_nonfinite_in_unselected_or_extra_fields_is_invalid(self):
        valid = '{"Probe":{"value":1}}'
        for invalid in ('{"Probe":{"value":1,"extra":NaN}}',
                        '{"Probe":{"value":1},"Metadata":{"x":Infinity}}'):
            with self.subTest(invalid=invalid):
                self.check_raw(valid, invalid, 1)
                self.check_raw(invalid, valid, 1)

    def test_invalid_tolerances_are_argument_errors(self):
        for token in ("NaN", "inf", "-inf", "-0.001", "1e999", "oops"):
            with self.subTest(token=token):
                result = self.check_values(1.0, 1.0, 2,
                                           ("--category", "Probe", "--tolerance=" + token))
                self.assertIn("finite nonnegative", result.stderr)

    def test_boolean_is_not_a_number(self):
        for a, b in ((True, 1), (False, 0), (True, 1.0), (False, 0.0)):
            for left, right in ((a, b), (b, a)):
                with self.subTest(left=left, right=right):
                    self.check_values(left, right, 1)
        self.check_values(True, True)
        self.check_values(False, False)
        self.check_values(True, False, 1)

    def test_strings_and_null_have_strict_types_and_values(self):
        for value in ("", "NaN", "Infinity", "ffffffffffffffff", None):
            with self.subTest(value=value):
                self.check_values(value, value)
        for left, right in (("1", 1), ("true", True), (None, ""), (None, False),
                            ("ffffffffffffffff", "fffffffffffffffe")):
            with self.subTest(left=left, right=right):
                self.check_values(left, right, 1)
                self.check_values(right, left, 1)

    def test_object_keys_are_directional(self):
        self.check_values({"x": 1}, {"x": 1, "Extension": 2})
        result = self.check_values({"x": 1, "required": 2}, {"x": 1}, 1)
        self.assertIn("/Probe/value/required", result.stdout)
        self.assertIn("missing on CNA side", result.stdout)

    def test_container_types_must_match_including_empty_nested_objects(self):
        for left, right in (({}, 1), ({}, []), ([], {}), ([], None), ({"x": 1}, [1])):
            with self.subTest(left=left, right=right):
                self.check_values({"witness": 1, "nested": left},
                                  {"witness": 1, "nested": right}, 1)

    def test_arrays_compare_each_field_with_tolerance(self):
        self.check_values([{"x": 1.0}, 0.0], [{"x": 1.00001, "extra": 3}, 0.00001])
        result = self.check_values([{"x": 1.0}], [{"x": 1.001}], 1)
        self.assertIn("/Probe/value/0/x", result.stdout)
        self.check_values([True], [1], 1)

    def test_array_lengths_and_order_are_exact(self):
        self.check_values([1, 2], [2, 1], 1)
        self.check_values([1], [1, 2], 1)
        self.check_values([1, 2], [1], 1)

    def test_keys_with_path_separators_are_literal(self):
        self.check_documents({"Probe/~": {"a/b~": 1}}, {"Probe/~": {"a/b~": 1}},
                             args=("--category", "Probe/~"))
        result = self.check_values({"a/b~": 1}, {"a/b~": 2}, 1)
        self.assertIn("/Probe/value/a~1b~0", result.stdout)

    def test_all_default_categories_are_required(self):
        valid = {key: {"value": 1} for key in ("NonRenderingApis", "PackedVector", "Viewport")}
        self.check_documents(valid, valid, args=())
        for missing in valid:
            incomplete = {key: value for key, value in valid.items() if key != missing}
            for left, right in ((incomplete, valid), (valid, incomplete)):
                with self.subTest(missing=missing, left=left):
                    result = self.check_documents(left, right, 1, args=())
                    self.assertIn("required category", result.stderr)

    def test_missing_requested_category_fails_alongside_valid_category(self):
        valid = {"Probe": {"value": 1}, "Required": {"value": 2}}
        incomplete = {"Probe": {"value": 1}}
        for left, right in ((valid, incomplete), (incomplete, valid), (incomplete, incomplete)):
            with self.subTest(left=left, right=right):
                self.check_documents(left, right, 1,
                                     ("--category", "Probe", "--category", "Required"))

    def test_no_categories_available_fails(self):
        self.check_documents({}, {}, 1)

    def test_explicit_categories_replace_defaults(self):
        self.check_values(1, 1)

    def test_duplicate_category_option_is_compared_once(self):
        result = self.check_values(1, 1, args=("--category", "Probe", "--category", "Probe"))
        self.assertIn("categories ['Probe']", result.stdout)

    def test_empty_categories_fail_on_either_side(self):
        valid = {"Probe": {"value": 1}}
        for empty in ({}, [], {"nested": {}}, {"nested": [[], {}]}):
            invalid = {"Probe": empty}
            for left, right in ((invalid, valid), (valid, invalid), (invalid, invalid)):
                with self.subTest(empty=empty, left=left):
                    result = self.check_documents(left, right, 1)
                    self.assertIn("no scalar leaves", result.stderr)

    def test_empty_selected_category_cannot_hide_behind_valid_one(self):
        document = {"Probe": {"value": 1}, "Empty": {}}
        self.check_documents(document, document, 1,
                             ("--category", "Probe", "--category", "Empty"))

    def test_scalar_categories_are_invalid(self):
        for scalar in (1, 1.0, True, "text", None):
            with self.subTest(scalar=scalar):
                result = self.check_documents({"Probe": scalar}, {"Probe": scalar}, 1)
                self.assertIn("must be an object or array", result.stderr)

    def test_nested_empty_containers_and_null_are_valid_in_populated_categories(self):
        value = {"annotations": [], "members": {}, "semantic": None}
        self.check_documents({"Probe": [value]}, {"Probe": [value]})

    def test_root_must_be_an_object(self):
        valid = '{"Probe":{"value":1}}'
        for bad in ("null", "[]", '["Probe"]', '"Probe"', "1", "true"):
            for left, right in ((bad, valid), (valid, bad)):
                with self.subTest(left=left, right=right):
                    result = self.check_raw(left, right, 1)
                    self.assertIn("document root must be an object", result.stderr)

    def test_malformed_json_is_rejected_on_either_side(self):
        valid = '{"Probe":{"value":1}}'
        for bad in ("", "{", '{"Probe":{"value":1,}}', valid + valid,
                    '{"Probe":{"value":01}}', '{"Probe":{"value":nan}}'):
            for left, right in ((bad, valid), (valid, bad)):
                with self.subTest(left=left, right=right):
                    self.check_raw(left, right, 1)

    def test_duplicate_keys_are_invalid_including_unselected_metadata(self):
        valid = '{"Probe":{"value":1}}'
        for invalid in ('{"Probe":{"value":2,"value":1}}',
                        '{"Probe":{},"Probe":{"value":1}}',
                        '{"Probe":{"value":1},"metadata":{"x":1,"x":1}}'):
            for left, right in ((invalid, valid), (valid, invalid)):
                with self.subTest(left=left, right=right):
                    result = self.check_raw(left, right, 1)
                    self.assertIn("duplicate object key", result.stderr)

    def test_unreadable_file_reports_input_error(self):
        result = self.run_cli("--category", "Probe")
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid FNA input", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_invalid_utf8_reports_input_error(self):
        self.fna.write_bytes(b'\xff')
        result = self.run_cli("--category", "Probe")
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid FNA input", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_excessive_nesting_fails_without_traceback(self):
        nested = '{"Probe":' + '[' * 2000 + '1' + ']' * 2000 + '}'
        self.check_raw(nested, nested, 1)

    def test_committed_effect_dumps_remain_valid(self):
        for mode in ("reflection", "states", "pixels"):
            with self.subTest(mode=mode):
                fixture = ROOT / "tests/fixtures/compiled-effects" / f"fna-effect-{mode}.json"
                raw = fixture.read_text(encoding="utf-8")
                args = []
                for key, value in json.loads(raw).items():
                    if isinstance(value, (dict, list)):
                        args.extend(("--category", key))
                self.assertTrue(args)
                self.check_raw(raw, raw, args=args)


if __name__ == "__main__":
    unittest.main()
