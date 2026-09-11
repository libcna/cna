#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-228: what FBX's `InheritType` does to a child.

Three scenes that differ in one property. A root scaled (2, 3, 5) carries a child that turns 37
degrees about Z and scales (1.5, 0.5, 2.5), and that child carries a grandchild that turns
(11, 22, 33) -- so the parent's scaling is non-uniform, the child's rotation is in the plane the
scaling stretches, and the grandchild sees whatever the child's world became.

  `fbx_inherit_rrss.fbx`  `InheritType 0`, `eInheritRrSs`, which is also what a file naming none
                          gets: the parent's scaling is applied after the child's own rotation and
                          the answer is the conjugate `S . R . S^-1`.
  `fbx_inherit_rsrs.fbx`  `InheritType 1`, `eInheritRSrs`: the scaling belongs to the parent's
                          world and the child's rotation comes back untouched.
  `fbx_inherit_rrs.fbx`   `InheritType 2`, `eInheritRrs`: the child does not inherit it at all.

    python3 tools/xna-pipeline-oracle/model/make_inherit_probes.py [outdir]
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_fbx_fixtures as F                                            # noqa: E402


def properties(translation, rotation, scaling, inherit):
    text = ("\t\tProperties60:  {\n"
            '\t\t\tProperty: "Lcl Translation", "Lcl Translation", "A+",%s\n'
            '\t\t\tProperty: "Lcl Rotation", "Lcl Rotation", "A+",%s\n'
            '\t\t\tProperty: "Lcl Scaling", "Lcl Scaling", "A+",%s\n'
            % (F.numbers(translation), F.numbers(rotation), F.numbers(scaling)))
    if inherit is not None:
        text += '\t\t\tProperty: "InheritType", "enum", "",%d\n' % inherit
    return text + "\t\t}\n"


def null_model(name, translation, rotation, scaling, inherit):
    return ('\tModel: "Model::%s", "Null" {\n\t\tVersion: 232\n%s'
            "\t\tMultiLayer: 0\n\t\tMultiTake: 1\n\t\tShading: Y\n\t\tCulling: \"CullingOff\"\n"
            "\t}\n" % (name, properties(translation, rotation, scaling, inherit)))


def write(path, inherit):
    models = (null_model("Root", (0, 0, 0), (0, 0, 0), (2, 3, 5), inherit) +
              null_model("Mid", (1.5, 2.25, 3.125), (0, 0, 37), (1.5, 0.5, 2.5), inherit) +
              null_model("Tip", (0.5, 0.25, 0.125), (11, 22, 33), (1, 1, 1), inherit) +
              F.mesh_model("Geo", [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [[0, 1, 2]]))
    relations = "".join('\tModel: "Model::%s", "%s" {\n\t}\n' % (name, kind)
                        for name, kind in (("Root", "Null"), ("Mid", "Null"),
                                           ("Tip", "Null"), ("Geo", "Mesh")))
    connections = ('\tConnect: "OO", "Model::Root", "Model::Scene"\n'
                   '\tConnect: "OO", "Model::Mid", "Model::Root"\n'
                   '\tConnect: "OO", "Model::Tip", "Model::Mid"\n'
                   '\tConnect: "OO", "Model::Geo", "Model::Tip"\n')
    F.write(path, {"count": 4, "models": 4, "materials": 0, "deformers": 0},
            models, relations, connections)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.abspath(os.path.join(HERE, "..", "..", "..")), "tests/assets/xna40/model")
    os.makedirs(out, exist_ok=True)
    for name, inherit in (("fbx_inherit_rrss.fbx", 0), ("fbx_inherit_rsrs.fbx", 1),
                          ("fbx_inherit_rrs.fbx", 2)):
        write(os.path.join(out, name), inherit)
    print("wrote 3 files to %s" % out)


if __name__ == "__main__":
    main()
