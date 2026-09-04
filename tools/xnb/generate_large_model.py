#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Generates a deterministic large glTF/GLB fixture for the Model-pipeline performance pass.

plans/plan_xnapipeline.md XNAP-93. The one measurement that row was missing was large models: the
committed corpus is small-model dominated -- it exists to cover *shapes*, not sizes -- and no
representative large source was available. Downloading one would be neither reproducible nor
licensable, so this authors one instead.

It stresses the things a Model build actually spends time on, rather than padding bytes:

  * **vertex and index counts**, which drive accessor decoding and buffer copying;
  * **mesh and primitive counts**, which drive the shared-resource table and the type table -- a
    part is three shared-resource references, and the writer interns each one;
  * **material count**, which drives effect interning;
  * **hierarchy depth**, which drives bone-table construction and the parent/child fixups.

Everything is a pure function of the requested scale: no clock, no randomness beyond a fixed
sequence, so two runs produce byte-identical files and a measurement is reproducible.

Usage:
    PYTHONPATH=tools python3 tools/xnb/generate_large_model.py --scale medium --out model.glb
    PYTHONPATH=tools python3 tools/xnb/generate_large_model.py --list

The fixtures are **not committed**: they are megabytes of derivable data, and the corpus manifest
gate (XNAP-59) covers the committed tree. Generate them where you need them.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gltf_fixtures.builder import FLOAT, UNSIGNED_INT, UNSIGNED_SHORT, GltfBuilder  # noqa: E402

#: Named scales, each roughly an order of magnitude apart in vertex count, so a superlinear cost
#: is visible rather than inferred from one point.
SCALES = {
    "small":  {"meshes": 4,   "partsPerMesh": 2, "gridSize": 12, "materials": 2,  "depth": 3},
    "medium": {"meshes": 24,  "partsPerMesh": 4, "gridSize": 24, "materials": 8,  "depth": 6},
    "large":  {"meshes": 96,  "partsPerMesh": 6, "gridSize": 40, "materials": 24, "depth": 10},
}


def grid_primitive(builder: GltfBuilder, size: int, offset: float, material: int) -> dict:
    """Builds one indexed triangle grid: (size+1)^2 vertices, size^2 * 2 triangles.

    A grid rather than a random point cloud because the importer's own validation walks indices
    and positions, and degenerate geometry would be rejected before it was measured.
    """
    positions = []
    normals = []
    texcoords = []
    for row in range(size + 1):
        for column in range(size + 1):
            u = column / size
            v = row / size
            # A deterministic surface, so the bounds are non-trivial and the bytes are not all
            # the same value (which would flatter any compression measured over them).
            height = 0.25 * math.sin(u * 6.283185307) * math.cos(v * 6.283185307)
            positions.append((u * 2.0 - 1.0 + offset, height, v * 2.0 - 1.0))
            normals.append((0.0, 1.0, 0.0))
            texcoords.append((u, v))

    indices = []
    for row in range(size):
        for column in range(size):
            top = row * (size + 1) + column
            bottom = top + size + 1
            indices.extend([top, bottom, top + 1, top + 1, bottom, bottom + 1])

    component = UNSIGNED_SHORT if len(positions) <= 0xFFFF else UNSIGNED_INT
    return {
        "attributes": {
            "POSITION": builder.add_packed_accessor(
                usage="position", values=positions, accessor_type="VEC3", with_bounds=True),
            "NORMAL": builder.add_packed_accessor(
                usage="normal", values=normals, accessor_type="VEC3"),
            "TEXCOORD_0": builder.add_packed_accessor(
                usage="texcoord", values=texcoords, accessor_type="VEC2"),
        },
        "indices": builder.add_packed_accessor(
            usage="indices", values=indices, accessor_type="SCALAR", component_type=component),
        "material": material,
        "mode": 4,
    }


def describe(scale: str) -> dict:
    """Returns the shape @p scale describes, without building it.

    Derived arithmetic rather than a measurement, so `--list` is instant: building the largest
    scale to report its vertex count would make listing cost ten seconds.
    """
    if scale not in SCALES:
        raise SystemExit(f"unknown scale '{scale}'; choose one of {', '.join(SCALES)}")
    shape = SCALES[scale]
    parts = shape["meshes"] * shape["partsPerMesh"]
    return {
        "scale": scale,
        "meshes": shape["meshes"],
        "parts": parts,
        "materials": shape["materials"],
        "nodes": shape["meshes"] * (shape["depth"] + 1),
        "vertices": parts * (shape["gridSize"] + 1) ** 2,
        "triangles": parts * shape["gridSize"] ** 2 * 2,
    }


def build(scale: str) -> tuple[bytes, dict]:
    """Builds the GLB for @p scale, returning its bytes and the shape it describes."""
    shape = SCALES[describe(scale)["scale"]]
    builder = GltfBuilder(f"cna-large-model-{scale}")

    materials = [
        builder.add_material({
            "name": f"Material{index}",
            "pbrMetallicRoughness": {
                "baseColorFactor": [
                    (index % 5) / 4.0, (index % 3) / 2.0, (index % 7) / 6.0, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.5 + (index % 2) * 0.25,
            },
        })
        for index in range(shape["materials"])
    ]

    meshes = []
    for meshIndex in range(shape["meshes"]):
        primitives = [
            grid_primitive(builder, shape["gridSize"], float(part),
                           materials[(meshIndex * shape["partsPerMesh"] + part) % len(materials)])
            for part in range(shape["partsPerMesh"])
        ]
        meshes.append(builder.add_mesh(primitives, name=f"Mesh{meshIndex:03d}"))

    # A chain of transform nodes above each mesh, so the bone table is deep rather than flat: an
    # XNA Model's bone hierarchy is where a Model build's fixups happen.
    roots = []
    for meshIndex, mesh in enumerate(meshes):
        node = builder.add_node(name=f"Mesh{meshIndex:03d}Node", mesh=mesh,
                                translation=[float(meshIndex % 8) * 3.0, 0.0,
                                             float(meshIndex // 8) * 3.0])
        for level in range(shape["depth"]):
            node = builder.add_node(
                name=f"Mesh{meshIndex:03d}Level{level}", children=[node],
                rotation=[0.0, 0.0, 0.0, 1.0],
                scale=[1.0, 1.0, 1.0])
        roots.append(node)

    builder.set_default_scene(builder.add_scene(roots, name="Scene"))

    return builder.to_glb_bytes(), describe(scale)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scale", default="medium", choices=sorted(SCALES))
    parser.add_argument("--out", type=Path, help="where to write the .glb")
    parser.add_argument("--list", action="store_true", help="print each scale's shape and exit")
    arguments = parser.parse_args(argv[1:])

    if arguments.list:
        for name in SCALES:
            shape = describe(name)
            print(f"{name:8s} {shape['vertices']:>9d} vertices  {shape['triangles']:>9d} triangles"
                  f"  {shape['parts']:>5d} parts  {shape['nodes']:>5d} nodes"
                  f"  {shape['materials']:>4d} materials")
        return 0

    if arguments.out is None:
        print("--out is required unless --list is given.", file=sys.stderr)
        return 2
    data, shape = build(arguments.scale)
    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    arguments.out.write_bytes(data)
    print(f"{arguments.out}: {len(data)} bytes, {shape['vertices']} vertices, "
          f"{shape['triangles']} triangles, {shape['parts']} parts, {shape['nodes']} nodes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
