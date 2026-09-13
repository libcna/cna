#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-160: what orders a scene's children.

SAMPLE-142's `Hammer.FBX` connects `Model::Root` and `Model:: Footsteps` to `Model::Scene` in that
order, declares them in that order and relates them in that order, and the genuine build answers
` Footsteps` first.  `XNASWEEP-154` established that a scene's order is its *connection* order, so
one of two things is different about this pair: the model subtype `"Root"`, which no other file in
the corpus uses at the top level, or the leading space in the other one's name.  These probes vary
exactly one of those at a time.

Run:  python3 tools/xna-pipeline-oracle/model/make_childorder_probes.py <output directory>
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import make_fbx_fixtures as fbx


def typed_model(name, kind, translation=(0, 0, 0)):
    """One model of an arbitrary FBX subtype: `Null`, `Root`, `LimbNode`, `Mesh`, ..."""
    return ('\tModel: "Model::%s", "%s" {\n\t\tVersion: 232\n%s'
            "\t\tMultiLayer: 0\n\t\tMultiTake: 1\n\t\tShading: Y\n\t\tCulling: \"CullingOff\"\n"
            "\t}\n" % (name, kind, fbx.properties(translation)))


def scene(path, children, connect_order=None):
    """A scene whose only content is `children`, a list of (name, subtype) pairs."""
    body = "".join(typed_model(name, kind, (i + 1, 0, 0))
                   for i, (name, kind) in enumerate(children))
    relations = "".join('\tModel: "Model::%s", "%s" {\n\t}\n' % (name, kind)
                        for name, kind in children)
    order = connect_order if connect_order is not None else range(len(children))
    connections = "".join('\tConnect: "OO", "Model::%s", "Model::Scene"\n' % children[i][0]
                          for i in order)
    fbx.write(path, {"count": len(children), "models": len(children), "materials": 0,
                     "deformers": 0}, body, relations, connections)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/xna-sample-sweep/childorder/fixtures"
    os.makedirs(out, exist_ok=True)
    p = lambda name: os.path.join(out, name + ".fbx")

    # The control: two plain nulls, which XNASWEEP-154 says come back in connection order.
    scene(p("co_null_null"), [("Alpha", "Null"), ("Bravo", "Null")])
    scene(p("co_null_null_rev"), [("Alpha", "Null"), ("Bravo", "Null")], [1, 0])

    # The subtype under test, both ways round in the connection list.
    scene(p("co_root_then_null"), [("Rooty", "Root"), ("Alpha", "Null")])
    scene(p("co_null_then_root"), [("Alpha", "Null"), ("Rooty", "Root")])
    # ... and with the file's own names, to rule the names in or out.
    scene(p("co_hammer_shape"), [("Root", "Root"), (" Footsteps", "Null")])
    scene(p("co_hammer_names_plain"), [("Root", "Null"), (" Footsteps", "Null")])

    # The leading space on its own, with no Root anywhere.
    scene(p("co_space_first"), [(" Alpha", "Null"), ("Bravo", "Null")])
    scene(p("co_space_second"), [("Bravo", "Null"), (" Alpha", "Null")])

    # Other subtypes, to say whether "Root" is special or every subtype sorts.
    for kind in ("LimbNode", "Mesh", "Marker", "Light"):
        scene(p("co_%s_then_null" % kind.lower()), [("Kind", kind), ("Alpha", "Null")])
        scene(p("co_null_then_%s" % kind.lower()), [("Alpha", "Null"), ("Kind", kind)])

    # Two Roots, and a Root between two nulls: where in the order does it land?
    scene(p("co_two_roots"), [("RootA", "Root"), ("RootB", "Root")])
    scene(p("co_null_root_null"), [("Alpha", "Null"), ("Rooty", "Root"), ("Charlie", "Null")])
    scene(p("co_root_null_null"), [("Rooty", "Root"), ("Alpha", "Null"), ("Charlie", "Null")])
    scene(p("co_null_null_root"), [("Alpha", "Null"), ("Charlie", "Null"), ("Rooty", "Root")])

    # Three nulls, to confirm the control at a length where a sort is visible.
    scene(p("co_three_nulls"), [("Charlie", "Null"), ("Alpha", "Null"), ("Bravo", "Null")])
    scene(p("co_three_nulls_rev"), [("Charlie", "Null"), ("Alpha", "Null"), ("Bravo", "Null")],
          [2, 1, 0])

    print("wrote %d probes to %s" % (len(fbx.RECORD), out))


if __name__ == "__main__":
    main()
