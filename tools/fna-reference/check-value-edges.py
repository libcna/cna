#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Audit MTH-CONF-003's exact retained differences; a pass is NOT FNA parity.

Run the strict comparator first to retain its unfiltered diagnostic. This separate audit uses
that same comparator at zero tolerance and accepts only the exact reviewed path/value pairs.
See docs/fna-reference-harness.md for the XNA decisions and bounded input domain.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import sys

COMPARATOR = Path(__file__).resolve().parents[2] / "scripts/compare-fna-reference.py"
SPEC = importlib.util.spec_from_file_location("compare_fna_reference", COMPARATOR)
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)
CATEGORIES = ("Vector2Edges", "MatrixPerspectiveEdges", "Vector3Edges", "Vector4Edges", "QuaternionEdges")
REVISION = "00498b0b81425b14bf8da23e68655215b9b5daa7"


def single(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def matrix(entries):
    result = {f"M{row}{column}": "+zero" for row in range(1, 5) for column in range(1, 5)}
    result.update(entries)
    return result


def quaternion_retained_differences():
    # Frozen paired observations, not runtime-generated expectations. Existing decisions:
    # bd46b6ffe (wide magnitude/normalization), df9234202 (wide inverse).
    witnesses = {'nonunit': {'normalize': {'Z': (0.547722578048706, 0.5477225184440613)},
             'inverse': {'Z': (-0.10000000894069672, -0.10000000149011612)}},
 'large': {'length': ('+inf', 1.0000000200408773e+20),
           'normalize': {'X': ('+zero', 1)},
           'inverse': {'X': ('-zero', -9.999999682655225e-21)}},
 'tiny': {'length': ('+zero', 1.0000000031710769e-30),
          'normalize': {'X': ('+inf', 1),
                        'Y': ('nan', '+zero'),
                        'Z': ('nan', '+zero'),
                        'W': ('nan', '+zero')},
          'inverse': {'X': ('-inf', -1.0000000150474662e+30),
                      'Y': ('nan', '-zero'),
                      'Z': ('nan', '-zero'),
                      'W': ('nan', '+zero')}},
 'seeded0': {'normalize': {'X': (-0.47781848907470703, -0.47781845927238464),
                           'Z': (0.8409605622291565, 0.8409605026245117),
                           'W': (0.23890924453735352, 0.23890922963619232)},
             'inverse': {'X': (0.03652968257665634, 0.03652967885136604),
                         'Y': (0.006575342733412981, 0.006575342267751694),
                         'W': (0.01826484128832817, 0.01826483942568302)}},
 'seeded2': {'normalize': {'W': (0.9571124911308289, 0.9571125507354736)}},
 'seeded3': {'normalize': {'X': (-0.8655256628990173, -0.8655256032943726),
                           'W': (-0.41361403465270996, -0.4136140048503876)},
             'inverse': {'W': (-0.02534467726945877, -0.02534467540681362)}}}
    expected = {}
    for case, operations in witnesses.items():
        for operation, fields in operations.items():
            prefix = f"/QuaternionEdges/unary/{case}"
            if operation == "length":
                expected[prefix + "/length"] = fields
                continue
            overloads = (operation, operation + "Out")
            if operation == "normalize":
                overloads += ("normalizeInstance",)
            for overload in overloads:
                for component, pair in fields.items():
                    expected[f"{prefix}/{overload}/{component}"] = pair
    return expected


def retained_differences():
    """Exact witnesses, not wildcard paths or relaxed numeric tolerances."""
    expected = {}
    for dimension in (2, 3, 4):
        prefix = f"/Vector{dimension}Edges/magnitude"
        expected[prefix + "/large/length"] = ("+inf", single(1e20))
        expected[prefix + "/tiny/length"] = ("+zero", single(1e-30))
        for overload in ("normalize", "normalizeOut", "normalizeInstance"):
            expected[f"{prefix}/large/{overload}/X"] = ("+zero", 1)
            expected[f"{prefix}/tiny/{overload}/X"] = ("+inf", 1)
            for component in "YZW"[:dimension - 1]:
                expected[f"{prefix}/tiny/{overload}/{component}"] = ("nan", "+zero")
    # Existing XNA wide-product/subtraction-order decision 0e7be08e0.
    for case in ("zero", "wideCancellation"):
        for overload in ("cross", "crossOut"):
            expected[f"/Vector3Edges/products/{case}/{overload}/Y"] = ("-zero", "+zero")
            if case == "wideCancellation":
                expected[f"/Vector3Edges/products/{case}/{overload}/Z"] = ("nan", "+zero")
    for kind in ("size", "fov", "offCenter"):
        cases = ["nearZero", "farZero", "equalDepth", "reversedDepth", "bothZero"]
        if kind == "fov":
            cases += ["zeroDimension", "negativeDimension", "fnaPi", "infiniteAngle"]
        for case in cases:
            for overload in ("valueException", "outException"):
                expected[f"/MatrixPerspectiveEdges/{kind}/{case}/{overload}"] = (
                    "ArgumentException", "ArgumentOutOfRangeException")
    prefix = "/MatrixPerspectiveEdges/fov/pi"
    # Measured FNA/net8 coefficient at the public Single Pi; XNA refuses this angle.
    projection = matrix({"M11": -4.371139183945161e-08, "M22": -4.371139183945161e-08,
                         "M33": -2, "M34": -1, "M43": -2})
    sentinel = matrix({"M11": 1, "M22": 1, "M33": 1, "M41": 7, "M42": 8, "M43": 9, "M44": 1})
    expected[prefix + "/value"] = (projection, "<missing on CNA side>")
    expected[prefix + "/valueException"] = ("none", "ArgumentOutOfRangeException")
    expected[prefix + "/outException"] = ("none", "ArgumentOutOfRangeException")
    for field in projection:
        if projection[field] != sentinel[field]:
            expected[prefix + "/out/" + field] = (projection[field], sentinel[field])
    expected.update(quaternion_retained_differences())
    return expected


def require_keys(value, names):
    if not isinstance(value, dict) or set(value) != set(names):
        raise ValueError(f"incomplete edge witness shape; expected keys {sorted(names)}")


def validate_vector_shape(vector, dimension):
    extra = () if dimension == 2 else ("spatial",)
    if dimension == 3:
        extra += ("products",)
    require_keys(vector, ("seed", "arithmetic", "magnitude", "format", "unequalEquals",
                         "unequalOperator", "unequalNotOperator", "signedZeroEquals", "signedZeroHashEqual") + extra)
    cases = ["zero", "signedZero", "boundaries", "outside", "infinity", "nan", "max", "subnormal"]
    require_keys(vector["arithmetic"], cases + [f"seeded{i}" for i in range(8)])
    operations = ("input", "divisor", "divide", "divideOut", "divideOperator", "scalarDivide",
                  "scalarDivideOut", "scalarDivideOperator", "clamp", "clampOut", "reversedClamp", "reversedClampOut")
    for case in vector["arithmetic"].values():
        require_keys(case, operations + ("equalsCopy", "operatorEqualCopy", "operatorUnequalCopy", "copyHashEqual"))
        for operation in operations:
            require_keys(case[operation], "XYZW"[:dimension])
    require_keys(vector["magnitude"], ("zero", "threeFour", "large", "tiny", "infinity", "nan"))
    for case in vector["magnitude"].values():
        require_keys(case, ("input", "length", "lengthSquared", "normalize", "normalizeOut", "normalizeInstance"))
        for operation in ("input", "normalize", "normalizeOut", "normalizeInstance"):
            require_keys(case[operation], "XYZW"[:dimension])

    if dimension > 2:
        require_keys(vector["spatial"], [f"fixed{i}" for i in range(8)] + [f"seeded{i}" for i in range(4)])
        operations = ["lerp", "smoothStep", "hermite", "catmullRom", "barycentric", "transform", "rotate"]
        operations += ["cross", "reflect", "normal"] if dimension == 3 else ["transform2", "rotate2", "transform3", "rotate3"]
        outputs = operations + [name + "Out" for name in operations]
        for case in vector["spatial"].values():
            require_keys(case, ["a", "b", "c", "d", "amount"] + outputs)
            for field in ["a", "b", "c", "d"] + outputs:
                require_keys(case[field], "XYZW"[:dimension])
    if dimension == 3:
        require_keys(vector["products"], ("zero", "wideCancellation"))
        for case in vector["products"].values():
            require_keys(case, ("a", "b", "cross", "crossOut"))
            for value in case.values():
                require_keys(value, "XYZ")


def validate_quaternion_shape(rotation):
    require_keys(rotation, ("seed", "unary", "pairs", "factories", "matrices"))
    require_keys(rotation["unary"], ["zero", "identity", "nonunit", "large", "tiny", "nan", "infinity"] +
                 [f"seeded{i}" for i in range(4)])
    fields = ["input", "normalize", "normalizeOut", "normalizeInstance", "inverse", "inverseOut",
              "conjugate", "conjugateOut", "conjugateInstance"]
    for case in rotation["unary"].values():
        require_keys(case, fields + ["length", "lengthSquared"])
        for field in fields:
            require_keys(case[field], "XYZW")
    require_keys(rotation["pairs"], ["identical", "opposite", "zero", "nearThreshold"] +
                 [f"orthogonal{i}" for i in range(7)] + [f"seeded{i}" for i in range(4)])
    fields = ["a", "b", "lerp", "lerpOut", "slerp", "slerpOut", "concatenate", "concatenateOut"]
    for case in rotation["pairs"].values():
        require_keys(case, fields + ["amount"])
        for field in fields:
            require_keys(case[field], "XYZW")
    require_keys(rotation["factories"], [f"angle{i}" for i in range(6)] + ["zeroAxis", "nonunitAxis"] +
                 [f"seeded{i}" for i in range(4)])
    fields = ["axisAngle", "axisAngleOut", "yawPitchRoll", "yawPitchRollOut"]
    for case in rotation["factories"].values():
        require_keys(case, fields + ["axis", "angle", "pitch", "roll"])
        require_keys(case["axis"], "XYZ")
        for field in fields:
            require_keys(case[field], "XYZW")
    require_keys(rotation["matrices"], ("identity", "xHalfTurn", "yHalfTurn", "zHalfTurn", "zero"))
    for case in rotation["matrices"].values():
        require_keys(case, ("input", "value", "out"))
        require_keys(case["input"], matrix({}))
        require_keys(case["value"], "XYZW")
        require_keys(case["out"], "XYZW")


def validate_witness_shape(document, is_reference):
    validate_quaternion_shape(document["QuaternionEdges"])
    for dimension in (2, 3, 4):
        validate_vector_shape(document[f"Vector{dimension}Edges"], dimension)
    projection = document["MatrixPerspectiveEdges"]
    require_keys(projection, ("seed", "size", "fov", "offCenter"))
    refused = {"nearZero", "farZero", "equalDepth", "reversedDepth", "bothZero"}
    cases = refused | {"ordinary", "zeroDimension", "negativeDimension", "nanNear", "nanFar",
                       "infiniteFar", "nanDimension"} | {f"seeded{i}" for i in range(4)}
    for kind in ("size", "fov", "offCenter"):
        names = cases | {"pi", "belowPi", "fnaPi", "infiniteAngle"} if kind == "fov" else cases
        require_keys(projection[kind], names)
        for name, case in projection[kind].items():
            fails = name in refused or (kind == "fov" and name in {
                "zeroDimension", "negativeDimension", "fnaPi", "infiniteAngle"})
            fails = fails or (kind == "fov" and name == "pi" and not is_reference)
            fields = {"a", "b", "near", "far", "valueException", "outException", "out"}
            require_keys(case, fields if fails else fields | {"value"})
            require_keys(case["out"], matrix({}))
            if not fails:
                require_keys(case["value"], matrix({}))


def same_pair(left, right):
    differences = []
    compare.compare("", list(left), list(right), 0, differences)
    compare.compare("", list(right), list(left), 0, differences)
    return not differences


def audit(fna, cna):
    identity = compare.reference_identity(fna)
    candidate, _ = compare.candidate_metadata(cna)
    if candidate is not None and candidate != identity:
        raise ValueError("candidate reference identity differs")
    if identity["sourceRevision"] != REVISION:
        raise ValueError("edge dispositions require the reviewed FNA revision")
    for document, is_reference in ((fna, True), (cna, False)):
        compare.validate_categories(document, CATEGORIES)
        validate_witness_shape(document, is_reference)
    mismatches = []
    for category in CATEGORIES:
        compare.compare("/" + category, fna[category], cna[category], 0, mismatches)
    actual = {path: (left, right) for path, left, right in mismatches}
    expected = retained_differences()
    # Also fail if a retained difference disappears: this is a changed contract to review.
    unexpected = sorted(path for path in actual if path not in expected or not same_pair(actual[path], expected[path]))
    if set(actual) != set(expected) or unexpected:
        missing = sorted(path for path in expected if path not in actual)
        raise ValueError(f"unreviewed differences: {unexpected}; missing retained witnesses: {missing}")
    return len(mismatches)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fna_json")
    parser.add_argument("cna_json")
    args = parser.parse_args()
    try:
        count = audit(compare.load_dump(args.fna_json), compare.load_dump(args.cna_json))
    except (OSError, ValueError, RecursionError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"PASS: {count} exact retained XNA differences; all other selected fields match at zero tolerance. NOT FNA parity.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
