#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-166: why `Yager.FBX`'s `Box01` gets two texture
coordinates.

`Box01` is the one mesh in that file whose vertex declaration XNA writes at stride 40, with
`TextureCoordinate` usage index 0 **and** 1, and the genuine importer answers it with a second
texture-coordinate channel carrying the same values as the first. It is also the one mesh in the
file with **two** textures connected to it, and the one that declares `LayerElementTransparentUV`
beside `LayerElementUV` and `LayerElementTransparentTextures` beside `LayerElementTexture`.

Three earlier fixtures reproduced the `TransparentUV` declaration alone and the genuine importer
answered a single `TextureCoordinate0`, so that element is not the trigger. None of them had a
*texture*. These probes vary the pieces one at a time -- how many textures are connected, which
layer elements name them, and whether a second UV set is declared -- against the same quad, so the
genuine importer's answer says which one makes the second channel.

    python3 tools/xna-pipeline-oracle/model/make_uv2_probes.py <directory>
"""
from __future__ import annotations

import os
import sys

HEADER = '''; FBX 6.1.0 project file
; Written by tools/xna-pipeline-oracle/model/make_uv2_probes.py for this repository.
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
\t\tCount: 1
\t}
\tObjectType: "Material" {
\t\tCount: 1
\t}
\tObjectType: "Texture" {
\t\tCount: %(textures)d
\t}
\tObjectType: "Video" {
\t\tCount: %(textures)d
\t}
}

Objects:  {
'''

FOOTER = '''}

Relations:  {
%(relations)s}

Connections:  {
%(connections)s}

Version5:  {
\tAmbientRenderSettings:  {
\t\tVersion: 101
\t\tAmbientLightColor: 0.0,0.0,0.0,0
\t}
}
'''

MATERIAL = '''\tMaterial: "Material::Probe", "" {
\t\tVersion: 102
\t\tShadingModel: "phong"
\t\tMultiLayer: 0
\t\tProperties60:  {
\t\t\tProperty: "ShadingModel", "KString", "", "phong"
\t\t\tProperty: "DiffuseColor", "Color", "A+",0.8,0.8,0.8
\t\t\tProperty: "SpecularColor", "Color", "A+",0.2,0.2,0.2
\t\t\tProperty: "EmissiveColor", "Color", "A+",0,0,0
\t\t\tProperty: "Opacity", "double", "",1
\t\t\tProperty: "Shininess", "double", "",8
\t\t}
\t}
'''


def texture(name, filename):
    return ('\tTexture: "Texture::%s", "TextureVideoClip" {\n'
            '\t\tType: "TextureVideoClip"\n'
            "\t\tVersion: 202\n"
            '\t\tTextureName: "Texture::%s"\n'
            "\t\tProperties60:  {\n"
            '\t\t\tProperty: "Translation", "Vector", "A+",0,0,0\n'
            '\t\t\tProperty: "Rotation", "Vector", "A+",0,0,0\n'
            '\t\t\tProperty: "Scaling", "Vector", "A+",1,1,1\n'
            '\t\t\tProperty: "Texture alpha", "Number", "A+",1\n'
            '\t\t\tProperty: "TextureTypeUse", "enum", "",0\n'
            '\t\t\tProperty: "CurrentTextureBlendMode", "enum", "",1\n'
            '\t\t\tProperty: "UseMaterial", "bool", "",1\n'
            '\t\t\tProperty: "UseMipMap", "bool", "",0\n'
            '\t\t\tProperty: "CurrentMappingType", "enum", "",0\n'
            '\t\t\tProperty: "UVSwap", "bool", "",0\n'
            '\t\t\tProperty: "WrapModeU", "enum", "",0\n'
            '\t\t\tProperty: "WrapModeV", "enum", "",0\n'
            '\t\t\tProperty: "TextureRotationPivot", "Vector3D", "",0,0,0\n'
            '\t\t\tProperty: "TextureScalingPivot", "Vector3D", "",0,0,0\n'
            '\t\t\tProperty: "VideoProperty", "object", ""\n'
            "\t\t}\n"
            '\t\tMedia: "Video::%s"\n'
            '\t\tFileName: "%s"\n'
            '\t\tRelativeFilename: "%s"\n'
            "\t\tModelUVTranslation: 0,0\n"
            "\t\tModelUVScaling: 1,1\n"
            '\t\tTexture_Alpha_Source: "None"\n'
            "\t\tCropping: 0,0,0,0\n"
            "\t}\n" % (name, name, name, filename, filename))


def video(name, filename):
    return ('\tVideo: "Video::%s", "Clip" {\n'
            "\t\tType: \"Clip\"\n"
            "\t\tProperties60:  {\n"
            '\t\t\tProperty: "Path", "charptr", "", "%s"\n'
            "\t\t}\n"
            "\t\tUseMipMap: 0\n"
            '\t\tFilename: "%s"\n'
            '\t\tRelativeFilename: "%s"\n'
            "\t}\n" % (name, filename, filename, filename))


UV_A = "0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8"
UV_B = "0.11,0.22,0.33,0.44,0.55,0.66,0.77,0.88"


def uv_element(element, typed_index, channel, values):
    return ('\t\t%s: %d {\n\t\t\tVersion: 101\n\t\t\tName: "%s"\n'
            '\t\t\tMappingInformationType: "ByPolygonVertex"\n'
            '\t\t\tReferenceInformationType: "IndexToDirect"\n'
            "\t\t\tUV: %s\n\t\t\tUVIndex: 0,1,2,0,2,3\n\t\t}\n"
            % (element, typed_index, channel, values))


def texture_element(element, typed_index, texture_id):
    return ('\t\t%s: %d {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
            '\t\t\tMappingInformationType: "AllSame"\n'
            '\t\t\tReferenceInformationType: "IndexToDirect"\n'
            '\t\t\tBlendMode: "Translucent"\n\t\t\tTextureAlpha: 1\n'
            "\t\t\tTextureId: %d\n\t\t}\n" % (element, typed_index, texture_id))


def free_mesh(name, elements, layers, material=True):
    """`elements` is a list of already-rendered layer-element blocks; `layers` is
    {layerNumber: [(type, typedIndex), ...]}."""
    text = ('\tModel: "Model::%s", "Mesh" {\n\t\tVersion: 232\n'
            "\t\tProperties60:  {\n"
            '\t\t\tProperty: "Lcl Translation", "Lcl Translation", "A+",0,0,0\n'
            '\t\t\tProperty: "Lcl Rotation", "Lcl Rotation", "A+",0,0,0\n'
            '\t\t\tProperty: "Lcl Scaling", "Lcl Scaling", "A+",1,1,1\n'
            "\t\t}\n"
            "\t\tMultiLayer: 0\n\t\tMultiTake: 1\n\t\tShading: Y\n"
            '\t\tCulling: "CullingOff"\n'
            "\t\tVertices: 0,0,0,1,0,0,1,1,0,0,1,0\n"
            "\t\tPolygonVertexIndex: 0,1,-3,0,2,-4\n"
            "\t\tGeometryVersion: 124\n" % name)
    text += ('\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
             '\t\t\tMappingInformationType: "ByVertice"\n'
             '\t\t\tReferenceInformationType: "Direct"\n'
             "\t\t\tNormals: 0,0,1,0,0,1,0,0,1,0,0,1\n\t\t}\n")
    if material:
        text += ('\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
                 '\t\t\tMappingInformationType: "AllSame"\n'
                 '\t\t\tReferenceInformationType: "IndexToDirect"\n'
                 "\t\t\tMaterials: 0\n\t\t}\n")
    text += "".join(elements)
    for number in sorted(layers):
        text += "\t\tLayer: %d {\n\t\t\tVersion: 100\n" % number
        for entry in layers[number]:
            text += ('\t\t\tLayerElement:  {\n\t\t\t\tType: "%s"\n\t\t\t\tTypedIndex: %d\n\t\t\t}\n'
                     % entry)
        text += "\t\t}\n"
    text += "\t}\n"
    return text


def build_free(name, elements, layers, texture_names, material=True):
    body = free_mesh(name, elements, layers, material)
    if material:
        body += MATERIAL
    for index, texture_name in enumerate(texture_names):
        body += texture(texture_name, "probe_%d.dds" % index)
    for index, texture_name in enumerate(texture_names):
        body += video(texture_name, "probe_%d.dds" % index)
    relations = '\tModel: "Model::%s", "Mesh" {\n\t}\n' % name
    if material:
        relations += '\tMaterial: "Material::Probe", "" {\n\t}\n'
    for texture_name in texture_names:
        relations += '\tTexture: "Texture::%s", "TextureVideoClip" {\n\t}\n' % texture_name
    for texture_name in texture_names:
        relations += '\tVideo: "Video::%s", "Clip" {\n\t}\n' % texture_name
    connections = '\tConnect: "OO", "Model::%s", "Model::Scene"\n' % name
    if material:
        connections += '\tConnect: "OO", "Material::Probe", "Model::%s"\n' % name
    for texture_name in texture_names:
        connections += '\tConnect: "OO", "Texture::%s", "Model::%s"\n' % (texture_name, name)
    for texture_name in texture_names:
        connections += ('\tConnect: "OO", "Video::%s", "Texture::%s"\n'
                        % (texture_name, texture_name))
    header = {"count": 2 + 2 * len(texture_names), "textures": len(texture_names)}
    return HEADER % header + body + FOOTER % {"relations": relations, "connections": connections}


def mesh(name, uv_sets, textures, transparent_textures, transparent_uv, layer_order):
    """`uv_sets` is a list of (typedIndex, channelName, values); `textures` and
    `transparent_textures` are the `TextureId` each layer element names, or None."""
    text = ('\tModel: "Model::%s", "Mesh" {\n\t\tVersion: 232\n'
            "\t\tProperties60:  {\n"
            '\t\t\tProperty: "Lcl Translation", "Lcl Translation", "A+",0,0,0\n'
            '\t\t\tProperty: "Lcl Rotation", "Lcl Rotation", "A+",0,0,0\n'
            '\t\t\tProperty: "Lcl Scaling", "Lcl Scaling", "A+",1,1,1\n'
            "\t\t}\n"
            "\t\tMultiLayer: 0\n\t\tMultiTake: 1\n\t\tShading: Y\n"
            '\t\tCulling: "CullingOff"\n'
            "\t\tVertices: 0,0,0,1,0,0,1,1,0,0,1,0\n"
            "\t\tPolygonVertexIndex: 0,1,-3,0,2,-4\n"
            "\t\tGeometryVersion: 124\n" % name)
    text += ('\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
             '\t\t\tMappingInformationType: "ByVertice"\n'
             '\t\t\tReferenceInformationType: "Direct"\n'
             "\t\t\tNormals: 0,0,1,0,0,1,0,0,1,0,0,1\n\t\t}\n")
    for typed_index, channel, values in uv_sets:
        text += ('\t\tLayerElementUV: %d {\n\t\t\tVersion: 101\n\t\t\tName: "%s"\n'
                 '\t\t\tMappingInformationType: "ByPolygonVertex"\n'
                 '\t\t\tReferenceInformationType: "IndexToDirect"\n'
                 "\t\t\tUV: %s\n\t\t\tUVIndex: 0,1,2,0,2,3\n\t\t}\n"
                 % (typed_index, channel, values))
    if transparent_uv is not None:
        text += ('\t\tLayerElementTransparentUV: 0 {\n\t\t\tVersion: 101\n'
                 '\t\t\tName: "UVChannel_TRANSPARENT0"\n'
                 '\t\t\tMappingInformationType: "ByPolygonVertex"\n'
                 '\t\t\tReferenceInformationType: "IndexToDirect"\n'
                 "\t\t\tUV: %s\n\t\t\tUVIndex: 0,1,2,0,2,3\n\t\t}\n" % transparent_uv)
    text += ('\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
             '\t\t\tMappingInformationType: "AllSame"\n'
             '\t\t\tReferenceInformationType: "IndexToDirect"\n'
             "\t\t\tMaterials: 0\n\t\t}\n")
    if textures is not None:
        text += ('\t\tLayerElementTexture: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
                 '\t\t\tMappingInformationType: "AllSame"\n'
                 '\t\t\tReferenceInformationType: "IndexToDirect"\n'
                 '\t\t\tBlendMode: "Translucent"\n\t\t\tTextureAlpha: 1\n'
                 "\t\t\tTextureId: %d\n\t\t}\n" % textures)
    if transparent_textures is not None:
        text += ('\t\tLayerElementTransparentTextures: 0 {\n\t\t\tVersion: 101\n\t\t\tName: ""\n'
                 '\t\t\tMappingInformationType: "AllSame"\n'
                 '\t\t\tReferenceInformationType: "IndexToDirect"\n'
                 '\t\t\tBlendMode: "Translucent"\n\t\t\tTextureAlpha: 1\n'
                 "\t\t\tTextureId: %d\n\t\t}\n" % transparent_textures)
    text += "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
    for entry in layer_order:
        text += ('\t\t\tLayerElement:  {\n\t\t\t\tType: "%s"\n\t\t\t\tTypedIndex: %d\n\t\t\t}\n'
                 % entry)
    text += "\t\t}\n\t}\n"
    return text


YAGER_LAYER = [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
               ("LayerElementTexture", 0), ("LayerElementTransparentTextures", 0),
               ("LayerElementUV", 0), ("LayerElementTransparentUV", 0),
               ("LayerElementSmoothing", 0)]


def build(name, uv_sets, textures, transparent_textures, transparent_uv, layer_order,
          texture_names):
    body = mesh(name, uv_sets, textures, transparent_textures, transparent_uv, layer_order)
    body += MATERIAL
    for index, texture_name in enumerate(texture_names):
        body += texture(texture_name, "probe_%d.dds" % index)
    for index, texture_name in enumerate(texture_names):
        body += video(texture_name, "probe_%d.dds" % index)
    relations = '\tModel: "Model::%s", "Mesh" {\n\t}\n' % name
    relations += '\tMaterial: "Material::Probe", "" {\n\t}\n'
    for texture_name in texture_names:
        relations += '\tTexture: "Texture::%s", "TextureVideoClip" {\n\t}\n' % texture_name
    for texture_name in texture_names:
        relations += '\tVideo: "Video::%s", "Clip" {\n\t}\n' % texture_name
    connections = '\tConnect: "OO", "Model::%s", "Model::Scene"\n' % name
    connections += '\tConnect: "OO", "Material::Probe", "Model::%s"\n' % name
    for texture_name in texture_names:
        connections += '\tConnect: "OO", "Texture::%s", "Model::%s"\n' % (texture_name, name)
    for texture_name in texture_names:
        connections += ('\tConnect: "OO", "Video::%s", "Texture::%s"\n'
                        % (texture_name, texture_name))
    header = {"count": 2 + 2 * len(texture_names), "textures": len(texture_names)}
    return HEADER % header + body + FOOTER % {"relations": relations, "connections": connections}


ONE_UV = [(0, "UVChannel_DIFFUSE0", UV_A)]
TWO_UV = [(0, "UVChannel_DIFFUSE0", UV_A), (1, "UVChannel_DIFFUSE1", UV_B)]

PLAIN_LAYER = [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
               ("LayerElementTexture", 0), ("LayerElementUV", 0)]

PROBES = [
    # name, uv sets, LayerElementTexture id, LayerElementTransparentTextures id,
    # TransparentUV values, Layer order, textures connected to the model
    ("uv2_one_texture", ONE_UV, 0, None, None, PLAIN_LAYER, ["MapA"]),
    ("uv2_two_textures", ONE_UV, 0, None, None, PLAIN_LAYER, ["MapA", "MapB"]),
    ("uv2_two_textures_transp", ONE_UV, 0, 1, None, YAGER_LAYER, ["MapA", "MapB"]),
    ("uv2_two_textures_transp_uv", ONE_UV, 0, 1, UV_B, YAGER_LAYER, ["MapA", "MapB"]),
    ("uv2_one_texture_transp0", ONE_UV, 0, 0, None, YAGER_LAYER, ["MapA"]),
    ("uv2_one_texture_transp_uv", ONE_UV, 0, 0, UV_B, YAGER_LAYER, ["MapA"]),
    ("uv2_two_textures_no_layer", ONE_UV, None, None, None,
     [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementUV", 0)],
     ["MapA", "MapB"]),
    ("uv2_two_textures_two_uv", TWO_UV, 0, 1, None,
     YAGER_LAYER[:4] + [("LayerElementUV", 0), ("LayerElementUV", 1)], ["MapA", "MapB"]),
    ("uv2_three_textures", ONE_UV, 0, 1, None, YAGER_LAYER, ["MapA", "MapB", "MapC"]),
    ("uv2_two_textures_transp_id0", ONE_UV, 0, 0, None, YAGER_LAYER, ["MapA", "MapB"]),
]


UV_C = "0.111,0.222,0.333,0.444,0.555,0.666,0.777,0.888"

# Batch two: which of the three pieces -- the `TransparentUV` element, the
# `TransparentTextures` element that gives the material a Transparency texture, and the texture
# itself -- the second channel actually needs, and whether the other UV element types behave the
# same way.
FREE_PROBES = [
    # a Transparency UV set with no Transparency *texture*
    ("uv2b_transpuv_diffuse_only",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
    # a Transparency UV set with no texture connected at all
    ("uv2b_transpuv_no_texture",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0)]},
     [], True),
    # the same, with no material either
    ("uv2b_transpuv_no_material",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementTransparentTextures", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementTexture", 0),
          ("LayerElementTransparentTextures", 0), ("LayerElementUV", 0),
          ("LayerElementTransparentUV", 0)]},
     ["MapA"], False),
    # declared but not named by the Layer block
    ("uv2b_transpuv_not_in_layer",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementTransparentTextures", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementTransparentTextures", 0), ("LayerElementUV", 0)]},
     ["MapA"], True),
    # the reflection pair, which CNA already reads, for the same treatment
    ("uv2b_reflectionuv",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementReflectionUV", 0, "UVChannel_REFLECTION0", UV_B),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementReflectionTextures", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementReflectionTextures", 0), ("LayerElementUV", 0),
          ("LayerElementReflectionUV", 0)]},
     ["MapA"], True),
    # three UV element types at once
    ("uv2b_three_uv_types",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      uv_element("LayerElementReflectionUV", 0, "UVChannel_REFLECTION0", UV_C),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementTransparentTextures", 0, 0),
      texture_element("LayerElementReflectionTextures", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementTransparentTextures", 0), ("LayerElementReflectionTextures", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0),
          ("LayerElementReflectionUV", 0)]},
     ["MapA"], True),
    # the Transparency pair alone, with no diffuse texture element
    ("uv2b_transpuv_only_transptex",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTransparentTextures", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementTransparentTextures", 0), ("LayerElementUV", 0),
          ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
    # the Transparency UV set in its own Layer block
    ("uv2b_transpuv_layer1",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementTransparentTextures", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementUV", 0)],
      1: [("LayerElementTransparentTextures", 0), ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
    # a Transparency UV set only, with its texture, and no diffuse UV set at all
    ("uv2b_transpuv_alone",
     [uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementTransparentTextures", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementTransparentTextures", 0), ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
]


# Batch three: `uv2b_transpuv_no_texture` came back with one channel where the same file with a
# texture came back with two, so the Layer block naming a UV set is necessary but not sufficient.
# These say what the other half of the condition is.
FREE_PROBES += [
    # a Layer that names both UV sets, one texture connected, and no texture layer element at all
    ("uv2c_tex_no_layerelem",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
    # the reflection pair with no texture, which is the control for the reading CNA already has
    ("uv2c_reflection_no_texture",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementReflectionUV", 0, "UVChannel_REFLECTION0", UV_B)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementUV", 0), ("LayerElementReflectionUV", 0)]},
     [], True),
    # a Transparency UV set alone with no texture at all
    ("uv2c_transpuv_alone_no_texture",
     [uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementTransparentUV", 0)]},
     [], True),
    # two textures connected, no texture layer element, both UV sets named
    ("uv2c_twotex_no_layerelem",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0)]},
     ["MapA", "MapB"], True),
    # a diffuse texture element naming a texture that is not connected
    ("uv2c_texid_out_of_range",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementTransparentTextures", 0, 3)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementTransparentTextures", 0), ("LayerElementUV", 0),
          ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
]


# Batch four: batch three narrowed the second half of the condition to "the geometry declares a
# texture layer element", which is a strange thing for a UV set to depend on. These say whether it
# is the texture element itself, the Layer naming it, or merely a fourth element of any kind.
FREE_PROBES += [
    # the texture element is there but no Layer names it
    ("uv2d_tex_not_in_layer",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
    # a fourth element that is not a texture: does any extra element do it?
    ("uv2d_smoothing_not_texture",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      '\t\tLayerElementSmoothing: 0 {\n\t\t\tVersion: 102\n\t\t\tName: ""\n'
      '\t\t\tMappingInformationType: "ByPolygon"\n'
      '\t\t\tReferenceInformationType: "Direct"\n'
      "\t\t\tSmoothing: 1,1\n\t\t}\n"],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0),
          ("LayerElementSmoothing", 0)]},
     [], True),
    # the transparency texture element only, and not named by the Layer
    ("uv2d_transptex_not_in_layer",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTransparentTextures", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
    # a texture element named by the Layer, with no texture connected at all
    ("uv2d_texelem_no_texture",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0)]},
     [], True),
    # the same UV element type named twice by one Layer, at two TypedIndexes
    ("uv2d_two_transpuv",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      uv_element("LayerElementTransparentUV", 1, "UVChannel_TRANSPARENT1", UV_C),
      texture_element("LayerElementTexture", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0),
          ("LayerElementTransparentUV", 1)]},
     ["MapA"], True),
]


# The six the rule stands on, written under their committed names. The rest of the batches above
# stay in the build directory: they ruled things out and their answers are in the plan row, but a
# fixture is only worth committing when a test reads it.
COMMITTED = {
    # both UV sets, and the texture element that lets the second one through
    "fbx_uv_transparent_pair": ("uv2b_transpuv_diffuse_only",),
    # the same file with the texture entry taken out of the Layer block: one channel
    "fbx_uv_transparent_no_texture": ("uv2b_transpuv_no_texture",),
    # the UV set declared but not named by the Layer: one channel
    "fbx_uv_transparent_unnamed": ("uv2f_transpuv_unnamed_diffuse",),
    # three UV element types at once, and three channels in the Layer's own order
    "fbx_uv_three_types": ("uv2f_three_types_diffuse",),
    # a Transparency UV set with no diffuse one: it takes index 0
    "fbx_uv_transparent_alone": ("uv2f_transpuv_alone_diffuse",),
    # the same UV element type named twice by one Layer: one channel, the last entry
    "fbx_uv_same_type_twice": ("uv2d_two_transpuv",),
}


def write_committed(directory):
    """The six fixtures a test reads, under their committed names."""
    os.makedirs(directory, exist_ok=True)
    by_name = {}
    for row in FREE_PROBES:
        by_name[row[0]] = ("free", row)
    for row in PROBES:
        by_name[row[0]] = ("fixed", row)
    for committed, (source,) in sorted(COMMITTED.items()):
        kind, row = by_name[source]
        if kind == "free":
            _, elements, layers, textures, material = row
            text = build_free(committed, elements, layers, textures, material)
        else:
            _, uv_sets, tex, transparent, transparent_uv, layer, textures = row
            text = build(committed, uv_sets, tex, transparent, transparent_uv, layer, textures)
        path = os.path.join(directory, committed + ".fbx")
        with open(path, "w", newline="\n") as handle:
            handle.write(text)
        print("wrote %s" % path)


# Batch five: the *other* texture layer elements. XNA answers `LayerElementTransparentTextures` as
# a material texture named `Transparency` and `LayerElementReflectionTextures` as `Reflection`;
# these say what it calls the rest, and in what order they come out.
FREE_PROBES += [
    ("uv2e_%s" % kind.lower(),
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElement%sTextures" % kind, 0, 1)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElement%sTextures" % kind, 0), ("LayerElementUV", 0)]},
     ["MapA", "MapB"], True)
    for kind in ("Specular", "Bump", "Emissive", "Ambient", "Diffuse", "Reflection", "Transparent",
                 "NormalMap", "Displacement", "Shininess")
]
FREE_PROBES += [
    # every kind the batch above showed XNA knows, at once, to fix the order they come out in
    ("uv2e_every",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementReflectionTextures", 0, 1),
      texture_element("LayerElementShininessTextures", 0, 1),
      texture_element("LayerElementNormalMapTextures", 0, 1),
      texture_element("LayerElementAmbientTextures", 0, 1),
      texture_element("LayerElementTransparentTextures", 0, 1),
      texture_element("LayerElementBumpTextures", 0, 1),
      texture_element("LayerElementSpecularTextures", 0, 1)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementReflectionTextures", 0), ("LayerElementShininessTextures", 0),
          ("LayerElementNormalMapTextures", 0), ("LayerElementAmbientTextures", 0),
          ("LayerElementTransparentTextures", 0), ("LayerElementBumpTextures", 0),
          ("LayerElementSpecularTextures", 0), ("LayerElementUV", 0)]},
     ["MapA", "MapB"], True),
    # all of them at once, to fix the order they come out in
    ("uv2e_all",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      texture_element("LayerElementTexture", 0, 0),
      texture_element("LayerElementSpecularTextures", 0, 1),
      texture_element("LayerElementBumpTextures", 0, 1),
      texture_element("LayerElementTransparentTextures", 0, 1),
      texture_element("LayerElementEmissiveTextures", 0, 1),
      texture_element("LayerElementReflectionTextures", 0, 1)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementSpecularTextures", 0), ("LayerElementBumpTextures", 0),
          ("LayerElementTransparentTextures", 0), ("LayerElementEmissiveTextures", 0),
          ("LayerElementReflectionTextures", 0), ("LayerElementUV", 0)]},
     ["MapA", "MapB"], True),
]


# Batch six: the same three questions with **only** the diffuse texture element named, so that a
# committed fixture turns on the UV rule alone. `uv2b_transpuv_diffuse_only` already showed the
# second set does not need its own texture channel; these carry that through the other cases, and
# they are what the committed fixtures are generated from -- CNA's importer answers the material's
# `Texture` and no other channel (see the `XNASWEEP-166` row), so a fixture that declared the other
# texture elements would be measuring something production deliberately does not do.
FREE_PROBES += [
    ("uv2f_transpuv_unnamed_diffuse",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementUV", 0)]},
     ["MapA"], True),
    ("uv2f_transpuv_alone_diffuse",
     [uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      texture_element("LayerElementTexture", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementTransparentUV", 0)]},
     ["MapA"], True),
    ("uv2f_three_types_diffuse",
     [uv_element("LayerElementUV", 0, "UVChannel_DIFFUSE0", UV_A),
      uv_element("LayerElementTransparentUV", 0, "UVChannel_TRANSPARENT0", UV_B),
      uv_element("LayerElementReflectionUV", 0, "UVChannel_REFLECTION0", UV_C),
      texture_element("LayerElementTexture", 0, 0)],
     {0: [("LayerElementNormal", 0), ("LayerElementMaterial", 0), ("LayerElementTexture", 0),
          ("LayerElementUV", 0), ("LayerElementTransparentUV", 0),
          ("LayerElementReflectionUV", 0)]},
     ["MapA"], True),
]


def main():
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    if "--committed" in sys.argv:
        write_committed(directory)
        return
    os.makedirs(directory, exist_ok=True)
    for name, elements, layers, textures, material in FREE_PROBES:
        path = os.path.join(directory, name + ".fbx")
        with open(path, "w", newline="\n") as handle:
            handle.write(build_free(name, elements, layers, textures, material))
        print("wrote %s" % path)
    for name, uv_sets, tex, transparent, transparent_uv, layer, textures in PROBES:
        path = os.path.join(directory, name + ".fbx")
        with open(path, "w", newline="\n") as handle:
            handle.write(build(name, uv_sets, tex, transparent, transparent_uv, layer, textures))
        print("wrote %s" % path)


if __name__ == "__main__":
    main()
