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
        self.assertEqual(audit.audit(self.reference, self.candidate), 1802)
        self.assertEqual(len(audit.retained_differences()), 1802)
        self.assertEqual(len(audit.half_retained_differences()), 48)

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

    def test_matrix_independent_literal_coefficients_on_both_implementations(self):
        for document in (self.reference, self.candidate):
            value = document["MatrixEdges"]
            self.assertEqual(value["seed"], 3237998083)
            for method in ("lookAt", "lookAtOut"):
                expected = audit.matrix({"M11": 1, "M21": "-zero", "M22": 1, "M32": "-zero", "M33": 1,
                                         "M41": "-zero", "M42": "-zero", "M43": -5, "M44": 1})
                self.assertEqual(value["cameras"]["ordinary"][method], expected)
            for method in ("axisAngle", "axisAngleOut"):
                self.assertEqual(value["rotations"]["seeded5"][method]["M22"], 0.664665699005127)
                self.assertEqual(value["rotations"]["seeded5"][method]["M33"], 0.664665699005127)
            for method in ("inverse", "inverseOut", "inverseAliased"):
                inverse = value["algebra"]["nearSingular"][method]
                self.assertEqual([inverse[key] for key in ("M11", "M12", "M21", "M22")],
                                 [8388609, -8388608, -8388608, 8388608])
                self.assertTrue(all(field == "nan" for field in value["algebra"]["zero"][method].values()))
            self.assertEqual(value["decomposition"]["seeded0"]["input"]["M11"], -6.25)
            self.assertEqual(value["algebra"]["seeded0"]["input"]["M12"], -1.125)
            for method in ("transpose", "transposeOut", "transposeAliased"):
                self.assertEqual(value["algebra"]["ieee"][method]["M11"], "-zero")
                self.assertEqual(value["algebra"]["sequential"][method]["M12"], 5)
            for method in ("lerp", "lerpOut", "lerpAliasedFirst", "lerpAliasedSecond"):
                self.assertEqual(value["algebra"]["sequential"][method]["M44"], 8.5)

    def test_matrix_factory_defect_is_rejected(self):
        changed = copy.deepcopy(self.candidate)
        changed["MatrixEdges"]["rotations"]["seeded5"]["axisAngle"]["M22"] = 0.6646657586097717
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)

    def test_matrix_retained_values_are_exact_and_cannot_disappear(self):
        paths = (("planes", "nonunit", "shadow", "M11"),
                 ("algebra", "seeded0", "inverse", "M13"),
                 ("decomposition", "negative", "scale", "Z"))
        for path in paths:
            changed = copy.deepcopy(self.candidate)
            reference = self.reference["MatrixEdges"]
            target = changed["MatrixEdges"]
            for key in path[:-1]:
                target = target[key]
                reference = reference[key]
            target[path[-1]] = 123
            with self.assertRaisesRegex(ValueError, "unreviewed differences"):
                audit.audit(self.reference, changed)
            target[path[-1]] = reference[path[-1]]
            with self.assertRaisesRegex(ValueError, "missing retained witnesses"):
                audit.audit(self.reference, changed)

    def test_matrix_matching_witness_omissions_on_both_sides_are_rejected(self):
        for group, case, field in (("cameras", "ordinary", "lookAtOut"),
                                   ("rotations", "angle0", "yawPitchRollOut"),
                                   ("planes", "unit", "shadowOut"),
                                   ("algebra", "identity", "inverseAliased"),
                                   ("decomposition", "unit", "rotation")):
            reference = copy.deepcopy(self.reference)
            candidate = copy.deepcopy(self.candidate)
            for document in (reference, candidate):
                del document["MatrixEdges"][group][case][field]
            with self.assertRaisesRegex(ValueError, "incomplete edge witness shape"):
                audit.audit(reference, candidate)

    def test_matrix_category_is_required(self):
        changed = copy.deepcopy(self.candidate)
        del changed["MatrixEdges"]
        with self.assertRaisesRegex(ValueError, "required category"):
            audit.audit(self.reference, changed)

    def test_geometry_literal_witnesses_on_both_implementations(self):
        for document in (self.reference, self.candidate):
            value = document["GeometryEdges"]
            self.assertEqual(value["seed"], 3237998083)
            unit = value["planes"]["unit"]
            self.assertEqual([unit[key] for key in ("dot", "dotOut", "dotCoordinate", "dotCoordinateOut",
                                                   "dotNormal", "dotNormalOut")], [-6, -6, -4, -4, -2, -2])
            for method in ("matrixTransform", "matrixTransformOut", "matrixTransformAliased"):
                self.assertEqual(unit[method], {"normal": {"X": "+zero", "Y": 0.25, "Z": "+zero"}, "d": -4})
            for method in ("frustumIntersectsPlane", "frustumIntersectsPlaneOut", "planeIntersectsFrustum"):
                self.assertEqual(value["relations"]["zero"][method], 1)
            self.assertEqual(value["pointSets"]["empty"]["boxException"], "ArgumentException")
            self.assertEqual(value["pointSets"]["empty"]["sphereException"], "ArgumentException")
            self.assertEqual(value["pointSets"]["pair"]["sphere"],
                             {"center": dict.fromkeys("XYZ", "+zero"), "radius": 2})
            self.assertEqual(value["frustums"]["identity"]["corners"]["p0"], {"X": -1, "Y": 1, "Z": "-zero"})
            self.assertEqual(value["relations"]["inside"]["rayIntersectsBox"], "+zero")
            self.assertEqual(value["relations"]["threshold1"]["rayIntersectsBox"], 1000000)

    def test_geometry_coplanar_plane_defect_cannot_be_retained(self):
        changed = copy.deepcopy(self.candidate)
        changed["GeometryEdges"]["relations"]["zero"]["frustumIntersectsPlane"] = 2
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)

    def test_geometry_omissions_are_rejected_on_both_sides(self):
        for group, case, field in (("planes", "unit", "dotOut"), ("relations", "inside", "rayIntersectsPlaneOut"),
                                   ("pointSets", "pair", "sphere"), ("frustums", "identity", "cornersOut")):
            reference = copy.deepcopy(self.reference)
            candidate = copy.deepcopy(self.candidate)
            for document in (reference, candidate):
                del document["GeometryEdges"][group][case][field]
            with self.assertRaisesRegex(ValueError, "incomplete edge witness shape"):
                audit.audit(reference, candidate)

    def test_geometry_retained_values_are_frozen(self):
        for path in ("/GeometryEdges/planes/large/normalize/d",
                     "/GeometryEdges/relations/stationary/rayIntersectsBox",
                     "/GeometryEdges/pointSets/triangleTie/sphere/center/X",
                     "/GeometryEdges/relations/negativeRadius/frustumIntersectsSphere"):
            changed = copy.deepcopy(self.candidate)
            target = changed
            for key in path.strip("/").split("/")[:-1]:
                target = target[key]
            target[path.split("/")[-1]] = 123
            with self.assertRaisesRegex(ValueError, "unreviewed differences"):
                audit.audit(self.reference, changed)
        retained = audit.geometry_retained_differences()
        prefix = "/GeometryEdges/relations/negativeRadius/"
        self.assertEqual({path: pair for path, pair in retained.items()
                          if path.startswith(prefix) and "Intersects" in path}, {
            prefix + "sphereIntersectsFrustum": (False, True),
            prefix + "frustumIntersectsSphere": (False, True),
            prefix + "frustumIntersectsSphereOut": (False, True),
        })
        for method in ("sphereIntersectsFrustum", "frustumIntersectsSphere", "frustumIntersectsSphereOut"):
            self.assertIs(self.reference["GeometryEdges"]["relations"]["negativeRadius"][method], False)
            self.assertIs(self.candidate["GeometryEdges"]["relations"]["negativeRadius"][method], True)

    def test_packing_literal_words_and_color_results_on_both_implementations(self):
        for document in (self.reference, self.candidate):
            packing = document["PackingEdges"]
            self.assertEqual(packing["seed"], 3237998083)
            for name, case, expected in (("Byte4", "ties", "0000000004020200"),
                                         ("Alpha8", "ties", "0000000000000004"),
                                         ("HalfSingle", "halfTiny", "0000000000000000"),
                                         ("HalfVector2", "halfTiny", "0000000000020000")):
                for field in ("constructor", "vectorConstructor", "pack"):
                    self.assertEqual(packing["packedTypes"][name][case][field], expected)
            for field in ("vector4", "floats4", "pack"):
                color = packing["colors"]["quarters"][field]
                self.assertEqual(color["packed"], "00000000ffbf8040")
                self.assertEqual([color[c] for c in "rgba"], [64, 128, 191, 255])
            self.assertEqual(packing["integers"]["case1"]["rgba"]["packed"], "00000000ffff0000")
            self.assertEqual(packing["packedTypes"]["Byte4"]["seeded0"]["input"]["X"], -0.78125)

    def test_packing_omissions_on_both_sides_fail_shape_validation(self):
        for name in self.reference["PackingEdges"]["packedTypes"]:
            reference = copy.deepcopy(self.reference)
            candidate = copy.deepcopy(self.candidate)
            for document in (reference, candidate):
                del document["PackingEdges"]["packedTypes"][name]["ties"]["vectorConstructor"]
            with self.assertRaisesRegex(ValueError, "incomplete edge witness shape"):
                audit.audit(reference, candidate)

    def test_packing_changed_words_and_unlisted_differences_are_rejected(self):
        changed = copy.deepcopy(self.candidate)
        changed["PackingEdges"]["packedTypes"]["Byte4"]["ties"]["pack"] = "0000000004030201"
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)
        changed = copy.deepcopy(self.candidate)
        changed["PackingEdges"]["packedTypes"]["HalfSingle"]["nonfinite"]["unpack"]["X"] = 98304
        with self.assertRaisesRegex(ValueError, "missing retained witnesses"):
            audit.audit(self.reference, changed)
        changed = copy.deepcopy(self.candidate)
        changed["PackingEdges"]["packedTypes"]["NormalizedByte2"]["negative"]["rawUnpack"]["Y"] = -2
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)

    def test_half_fields_follow_the_shipped_xna_il_decision(self):
        reference = self.reference["PackingEdges"]["packedTypes"]["HalfSingle"]
        candidate = self.candidate["PackingEdges"]["packedTypes"]["HalfSingle"]
        self.assertEqual(reference["nonfinite"]["constructor"], "0000000000007e00")
        self.assertEqual(candidate["nonfinite"]["constructor"], "0000000000007fff")
        self.assertEqual(reference["nonfinite"]["unpack"]["X"], 98304)
        self.assertEqual(candidate["nonfinite"]["unpack"]["X"], 131008)
        for document in (reference, candidate):
            self.assertEqual(document["one"]["rawUnpack"]["X"], -131008)
        self.assertEqual(candidate["one"]["repackedRaw"], "000000000000ffff")
        changed = copy.deepcopy(self.candidate)
        changed["PackingEdges"]["packedTypes"]["HalfSingle"]["one"]["rawUnpack"]["X"] = "nan"
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)
        self.assertEqual(audit.audit(self.reference, self.candidate), 1802)

    def test_curve_literal_results_and_identical_evaluation_key_data(self):
        for document in (self.reference, self.candidate):
            curves = document["CurveEdges"]
            self.assertEqual(curves["seed"], 3237998083)
            for name, expected in (("empty", "+zero"), ("single", 7)):
                for loop in curves["cases"][name]["evaluations"].values():
                    for point in loop["values"].values():
                        self.assertEqual(point["value"], expected)
            smooth = curves["cases"]["smooth"]["evaluations"]["pre0post0"]["values"]
            self.assertEqual(smooth["p6"]["value"], -0.484375)
            for case in curves["cases"].values():
                self.assertEqual(case["negativeIndex"], "ArgumentOutOfRangeException")
                self.assertEqual(case["endIndex"], "ArgumentOutOfRangeException")
        for name, reference in self.reference["CurveEdges"]["cases"].items():
            self.assertEqual(reference["keys"], self.candidate["CurveEdges"]["cases"][name]["keys"])
        self.assertEqual(self.candidate["CurveEdges"]["cases"]["stepShifted"]["evaluations"]["pre0post0"]["values"]["p6"]["value"], 1)

    def test_curve_matching_witness_omissions_fail_on_both_sides(self):
        for field in ("evaluations", "tangents"):
            reference = copy.deepcopy(self.reference)
            candidate = copy.deepcopy(self.candidate)
            key = "pre0post0" if field == "evaluations" else "in0out0"
            for document in (reference, candidate):
                del document["CurveEdges"]["cases"]["smooth"][field][key]
            with self.assertRaisesRegex(ValueError, "incomplete edge witness shape"):
                audit.audit(reference, candidate)

    def test_curve_unlisted_and_changed_retained_values_fail(self):
        changed = copy.deepcopy(self.candidate)
        changed["CurveEdges"]["cases"]["smooth"]["evaluations"]["pre0post0"]["values"]["p6"]["value"] = 123
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)
        changed = copy.deepcopy(self.candidate)
        changed["CurveEdges"]["cases"]["smooth"]["evaluations"]["pre0post0"]["values"]["p11"]["value"] = "+zero"
        with self.assertRaisesRegex(ValueError, "missing retained witnesses"):
            audit.audit(self.reference, changed)

    def test_curve_insertion_and_tiny_tangent_follow_shipped_xna_il(self):
        self.assertEqual(len(audit.CURVE_IL_RETAINED), 20)
        for path in audit.CURVE_IL_RETAINED:
            self.assertIn(path, audit.retained_differences())
        changed = copy.deepcopy(self.candidate)
        changed["CurveEdges"]["cases"]["tiny"]["tangents"]["in0out2"]["both"]["k0"]["out"] = 3
        with self.assertRaisesRegex(ValueError, "unreviewed differences"):
            audit.audit(self.reference, changed)
        cases = self.candidate["CurveEdges"]["cases"]
        self.assertEqual([cases["duplicateStart"]["insertionKeys"][f"k{i}"]["value"]
                          for i in range(3)], ["+zero", 10, 20])
        self.assertEqual([cases["allEqual"]["insertionKeys"][f"k{i}"]["value"]
                          for i in range(2)], [3, 9])
        self.assertEqual(cases["tiny"]["tangents"]["in0out2"]["both"]["k0"]["out"], 2)
        changed = copy.deepcopy(self.candidate)
        changed["CurveEdges"]["cases"]["tiny"]["tangents"]["in0out2"]["both"]["k0"]["out"] = "+zero"
        with self.assertRaisesRegex(ValueError, "missing retained witnesses"):
            audit.audit(self.reference, changed)

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
