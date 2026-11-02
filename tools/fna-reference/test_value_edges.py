#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Real-dump assertions and negative controls; requires FNA_EDGE_REFERENCE/CNA_EDGE_CANDIDATE."""
import copy
import importlib.util
import os
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location("edge_audit", Path(__file__).with_name("check-value-edges.py"))
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class ValueEdgeEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        reference = os.environ.get("FNA_EDGE_REFERENCE")
        candidate = os.environ.get("CNA_EDGE_CANDIDATE")
        if not reference or not candidate:
            raise unittest.SkipTest("set FNA_EDGE_REFERENCE and CNA_EDGE_CANDIDATE to fresh real dumps")
        cls.reference = audit.compare.load_dump(reference)
        cls.candidate = audit.compare.load_dump(candidate)

    def test_reviewed_dispositions_and_zero_tolerance_remainder(self):
        self.assertEqual(audit.audit(self.reference, self.candidate), 155)

    def test_independent_literal_witnesses_on_both_implementations(self):
        for document in (self.reference, self.candidate):
            with self.subTest(implementation="FNA" if document is self.reference else "CNA"):
                vector = document["Vector2Edges"]
                self.assertEqual(vector["seed"], 3237998083)
                self.assertEqual(vector["format"], "{X:1.5 Y:-2.25}")
                boundary = vector["arithmetic"]["boundaries"]
                for method in ("divide", "divideOut", "divideOperator"):
                    self.assertEqual(boundary[method], {"X": 2, "Y": 2})
                for method in ("scalarDivide", "scalarDivideOut", "scalarDivideOperator"):
                    self.assertEqual(boundary[method], {"X": 2, "Y": -2})
                for method in ("clamp", "clampOut"):
                    self.assertEqual(boundary[method], {"X": -2, "Y": 2})
                for method in ("reversedClamp", "reversedClampOut"):
                    self.assertEqual(boundary[method], {"X": 2, "Y": 2})
                for method in ("normalize", "normalizeOut", "normalizeInstance"):
                    self.assertEqual(vector["magnitude"]["zero"][method], {"X": "nan", "Y": "nan"})
                # Independent perspective coefficients for width=1, height=2, near=1, far=2.
                expected = audit.matrix({"M11": 2, "M22": 1, "M33": -2, "M34": -1, "M43": -2})
                for method in ("value", "out"):
                    self.assertEqual(document["MatrixPerspectiveEdges"]["size"]["ordinary"][method], expected)

    def test_spatial_vector_literal_witnesses_on_both_implementations(self):
        for document in (self.reference, self.candidate):
            for dimension in (3, 4):
                vector = document[f"Vector{dimension}Edges"]
                components = "XYZW"[:dimension]
                fixed = vector["spatial"]["fixed3"]  # amount 1/2
                expected = {"lerp": (2.5, 2.5, 2.5, 2.5),
                            "smoothStep": (2.5, 2.5, 2.5, 2.5),
                            "hermite": (-0.25, 2, 1.25, 4),
                            "barycentric": (1.75, 2.25, 1.75, 2.25)}
                for method, values in expected.items():
                    for overload in (method, method + "Out"):
                        self.assertEqual(fixed[overload], dict(zip(components, values)))
                for overload in ("transform", "transformOut"):
                    values = (12, 2, 22) if dimension == 3 else (33, 26, 49, 4.5)
                    self.assertEqual(fixed[overload], dict(zip(components, values)))
                for overload in ("rotate", "rotateOut"):
                    self.assertEqual(fixed[overload], dict(zip(components, (-6, -3, 2, 4))))

    def test_spatial_witness_changes_and_omissions_are_rejected(self):
        for dimension in (3, 4):
            changed = copy.deepcopy(self.candidate)
            changed[f"Vector{dimension}Edges"]["spatial"]["seeded0"]["transform"]["X"] = 123
            with self.assertRaisesRegex(ValueError, "unreviewed differences"):
                audit.audit(self.reference, changed)
            reference = copy.deepcopy(self.reference)
            candidate = copy.deepcopy(self.candidate)
            for document in (reference, candidate):
                del document[f"Vector{dimension}Edges"]["spatial"]["fixed0"]["lerpOut"]
            with self.assertRaisesRegex(ValueError, "incomplete edge witness shape"):
                audit.audit(reference, candidate)

    def test_quaternion_literal_results_and_factory_regression(self):
        for document in (self.reference, self.candidate):
            rotation = document["QuaternionEdges"]
            identity = {"X": "+zero", "Y": "+zero", "Z": "+zero", "W": 1}
            for name in ("identical", "opposite"):
                for operation in ("lerp", "lerpOut", "slerp", "slerpOut"):
                    self.assertEqual(rotation["pairs"][name][operation], identity)
            for component, name in zip("XYZ", ("xHalfTurn", "yHalfTurn", "zHalfTurn")):
                expected = {key: 1 if key == component else "+zero" for key in "XYZW"}
                for overload in ("value", "out"):
                    self.assertEqual(rotation["matrices"][name][overload], expected)
            for operation in ("axisAngle", "axisAngleOut"):
                self.assertEqual(rotation["factories"]["seeded1"][operation]["X"], 0.19407309591770172)

    def test_quaternion_factory_defect_cannot_be_retained(self):
        changed = copy.deepcopy(self.candidate)
        changed["QuaternionEdges"]["factories"]["seeded1"]["axisAngle"]["X"] = 0.19407311081886292
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)
        reference = copy.deepcopy(self.reference)
        candidate = copy.deepcopy(self.candidate)
        for document in (reference, candidate):
            del document["QuaternionEdges"]["pairs"]["seeded0"]["slerpOut"]
        with self.assertRaisesRegex(ValueError, "incomplete edge witness shape"):
            audit.audit(reference, candidate)

    def test_unexpected_change_is_rejected(self):
        changed = copy.deepcopy(self.candidate)
        changed["Vector2Edges"]["arithmetic"]["seeded0"]["divide"]["X"] = 123
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)

    def test_changed_retained_value_is_rejected(self):
        changed = copy.deepcopy(self.candidate)
        changed["Vector2Edges"]["magnitude"]["large"]["normalize"]["X"] = 2
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)

    def test_boolean_cannot_replace_retained_numeric_value(self):
        changed = copy.deepcopy(self.candidate)
        changed["Vector2Edges"]["magnitude"]["large"]["normalize"]["X"] = True
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)

    def test_omitted_matching_witness_on_both_sides_is_rejected(self):
        reference = copy.deepcopy(self.reference)
        candidate = copy.deepcopy(self.candidate)
        for document in (reference, candidate):
            del document["Vector2Edges"]["arithmetic"]["seeded0"]["divide"]
        with self.assertRaisesRegex(ValueError, "incomplete edge witness shape"):
            audit.audit(reference, candidate)

    def test_disappearing_retained_difference_requires_review(self):
        changed = copy.deepcopy(self.candidate)
        changed["Vector2Edges"]["magnitude"]["large"]["normalize"]["X"] = "+zero"
        with self.assertRaisesRegex(ValueError, "missing retained witnesses"):
            audit.audit(self.reference, changed)

    def test_missing_category_is_rejected(self):
        changed = copy.deepcopy(self.candidate)
        del changed["Vector2Edges"]
        with self.assertRaisesRegex(ValueError, "required category"):
            audit.audit(self.reference, changed)

    def test_reference_identity_is_required_and_pinned(self):
        changed = copy.deepcopy(self.reference)
        del changed["metadata"]
        with self.assertRaisesRegex(ValueError, "missing metadata.reference"):
            audit.audit(changed, self.candidate)
        changed = copy.deepcopy(self.reference)
        changed["metadata"]["reference"]["sourceRevision"] = "0" * 40
        with self.assertRaisesRegex(ValueError, "reviewed FNA revision"):
            audit.audit(changed, self.candidate)


if __name__ == "__main__":
    unittest.main()
