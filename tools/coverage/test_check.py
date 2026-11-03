#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""The coverage gate must refuse missing, incompatible and stale evidence."""

import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))
SPEC = importlib.util.spec_from_file_location('coverage_check', Path(__file__).with_name('check.py'))
check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(check)


class ThresholdTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.artifact = Path(self.temp.name) / 'object.o'
        self.artifact.write_bytes(b'object')
        self.profile = self.artifact.with_suffix('.gcda')
        self.profile.write_bytes(b'profile')
        self.policy = {'schema': 1, 'gcc_version': '14.2.0', 'targets': ['Tests'],
                       'configuration': {'CNA_PLATFORM': 'HEADLESS'},
                       'modules': {'math': {'src': {'lines': {
                           'minimum_percent': 80, 'minimum_total': 95}}}}}
        self.report = {'schema': 1, 'status': 'passed', 'build_exit_code': 0,
                       'gcov_version': 'gcov (GCC) 14.2.0\n',
                       'prepared': {'targets': ['Tests'], 'configuration': {
                           'cache': {'CNA_PLATFORM': 'HEADLESS'}}},
                       'runs': [{'target': 'Tests', 'exit_code': 0,
                                 'cases': [{'status': 'passed'}]}],
                       'artifacts': {str(self.artifact): check.digest(self.artifact)},
                       'profiles': {str(self.profile): check.digest(self.profile)},
                       'modules': {'math': {'src': {'lines': {
                           'covered': 80, 'total': 100, 'percent': 0}}}}}
        patch = mock.patch.object(check, 'verify_prepared')
        self.verify = patch.start()
        self.addCleanup(patch.stop)
        patch = mock.patch.object(check, 'counter_quality')
        self.quality = patch.start()
        self.addCleanup(patch.stop)

    def test_exact_floor_passes_and_stored_percent_is_ignored(self):
        self.assertTrue(check.check(self.report, self.policy)[0]['passed'])
        self.verify.assert_called_once_with(self.report['prepared'])

    def test_rounded_up_percent_does_not_hide_a_regression(self):
        self.policy['modules']['math']['src']['lines']['minimum_percent'] = 80.001
        self.report['modules']['math']['src']['lines']['percent'] = 100
        self.assertFalse(check.check(self.report, self.policy)[0]['passed'])

    def test_failed_empty_skipped_and_missing_target_runs_are_rejected(self):
        for runs in ([], [{'target': 'Tests', 'exit_code': 1, 'cases': [{'status': 'passed'}]}],
                     [{'target': 'Tests', 'exit_code': 0, 'cases': []}],
                     [{'target': 'Tests', 'exit_code': 0, 'cases': [{'status': 'skipped'}]}],
                     [{'target': 'Tests', 'exit_code': 0, 'cases': [{'status': 'failed'}]}]):
            with self.subTest(runs=runs), self.assertRaises(check.CoverageError):
                report = copy.deepcopy(self.report)
                report['runs'] = runs
                check.check(report, self.policy)

    def test_incompatible_profile_and_unfinished_reports_are_rejected(self):
        for key, value in [('schema', 99), ('status', 'running'), ('build_exit_code', 1),
                           ('gcov_version', 'gcov (GCC) 15.0.0')]:
            with self.subTest(key=key), self.assertRaises(check.CoverageError):
                report = copy.deepcopy(self.report)
                report[key] = value
                check.check(report, self.policy)
        self.report['prepared']['configuration']['cache']['CNA_PLATFORM'] = 'SDL3'
        with self.assertRaises(check.CoverageError):
            check.check(self.report, self.policy)

    def test_changed_selection_duplicate_runs_and_no_provenance_are_rejected(self):
        for key in ('targets', 'duplicate', 'artifacts', 'profiles'):
            with self.subTest(key=key), self.assertRaises(check.CoverageError):
                report = copy.deepcopy(self.report)
                if key == 'targets':
                    report['prepared']['targets'].append('OtherTests')
                elif key == 'duplicate':
                    report['runs'] *= 2
                else:
                    report[key] = {}
                check.check(report, self.policy)

    def test_missing_shrunken_zero_and_invalid_denominators_are_rejected(self):
        for covered, total in [(80, 90), (0, 0), (101, 100), (-1, 100), (True, 100)]:
            with self.subTest(covered=covered, total=total), self.assertRaises(check.CoverageError):
                self.report['modules']['math']['src']['lines'].update(covered=covered, total=total)
                check.check(self.report, self.policy)
        self.report['modules'] = {}
        with self.assertRaises(check.CoverageError):
            check.check(self.report, self.policy)

    def test_bad_thresholds_and_empty_or_malformed_policies_are_rejected(self):
        for floor in (-1, 101, float('nan'), float('inf'), '80', True):
            with self.subTest(floor=floor), self.assertRaises(check.CoverageError):
                self.policy['modules']['math']['src']['lines']['minimum_percent'] = floor
                check.check(self.report, self.policy)
        for policy in ({}, {'schema': 99}):
            with self.assertRaises(check.CoverageError):
                check.check(self.report, policy)

    def test_stale_sources_artifacts_and_profiles_are_rejected(self):
        self.verify.side_effect = check.CoverageError('source changed')
        with self.assertRaises(check.CoverageError):
            check.check(self.report, self.policy)
        self.verify.side_effect = None
        self.artifact.write_bytes(b'changed')
        with self.assertRaises(check.CoverageError):
            check.check(self.report, self.policy)
        self.report['artifacts'][str(self.artifact)] = check.digest(self.artifact)
        self.profile.unlink()
        with self.assertRaises(check.CoverageError):
            check.check(self.report, self.policy)

    def test_negative_gated_counters_refuse_an_apparently_passing_percentage(self):
        self.quality.side_effect = check.CoverageError('Negative gcov counter')
        with self.assertRaisesRegex(check.CoverageError, 'Negative gcov'):
            check.check(self.report, self.policy)


class CounterQualityTests(unittest.TestCase):
    def test_raw_negative_counters_rejected_only_in_selected_areas(self):
        import json
        from types import SimpleNamespace
        source = Path('/source')
        unit = {'source': '/source/modules/math/src/A.cpp', 'object': '/build/A.cpp.o'}
        report = {'prepared': {'source': str(source), 'build': '/build', 'units': [unit]}}
        policy = {'modules': {'math': {'src': {'lines': {}}}}}
        for metric in ('line', 'branch', 'function', 'none', 'ungated header'):
            file = {'file': str(source / 'modules/math/src/A.cpp'),
                    'functions': [{'execution_count': -1 if metric == 'function' else 1}],
                    'lines': [{'count': -1 if metric == 'line' else 1,
                               'branches': [{'count': -1 if metric == 'branch' else 1}]}]}
            if metric == 'ungated header':
                file['file'] = str(source / 'modules/math/include/A.hpp')
                file['lines'][0]['count'] = -1
            result = SimpleNamespace(returncode=0, stderr='', stdout=json.dumps(
                {'current_working_directory': '/build', 'files': [file]}))
            with self.subTest(metric=metric), mock.patch.object(check.subprocess, 'run', return_value=result):
                if metric in ('none', 'ungated header'):
                    check.counter_quality(report, policy)
                else:
                    with self.assertRaisesRegex(check.CoverageError, 'Negative gcov'):
                        check.counter_quality(report, policy)
if __name__ == '__main__':
    unittest.main()
