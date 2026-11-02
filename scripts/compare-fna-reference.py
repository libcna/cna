#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""graphics Task 479 (Phase 53): compare CNA's reference-value JSON dump against FNA's.

Diffs the JSON produced by the `cna_reference_dump` tool (tools/cna-reference/) against the JSON
produced by the FNA-side reference harness (tools/fna-reference/), category by category. This is
the last mile of the deterministic-conformance chain started in Tasks 471-473/476: those tasks
proved FNA's own real values could be captured as JSON; this script is what actually diffs them
against CNA's real C++ output, rather than requiring a human to eyeball two JSON files side by
side.

The comparison is intentionally one-directional: for every key present on the FNA side, the same
key path must exist on the CNA side with an equal (or, for floats, sufficiently close) value.
Keys that exist only on the CNA side are NOT reported as mismatches -- CNA has real CNAEXT
extensions with no FNA equivalent (e.g. PrimitiveType.PointListEXT, several SurfaceFormat *EXT
values), and requiring the reverse direction too would make this script reject legitimate,
intentional CNA extensions as false failures.

Usage:
    python3 scripts/compare-fna-reference.py <fna-reference-values.json> <cna-reference-values.json>
    [--tolerance 1e-4] [--category NonRenderingApis --category PackedVector --category Viewport]

Integer pairs compare exactly. Each finite floating-point field uses the absolute tolerance,
including mixed integer/float tokens (the writers may emit integral floats as integers).
Positive and negative zero compare equal. Non-finite numbers are unsupported, including
NaN versus NaN: no field currently has an approved NaN-equivalence rule.

Both roots must be JSON objects. Every selected category is required on both sides and must
be an object/array containing at least one scalar leaf. Arrays compare in order with equal
lengths. Boolean, string, null and container types must match. Duplicate keys are invalid.

Exit code 0 if every selected category matched; 1 for invalid input or mismatches;
2 for invalid command-line arguments (including non-finite/negative tolerances).
"""
import argparse
import json
import math
import sys
from fractions import Fraction

DEFAULT_CATEGORIES = ["NonRenderingApis", "PackedVector", "Viewport"]


def is_number(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def child_path(path, key):
    """Escape object keys as JSON Pointer tokens for unambiguous diagnostics."""
    return f"{path}/{str(key).replace('~', '~0').replace('/', '~1')}"


def compare(path, fna_value, cna_value, tolerance, mismatches):
    """Compare two nodes, allowing extra CNA object keys but no missing FNA keys."""
    if is_number(fna_value) and is_number(cna_value):
        if any(isinstance(v, float) and not math.isfinite(v) for v in (fna_value, cna_value)):
            mismatches.append((path, fna_value, "<unsupported non-finite number>"))
        elif isinstance(fna_value, int) and isinstance(cna_value, int):
            if fna_value != cna_value:
                mismatches.append((path, fna_value, cna_value))
        else:
            # Mixed subtraction must not round a large integer through binary64 first.
            if abs(Fraction(fna_value) - Fraction(cna_value)) > Fraction(tolerance):
                mismatches.append((path, fna_value, cna_value))
        return

    if type(fna_value) is not type(cna_value):
        mismatches.append((path, fna_value, cna_value))
        return

    if isinstance(fna_value, dict):
        for k, v in fna_value.items():
            if k not in cna_value:
                mismatches.append((child_path(path, k), v, "<missing on CNA side>"))
            else:
                compare(child_path(path, k), v, cna_value[k], tolerance, mismatches)
    elif isinstance(fna_value, list):
        if len(fna_value) != len(cna_value):
            mismatches.append((path, fna_value, cna_value))
        else:
            for i, (fna_item, cna_item) in enumerate(zip(fna_value, cna_value)):
                compare(child_path(path, i), fna_item, cna_item, tolerance, mismatches)
    elif fna_value != cna_value:
        mismatches.append((path, fna_value, cna_value))


def reject_nonfinite(token):
    raise ValueError(f"unsupported non-finite number {token!r}; no NaN-equivalence rule is approved")


def finite_float(token):
    value = float(token)
    if not math.isfinite(value):
        reject_nonfinite(token)
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate object key {key!r}")
        result[key] = value
    return result


def load_dump(filename):
    with open(filename, encoding="utf-8") as stream:
        value = json.load(stream, parse_constant=reject_nonfinite, parse_float=finite_float,
                          object_pairs_hook=unique_object)
    if not isinstance(value, dict):
        raise ValueError("document root must be an object")
    return value


def has_scalar(value):
    if isinstance(value, dict):
        return any(has_scalar(child) for child in value.values())
    if isinstance(value, list):
        return any(has_scalar(child) for child in value)
    return True


def validate_categories(document, categories):
    for category in categories:
        if category not in document:
            raise ValueError(f"required category {category!r} is missing")
        value = document[category]
        if not isinstance(value, (dict, list)):
            raise ValueError(f"category {category!r} must be an object or array")
        if not has_scalar(value):
            raise ValueError(f"category {category!r} is empty (no scalar leaves)")


def parse_tolerance(token):
    try:
        value = float(token)
    except ValueError as error:
        raise argparse.ArgumentTypeError("tolerance must be a finite nonnegative number") from error
    if not math.isfinite(value) or value < 0:
        raise argparse.ArgumentTypeError("tolerance must be a finite nonnegative number")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("fna_json", help="Path to the FNA-side reference-values.json")
    parser.add_argument("cna_json", help="Path to the CNA-side cna-reference-values.json")
    parser.add_argument("--tolerance", type=parse_tolerance, default=1e-4,
                         help="Absolute tolerance for float comparisons (default: 1e-4)")
    parser.add_argument("--category", action="append", dest="categories",
                         help="Top-level key to compare; repeatable. Default: %s" % DEFAULT_CATEGORIES)
    args = parser.parse_args()

    categories = list(dict.fromkeys(args.categories or DEFAULT_CATEGORIES))
    documents = []
    for side, filename in (("FNA", args.fna_json), ("CNA", args.cna_json)):
        try:
            document = load_dump(filename)
            validate_categories(document, categories)
        except (OSError, ValueError, RecursionError) as error:
            print(f"error: invalid {side} input {filename!r}: {error}", file=sys.stderr)
            return 1
        documents.append(document)
    fna, cna = documents

    mismatches = []
    try:
        for category in categories:
            compare(child_path("", category), fna[category], cna[category], args.tolerance, mismatches)
    except RecursionError:
        print("error: input nesting exceeds comparison limit", file=sys.stderr)
        return 1

    if mismatches:
        print(f"FAIL: {len(mismatches)} mismatch(es) across categories {categories}:")
        for path, fna_value, cna_value in mismatches:
            print(f"  {path}: FNA={fna_value!r} CNA={cna_value!r}")
        return 1

    print(f"PASS: all compared values match across categories {categories} "
          f"(tolerance={args.tolerance})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
