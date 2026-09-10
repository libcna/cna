#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-213: deep bone chains under controlled roots.

`XNASWEEP-193` measured that a node answers the quotient of two `float` world transforms. Six of
RobotGame's mech skeletons still differ from the genuine pipeline by one float ulp at depth one,
growing with depth -- which is what a quotient's own round trip would do, and which nothing in the
rule itself predicts. These probes separate the two: the *same* chain of children under roots that
differ only in what they do to the world, deep enough for an accumulation to show.

Each file is one chain: a root carrying the variant's own transform, eight nulls below it carrying
an identical local transform each, and a triangle at the bottom so `ModelProcessor` has something
to build. The answered `bones[]` are then depths 0 to 9 of the same arithmetic.

    python3 tools/xna-pipeline-oracle/model/make_bone_chain_probes.py <outdir>
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_fbx_fixtures as F                                            # noqa: E402

# The root transforms worth separating. `robotgame` is the shape the six mech skeletons have --
# a hundredth written back as a double under a unit of a hundred, which is where `0.99999994`
# comes from -- and the rest vary one property at a time around it.
ROOTS = {
    "identity":      dict(t=(0, 0, 0), r=(0, 0, 0), s=(1, 1, 1), unit=1),
    "robotgame":     dict(t=(0, 0, 0), r=(0, 0, 0), s=(0.00999999977648258,) * 3, unit=100),
    "translation":   dict(t=(10, 20, 30), r=(0, 0, 0), s=(1, 1, 1), unit=1),
    "uniform":       dict(t=(0, 0, 0), r=(0, 0, 0), s=(2, 2, 2), unit=1),
    "nonuniform":    dict(t=(0, 0, 0), r=(0, 0, 0), s=(2, 3, 5), unit=1),
    "rotation":      dict(t=(0, 0, 0), r=(0, 0, 37), s=(1, 1, 1), unit=1),
    "rotscale":      dict(t=(0, 0, 0), r=(30, 0, 0), s=(2, 2, 2), unit=1),
    "powerof2":      dict(t=(0, 0, 0), r=(0, 0, 0), s=(0.25, 0.25, 0.25), unit=4),
    "tenth":         dict(t=(0, 0, 0), r=(0, 0, 0), s=(0.1, 0.1, 0.1), unit=10),
    "ulpscale":      dict(t=(0, 0, 0), r=(0, 0, 0), s=(0.99999994,) * 3, unit=1),
    "unitonly":      dict(t=(0, 0, 0), r=(0, 0, 0), s=(1, 1, 1), unit=100),
    "skew":          dict(t=(0, 0, 0), r=(0, 0, 1e-11), s=(1, 1, 1), unit=1),
}

# The child every level of every chain carries, so the only variable is the root.
CHILDREN = {
    "flat":    dict(t=(0, 0, 0), r=(0, 0, 0), s=(1, 1, 1)),
    "move":    dict(t=(1.5, 2.25, 3.125), r=(0, 0, 0), s=(1, 1, 1)),
    "turn":    dict(t=(0, 0, 0), r=(0, 0, 30), s=(1, 1, 1)),
    "mixed":   dict(t=(1.5, 2.25, 3.125), r=(11, 22, 33), s=(1, 1, 1)),
    "scaled":  dict(t=(1.5, 0, 0), r=(0, 0, 30), s=(1.5, 1.5, 1.5)),
}

DEPTH = 8


def chain(name, root, child):
    models = F.null_model("Root", root["t"], root["r"], root["s"])
    relations = '\tModel: "Model::Root", "Null" {\n\t}\n'
    connections = '\tConnect: "OO", "Model::Root", "Model::Scene"\n'
    previous = "Root"
    for level in range(1, DEPTH + 1):
        node = "L%d" % level
        models += F.null_model(node, child["t"], child["r"], child["s"])
        relations += '\tModel: "Model::%s", "Null" {\n\t}\n' % node
        connections += '\tConnect: "OO", "Model::%s", "Model::%s"\n' % (node, previous)
        previous = node
    models += F.mesh_model("Tip", [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [[0, 1, 2]])
    relations += '\tModel: "Model::Tip", "Mesh" {\n\t}\n'
    connections += '\tConnect: "OO", "Model::Tip", "Model::%s"\n' % previous
    header = {"count": DEPTH + 2, "models": DEPTH + 2, "materials": 0, "deformers": 0}
    if root["unit"] != 1:
        header["globals"] = '\tObjectType: "GlobalSettings" {\n\t\tCount: 1\n\t}\n'
        models = F.global_settings(root["unit"]) + models
        header["count"] += 1
    return header, models, relations, connections


COMMITTED = ("identity", "mixed", 3, "fbx_rotation_chain.fbx")
"""The one chain this repository keeps: three nodes under an identity root, each carrying the same
three-axis `Lcl Rotation` and translation and *no* `PreRotation`, which is the shape that has no
pivot residue and used to be given one (`XNASWEEP-213`)."""


def main(argv) -> int:
    out = argv[0] if argv else "build/xna-sample-sweep/probe-bonechain"
    sources = os.path.join(out, "src", "model")
    os.makedirs(sources, exist_ok=True)
    cases = []
    for root_name, root in ROOTS.items():
        for child_name, child in CHILDREN.items():
            name = "%s_%s" % (root_name, child_name)
            header, models, relations, connections = chain(name, root, child)
            F.write(os.path.join(sources, name + ".fbx"), header, models, relations, connections)
            cases.append({"case": "bonechain/" + name, "source": "model/%s.fbx" % name,
                          "importer": "FbxImporter", "processor": "ModelProcessor",
                          "platform": "Windows", "profile": "HiDef"})
    # The committed fixture, written where the tests look for it.
    global DEPTH
    root_name, child_name, depth, filename = COMMITTED
    keep, DEPTH = DEPTH, depth
    header, models, relations, connections = chain(filename, ROOTS[root_name], CHILDREN[child_name])
    DEPTH = keep
    committed = os.path.join(os.path.abspath(os.path.join(HERE, "..", "..", "..")),
                             "tests", "assets", "xna40", "model", filename)
    F.write(committed, header, models, relations, connections)
    print("committed fixture: %s" % committed)

    with open(os.path.join(out, "bonechain.json"), "w") as handle:
        json.dump({"format": "CNA.XnaDifferential.Corpus", "version": 1,
                   "comment": "plans/plan_xna_sample_xnb_sweep.md XNASWEEP-213 bone chains",
                   "cases": cases}, handle, indent=1)
    print("%d cases, %d sources" % (len(cases), len(os.listdir(sources))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
