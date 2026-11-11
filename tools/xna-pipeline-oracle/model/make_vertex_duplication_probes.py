#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""KI-XVERT-WINDOW: how the genuine X importer resolves a face corner to a vertex.

XNA's XImporter loads a `.x` through D3DX (`D3DXLoadMeshHierarchyFromXW`), so the vertex every face
corner ends on is decided by d3dx9's loader. These fixtures pin that decision where a
`VertexDuplicationIndices` block, a repeated vertex or the data a vertex carries makes it visible.
Each fixture is a probe whose genuine answer was measured before it was committed (cnaplan
evidence rev002/ki-xvert-02, rounds 1 to 4; `source` names the probe).

  x_dup_pair, x_dup_triple, x_dup_absent_representative
        two or three corners of one triangle whose vertices share a label
  x_dup_face_order, x_dup_shared_split, x_dup_group_of_four
        the same groups over several faces: what a face finds depends on the faces before it
  x_dup_ring_order, x_dup_split_source, x_dup_first_corner_group, x_dup_insertion_a/_b,
  x_dup_four_middle_representative
        which assigned vertex of a label is found first, what a new one copies, and where it goes
  x_repeat_slot_third, x_repeat_slot_match
        a vertex named twice in a triangle whose own vertex already holds another normal (no map)
  x_dup_texcoords, x_dup_texcoords_shared, x_dup_colours
        texture coordinates and colours take part in the match
  x_dup_quad, x_dup_materials
        polygons (per fan triangle) and material batches
  x_normal_lengths
        two declared normals with one direction and two lengths are two normals
  x_dup_label_out_of_range, x_dup_short, x_dup_long, x_dup_not_representative
        the maps the importer refuses

    python3 tools/xna-pipeline-oracle/model/make_vertex_duplication_probes.py tests/assets/xna40/model
"""
from __future__ import annotations

import os
import sys

TEMPLATE = """template VertexDuplicationIndices {
 <b8d65549-d7c9-4995-89cf-53a9a8b031e3>
 DWORD nIndices;
 DWORD nOriginalVertices;
 array DWORD indices[nIndices];
}
"""

FIXTURES = {
    'x_dup_face_order': dict(
        positions=[[0, 0, 0], [4, 0, 0], [0, 4, 0], [0, 0, 4], [4, 4, 0], [-4, 0, 0]],
        faces=[[1, 3, 4], [0, 4, 5]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0], [0, 1, 1], [1, 0, 1]],
        normal_faces=[[1, 3, 4], [0, 4, 5]],
        duplication=[0, 1, 2, 3, 3, 3],
        original=4,
        source='F05_f21'),
    'x_dup_pair': dict(
        positions=[[1, 0, 0], [0, 2, 0], [3, 0, 0]],
        faces=[[0, 1, 2]],
        normals=[[0, 1, 0], [1, 0, 0], [0, 0, 1]],
        normal_faces=[[0, 1, 2]],
        duplication=[0, 0, 2],
        original=3,
        source='T01_002'),
    'x_dup_triple': dict(
        positions=[[1, 0, 0], [0, 2, 0], [3, 0, 0]],
        faces=[[0, 1, 2]],
        normals=[[0, 1, 0], [1, 0, 0], [0, 0, 1]],
        normal_faces=[[0, 1, 2]],
        duplication=[2, 2, 2],
        original=3,
        source='T06_222'),
    'x_dup_absent_representative': dict(
        positions=[[1, 0, 0], [0, 2, 0], [3, 0, 0], [0, 0, 4]],
        faces=[[0, 1, 2]],
        normals=[[0, 1, 0], [1, 0, 0], [0, 0, 1], [-1, 0, 0]],
        normal_faces=[[0, 1, 2]],
        duplication=[3, 3, 2, 3],
        original=4,
        source='T09_3323'),
    'x_dup_shared_split': dict(
        positions=[[1, 0, 0], [0, 2, 0], [3, 0, 0], [0, 0, 4]],
        faces=[[0, 2, 3], [1, 2, 3]],
        normals=[[0, 1, 0], [1, 0, 0], [0, 0, 1], [-1, 0, 0]],
        normal_faces=[[0, 2, 3], [1, 2, 3]],
        duplication=[2, 2, 2, 3],
        original=4,
        source='C00_2223_both'),
    'x_dup_group_of_four': dict(
        positions=[[0, 0, 0], [4, 0, 0], [0, 4, 0], [0, 0, 4], [4, 4, 0], [-4, 0, 0]],
        faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0], [0, 1, 1], [1, 0, 1]],
        normal_faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        duplication=[0, 1, 2, 0, 0, 0],
        original=4,
        source='F15_map012000'),
    'x_dup_ring_order': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3], [7, 0, 0.5], [8, 2, 3.5], [9, 4, 0.5]],
        faces=[[0, 4, 5], [1, 6, 7], [2, 3, 8]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 6, 7], [0, 8, 9], [1, 0, 10]],
        duplication=[0, 0, 0, 0, 4, 5, 6, 7, 8],
        original=9,
        source='S1_index_eq_creation'),
    'x_dup_split_source': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3], [7, 0, 0.5], [8, 2, 3.5], [9, 4, 0.5]],
        faces=[[1, 4, 5], [0, 2, 3]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 6, 7], [1, 0, 2]],
        duplication=[0, 0, 0, 0, 4, 5, 6, 7, 8],
        original=9,
        source='SP1_split_source'),
    'x_dup_first_corner_group': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3], [7, 0, 0.5], [8, 2, 3.5]],
        faces=[[0, 1, 2], [3, 4, 5], [3, 6, 7]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 8, 9], [1, 10, 11], [0, 12, 13]],
        duplication=[0, 1, 2, 0, 4, 5, 6, 7],
        original=8,
        source='AS2_own_set_other'),
    'x_dup_insertion_a': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0]],
        faces=[[3, 0, 2, 1], [1, 4, 2], [4, 2, 0], [3, 4, 3, 4], [3, 4, 1], [2, 4, 2, 0]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[5, 5, 4, 5], [2, 0, 1], [1, 4, 3], [1, 1, 5, 2], [4, 7, 3], [1, 1, 4, 4]],
        duplication=[0, 1, 0, 1, 4],
        original=5,
        source='X14_random'),
    'x_dup_insertion_b': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3], [7, 0, 0.5]],
        faces=[[6, 2, 5], [4, 2, 0], [4, 5, 5, 2], [6, 0, 2, 3], [0, 6, 6]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[2, 4, 0], [2, 0, 7], [3, 4, 5, 4], [6, 2, 4, 0], [0, 5, 3]],
        duplication=[0, 0, 2, 3, 0, 2, 3],
        original=7,
        source='X16_random'),
    'x_repeat_slot_third': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3]],
        faces=[[0, 1, 2], [0, 3, 0]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[6, 7, 8], [0, 9, 2]],
        original=4,
        source='E1_nomap_third'),
    'x_repeat_slot_match': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3]],
        faces=[[0, 1, 2], [0, 3, 0]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[6, 7, 8], [0, 9, 6]],
        original=4,
        source='E2_nomap_match_slot'),
    'x_dup_texcoords': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3]],
        faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        duplication=[0, 1, 2, 3, 3, 5],
        original=6,
        uvs=[[0, 1], [0.0625, 0.96875], [0.125, 0.9375], [0.1875, 0.90625], [0.25, 0.875], [0.3125, 0.84375]],
        source='U3_drop_case'),
    'x_dup_texcoords_shared': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3]],
        faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        duplication=[0, 1, 2, 3, 3, 5],
        original=6,
        uvs=[[0, 1], [0.0625, 0.96875], [0.125, 0.9375], [0.1875, 0.90625], [0.1875, 0.90625], [0.3125, 0.84375]],
        source='U6_drop_case_same_uv'),
    'x_dup_quad': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3]],
        faces=[[0, 1, 2, 3]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 1, 2, 3]],
        duplication=[0, 1, 0, 3],
        original=4,
        source='Q1_quad_02'),
    'x_dup_materials': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3]],
        faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        duplication=[0, 1, 2, 3, 3, 3],
        original=6,
        materials=[1, 0, 1, 0],
        source='M1_fixture_1010'),
    'x_dup_label_out_of_range': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0]],
        faces=[[0, 1, 2]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 1, 2]],
        duplication=[0, 1, 7],
        original=3,
        source='V1_label_out_of_range'),
    'x_dup_short': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0]],
        faces=[[0, 1, 2]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 1, 2]],
        duplication=[0, 0],
        original=3,
        source='V2_short_map'),
    'x_dup_long': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0]],
        faces=[[0, 1, 2]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1], [1, 1, -1], [-1, -1, 1], [2, 1, 0], [0, 2, 1], [1, 0, 2], [2, 0, 1], [1, 2, 0], [0, 1, 2]],
        normal_faces=[[0, 1, 2]],
        duplication=[0, 0, 2, 3],
        original=3,
        source='V3_long_map'),
    'x_dup_not_representative': dict(
        positions=[[1, 0, 0], [0, 2, 0], [3, 0, 0]],
        faces=[[0, 1, 2]],
        normals=[[0, 1, 0], [1, 0, 0], [0, 0, 1]],
        normal_faces=[[0, 1, 2]],
        duplication=[1, 0, 2],
        original=3,
        source='T11_102'),
    'x_dup_colours': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3]],
        faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1]],
        normal_faces=[[0, 1, 2], [0, 4, 5], [1, 3, 4], [1, 2, 3]],
        duplication=[0, 1, 2, 3, 3, 5],
        original=6,
        colors=[[1, 0, 0, 1], [0, 1, 0, 1], [0, 0, 1, 0.5], [1, 0, 0, 1], [0, 1, 0, 1], [0, 0, 1, 0.5]],
        source='K2_drop_case_colours'),
    'x_dup_four_middle_representative': dict(
        positions=[[1, 0, 0], [2, 2, 3], [3, 4, 0], [4, 0, 3], [5, 2, 0], [6, 4, 3], [7, 0, 0.5], [8, 2, 3.5]],
        faces=[[1, 7, 3], [5, 6, 7], [6, 1, 0], [3, 5, 1, 6], [2, 4, 0]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, -1, 0], [0, 0, -1], [1, 1, 0], [1, 0, 1], [0, 1, 1], [-1, 1, 0], [1, -1, 0], [0, 1, -1], [-1, 0, 1], [1, 1, 1], [-1, 1, 1], [1, -1, 1]],
        normal_faces=[[0, 1, 2], [0, 2, 3], [2, 4, 5], [1, 0, 4, 3], [5, 5, 6]],
        duplication=[2, 3, 2, 3, 2, 3, 3, 7],
        original=8,
        source='G1_four_rep_mid'),
    'x_normal_lengths': dict(
        positions=[[1, 0, 0], [2, 2, 3]],
        faces=[[0, 1, 0]],
        normals=[[1, 0, 0], [0, 1, 0], [0, 0, 1], [-1, 0, 0], [0, 0, 2], [0, 0, 3]],
        normal_faces=[[2, 0, 4]],
        original=2,
        source='NE1_repeat_lengths'),
}


def rows(values, face=False):
    out = []
    for i, row in enumerate(values):
        body = ("%d;%s" % (len(row), ",".join(map(str, row)))) if face else ";".join("%.6f" % c for c in row)
        out.append("  " + body + ";" + (";" if i == len(values) - 1 else ","))
    return out


def text(name, spec):
    lines = ["xof 0303txt 0032",
             "// Written by tools/xna-pipeline-oracle/model/make_vertex_duplication_probes.py (%s)."
             % spec["source"]]
    if "duplication" in spec:
        lines.append(TEMPLATE)
    lines += ["Mesh %s {" % name[2:], " %d;" % len(spec["positions"])]
    lines += rows(spec["positions"])
    lines.append(" %d;" % len(spec["faces"]))
    lines += rows(spec["faces"], True)
    lines += [" MeshNormals {", "  %d;" % len(spec["normals"])]
    lines += rows(spec["normals"])
    lines.append("  %d;" % len(spec["normal_faces"]))
    lines += rows(spec["normal_faces"], True)
    lines.append(" }")
    if "uvs" in spec:
        lines += [" MeshTextureCoords {", "  %d;" % len(spec["uvs"])]
        lines += rows(spec["uvs"])
        lines.append(" }")
    if "colors" in spec:
        colours = spec["colors"]
        lines += [" MeshVertexColors {", "  %d;" % len(colours)]
        for i, c in enumerate(colours):
            lines.append("  %d;%s;%s" % (i, ";".join("%.6f" % x for x in c), ";" if i == len(colours) - 1 else ","))
        lines.append(" }")
    if "materials" in spec:
        count = max(spec["materials"]) + 1
        lines += [" MeshMaterialList {", "  %d;" % count, "  %d;" % len(spec["materials"]),
                  "  " + ",".join(map(str, spec["materials"])) + ";"]
        for k in range(count):
            lines += ["  Material {", "   %.6f;%.6f;%.6f;1.000000;;" % (0.2 + 0.3 * k, 0.5, 0.5),
                      "   10.000000;", "   0.000000;0.000000;0.000000;;",
                      "   0.000000;0.000000;0.000000;;", "  }"]
        lines.append(" }")
    if "duplication" in spec:
        lines += [" VertexDuplicationIndices {", "  %d;" % len(spec["duplication"]),
                  "  %d;" % spec["original"], "  " + ",".join(map(str, spec["duplication"])) + ";", " }"]
    lines.append("}")
    return "\n".join(lines) + "\n"


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(directory, exist_ok=True)
    for name, spec in FIXTURES.items():
        with open(os.path.join(directory, name + ".x"), "w", encoding="ascii", newline="\n") as handle:
            handle.write(text(name, spec))
    print("wrote %d fixtures to %s" % (len(FIXTURES), directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
