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
CATEGORIES = ("Vector2Edges", "MatrixPerspectiveEdges")
REVISION = "00498b0b81425b14bf8da23e68655215b9b5daa7"


def single(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def matrix(entries):
    result = {f"M{row}{column}": "+zero" for row in range(1, 5) for column in range(1, 5)}
    result.update(entries)
    return result


def retained_differences():
    """Exact witnesses, not wildcard paths or relaxed numeric tolerances."""
    expected = {}
    prefix = "/Vector2Edges/magnitude"
    expected[prefix + "/large/length"] = ("+inf", single(1e20))
    expected[prefix + "/tiny/length"] = ("+zero", single(1e-30))
    for overload in ("normalize", "normalizeOut", "normalizeInstance"):
        expected[f"{prefix}/large/{overload}/X"] = ("+zero", 1)
        expected[f"{prefix}/tiny/{overload}/X"] = ("+inf", 1)
        expected[f"{prefix}/tiny/{overload}/Y"] = ("nan", "+zero")
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
    return expected


def require_keys(value, names):
    if not isinstance(value, dict) or set(value) != set(names):
        raise ValueError(f"incomplete edge witness shape; expected keys {sorted(names)}")


def validate_witness_shape(document, is_reference):
    vector = document["Vector2Edges"]
    require_keys(vector, ("seed", "arithmetic", "magnitude", "format", "unequalEquals",
                         "unequalOperator", "unequalNotOperator", "signedZeroEquals", "signedZeroHashEqual"))
    cases = ["zero", "signedZero", "boundaries", "outside", "infinity", "nan", "max", "subnormal"]
    require_keys(vector["arithmetic"], cases + [f"seeded{i}" for i in range(8)])
    operations = ("input", "divisor", "divide", "divideOut", "divideOperator", "scalarDivide",
                  "scalarDivideOut", "scalarDivideOperator", "clamp", "clampOut", "reversedClamp", "reversedClampOut")
    for case in vector["arithmetic"].values():
        require_keys(case, operations + ("equalsCopy", "operatorEqualCopy", "operatorUnequalCopy", "copyHashEqual"))
        for operation in operations:
            require_keys(case[operation], ("X", "Y"))
    require_keys(vector["magnitude"], ("zero", "threeFour", "large", "tiny", "infinity", "nan"))
    for case in vector["magnitude"].values():
        require_keys(case, ("input", "length", "lengthSquared", "normalize", "normalizeOut", "normalizeInstance"))
        for operation in ("input", "normalize", "normalizeOut", "normalizeInstance"):
            require_keys(case[operation], ("X", "Y"))
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
