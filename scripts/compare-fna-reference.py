#!/usr/bin/env python3
"""Task 479 (plans/plan_graphics.md Phase 53): compare CNA's reference-value JSON dump against FNA's.

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

Exit code 0 if every compared key matched; 1 if any mismatch or missing key was found.
"""
import argparse
import json
import sys

DEFAULT_CATEGORIES = ["NonRenderingApis", "PackedVector", "Viewport"]


def is_number(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def compare(path, fna_value, cna_container, tolerance, mismatches):
    """Recursively compare fna_value (found at `path`) against the same path in cna_container."""
    keys = path.split("/")
    cna_value = cna_container
    for k in keys:
        if not isinstance(cna_value, dict) or k not in cna_value:
            mismatches.append((path, fna_value, "<missing on CNA side>"))
            return
        cna_value = cna_value[k]

    if isinstance(fna_value, dict):
        for k, v in fna_value.items():
            compare(f"{path}/{k}", v, cna_container, tolerance, mismatches)
        return

    if is_number(fna_value) and is_number(cna_value):
        if abs(float(fna_value) - float(cna_value)) > tolerance:
            mismatches.append((path, fna_value, cna_value))
    elif fna_value != cna_value:
        mismatches.append((path, fna_value, cna_value))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("fna_json", help="Path to the FNA-side reference-values.json")
    parser.add_argument("cna_json", help="Path to the CNA-side cna-reference-values.json")
    parser.add_argument("--tolerance", type=float, default=1e-4,
                         help="Absolute tolerance for float comparisons (default: 1e-4)")
    parser.add_argument("--category", action="append", dest="categories",
                         help="Top-level key to compare; repeatable. Default: %s" % DEFAULT_CATEGORIES)
    args = parser.parse_args()

    categories = args.categories or DEFAULT_CATEGORIES

    with open(args.fna_json, encoding="utf-8") as f:
        fna = json.load(f)
    with open(args.cna_json, encoding="utf-8") as f:
        cna = json.load(f)

    mismatches = []
    compared_categories = []
    for category in categories:
        if category not in fna:
            print(f"warning: category '{category}' not found in FNA JSON, skipping", file=sys.stderr)
            continue
        compared_categories.append(category)
        compare(category, fna[category], cna, args.tolerance, mismatches)

    if not compared_categories:
        print("error: no requested categories were found in the FNA JSON", file=sys.stderr)
        return 1

    if mismatches:
        print(f"FAIL: {len(mismatches)} mismatch(es) across categories {compared_categories}:")
        for path, fna_value, cna_value in mismatches:
            print(f"  {path}: FNA={fna_value!r} CNA={cna_value!r}")
        return 1

    print(f"PASS: all compared values match across categories {compared_categories} "
          f"(tolerance={args.tolerance})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
