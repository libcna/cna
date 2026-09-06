#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xnapipeline_parity.md XNAPP-215, XNAPP-217: the FBX corpus.

Written in FBX 6.1 ASCII, by hand, for one measured reason: XNA's importer carries FBX SDK
2011.3.1, and every FBX a current tool writes is version 7400 or 7500, which that SDK refuses --
`Error code: 0 encountered when importing the scene`, recorded in the oracle for an Assimp-written
file. 6100 is a version it reads, and it is a text format, so authoring it here keeps the corpus
CNA's own content in a container CNA also wrote.

Run:  python3 tools/xna-pipeline-oracle/model/make_fbx_fixtures.py
"""
import os
import sys

HEADER = """; FBX 6.1.0 project file
; Written by tools/xna-pipeline-oracle/model/make_fbx_fixtures.py for this repository.
;----------------------------------------------------

FBXHeaderExtension:  {
\tFBXHeaderVersion: 1003
\tFBXVersion: 6100
\tCreationTimeStamp:  {
\t\tVersion: 1000
\t\tYear: 2026
\t\tMonth: 1
\t\tDay: 1
\t\tHour: 0
\t\tMinute: 0
\t\tSecond: 0
\t\tMillisecond: 0
\t}
\tCreator: "CNA"
}
CreationTime: "2026-01-01 00:00:00:000"
Creator: "CNA"

Definitions:  {
\tVersion: 100
\tCount: %(count)d
\tObjectType: "Model" {
\t\tCount: %(models)d
\t}
\tObjectType: "Material" {
\t\tCount: %(materials)d
\t}
\tObjectType: "Deformer" {
\t\tCount: %(deformers)d
\t}
}

Objects:  {
"""

FOOTER = """}

Relations:  {
%(relations)s}

Connections:  {
%(connections)s}

Takes:  {
\tCurrent: "%(take)s"
%(takes)s}

Version5:  {
\tAmbientRenderSettings:  {
\t\tVersion: 101
\t\tAmbientLightColor: 0,0,0,0
\t}
}
"""


def numbers(values):
    return ",".join(("%g" % v) for v in values)


def flatten(tuples):
    out = []
    for one in tuples:
        out.extend(one)
    return out


def properties(translation=(0, 0, 0), rotation=(0, 0, 0), scaling=(1, 1, 1)):
    return ("\t\tProperties60:  {\n"
            '\t\t\tProperty: "Lcl Translation", "Lcl Translation", "A+",%s\n'
            '\t\t\tProperty: "Lcl Rotation", "Lcl Rotation", "A+",%s\n'
            '\t\t\tProperty: "Lcl Scaling", "Lcl Scaling", "A+",%s\n'
            "\t\t}\n" % (numbers(translation), numbers(rotation), numbers(scaling)))


def null_model(name, translation=(0, 0, 0), rotation=(0, 0, 0), scaling=(1, 1, 1)):
    return ('\tModel: "Model::%s", "Null" {\n'
            "\t\tVersion: 232\n%s"
            "\t\tMultiLayer: 0\n\t\tMultiTake: 1\n\t\tShading: Y\n\t\tCulling: \"CullingOff\"\n"
            "\t}\n" % (name, properties(translation, rotation, scaling)))


def mesh_model(name, vertices, polygons, normals=None, uvs=None, colors=None,
               materials_per_polygon=None, translation=(0, 0, 0), scaling=(1, 1, 1)):
    text = '\tModel: "Model::%s", "Mesh" {\n\t\tVersion: 232\n' % name
    text += properties(translation, (0, 0, 0), scaling)
    text += "\t\tMultiLayer: 0\n\t\tMultiTake: 1\n\t\tShading: Y\n\t\tCulling: \"CullingOff\"\n"
    text += "\t\tVertices: %s\n" % numbers(flatten(vertices))
    indices = []
    for polygon in polygons:
        for i, index in enumerate(polygon):
            indices.append(index if i + 1 < len(polygon) else -index - 1)
    text += "\t\tPolygonVertexIndex: %s\n" % ",".join(str(i) for i in indices)
    text += "\t\tGeometryVersion: 124\n"
    layers = []
    if normals:
        text += ('\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
                 '\t\t\tMappingInformationType: "ByVertice"\n'
                 '\t\t\tReferenceInformationType: "Direct"\n'
                 "\t\t\tNormals: %s\n\t\t}\n" % numbers(flatten(normals)))
        layers.append("LayerElementNormal")
    if colors:
        text += ('\t\tLayerElementColor: 0 {\n\t\t\tVersion: 101\n\t\t\tName: "Col"\n'
                 '\t\t\tMappingInformationType: "ByVertice"\n'
                 '\t\t\tReferenceInformationType: "Direct"\n'
                 "\t\t\tColors: %s\n\t\t}\n" % numbers(flatten(colors)))
        layers.append("LayerElementColor")
    if uvs:
        text += ('\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: "UVChannel_1"\n'
                 '\t\t\tMappingInformationType: "ByVertice"\n'
                 '\t\t\tReferenceInformationType: "Direct"\n'
                 "\t\t\tUV: %s\n\t\t}\n" % numbers(flatten(uvs)))
        layers.append("LayerElementUV")
    if materials_per_polygon is not None:
        text += ('\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
                 '\t\t\tMappingInformationType: "ByPolygon"\n'
                 '\t\t\tReferenceInformationType: "IndexToDirect"\n'
                 "\t\t\tMaterials: %s\n\t\t}\n" % ",".join(str(m) for m in materials_per_polygon))
        layers.append("LayerElementMaterial")
    text += "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
    for layer in layers:
        text += ('\t\t\tLayerElement:  {\n\t\t\t\tType: "%s"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n' % layer)
    text += "\t\t}\n\t}\n"
    return text


def material(name, diffuse, specular, emissive, opacity, shininess):
    return ('\tMaterial: "Material::%s", "" {\n\t\tVersion: 102\n\t\tShadingModel: "phong"\n'
            "\t\tMultiLayer: 0\n\t\tProperties60:  {\n"
            '\t\t\tProperty: "ShadingModel", "KString", "", "phong"\n'
            '\t\t\tProperty: "DiffuseColor", "Color", "A+",%s\n'
            '\t\t\tProperty: "SpecularColor", "Color", "A+",%s\n'
            '\t\t\tProperty: "EmissiveColor", "Color", "A+",%s\n'
            '\t\t\tProperty: "Opacity", "double", "",%g\n'
            '\t\t\tProperty: "Shininess", "double", "",%g\n'
            "\t\t}\n\t}\n" % (name, numbers(diffuse), numbers(specular), numbers(emissive),
                              opacity, shininess))


RECORD = []


def write(path, header, body, relations, connections, take="", takes=""):
    text = HEADER % header + body + FOOTER % {"relations": relations, "connections": connections,
                                              "take": take, "takes": takes}
    with open(path, "w", newline="\n") as handle:
        handle.write(text)
    RECORD.append((os.path.basename(path), len(text)))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    out = os.path.join(repo, "tests/assets/xna40/model")
    os.makedirs(out, exist_ok=True)

    quad = [(-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)]
    two = [[0, 1, 2], [0, 2, 3]]

    # 1. A textured, coloured quad with normals: what a mesh's channels become.
    write(os.path.join(out, "fbx_quad_textured.fbx"),
          {"count": 2, "models": 1, "materials": 1, "deformers": 0},
          mesh_model("Quad", quad, two, [(0, 0, 1)] * 4, [(0, 1), (1, 1), (1, 0), (0, 0)],
                     [(1, 0, 0, 1), (0, 1, 0, 1), (0, 0, 1, 1), (1, 1, 1, 0.5)]) +
          material("Painted", (0.8, 0.7, 0.6), (0.1, 0.2, 0.3), (0.01, 0.02, 0.03), 1.0, 24.0),
          '\tModel: "Model::Quad", "Mesh" {\n\t}\n\tMaterial: "Material::Painted", "" {\n\t}\n',
          '\tConnect: "OO", "Model::Quad", "Model::Scene"\n'
          '\tConnect: "OO", "Material::Painted", "Model::Quad"\n')

    # 2. A hierarchy, so what a node's transform and its composition become is visible.
    write(os.path.join(out, "fbx_hierarchy.fbx"),
          {"count": 3, "models": 3, "materials": 0, "deformers": 0},
          null_model("World", (5, 0, 0)) + null_model("Child", (0, 3, 0), (0, 0, 0), (2, 2, 2)) +
          mesh_model("Tri", [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [[0, 1, 2]]),
          '\tModel: "Model::World", "Null" {\n\t}\n\tModel: "Model::Child", "Null" {\n\t}\n'
          '\tModel: "Model::Tri", "Mesh" {\n\t}\n',
          '\tConnect: "OO", "Model::World", "Model::Scene"\n'
          '\tConnect: "OO", "Model::Child", "Model::World"\n'
          '\tConnect: "OO", "Model::Tri", "Model::Child"\n')

    # 3. Oblique positions and normals: what, if anything, is done to a coordinate.
    write(os.path.join(out, "fbx_oblique.fbx"),
          {"count": 1, "models": 1, "materials": 0, "deformers": 0},
          mesh_model("Oblique", [(1, 2, 3), (4, 5, 6), (7, 8, 9)], [[0, 1, 2]],
                     [(0.6, 0, 0.8), (0, 0.6, 0.8), (0.267261, 0.534522, 0.801784)],
                     [(0.1, 0.2), (0.3, 0.4), (0.5, 0.6)]),
          '\tModel: "Model::Oblique", "Mesh" {\n\t}\n',
          '\tConnect: "OO", "Model::Oblique", "Model::Scene"\n')

    # 4. Two materials assigned per polygon: whether the mesh splits into two batches.
    write(os.path.join(out, "fbx_two_materials.fbx"),
          {"count": 3, "models": 1, "materials": 2, "deformers": 0},
          mesh_model("Split", [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)], two,
                     materials_per_polygon=[0, 1]) +
          material("Red", (1, 0, 0), (0, 0, 0), (0, 0, 0), 1.0, 1.0) +
          material("Blue", (0, 0, 1), (0, 0, 0), (0, 0, 0), 0.5, 2.0),
          '\tModel: "Model::Split", "Mesh" {\n\t}\n\tMaterial: "Material::Red", "" {\n\t}\n'
          '\tMaterial: "Material::Blue", "" {\n\t}\n',
          '\tConnect: "OO", "Model::Split", "Model::Scene"\n'
          '\tConnect: "OO", "Material::Red", "Model::Split"\n'
          '\tConnect: "OO", "Material::Blue", "Model::Split"\n')

    # 5. A mesh with no normals at all, so whether the importer generates them is visible.
    write(os.path.join(out, "fbx_bare_mesh.fbx"),
          {"count": 1, "models": 1, "materials": 0, "deformers": 0},
          mesh_model("Loose", [(0, 0, 0), (2, 0, 0), (0, 2, 0)], [[0, 1, 2]]),
          '\tModel: "Model::Loose", "Mesh" {\n\t}\n',
          '\tConnect: "OO", "Model::Loose", "Model::Scene"\n')

    # 6. A quad polygon rather than two triangles: how an n-gon is triangulated.
    write(os.path.join(out, "fbx_quad_polygon.fbx"),
          {"count": 1, "models": 1, "materials": 0, "deformers": 0},
          mesh_model("OneQuad", quad, [[0, 1, 2, 3]]),
          '\tModel: "Model::OneQuad", "Mesh" {\n\t}\n',
          '\tConnect: "OO", "Model::OneQuad", "Model::Scene"\n')

    # The refusal corpus.
    with open(os.path.join(out, "fbx_empty.fbx"), "wb") as handle:
        handle.write(b"")
    RECORD.append(("fbx_empty.fbx", 0))
    with open(os.path.join(out, "fbx_not_fbx.fbx"), "wb") as handle:
        handle.write(b"this is not an FBX file at all\n")
    RECORD.append(("fbx_not_fbx.fbx", 31))
    # The same nonsense at a size a loader can probe: the two messages XNA gives for a file it
    # cannot read differ, and whether the difference is the content or the size is only visible
    # with both.
    with open(os.path.join(out, "fbx_not_fbx_large.fbx"), "wb") as handle:
        handle.write(b"this is not an FBX file at all, and it is long enough to probe.\n" * 16)
    RECORD.append(("fbx_not_fbx_large.fbx", 64 * 16))
    with open(os.path.join(out, "fbx_quad_textured.fbx"), "rb") as handle:
        whole = handle.read()
    with open(os.path.join(out, "fbx_truncated.fbx"), "wb") as handle:
        handle.write(whole[:400])
    RECORD.append(("fbx_truncated.fbx", 400))

    with open(os.path.join(out, "FBX-PROVENANCE.md"), "w", encoding="utf-8") as handle:
        handle.write("# FBX corpus (authored)\n\n")
        handle.write("Written by `tools/xna-pipeline-oracle/model/make_fbx_fixtures.py`, in FBX 6.1\n")
        handle.write("ASCII. That version is chosen for a measured reason: XNA's importer carries FBX\n")
        handle.write("SDK 2011.3.1, and an FBX written by a current tool is version 7400 or 7500, which\n")
        handle.write("that SDK refuses -- `Error code: 0 encountered when importing the scene`, recorded\n")
        handle.write("in the oracle for an Assimp-written file. Nothing here was downloaded and nothing\n")
        handle.write("is third-party content.\n\n")
        handle.write("| File | Bytes |\n|---|---:|\n")
        for name, size in sorted(RECORD):
            handle.write("| `%s` | %d |\n" % (name, size))
    print("make_fbx_fixtures: wrote %d files to %s" % (len(RECORD), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
