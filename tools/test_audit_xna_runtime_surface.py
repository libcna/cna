#!/usr/bin/env python3
"""Regression checks for the Microsoft XNA runtime type audit."""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import audit_xna_runtime_surface as audit


class RuntimeSurfaceAuditTests(unittest.TestCase):
    def test_nested_type_must_be_inside_declaring_collection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "modules/graphics/include/ModelBoneCollection.hpp"
            header.parent.mkdir(parents=True)
            header.write_text(
                "namespace Microsoft::Xna::Framework::Graphics {\n"
                "class ModelBoneCollection { public: struct Enumerator {}; };\n"
                "}\n",
                encoding="utf-8",
            )
            reference = "Microsoft.Xna.Framework.Graphics.ModelBoneCollection.Enumerator"
            documented = {reference, "Microsoft.Xna.Framework.Graphics.ModelBoneCollection"}
            with patch.object(audit, "REPO", root):
                present = audit.represented_type(reference, {"ModelBoneCollection": [header]},
                                                 documented)
                self.assertEqual(present["classification"], "exactly represented")
                self.assertTrue(present["nested_public_type"])
                header.write_text(
                    "namespace Microsoft::Xna::Framework::Graphics {\n"
                    "class ModelBoneCollection {}; struct Enumerator {};\n"
                    "}\n",
                    encoding="utf-8",
                )
                missing = audit.represented_type(reference, {"ModelBoneCollection": [header]},
                                                 documented)
                self.assertEqual(missing["classification"], "missing")
                header.write_text(
                    "namespace Microsoft::Xna::Framework::Graphics {\n"
                    "class ModelBoneCollection { private: struct Enumerator {}; };\n"
                    "}\n",
                    encoding="utf-8",
                )
                private = audit.represented_type(reference, {"ModelBoneCollection": [header]},
                                                 documented)
                self.assertEqual(private["classification"], "missing")

    def test_forward_declaration_does_not_count(self):
        self.assertIsNone(audit.named_body("class ModelBoneCollection;", "ModelBoneCollection"))

    def test_clr_arity_pairs_remain_distinct(self):
        nongeneric, substitute = audit.normalized_segments(
            "Microsoft.Xna.Framework.Content.ContentTypeReader")
        generic, exact = audit.normalized_segments(
            "Microsoft.Xna.Framework.Content.ContentTypeReader`1")
        self.assertEqual(nongeneric[-1], "ContentTypeReaderBase")
        self.assertEqual(generic[-1], "ContentTypeReader")
        self.assertTrue(substitute)
        self.assertFalse(exact)

    def test_incomplete_reference_corpus_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                audit.reference_members(Path(directory))


if __name__ == "__main__":
    unittest.main()
