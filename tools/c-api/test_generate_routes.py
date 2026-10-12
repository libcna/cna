#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Generator regressions; requires Python 3.11+, no CNA build or third-party packages."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import generate_routes as generator


class RouteValidationTests(unittest.TestCase):
    def route(self, parameters, entry, group=None):
        types = generator.CTypes(frozenset(), frozenset(), frozenset(),
                                {"cna_probe": [generator.parse_param(p) for p in parameters]})
        return generator.Route("Probe", group or {}, "cna_probe", entry, types)

    def test_refuse_nul_names_a_generated_text_read(self):
        parameters = ["CNA_StringView name"]
        for entry, group in [
            ({"call": "Use(name)", "refuse_nul": ["nmae"]}, {}),
            ({"result": "Use(name)", "refuse_nul": ["name"]}, {}),
            ({"call": "Use(object)", "refuse_nul": ["name"]},
             {"lookup": "ReadName", "object": "std::string"}),
        ]:
            with self.subTest(entry=entry), self.assertRaisesRegex(generator.SpecError, "refuse_nul"):
                self.route(parameters, entry, group)
        body = self.route(parameters, {"call": "Use(name)", "refuse_nul": ["name"]}).render()
        self.assertIn("EmbeddedNul::Refused", body)

    def test_formatted_copy_does_not_bypass_bool_validation(self):
        body = self.route(
            ["CNA_Bool value", "char* destination", "uint64_t capacity", "uint64_t* out_bytes"],
            {"copy": "Format(value)"}).render()
        self.assertIn('ValidateCanonicalBool(c_value, "value")', body)
        self.assertLess(body.index("ValidateCopyOutput"), body.index("ValidateCanonicalBool"))
        self.assertLess(body.index("ValidateCanonicalBool"), body.index("Format(value)"))

    def test_malformed_groups_have_spec_diagnostics(self):
        types = generator.CTypes(frozenset(), frozenset(), frozenset(), {})
        entries = [
            'routes = []',
            'lookup = 1\n[r.routes]',
            'lookup = "Read"\narguments = true\n[r.routes]',
            'lookup = "Read"\naccepts_null_handle = "false"\n[r.routes]',
            'lookup = "Read"\nobject = "Object"\nother = { lookup = 1, object = "Object" }\n[r.routes]',
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Probe.toml"
            for entry in entries:
                path.write_text("[r]\n" + entry + "\n", encoding="utf-8")
                with self.subTest(entry=entry), self.assertRaisesRegex(generator.SpecError, "Probe.toml"):
                    generator.load_family(path, types)


class RegenerationTests(unittest.TestCase):
    def test_clean_recovery_determinism_and_drift(self):
        # Work in an isolated source tree: never damage a maintainer's generated files.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "tools/c-api/generate_routes.py"
            script.parent.mkdir(parents=True)
            shutil.copy2(generator.__file__, script)
            api = root / "modules/c-api"
            for name in ("spec", "include", "src"):
                shutil.copytree(generator.C_API_DIR / name, api / name)
            (api / "tests/pure_c").mkdir(parents=True)
            shutil.rmtree(api / "src/generated")

            def run(*args, expected=0, seed="1"):
                result = subprocess.run([sys.executable, str(script), *args], cwd=root,
                                        env={**os.environ, "PYTHONHASHSEED": seed, "LC_ALL": "C"},
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                return result.stdout

            run("--check", expected=1)
            run()
            paths = sorted((api / "src/generated").glob("*.inc")) + [
                api / "tests/pure_c/GeneratedRoutesSmoke.c"]
            first = {p: p.read_bytes() for p in paths}
            run(seed="987654")
            self.assertEqual(first, {p: p.read_bytes() for p in paths})
            run("--check")
            for path, data in first.items():
                self.assertEqual(data, (generator.REPO_ROOT / path.relative_to(root)).read_bytes())
            victim = paths[0]
            victim.write_bytes(first[victim] + b"// deliberate drift\n")
            self.assertIn("is stale", run("--check", expected=1))
            run()
            self.assertEqual(first[victim], victim.read_bytes())
            orphan = api / "src/generated/Orphan.inc"
            orphan.write_text("// obsolete generated file\n", encoding="utf-8")
            self.assertIn("has no spec", run("--check", expected=1))
            run()
            self.assertFalse(orphan.exists())
            run("--check")


if __name__ == "__main__":
    unittest.main()
