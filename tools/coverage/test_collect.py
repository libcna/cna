#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Regression checks for coverage evidence rejection and counting semantics."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace

SPEC = importlib.util.spec_from_file_location('collect', Path(__file__).with_name('collect.py'))
collect = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collect)


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        self.build.mkdir()
        self.unit = str(self.build / 'a.cpp.o')
        self.source = self.root / 'modules/math/src/A.cpp'
        self.source.parent.mkdir(parents=True)
        self.source.write_text('int example() { return 1; }\n')

    def document(self, count=1):
        return {'format_version': '2', 'gcc_version': '14.2.0',
                'current_working_directory': str(self.build),
                'data_file': str(Path(self.unit).with_suffix('.gcno')),
                'files': [{'file': str(self.source),
                           'functions': [{'name': '_Z7examplev', 'start_line': 1,
                                          'start_column': 1, 'execution_count': count}],
                           'lines': [{'line_number': 1, 'count': count,
                                      'function_name': '_Z7examplev',
                                      'branches': [
                                          {'source_block_id': 0, 'destination_block_id': i,
                                           'throw': i == 2, 'fallthrough': i == 1,
                                           'count': count if i == 1 else 0}
                                          for i in (1, 2)]}]}]}

    def aggregator(self):
        return collect.Aggregator(self.root, self.build, '14.2.0')

    def test_counts_zero_hit_functions_and_exception_arcs(self):
        document = self.document()
        document['files'][0]['functions'].append(
            {'name': '_Z6unusedv', 'start_line': 2, 'start_column': 1, 'execution_count': 0})
        aggregator = self.aggregator()
        aggregator.add(document, self.unit)
        result = aggregator.result()['modules']['math']['src']
        self.assertEqual(result['functions'], {'covered': 1, 'total': 2, 'percent': 50.0})
        self.assertEqual(result['branches'], {'covered': 1, 'total': 2, 'percent': 50.0})

    def test_zero_denominator_is_distinct_from_uncovered_code(self):
        self.assertEqual(collect.format_metric(collect.metric([])), '0/0 (n/a)')
        self.assertEqual(collect.format_metric(collect.metric([0, 0])), '0/2 (0.0%)')

    def test_unions_functions_and_lines_but_retains_tu_branch_instances(self):
        aggregator = self.aggregator()
        aggregator.add(self.document(), self.unit)
        second = self.document(0)
        other_unit = str(self.build / 'b.cpp.o')
        second['data_file'] = str(Path(other_unit).with_suffix('.gcno'))
        aggregator.add(second, other_unit)
        stats = aggregator.result()['modules']['math']['src']
        self.assertEqual(stats['lines']['total'], 1)
        self.assertEqual(stats['functions']['total'], 1)
        self.assertEqual(stats['branches']['total'], 4)
        self.assertEqual(stats['branches']['covered'], 1)

    def test_separates_header_instantiations_from_src(self):
        document = self.document()
        document['files'][0]['file'] = str(self.root / 'modules/math/include/A.hpp')
        aggregator = self.aggregator()
        aggregator.add(document, self.unit)
        self.assertEqual(set(aggregator.result()['modules']['math']), {'include'})

    def test_duplicate_branch_inputs_are_rejected(self):
        aggregator = self.aggregator()
        aggregator.add(self.document(), self.unit)
        with self.assertRaises(collect.CoverageError):
            aggregator.add(self.document(), self.unit)

    def test_wrong_version_or_unit_is_rejected(self):
        for key, value in [('gcc_version', '13.0.0'), ('format_version', '1'),
                           ('data_file', '/foreign/config/a.gcno')]:
            with self.subTest(key=key):
                document = self.document()
                document[key] = value
                with self.assertRaises(collect.CoverageError):
                    self.aggregator().add(document, self.unit)

    def test_empty_or_unexecuted_production_is_rejected(self):
        for document in (None, self.document(0)):
            aggregator = self.aggregator()
            if document:
                aggregator.add(document, self.unit)
            with self.assertRaises(collect.CoverageError):
                aggregator.result()

    def test_conditionally_empty_translation_unit_is_allowed(self):
        note = Path(self.unit).with_suffix('.gcno')
        document = self.document()
        document['files'] = []
        result = SimpleNamespace(returncode=0, stdout=json.dumps(document), stderr=
            f'{note}:no functions found\n'
            f'{note.with_suffix(".gcda")}:cannot open data file, assuming not executed\n')
        self.assertEqual(collect.gcov_document(result, note), document)

    def test_gcov_mismatch_warning_or_failed_exit_is_rejected(self):
        note = Path(self.unit).with_suffix('.gcno')
        for code, warning in [(1, ''), (0, 'stamp mismatch'), (0, f'{note}:no functions found')]:
            with self.subTest(code=code, warning=warning), self.assertRaises(collect.CoverageError):
                collect.gcov_document(SimpleNamespace(returncode=code,
                    stdout=json.dumps(self.document()), stderr=warning), note)

    def test_exclusions_have_reasons_and_renderer_ownership_is_preserved(self):
        for relative, reason in [
            ('third_party/a.c', 'third-party dependency'),
            ('vendor/a.hpp', 'vendored dependency'),
            ('modules/math/tests/A.cpp', 'test code'),
            ('tools/a.cpp', 'tool code'),
            ('modules/math/src/generated/A.cpp', 'generated source'),
            ('build/generated/A.cpp', 'generated/build-tree source')]:
            with self.subTest(path=relative):
                self.assertEqual(collect.classify(self.root / relative, self.root, self.build),
                                 (None, reason))
        self.assertEqual(collect.classify(self.root / 'modules/renderers/headless/src/A.cpp',
                                         self.root, self.build),
                         (('renderers/headless', 'src'), None))

    def xml(self, cases):
        path = self.root / 'tests.xml'
        path.write_text('<testsuites><testsuite name="Suite">' + cases +
                        '</testsuite></testsuites>')
        return path

    def test_exact_cases_include_skip_reasons_and_disabled_cases(self):
        path = self.xml('<testcase name="Pass" status="run" result="completed"/>'
                        '<testcase name="Skip" result="skipped"><skipped message="no GPU"/>'
                        '</testcase><testcase name="DISABLED_X" status="notrun"/>')
        cases = collect.test_results(path)
        collect.validate_cases(cases, path)
        self.assertEqual([c['status'] for c in cases], ['passed', 'skipped', 'disabled'])
        self.assertEqual(cases[1]['message'], 'no GPU')
        self.assertEqual(cases[0]['name'], 'Suite.Pass')

    def test_failed_empty_and_all_skipped_runs_are_rejected(self):
        for content in ('', '<testcase name="Skip"><skipped/></testcase>',
                        '<testcase name="Fail"><failure message="bad"/></testcase>',
                        '<testcase name="Pass"/><testcase name="Fail"><error/></testcase>'):
            path = self.xml(content)
            with self.subTest(content=content), self.assertRaises(collect.CoverageError):
                collect.validate_cases(collect.test_results(path), path)

    def test_config_and_source_drift_are_rejected(self):
        prepared = {'build': str(self.build), 'configuration': {'flags': '--coverage'},
                    'sources': {str(self.source): collect.digest(self.source)}}
        with mock.patch.object(collect, 'configuration', return_value={'flags': '-O2'}):
            with self.assertRaises(collect.CoverageError):
                collect.verify_prepared(prepared)
        self.source.write_text('changed\n')
        with mock.patch.object(collect, 'configuration', return_value=prepared['configuration']):
            with self.assertRaises(collect.CoverageError):
                collect.verify_prepared(prepared)

    def test_invalid_profile_removes_old_reports(self):
        path = self.root / 'prepared.json'
        path.write_text(json.dumps({'build': str(self.build), 'source': str(self.root)}))
        for name in ('coverage.json', 'COVERAGE_BASELINE.md'):
            (self.root / name).write_text('stale success')
        with mock.patch.object(collect, 'verify_prepared',
                               side_effect=collect.CoverageError('mixed configurations')):
            with self.assertRaises(collect.CoverageError):
                collect.collect(path, self.root, 'gcov')
        self.assertFalse((self.root / 'coverage.json').exists())
        self.assertFalse((self.root / 'COVERAGE_BASELINE.md').exists())

    def test_malformed_manifest_cannot_leave_a_stale_success_report(self):
        path = self.root / 'prepared.json'
        path.write_text('{invalid')
        for name in ('coverage.json', 'COVERAGE_BASELINE.md'):
            (self.root / name).write_text('stale success')
        with self.assertRaises(ValueError):
            collect.collect(path, self.root, 'gcov')
        self.assertFalse((self.root / 'coverage.json').exists())
        self.assertFalse((self.root / 'COVERAGE_BASELINE.md').exists())

    def test_execution_uses_source_root_and_failed_or_empty_runs_never_publish(self):
        for exit_code, xml in [(1, '<testcase name="Failed"><failure/></testcase>'),
                               (0, '')]:
            with self.subTest(exit_code=exit_code):
                prepared_path = self.root / 'prepared.json'
                prepared = {'build': str(self.build), 'source': str(self.root),
                            'targets': ['FakeTests'], 'units': [{'object': self.unit}],
                            'configuration': {'cache': {'CNA_PLATFORM': 'HEADLESS',
                                'CNA_GRAPHICS_RENDERER': 'HEADLESS',
                                'CMAKE_CXX_COMPILER': 'c++'}}}
                collect.save(prepared_path, prepared)
                for path in (Path(self.unit), Path(self.unit).with_suffix('.gcno'),
                             self.build / 'FakeTests'):
                    path.write_text('artifact')

                def run(command, **kwargs):
                    if command[0] == 'cmake':
                        return SimpleNamespace(returncode=0)
                    self.assertEqual(command[0], 'env')
                    self.assertEqual(kwargs['cwd'], self.root)
                    self.assertNotIn('DISPLAY', kwargs['env'])
                    self.assertNotIn('WAYLAND_DISPLAY', kwargs['env'])
                    (self.root / 'FakeTests.xml').write_text(
                        '<testsuites><testsuite name="Fake">' + xml +
                        '</testsuite></testsuites>')
                    return SimpleNamespace(returncode=exit_code)

                with mock.patch.object(collect, 'verify_prepared'), \
                     mock.patch.object(collect, 'execute', return_value='14.2.0'), \
                     mock.patch.object(collect, 'revision', return_value={}), \
                     mock.patch.object(collect.subprocess, 'run', side_effect=run), \
                     self.assertRaises(collect.CoverageError):
                    collect.collect(prepared_path, self.root, 'gcov')
                self.assertFalse((self.root / 'coverage.json').exists())
                self.assertFalse((self.root / 'COVERAGE_BASELINE.md').exists())
                evidence = json.loads((self.root / 'run.json').read_text())
                self.assertEqual(evidence['status'], 'failed')
                self.assertEqual(evidence['runs'][0]['exit_code'], exit_code)


if __name__ == '__main__':
    unittest.main()
