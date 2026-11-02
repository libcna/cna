#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Pin declaration, configured registration and actual execution as separate facts."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import final_audit as audit
import evidence_layers as layers
sys.path.insert(0, str(Path(__file__).resolve().parent / "tests"))
from evidence_fixture import capture


class AuditEvidenceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cna-presence-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "modules").mkdir()
        (self.root / "cmake").mkdir()
        (self.root / "CMakeLists.txt").write_text("")
        self.source = self.root / "modules/test.cpp"
        self.cmake = self.root / "cmake/test.cmake"
        self.checks = [("Product", "required C++ witness", "tests", ["Probe.Required"]),
                       ("Runtime", "required runtime category", "ctests", ["RuntimeCategory"])]
        self.source.write_text("TEST(Probe, Required) {}")
        self.cmake.write_text("add_test(NAME RuntimeCategory COMMAND fixture)")

    def args(self, records=(), inventory=None):
        return argparse.Namespace(execution_record=list(records), configured_inventory=inventory)

    def test_comments_strings_and_literal_disabled_code_are_not_declarations(self):
        for source in ('// TEST(Probe, Required) {}', '/* TEST(Probe, Required) {} */',
                       'const char* s = "TEST(Probe, Required)";',
                       'auto s = R"tag(TEST(Probe, Required))tag";',
                       '#if 0\nTEST(Probe, Required) {}\n#endif',
                       '#if (0)\n#if FEATURE\nTEST(Probe, Required) {}\n#endif\n#endif'):
            with self.subTest(source=source):
                self.source.write_text(source)
                self.assertEqual(audit.test_names_in(str(self.root), ['Probe.Required']), set())

    def test_conditionals_do_not_pretend_to_evaluate_the_build(self):
        self.source.write_text('#if 0\nTEST(Probe, Absent) {}\n#else\nTEST(Probe, Required) {}\n#endif')
        self.assertEqual(audit.test_names_in(str(self.root), ['Probe.Required', 'Probe.Absent']), {'Probe.Required'})
        self.source.write_text('#if UNKNOWN_FEATURE\nTEST(Probe, Required) {}\n#endif')
        self.assertEqual(audit.test_names_in(str(self.root), ['Probe.Required']), {'Probe.Required'})
        result = layers.execution_layers({'Probe.Required': True}, self.args())
        self.assertEqual(result['actual_execution']['status'], 'not-established')

    def test_cmake_comments_false_blocks_and_unknown_conditions(self):
        for text in ('# add_test(NAME RuntimeCategory COMMAND fixture)',
                     '#[=[add_test(NAME RuntimeCategory COMMAND fixture)]=]',
                     'if(FALSE)\nadd_test(NAME RuntimeCategory COMMAND fixture)\nendif()'):
            self.cmake.write_text(text)
            self.assertFalse(audit.ctest_names_in(str(self.root), ['RuntimeCategory']))
        self.cmake.write_text('if(UNKNOWN)\nadd_test(NAME "RuntimeCategory" COMMAND fixture)\nendif()')
        self.assertEqual(audit.ctest_names_in(str(self.root), ['RuntimeCategory']), {'RuntimeCategory'})

    def test_unregistered_and_missing_required_category_never_establish_execution(self):
        record = capture(self.root / 'other', name='OtherCategory')
        with patch.object(audit, 'CHECKS', self.checks):
            self.assertTrue(all(ok for ok, _ in audit.run(str(self.root), {}, {})))
        result = layers.execution_layers({'Probe.Required': True, 'RuntimeCategory': True}, self.args([record]))
        self.assertEqual(result['configured_registration']['missing_or_disabled'], ['Probe.Required', 'RuntimeCategory'])
        self.assertEqual(result['actual_execution']['status'], 'not-established')

    def test_configured_registration_is_separate_from_execution(self):
        record = capture(self.root / 'registered')
        inventory = record.parent / 'configured.json'
        result = layers.execution_layers({'Probe.Required': True}, self.args(inventory=inventory))
        self.assertEqual(result['configured_registration']['candidates']['Probe.Required'], ['Probe.Required'])
        self.assertEqual(result['actual_execution']['status'], 'not-established')

    def test_passed_empty_body_has_no_assertion_or_qualification_claim(self):
        record = capture(self.root / 'empty-body')
        self.assertEqual(layers.ctest_evidence.validate(json.loads(record.read_text()))['outcome'], 'pass')
        result = layers.execution_layers({'Probe.Required': True}, self.args([record]))
        self.assertEqual(result['actual_execution']['status'], 'observed')
        self.assertEqual(result['runtime_qualification']['status'], 'not-established')
        self.assertIsNone(result['runtime_qualification']['executed_assertions'])

    def test_metadata_cannot_satisfy_required_runtime_category(self):
        record = capture(self.root / 'metadata', kind='metadata')
        result = layers.execution_layers({'Probe.Required': True}, self.args([record]))
        self.assertEqual(result['actual_execution']['missing'], ['Probe.Required'])
        result = layers.execution_layers({'Probe.Required': False}, self.args([record]))
        self.assertEqual(result['actual_execution']['status'], 'observed')

    def test_disabled_skipped_failed_and_tampered_records_never_satisfy_execution(self):
        for state in ('not_run', 'skipped', 'failed'):
            with self.subTest(state=state):
                record = capture(self.root / state, state=state)
                result = layers.execution_layers({'Probe.Required': True}, self.args([record]))
                self.assertEqual(result['actual_execution']['status'], 'not-established')
        record = capture(self.root / 'disabled', disabled=True)
        self.assertEqual(layers.execution_layers({'Probe.Required': True}, self.args([record]))['actual_execution']['missing'], ['Probe.Required'])
        record = capture(self.root / 'tampered')
        (record.parent / 'ctest.log').write_text('truncated')
        result = layers.execution_layers({'Probe.Required': True}, self.args([record]))
        self.assertTrue(result['actual_execution']['errors'])

    def test_aggregate_gtest_zero_cases_and_wrong_cases_are_not_execution(self):
        for index, output in enumerate(('[==========] Running 0 tests',
                                        '[ RUN      ] Other.Case\n[       OK ] Other.Case (0 ms)',
                                        '[ RUN      ] Probe.Required',
                                        '[       OK ] Probe.Required (0 ms)')):
            record = capture(self.root / str(index), name='Aggregate',
                             command=['fixture', '--gtest_filter=Probe.*'], output=output)
            result = layers.execution_layers({'Probe.Required': True}, self.args([record]))
            self.assertEqual(result['actual_execution']['status'], 'not-established')
        record = capture(self.root / 'ran', name='Aggregate', command=['fixture', '--gtest_filter=Probe.*'],
                         output='[ RUN      ] Probe.Required\n[       OK ] Probe.Required (0 ms)')
        result = layers.execution_layers({'Probe.Required': True}, self.args([record]))
        self.assertEqual(result['actual_execution']['witnesses']['Probe.Required'], ['Aggregate'])
        self.assertEqual(result['runtime_qualification']['status'], 'not-established')

    def test_metadata_labels_cannot_be_promoted_by_a_runtime_record(self):
        record = capture(self.root / 'labelled')
        data = json.loads(record.read_text())
        for key in ('configured', 'selected'):
            path = Path(data['artifacts'][key]['path'])
            inventory = json.loads(path.read_text())
            inventory['tests'][0]['properties'].append({'name': 'LABELS', 'value': ['metadata']})
            path.write_text(json.dumps(inventory))
            data['artifacts'][key] = layers.ctest_evidence.artifact(path)
        record.write_text(json.dumps(data))
        self.assertEqual(layers.ctest_evidence.validate(data)['outcome'], 'pass')
        result = layers.execution_layers({'Probe.Required': True}, self.args([record]))
        self.assertEqual(result['actual_execution']['status'], 'not-established')

    def test_multi_configuration_captures_cannot_be_combined(self):
        first = capture(self.root / 'debug')
        second = capture(self.root / 'release', name='RuntimeCategory')
        data = json.loads(second.read_text())
        data['selection']['config'] = 'Release'
        second.write_text(json.dumps(data))
        result = layers.execution_layers({'Probe.Required': True, 'RuntimeCategory': True}, self.args([first, second]))
        self.assertEqual(result['actual_execution']['status'], 'not-established')

    def test_invalid_record_and_inventory_are_diagnostics_not_execution(self):
        bad = self.root / 'bad.json'
        bad.write_text('{')
        for args in (self.args([bad]), self.args(inventory=bad)):
            result = layers.execution_layers({'Probe.Required': True}, args)
            self.assertEqual(result['actual_execution']['status'], 'not-established')
            self.assertTrue(result['actual_execution']['errors'])

    def test_filtered_out_case_is_not_a_registration_candidate(self):
        record = capture(self.root / 'filter', name='Aggregate', command=['fixture', '--gtest_filter=Probe.*-*.Required'])
        result = layers.execution_layers({'Probe.Required': True}, self.args([record]))
        self.assertEqual(result['configured_registration']['missing_or_disabled'], ['Probe.Required'])

    def test_different_capture_identities_and_inventories_cannot_be_combined(self):
        first = capture(self.root / 'first')
        second = capture(self.root / 'second', name='RuntimeCategory')
        mismatched = layers.execution_layers({'Probe.Required': True, 'RuntimeCategory': True}, self.args([first, second]))
        self.assertTrue(any('configured inventories differ' in error for error in mismatched['actual_execution']['errors']))
        record = json.loads(second.read_text())
        record['build']['cache_sha256'] = 'c' * 64
        second.write_text(json.dumps(record))
        result = layers.execution_layers({'Probe.Required': True, 'RuntimeCategory': True}, self.args([first, second]))
        self.assertEqual(result['actual_execution']['status'], 'not-established')
        self.assertTrue(result['actual_execution']['errors'])
        result = layers.execution_layers({'Probe.Required': True}, self.args([first], second.parent / 'configured.json'))
        self.assertTrue(result['actual_execution']['errors'])

    def test_pipeline_cli_default_and_required_execution_have_distinct_exit_codes(self):
        with patch.object(audit, 'CHECKS', self.checks), patch.object(audit.parity_report, 'load', return_value={}), \
             patch.object(audit.parity_report, 'build_report', return_value=(None, None, {})), contextlib.redirect_stdout(io.StringIO()):
            arguments = ['audit', '--repo', str(self.root), '--output', str(self.root / 'audit.md'),
                         '--evidence-report', str(self.root / 'layers.json')]
            self.assertEqual(audit.main(arguments), 0)
            self.assertEqual(audit.main(arguments + ['--check', '--require-execution']), 1)
            names = ['Probe.Required', 'RuntimeCategory']
            first = capture(self.root / 'product', configured_names=names)
            second = capture(self.root / 'runtime', name='RuntimeCategory', configured_names=names)
            self.assertEqual(audit.main(arguments + ['--check', '--require-execution', '--execution-record', str(first),
                                                    '--execution-record', str(second)]), 0)
            report = json.loads((self.root / 'layers.json').read_text())
            self.assertEqual(report['declaration_presence']['passed'], 2)
            self.assertEqual(report['actual_execution']['status'], 'observed')
            self.assertEqual(len(report['actual_execution']['records']), 2)
            self.assertEqual(report['runtime_qualification']['status'], 'not-established')


if __name__ == '__main__':
    unittest.main()
