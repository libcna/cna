#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-237: which sign of zero a node's transform carries.

SAMPLE-046's `spaceship.fbx` is the corpus's only file whose reference differs from CNA's output
in nothing but the sign of two zeros: `Fbx_Root`'s `M12` and `M31` are `+0.0` in XNA's answer and
`-0.0` in CNA's, and every other one of the sixteen entries is bit for bit equal. The file's own
`Lcl Rotation` is why -- its Y and Z are the double whose bits are `00 00 00 80 00 00 00 80`,
which is a pair of `float` `-0.0`s written into a `double` slot and reads as the *negative
denormal* `-1.0609978955e-314`. Multiplied by the root's `0.05` scaling and narrowed to `float`
that underflows to `-0.0`, which is exactly what CNA answers and what XNA does not.

These probes separate the candidates one property at a time: is it the parse (XNA reading the
angle as a `float`, where the denormal is already `-0.0`), the arithmetic (an underflow flushed to
a positive zero), or the composition (a sum, in which `0.0 + -0.0` is `+0.0` and a lone product is
not)? Each file is one `Null` root carrying the variant and one triangle under it, so the answered
`bones[]` are that root's transform and nothing else.

    python3 tools/xna-pipeline-oracle/model/make_signed_zero_transform_probes.py <outdir>
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_fbx_fixtures as F                                            # noqa: E402

# The root's `Lcl Rotation` X in the sample, exactly.
X = 1.5207624002755438e-05
# The double the sample's file holds in Y and Z: bits `00 00 00 80 00 00 00 80`.
DENORMAL = -1.0609978955e-314

# name -> (rotation, scaling). Each varies one thing against the one above it.
CASES = {
    # The shape the sample has, and the two readings of its Y and Z that differ.
    "sample_denormal":   ((X, DENORMAL, DENORMAL), (0.05,) * 3),
    "sample_negzero":    ((X, -0.0, -0.0), (0.05,) * 3),
    "sample_poszero":    ((X, 0.0, 0.0), (0.05,) * 3),
    "sample_posdenormal": ((X, -DENORMAL, -DENORMAL), (0.05,) * 3),
    # The same angles with no scaling, so nothing underflows: whatever sign survives here is the
    # parse's or the rotation's, not the narrowing's.
    "unscaled_denormal": ((X, DENORMAL, DENORMAL), (1.0,) * 3),
    "unscaled_negzero":  ((X, -0.0, -0.0), (1.0,) * 3),
    # One axis at a time, to say which entry each angle owns.
    "y_denormal":        ((0.0, DENORMAL, 0.0), (0.05,) * 3),
    "z_denormal":        ((0.0, 0.0, DENORMAL), (0.05,) * 3),
    "x_denormal":        ((DENORMAL, 0.0, 0.0), (0.05,) * 3),
    "y_negzero":         ((0.0, -0.0, 0.0), (0.05,) * 3),
    "z_negzero":         ((0.0, 0.0, -0.0), (0.05,) * 3),
    "x_negzero":         ((-0.0, 0.0, 0.0), (0.05,) * 3),
    # No rotation at all: the scaling's own zeros.
    "scale_only":        ((0.0, 0.0, 0.0), (0.05,) * 3),
    # A scaling small enough that even the *narrowed* product of a normal angle underflows, so the
    # narrowing is separated from the denormal input.
    "tiny_scale":        ((X, 0.0, 0.0), (1.0e-30,) * 3),
}


def exact(values):
    """`%.17g`, because `%g` would round the X angle away and flatten the denormal."""
    return ",".join(("%.17g" % value) for value in values)


def probe(name, rotation, scaling):
    models = ('\tModel: "Model::Root", "Null" {\n\t\tVersion: 232\n'
              '\t\tProperties60:  {\n'
              '\t\t\tProperty: "Lcl Translation", "Lcl Translation", "A+",0,0,0\n'
              '\t\t\tProperty: "Lcl Rotation", "Lcl Rotation", "A+",%s\n'
              '\t\t\tProperty: "Lcl Scaling", "Lcl Scaling", "A+",%s\n'
              '\t\t}\n'
              '\t\tMultiLayer: 0\n\t\tMultiTake: 1\n\t\tShading: Y\n'
              '\t\tCulling: "CullingOff"\n\t}\n' % (exact(rotation), exact(scaling)))
    models += F.mesh_model("Tip", [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [[0, 1, 2]])
    relations = ('\tModel: "Model::Root", "Null" {\n\t}\n'
                 '\tModel: "Model::Tip", "Mesh" {\n\t}\n')
    connections = ('\tConnect: "OO", "Model::Root", "Model::Scene"\n'
                   '\tConnect: "OO", "Model::Tip", "Model::Root"\n')
    header = {"count": 2, "models": 2, "materials": 0, "deformers": 0}
    return header, models, relations, connections


def main(argv) -> int:
    out = argv[0] if argv else "build/xna-sample-sweep/probe-signedzero"
    sources = os.path.join(out, "src", "model")
    os.makedirs(sources, exist_ok=True)
    cases = []
    for name, (rotation, scaling) in CASES.items():
        F.write(os.path.join(sources, name + ".fbx"), *probe(name, rotation, scaling))
        cases.append({"case": "signedzero/" + name, "source": "model/%s.fbx" % name,
                      "importer": "FbxImporter", "processor": "ModelProcessor",
                      "platform": "Windows", "profile": "HiDef"})
    with open(os.path.join(out, "signedzero.json"), "w") as handle:
        json.dump({"format": "CNA.XnaDifferential.Corpus", "version": 1,
                   "comment": "plans/plan_xna_sample_xnb_sweep.md XNASWEEP-237 signed-zero probes",
                   "cases": cases}, handle, indent=1)
    print("%d cases in %s" % (len(cases), sources))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
