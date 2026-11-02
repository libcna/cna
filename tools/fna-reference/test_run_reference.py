#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""CPU-only recipe safety regressions; does not build or run FNA."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("recipe", Path(__file__).with_name("run-reference.py"))
recipe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(recipe)


class RecipeTests(unittest.TestCase):
    def test_repository_output_requires_explicit_update_even_when_new(self):
        with self.assertRaisesRegex(ValueError, "requires --update"):
            recipe.output_paths(recipe.ROOT / "tests/fixtures/new-oracle", "values", False)
        paths = recipe.output_paths(recipe.ROOT / "tests/fixtures/new-oracle", "values", True)
        self.assertEqual(len(paths), 2)

    def test_fresh_external_output_is_allowed_without_update(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = recipe.output_paths(Path(directory), "values", False)
            self.assertEqual([p.name for p in paths], ["values-1.json", "values-2.json"])
            self.assertFalse(any(p.exists() for p in paths))

    def test_existing_outputs_are_preserved_without_update(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "values-2.json"
            output.write_text("original", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Output exists"):
                recipe.output_paths(Path(directory), "values", False)
            self.assertEqual(output.read_text(), "original")
            recipe.output_paths(Path(directory), "values", True)

    def test_output_symlink_is_refused_even_with_update(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "values-1.json").symlink_to(path / "elsewhere.json")
            for update in (False, True):
                with self.assertRaisesRegex(ValueError, "symlink"):
                    recipe.output_paths(path, "values", update)

    def test_directory_symlink_cannot_bypass_repository_guard(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "link"
            path.symlink_to(recipe.ROOT, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "requires --update"):
                recipe.output_paths(path, "values", False)


if __name__ == "__main__":
    unittest.main()
