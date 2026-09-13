#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-189: the sign of a zero in a converted transform.

The `.x` importer changes a `FrameTransformMatrix` into the right-handed basis the pipeline holds,
and the change is a *multiplication* by `diag(1, 1, -1, 1)` on both sides rather than the five
negations it equals in value. The two differ only in the sign of a zero: negating `+0` answers
`-0`, where a dot product of four terms answers whatever its four signed zeros add up to, and IEEE
addition makes `(-0) + (+0)` positive.

The basis matrices' own zeros carry a sign too, because a zero coefficient times a negative entry
is a negative zero -- and the two multiplications do **not** carry the same signs. What 144 measured
matrices pin down is `B_L`'s `M24` and `M31`, both negative zeros, and every off-diagonal zero of
`B_R`'s first and third columns, all positive; the rest is not constrained by any of them. Each sign
pattern below is one the fit needs, and the last two are what separates the two sides: every term of
one dot product is a negative zero there, and one basis used twice answers the wrong sign.

`Car` is the shape SAMPLE-028's `Car.x` actually has, and `Dense` is the negative control -- no zero
anywhere in its upper 3x3, so it measures the plain negation and nothing else.

    python3 tools/xna-pipeline-oracle/model/make_basis_signed_zero_fixture.py <directory>
"""
from __future__ import annotations

import os
import sys

# Row-major, exactly as a FrameTransformMatrix writes them. "-0.0" is written "-0.000000".
FRAMES = [
    ("Car", [0.366722, 0.0, 0.0, 0.0,
             0.0, 0.366722, 0.0, 0.0,
             0.0, 0.0, 0.366722, 0.0,
             -0.000001, 1.534720, 0.741776, 1.0]),
    ("Dense", [1.0, 2.0, 3.0, 0.0,
               4.0, 5.0, 6.0, 0.0,
               7.0, 8.0, 9.0, 0.0,
               10.0, 11.0, 12.0, 1.0]),
    ("RowNegative", [-1.5, -0.0, -1.5, -1.5,
                     2.5, -1.5, 0.0, -1.5,
                     2.5, -1.5, -1.5, -1.5,
                     -0.0, -0.0, -1.5, -0.0]),
    ("ColumnNegative", [-1.5, 0.0, -1.5, 2.5,
                        -0.0, -1.5, -1.5, -1.5,
                        -1.5, -1.5, 2.5, -0.0,
                        2.5, 2.5, 2.5, -1.5]),
    ("MixedA", [2.5, 2.5, 2.5, -0.0,
                -0.0, -1.5, 2.5, 2.5,
                2.5, 2.5, -0.0, 0.0,
                2.5, -1.5, 2.5, 2.5]),
    ("MixedB", [0.0, -0.0, -1.5, 2.5,
                -0.0, 2.5, -0.0, -1.5,
                2.5, -1.5, 0.0, -0.0,
                0.0, -1.5, 0.0, -0.0]),
    ("MixedC", [-1.5, 2.5, 2.5, -1.5,
                2.5, 2.5, 2.5, 2.5,
                2.5, 2.5, 0.0, 0.0,
                -0.0, 2.5, -0.0, -1.5]),
    ("MixedD", [0.0, -1.5, 0.0, -0.0,
                2.5, -1.5, -1.5, -0.0,
                -0.0, -0.0, 2.5, 0.0,
                -1.5, -0.0, 2.5, -0.0]),
    ("MixedE", [-1.5, -1.5, -1.5, -1.5,
                2.5, -0.0, -0.0, 2.5,
                0.0, 0.0, -0.0, -1.5,
                -1.5, 2.5, 0.0, -0.0]),
    # The two a single shared basis could not answer: every term of one dot product is a negative
    # zero there, which is what separates the two multiplications' own sign patterns.
    ("RowAllNegative", [-1.5, -1.5, 2.5, 2.5,
                        -1.5, -0.0, -1.5, 2.5,
                        -1.5, -1.5, -0.0, 0.0,
                        -0.0, 0.0, 2.5, 0.0]),
    ("ColumnAllNegative", [0.0, -1.5, -1.5, -0.0,
                           -1.5, 2.5, -1.5, 2.5,
                           0.0, 2.5, 2.5, 2.5,
                           -0.0, -1.5, -0.0, -1.5]),
]

MESH = """  Mesh Point {
   3;
   0.000000;0.000000;5.000000;,
   1.000000;0.000000;5.000000;,
   0.000000;1.000000;5.000000;;
   1;
   3;0,1,2;;
  }
"""

HEADER = """xof 0303txt 0032
// Written by tools/xna-pipeline-oracle/model/make_basis_signed_zero_fixture.py for this repository.

"""


def literal(value: float) -> str:
    # "%f" is what writes a negative zero as "-0.000000"; the sign is the whole point of the file.
    return "%f" % value


def frame(name: str, values: list[float]) -> str:
    rows = ",\n".join("   " + ", ".join(literal(values[r * 4 + c]) for c in range(4))
                      for r in range(4))
    return "Frame %s {\n  FrameTransformMatrix {\n%s;;\n  }\n%s}\n\n" % (name, rows, MESH)


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "x_basis_signed_zero.x")
    with open(path, "w", newline="\n") as handle:
        handle.write(HEADER)
        for name, values in FRAMES:
            handle.write(frame(name, values))
    print("wrote %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
