#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Regressions for the ABI baseline's constant measurement (CBIND-159).

Each case compiles the real probe against a small header tree, so what is proven is what the gate
does: a flag written `(UINT32_C(1) << n)` that moves or vanishes, or a mask built from such flags
that changes, is reported as a break. Needs a C compiler (`$CC`, else `cc`), no CNA build.
"""

import os
from pathlib import Path
import re
import tempfile
import unittest

import generate_abi_baseline as baseline

COMPILER = os.environ.get("CC", "cc")

PRELUDE = """#ifndef CNA_C_CNA_H
#define CNA_C_CNA_H
#include <stdint.h>
#define CNA_ABI_VERSION_MAJOR UINT32_C(0)
#define CNA_ABI_VERSION_MINOR UINT32_C(1)
#define CNA_ABI_VERSION_PATCH UINT32_C(0)
#define CNA_ABI_VERSION UINT32_C(256)
"""

# The flags ABI 0.46.0 had and 0.47.0 retired, written exactly as each generation's graphics.h
# wrote them -- the mask on one line, then built across continuation lines from the bits it keeps.
RETIRED_046 = """
#define CNA_GRAPHICS_CAPABILITY_FLAG_HALF_FLOAT_TEXTURE_LINEAR_FILTERING (UINT64_C(1) << 16)
#define CNA_GRAPHICS_CAPABILITY_FLAG_COMPUTE_SHADERS (UINT64_C(1) << 17)
#define CNA_GRAPHICS_CAPABILITY_FLAG_INDIRECT_DRAW (UINT64_C(1) << 18)
#define CNA_RENDERER_FORMAT_USAGE_TEXTURE_STORAGE (UINT32_C(1) << 0)
#define CNA_RENDERER_FORMAT_USAGE_SAMPLED (UINT32_C(1) << 1)
#define CNA_RENDERER_FORMAT_USAGE_FILTERABLE (UINT32_C(1) << 2)
#define CNA_RENDERER_FORMAT_USAGE_RENDER_TARGET (UINT32_C(1) << 3)
#define CNA_RENDERER_FORMAT_USAGE_BLENDABLE (UINT32_C(1) << 4)
#define CNA_RENDERER_FORMAT_USAGE_STORAGE_READ (UINT32_C(1) << 5)
#define CNA_RENDERER_FORMAT_USAGE_STORAGE_WRITE (UINT32_C(1) << 6)
#define CNA_RENDERER_FORMAT_USAGE_STORAGE_ATOMIC (UINT32_C(1) << 7)
#define CNA_RENDERER_FORMAT_USAGE_TRANSFER_SOURCE (UINT32_C(1) << 8)
#define CNA_RENDERER_FORMAT_USAGE_TRANSFER_DESTINATION (UINT32_C(1) << 9)
#define CNA_RENDERER_FORMAT_USAGE_MIPMAPPED (UINT32_C(1) << 10)
#define CNA_RENDERER_FORMAT_USAGE_MULTISAMPLE (UINT32_C(1) << 11)
#define CNA_RENDERER_FORMAT_USAGE_COLOR_TRANSFER (UINT32_C(1) << 12)
#define CNA_RENDERER_FORMAT_USAGE_ALL ((UINT32_C(1) << 13) - UINT32_C(1))
"""
RETIRED_047 = """
#define CNA_GRAPHICS_CAPABILITY_FLAG_HALF_FLOAT_TEXTURE_LINEAR_FILTERING (UINT64_C(1) << 16)
#define CNA_RENDERER_FORMAT_USAGE_TEXTURE_STORAGE (UINT32_C(1) << 0)
#define CNA_RENDERER_FORMAT_USAGE_SAMPLED (UINT32_C(1) << 1)
#define CNA_RENDERER_FORMAT_USAGE_FILTERABLE (UINT32_C(1) << 2)
#define CNA_RENDERER_FORMAT_USAGE_RENDER_TARGET (UINT32_C(1) << 3)
#define CNA_RENDERER_FORMAT_USAGE_BLENDABLE (UINT32_C(1) << 4)
#define CNA_RENDERER_FORMAT_USAGE_TRANSFER_SOURCE (UINT32_C(1) << 8)
#define CNA_RENDERER_FORMAT_USAGE_TRANSFER_DESTINATION (UINT32_C(1) << 9)
#define CNA_RENDERER_FORMAT_USAGE_MIPMAPPED (UINT32_C(1) << 10)
#define CNA_RENDERER_FORMAT_USAGE_MULTISAMPLE (UINT32_C(1) << 11)
#define CNA_RENDERER_FORMAT_USAGE_COLOR_TRANSFER (UINT32_C(1) << 12)
#define CNA_RENDERER_FORMAT_USAGE_ALL                                                   \\
    (CNA_RENDERER_FORMAT_USAGE_TEXTURE_STORAGE | CNA_RENDERER_FORMAT_USAGE_SAMPLED |    \\
     CNA_RENDERER_FORMAT_USAGE_FILTERABLE | CNA_RENDERER_FORMAT_USAGE_RENDER_TARGET |   \\
     CNA_RENDERER_FORMAT_USAGE_BLENDABLE | CNA_RENDERER_FORMAT_USAGE_TRANSFER_SOURCE |  \\
     CNA_RENDERER_FORMAT_USAGE_TRANSFER_DESTINATION | CNA_RENDERER_FORMAT_USAGE_MIPMAPPED | \\
     CNA_RENDERER_FORMAT_USAGE_MULTISAMPLE | CNA_RENDERER_FORMAT_USAGE_COLOR_TRANSFER)
"""

FLAGS = """
#define CNA_PROBE_FLAG_A (UINT32_C(1) << 0)
#define CNA_PROBE_FLAG_B (UINT32_C(1) << 1)
#define CNA_PROBE_FLAG_C (UINT64_C(1) << 40)
#define CNA_PROBE_ALL (CNA_PROBE_FLAG_A | CNA_PROBE_FLAG_B)
#define CNA_PROBE_COUNT UINT32_C(7)
#define CNA_PROBE_NONE INT32_C(-1)
#define CNA_PROBE_PLAIN 4
#define CNA_PROBE_YES ((uint8_t)1)
"""


def measure(body: str) -> dict:
    with tempfile.TemporaryDirectory() as root:
        headers = Path(root) / "CNA" / "C"
        headers.mkdir(parents=True)
        (headers / "cna.h").write_text(PRELUDE + body + "\n#endif\n", encoding="utf-8")
        return baseline.measure(None, COMPILER, [], include_dir=Path(root))


def breaks(old_body: str, new_body: str) -> list[str]:
    additions, found, _ = baseline.describe_differences(measure(old_body), measure(new_body))
    return found


class ConstantMeasurementTests(unittest.TestCase):
    def test_shift_and_plain_constants_are_measured_by_value(self):
        integers = measure(FLAGS)["integers"]
        self.assertEqual(integers["CNA_PROBE_FLAG_A"], 1)
        self.assertEqual(integers["CNA_PROBE_FLAG_B"], 2)
        self.assertEqual(integers["CNA_PROBE_FLAG_C"], 1 << 40)
        self.assertEqual(integers["CNA_PROBE_ALL"], 3)
        self.assertEqual(integers["CNA_PROBE_COUNT"], 7)
        self.assertEqual(integers["CNA_PROBE_NONE"], -1)
        self.assertEqual(integers["CNA_PROBE_PLAIN"], 4)
        self.assertEqual(integers["CNA_PROBE_YES"], 1)

    def test_an_unchanged_header_has_no_difference(self):
        additions, found, _ = baseline.describe_differences(measure(FLAGS), measure(FLAGS))
        self.assertEqual((additions, found), ([], []))

    def test_a_flag_that_moves_to_another_bit_is_a_break(self):
        moved = FLAGS.replace("CNA_PROBE_FLAG_B (UINT32_C(1) << 1)", "CNA_PROBE_FLAG_B (UINT32_C(1) << 2)")
        self.assertEqual(breaks(FLAGS, moved), [
            "constant changed: CNA_PROBE_ALL: 3 -> 5",
            "constant changed: CNA_PROBE_FLAG_B: 2 -> 4",
        ])

    def test_a_removed_flag_is_a_break(self):
        removed = FLAGS.replace("#define CNA_PROBE_FLAG_C (UINT64_C(1) << 40)\n", "")
        self.assertEqual(breaks(FLAGS, removed), ["constant removed: CNA_PROBE_FLAG_C"])

    def test_a_mask_that_changes_is_a_break(self):
        narrowed = FLAGS.replace("(CNA_PROBE_FLAG_A | CNA_PROBE_FLAG_B)", "(CNA_PROBE_FLAG_A)")
        self.assertEqual(breaks(FLAGS, narrowed), ["constant changed: CNA_PROBE_ALL: 3 -> 1"])

    def test_an_ordinary_constant_that_changes_is_still_a_break(self):
        changed = FLAGS.replace("CNA_PROBE_COUNT UINT32_C(7)", "CNA_PROBE_COUNT UINT32_C(8)")
        self.assertEqual(breaks(FLAGS, changed), ["constant changed: CNA_PROBE_COUNT: 7 -> 8"])

    def test_a_floating_constant_is_recorded_exactly_rather_than_truncated(self):
        measured = measure("#define CNA_PROBE_PI 3.14159274F\n#define CNA_PROBE_ZONE (7849.0f / 32768.0f)\n")
        self.assertEqual(measured["integers"], {"CNA_ABI_VERSION": 256, "CNA_ABI_VERSION_MAJOR": 0,
                                                "CNA_ABI_VERSION_MINOR": 1, "CNA_ABI_VERSION_PATCH": 0})
        self.assertEqual(measured["floats"], {"CNA_PROBE_PI": 3.1415927410125732,
                                              "CNA_PROBE_ZONE": 7849.0 / 32768.0})

    def test_the_0_46_to_0_47_retirement_of_flags_is_reported(self):
        # These are the differences the 0.46.0 -> 0.47.0 review had to find by hand.
        self.assertEqual(breaks(RETIRED_046, RETIRED_047), [
            "constant removed: CNA_GRAPHICS_CAPABILITY_FLAG_COMPUTE_SHADERS",
            "constant removed: CNA_GRAPHICS_CAPABILITY_FLAG_INDIRECT_DRAW",
            "constant removed: CNA_RENDERER_FORMAT_USAGE_STORAGE_ATOMIC",
            "constant removed: CNA_RENDERER_FORMAT_USAGE_STORAGE_READ",
            "constant removed: CNA_RENDERER_FORMAT_USAGE_STORAGE_WRITE",
            "constant changed: CNA_RENDERER_FORMAT_USAGE_ALL: 8191 -> 7967",
        ])


class PublicHeaderTests(unittest.TestCase):
    def test_every_value_macro_of_the_public_headers_is_measured(self):
        measured = baseline.measure(None, COMPILER, [])
        recorded = set().union(*(measured[section] for section in ("integers", "floats", "strings", "colors")))
        defined = {
            match.group(1)
            for text in baseline.read_headers().values()
            for match in re.finditer(r"^#define[ \t]+(CNA_[A-Z0-9_]+)[ \t]+\S", text, re.M)
        } - baseline.SKIPPED_MACROS
        self.assertEqual(sorted(defined - recorded), [])
        usage = {name: value for name, value in measured["integers"].items()
                 if name.startswith("CNA_RENDERER_FORMAT_USAGE_") and name != "CNA_RENDERER_FORMAT_USAGE_ALL"}
        self.assertEqual(len(usage), 10)
        mask = 0
        for value in usage.values():
            mask |= value
        self.assertEqual(measured["integers"]["CNA_RENDERER_FORMAT_USAGE_ALL"], mask)
        self.assertEqual(measured["integers"]["CNA_GRAPHICS_CAPABILITY_FLAG_INSTANCING"], 1 << 10)
        self.assertAlmostEqual(measured["floats"]["CNA_MATH_PI"], 3.14159274, places=6)


if __name__ == "__main__":
    unittest.main()
