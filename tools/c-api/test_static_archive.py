#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Link-closure regression tests; no compiler, build tree or temporary files needed."""
import unittest
from pathlib import Path
from unittest.mock import patch

import generate_static_archive as archive


class StaticClosureTests(unittest.TestCase):
    build = Path("/build")
    module = Path("/build/CNA/modules/c-api")
    runtime_dir = Path("/build/CNA")
    runtime = Path("/build/CNA/libcna.so")

    def tokens(self, module, build, target="cna_c_api"):
        self.assertEqual(build, self.build)
        if target == "cna_shared":
            self.assertEqual(module, self.runtime_dir)
            return ("c++ generated/stub.o modules/core/libcore.a libsharp.a "
                    "modules/core/libcore.a -lSDL3 -lcurl -o libcna.so".split(), self.runtime_dir)
        self.assertEqual(module, self.module)
        return ("c++ CnaCApi.o /build/CNA/libcna.so /build/CNA/libsharp.a "
                "-lSDL3 -o libcna_c_api.so".split(), self.module)

    @patch.object(archive, "link_line_tokens")
    def test_expands_runtime_and_deduplicates_archives(self, tokens):
        tokens.side_effect = self.tokens
        objects, archives, external = archive.read_static_closure(
            self.module, self.build, self.runtime, self.runtime_dir)
        self.assertEqual(objects, [str(self.module / "CnaCApi.o"), "/build/CNA/generated/stub.o"])
        self.assertEqual(archives, ["/build/CNA/libsharp.a", "/build/CNA/modules/core/libcore.a"])
        self.assertEqual(external, ["-lSDL3", "-lcurl"])

    @patch.object(archive, "link_line_tokens")
    def test_adapter_without_archives_can_expand_runtime(self, tokens):
        tokens.side_effect = lambda module, build, target="cna_c_api": (
            self.tokens(module, build, target) if target == "cna_shared" else
            ("c++ CnaCApi.o /build/CNA/libcna.so -o libcna_c_api.so".split(), self.module))
        _, archives, external = archive.read_static_closure(
            self.module, self.build, self.runtime, self.runtime_dir)
        self.assertEqual(len(archives), 2)
        self.assertNotIn(str(self.runtime), external)

    @patch.object(archive, "link_line_tokens")
    def test_static_runtime_configuration_unchanged(self, tokens):
        tokens.return_value = ("c++ api.o core.a core.a -lm -o libcna_c_api.so".split(), self.module)
        self.assertEqual(archive.read_static_closure(self.module, self.build),
                         ([str(self.module / "api.o")], [str(self.module / "core.a")], ["-lm"]))

    @patch.object(archive, "link_line_tokens")
    def test_missing_runtime_edge_is_refused(self, tokens):
        tokens.return_value = ("c++ api.o core.a -o libcna_c_api.so".split(), self.module)
        with self.assertRaisesRegex(SystemExit, "does not contain the shared runtime"):
            archive.read_static_closure(self.module, self.build, self.runtime, self.runtime_dir)

    def test_partial_runtime_configuration_is_refused(self):
        for library, directory in [(self.runtime, None), (None, self.runtime_dir)]:
            with self.subTest(library=library, directory=directory):
                with self.assertRaisesRegex(SystemExit, "supplied together"):
                    archive.read_static_closure(self.module, self.build, library, directory)

    @patch.object(archive, "link_line_tokens")
    def test_no_objects_or_no_static_archives_remains_an_error(self, tokens):
        for line in ["c++ core.a -o libcna_c_api.so", "c++ api.o -o libcna_c_api.so"]:
            tokens.return_value = (line.split(), self.module)
            with self.subTest(line=line):
                with self.assertRaisesRegex(SystemExit, "no objects or no archives"):
                    archive.read_static_closure(self.module, self.build)


if __name__ == "__main__":
    unittest.main()
