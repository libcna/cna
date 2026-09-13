#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-176: parent/child pairs for the pivot rule.

Writes one ASCII FBX 6.1 file holding a flat list of parent/child (and optionally grandchild)
chains, each with its own pivot configuration, so a single import answers for all of them.

    python3 tools/xna-pipeline-oracle/model/make_pivot_probe.py <spec.json> <out.fbx>

The spec is a list of chains; each chain is a list of node property dictionaries, root first:

    [[{"lclRotation": [0,0,90], "preRotation": [-90,0,0], "rotationActive": 1},
      {"lclRotation": [0,0,-90]}]]
"""
from __future__ import annotations

import json
import os
import sys

HEADER = '''; FBX 6.1.0 project file
; Written by tools/xna-pipeline-oracle/model/make_pivot_probe.py for
; plans/plan_xna_sample_xnb_sweep.md XNASWEEP-176.
FBXHeaderExtension:  {
\tFBXHeaderVersion: 1003
\tFBXVersion: 6100
\tCreator: "CNA"
}
CreationTime: "2026-01-01 00:00:00:000"
Creator: "CNA"

'''

DEFAULTS = {
    "lclTranslation": [0, 0, 0],
    "lclRotation": [0, 0, 0],
    "lclScaling": [1, 1, 1],
    "rotationOrder": 0,
    "preRotation": [0, 0, 0],
    "postRotation": [0, 0, 0],
    "rotationActive": 0,
    "rotationPivot": [0, 0, 0],
    "scalingPivot": [0, 0, 0],
    "rotationOffset": [0, 0, 0],
    "scalingOffset": [0, 0, 0],
    "inheritType": 0,
}


def number(value):
    text = repr(float(value))
    return text[:-2] if text.endswith(".0") else text


def model(name, node):
    p = dict(DEFAULTS)
    p.update(node)
    lines = ['\tModel: "Model::%s", "Null" {' % name, "\t\tVersion: 232", "\t\tProperties60:  {"]
    def vector(label, kind, values):
        lines.append('\t\t\tProperty: "%s", "%s", "A+",%s,%s,%s'
                     % (label, kind, number(values[0]), number(values[1]), number(values[2])))
    def plain(label, kind, values):
        lines.append('\t\t\tProperty: "%s", "%s", "",%s,%s,%s'
                     % (label, kind, number(values[0]), number(values[1]), number(values[2])))
    vector("Lcl Translation", "Lcl Translation", p["lclTranslation"])
    vector("Lcl Rotation", "Lcl Rotation", p["lclRotation"])
    vector("Lcl Scaling", "Lcl Scaling", p["lclScaling"])
    lines.append('\t\t\tProperty: "RotationOrder", "enum", "",%d' % p["rotationOrder"])
    plain("PreRotation", "Vector3D", p["preRotation"])
    plain("PostRotation", "Vector3D", p["postRotation"])
    plain("RotationPivot", "Vector3D", p["rotationPivot"])
    plain("ScalingPivot", "Vector3D", p["scalingPivot"])
    plain("RotationOffset", "Vector3D", p["rotationOffset"])
    plain("ScalingOffset", "Vector3D", p["scalingOffset"])
    lines.append('\t\t\tProperty: "RotationActive", "bool", "",%d' % p["rotationActive"])
    lines.append('\t\t\tProperty: "InheritType", "enum", "",%d' % p["inheritType"])
    lines.append("\t\t}")
    lines += ["\t\tMultiLayer: 0", "\t\tMultiTake: 1", "\t\tShading: Y",
              '\t\tCulling: "CullingOff"', "\t}"]
    return "\n".join(lines)


def main() -> int:
    spec = json.load(open(sys.argv[1], encoding="utf-8"))
    out = sys.argv[2]
    names = []
    bodies = []
    connections = []
    for chain_at, chain in enumerate(spec):
        parent = "Scene"
        for depth, node in enumerate(chain):
            name = "N%03d_%d" % (chain_at, depth)
            names.append(name)
            bodies.append(model(name, node))
            connections.append('\tConnect: "OO", "Model::%s", "Model::%s"' % (name, parent))
            parent = name
    text = HEADER
    text += "Definitions:  {\n\tVersion: 100\n\tCount: %d\n\tObjectType: \"Model\" {\n\t\tCount: %d\n\t}\n}\n\n" % (
        len(names), len(names))
    text += "Objects:  {\n" + "\n".join(bodies) + "\n}\n\n"
    text += "Relations:  {\n" + "\n".join(
        '\tModel: "Model::%s", "Null" {\n\t}' % name for name in names) + "\n}\n\n"
    text += "Connections:  {\n" + "\n".join(connections) + "\n}\n\n"
    text += 'Takes:  {\n\tCurrent: ""\n}\n'
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", newline="\n", encoding="utf-8") as handle:
        handle.write(text)
    print("wrote %s (%d nodes)" % (out, len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
