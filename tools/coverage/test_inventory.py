#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Inventory regression controls for conditional and parameterized witnesses."""

import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('campaign_inventory', Path(__file__).with_name('inventory.py'))
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)


class InventoryTests(unittest.TestCase):
    def test_literal_ctest_mode_loops_keep_all_named_regressions(self):
        cmake = 'add_test(NAME Single COMMAND binary)\n' \
                'foreach(mode IN ITEMS shared device-platform-regression exit-regression)\n' \
                ' add_test(NAME CApi_Lifetime_${mode} COMMAND sequence ${mode})\nendforeach()'
        self.assertEqual(inventory.c_api_registrations(cmake), [
            ('Single', 'binary'), ('CApi_Lifetime_shared', 'sequence shared'),
            ('CApi_Lifetime_device-platform-regression', 'sequence device-platform-regression'),
            ('CApi_Lifetime_exit-regression', 'sequence exit-regression')])

    def test_multiline_typed_and_parameterized_definitions_are_not_expanded_counts(self):
        definitions = inventory.definitions('TEST_F(Fixture,\n Case) {}\n'
            'TYPED_TEST(Types, Value) {}\nTEST_P(Params, Value) {}\n'
            'INSTANTIATE_TEST_SUITE_P(Seeds, Params, Values(1, 2, 3));\n')
        self.assertEqual([d['name'] for d in definitions], ['Fixture.Case', 'Types.Value', 'Params.Value'])
        self.assertEqual([d['kind'] for d in definitions], ['TEST_F', 'TYPED_TEST', 'TEST_P'])

    def test_foreign_or_gpu_reference_lanes_cannot_inherit_general_cpu_ci(self):
        for file in ('tools/fna-reference/test_value_edges.py',
                     'tools/graphics/test_compiled_effect_fuzzer.py',
                     'modules/renderers/webgpu/examples/webgpu_sprite_depthstencil_test.cpp'):
            with self.subTest(file=file):
                workflow, reason = inventory.lane(file)
                self.assertIsNone(workflow)
                self.assertTrue(reason)

    def test_c_abi_enablement_is_explicit_and_metadata_is_not_runtime(self):
        workflow, reason = inventory.lane('modules/c-api/tests/pure_c/ContentValuesSmoke.c')
        self.assertIn('CNA_BUILD_C_API=ON', workflow)
        self.assertIn('metadata workflows did not execute', reason)
        workflow, reason = inventory.lane('tools/c-api/test_check_route_test_coverage.py')
        self.assertIn('metadata', reason)


if __name__ == '__main__':
    unittest.main()
