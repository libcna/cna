#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Regress source census shortcuts without claiming C ABI behavioral coverage."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import check_route_test_coverage as routes
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'xna-pipeline-oracle' / 'tests'))
from evidence_fixture import capture


class RouteEvidenceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='cna-route-presence-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.headers = self.root / 'headers'
        self.tests = self.root / 'tests'
        self.headers.mkdir()
        self.tests.mkdir()
        (self.headers / 'api.h').write_text('CNA_C_API int cna_probe(void);')
        self.source = self.tests / 'test.c'
        self.source.write_text('void unused(void) { (void)&cna_probe; }')
        self.budget = self.root / 'budget.json'
        self.budget.write_text('{"uncovered_route_budget": 0, "uncovered_routes": []}')
        for name, value in [('HEADER_DIR', self.headers), ('TEST_DIRS', (self.tests,)), ('BUDGET_PATH', self.budget)]:
            patcher = patch.object(routes, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.report = self.root / 'layers.json'

    def cli(self, *arguments):
        with patch.object(sys, 'argv', ['route-check', '--check', '--evidence-report', str(self.report), *arguments]), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            code = routes.main()
        return code, json.loads(self.report.read_text()), output.getvalue()

    def test_comment_literal_and_if_zero_references_no_longer_pay_budget(self):
        for source in ('// cna_probe()', '/* cna_probe */', 'const char* s = "cna_probe";',
                       '#if 0\ncna_probe();\n#endif',
                       'auto s = R"tag(cna_probe())tag";'):
            with self.subTest(source=source):
                self.source.write_text(source)
                code, report, _ = self.cli()
                self.assertEqual(code, 1)
                self.assertEqual(report['declaration_presence']['uncovered'], ['cna_probe'])
                self.assertEqual(report['runtime_qualification']['status'], 'not-established')

    def test_comment_and_inactive_exports_are_not_public_declarations(self):
        (self.headers / 'api.h').write_text('''CNA_C_API int cna_probe(void);
// CNA_C_API int cna_comment(void);
#if 0
CNA_C_API int cna_disabled(void);
#endif
''')
        self.assertEqual(routes.declared_routes(), {'cna_probe': 'api.h'})

    def test_inactive_unknown_condition_remains_source_presence_only(self):
        self.source.write_text('#if UNKNOWN_FEATURE\ncna_probe();\n#endif')
        code, report, _ = self.cli()
        self.assertEqual(code, 0)
        self.assertEqual(report['declaration_presence']['named'], 1)
        self.assertEqual(report['configured_registration']['status'], 'not-established')
        self.assertEqual(report['actual_execution']['status'], 'not-established')

    def test_unregistered_unused_route_reference_cannot_establish_execution(self):
        code, report, _ = self.cli('--require-execution', '--required-ctest', 'RequiredCategory')
        self.assertEqual(code, 1)
        self.assertEqual(report['declaration_presence']['named'], 1)
        self.assertEqual(report['actual_execution']['missing'], ['RequiredCategory'])

    def test_execution_requires_explicit_required_categories(self):
        record = capture(self.root / 'capture', name='OtherCategory')
        code, report, output = self.cli('--require-execution', '--execution-record', str(record))
        self.assertEqual(code, 1)
        self.assertIn('needs explicit --required-ctest', output)
        self.assertEqual(report['actual_execution']['status'], 'not-established')
        code, report, _ = self.cli('--require-execution', '--required-ctest', 'RequiredCategory',
                                   '--execution-record', str(record))
        self.assertEqual(code, 1)
        self.assertEqual(report['actual_execution']['missing'], ['RequiredCategory'])

    def test_configured_only_is_not_execution(self):
        record = capture(self.root / 'capture', name='RequiredCategory')
        code, report, _ = self.cli('--require-execution', '--required-ctest', 'RequiredCategory',
                                   '--configured-inventory', str(record.parent / 'configured.json'))
        self.assertEqual(code, 1)
        self.assertEqual(report['configured_registration']['missing_or_disabled'], [])
        self.assertEqual(report['actual_execution']['status'], 'not-established')

    def test_metadata_disabled_skip_and_zero_gtests_cannot_satisfy_runtime_witness(self):
        for index, options in enumerate(({'kind': 'metadata'}, {'state': 'not_run'}, {'state': 'skipped'},
                                        {'disabled': True}, {'output': '0 tests ran'},
                                        {'command': ['fixture', '--gtest_filter=Probe.*'],
                                         'output': '[==========] Running 0 tests'})):
            with self.subTest(options=options):
                record = capture(self.root / str(index), name='RequiredCategory', **options)
                code, report, _ = self.cli('--require-execution', '--required-ctest', 'RequiredCategory',
                                           '--execution-record', str(record))
                self.assertEqual(code, 1)
                self.assertEqual(report['actual_execution']['status'], 'not-established')

    def test_passing_ctest_with_zero_assertion_evidence_never_qualifies_routes(self):
        record = capture(self.root / 'capture', name='RequiredCategory')
        code, report, output = self.cli('--require-execution', '--required-ctest', 'RequiredCategory',
                                       '--execution-record', str(record))
        self.assertEqual(code, 0)
        self.assertEqual(report['actual_execution']['status'], 'observed')
        self.assertEqual(report['runtime_qualification']['status'], 'not-established')
        self.assertIsNone(report['runtime_qualification']['executed_assertions'])
        self.assertIn('runtime qualification: not-established', output)


if __name__ == '__main__':
    unittest.main()
