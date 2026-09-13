#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-176: the pivot-conversion residue a child inherits.

FBX's pivot set is not something an XNA `Matrix` can carry, so the conversion replaces a node's
`Rpost^-1 . R . Rpre` with a single Euler triple obtained by decomposing it -- from the *float*
matrix, which at a quarter turn cannot tell 90 from 90.0000839 because `sin` of both is `1.0f`.
The node keeps its composed rotation; its children carry the difference, each multiplied by
`E . P^-1`. And it stops there: a child that inherited a residue passes none of its own on.

Two files, because the two halves need different instruments:

  * `fbx_pivot_residue.fbx` -- five parent/child pairs whose whole graph the genuine importer and
    CNA agree on bit for bit, so it joins the graph regression. Three carry a visible residue (a
    `PreRotation` on X with the rotation on Z, which is the shape RobotGame's mechs have; the same
    on Z and Y; and a child carrying only a translation, which shows the residue turns the
    rotation and leaves the translation alone) and two are negative controls whose decomposition
    is exact, so no residue is applied at all: a `PreRotation` and a rotation on the *same* axis,
    and a `PostRotation` where the composition stays a single-axis turn.

  * `fbx_pivot_residue_chain.fbx` -- a three-deep chain of two degenerate nodes and a child, which
    is where the residue's *stopping* is observable. CNA does not reproduce this file's grandchild
    bit for bit -- one entry of a quarter turn is an ulp out, which is the composition's own
    residue and not this rule's -- so it is checked by a tolerance a chained residue could not
    pass rather than by the graph.

    python3 tools/xna-pipeline-oracle/model/make_pivot_residue_fixture.py <directory>
"""
from __future__ import annotations

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# 90.0000839233398 is `Kiev.FBX`'s own: a quarter turn as a `float` written back as a decimal.
NEAR = 90.0000839233398
CHILD = {"lclRotation": [0, 0, -NEAR]}

PAIRS = [
    # PreRotation X, rotation Z: RobotGame's own shape, and the one the corpus needs.
    [{"lclRotation": [0, 0, NEAR], "preRotation": [-90, 0, 0], "rotationActive": 1}, dict(CHILD)],
    # PreRotation Z, rotation Y: the same degeneracy on another pair of axes.
    [{"lclRotation": [0, NEAR, 0], "preRotation": [0, 0, -90], "rotationActive": 1}, dict(CHILD)],
    # A child with only a translation: the residue turns the rotation, not the translation.
    [{"lclRotation": [0, 0, NEAR], "preRotation": [-90, 0, 0], "rotationActive": 1},
     {"lclTranslation": [3, 4, 5]}],
    # Negative control: PreRotation and rotation on the same axis compose to a single turn, whose
    # decomposition is exact, so no residue exists and the child is its own.
    [{"lclRotation": [0, 0, NEAR], "preRotation": [0, 0, -90], "rotationActive": 1}, dict(CHILD)],
    # Negative control: a PostRotation on the rotation's own axis, likewise exact.
    [{"lclRotation": [0, 0, NEAR], "postRotation": [0, 0, -90], "rotationActive": 1}, dict(CHILD)],
]

CHAIN = [[{"lclRotation": [0, 0, NEAR], "preRotation": [-90, 0, 0], "rotationActive": 1},
          {"lclRotation": [0, 0, NEAR], "preRotation": [-90, 0, 0], "rotationActive": 1},
          dict(CHILD)]]


def write(directory, name, spec):
    path = os.path.join(directory, name)
    spec_path = path + ".spec.json"
    with open(spec_path, "w", encoding="utf-8") as handle:
        json.dump(spec, handle)
    subprocess.check_call([sys.executable, os.path.join(HERE, "make_pivot_probe.py"),
                          spec_path, path])
    os.remove(spec_path)


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    write(directory, "fbx_pivot_residue.fbx", PAIRS)
    write(directory, "fbx_pivot_residue_chain.fbx", CHAIN)
    return 0


if __name__ == "__main__":
    sys.exit(main())
