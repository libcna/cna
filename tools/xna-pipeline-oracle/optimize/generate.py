"""Generate the synthetic mesh families that separate the parts of `OptimizeForCache`'s rule.

Every probe is small enough that its whole answer can be reasoned about by hand, and the families
are built so that a pair of probes differs in exactly one thing: the input face order, the vertex
numbering, the winding, the coordinates, or one step of the topology.  The generator writes two
files -- the flat text the genuine-XNA driver reads, and a JSON copy of the same topologies for the
scorer -- so that neither side can drift from the other.  XNASWEEP-149.
"""

from __future__ import annotations

import json
import random
import sys
from typing import Dict, List, Sequence, Tuple

Face = Tuple[int, int, int]


class Probes:
    def __init__(self):
        self.items: List[dict] = []
        self.names = set()

    def add(self, name: str, positions: Sequence[Sequence[float]], faces: Sequence[Face],
            shared: bool = True) -> None:
        if name in self.names:
            raise SystemExit("duplicate probe name %s" % name)
        self.names.add(name)
        self.items.append({"name": name, "shared": bool(shared),
                           "positions": [list(map(float, p)) for p in positions],
                           "faces": [list(map(int, f)) for f in faces]})


# --------------------------------------------------------------------------------------------
# building blocks


def grid(cols: int, rows: int):
    """A `cols` x `rows` quad grid, two triangles per quad, row by row."""
    positions = [(x, y, 0.0) for y in range(rows + 1) for x in range(cols + 1)]
    faces: List[Face] = []
    for y in range(rows):
        for x in range(cols):
            a = y * (cols + 1) + x
            b, c, e = a + 1, a + cols + 1, a + cols + 2
            faces.append((a, b, e))
            faces.append((a, e, c))
    return positions, faces


def strip(count: int):
    positions = [(i // 2, i % 2, 0.0) for i in range(2 * (count + 1))]
    faces: List[Face] = []
    for i in range(count):
        a, b, c, d = 2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 3
        faces.append((a, b, d))
        faces.append((a, d, c))
    return positions, faces


def open_fan(count: int, centre: int = 0):
    """`count` triangles around one centre, the rim an open path."""
    rim = [v for v in range(count + 2) if v != centre][:count + 1]
    positions = [(0.0, 0.0, 0.0)] * (count + 2)
    faces = [(centre, rim[i], rim[i + 1]) for i in range(count)]
    coords = []
    for v in range(count + 2):
        coords.append((0.0, 0.0, 0.0) if v == centre else (float(v), 1.0, 0.0))
    return coords, faces


def closed_fan(count: int, centre: int = 0):
    rim = [v for v in range(count + 1) if v != centre]
    coords = [(0.0, 0.0, 0.0)] * (count + 1)
    for i, v in enumerate(rim):
        coords[v] = (float(i), 1.0, 0.0)
    coords[centre] = (0.0, 0.0, 0.0)
    faces = [(centre, rim[i], rim[(i + 1) % count]) for i in range(count)]
    return coords, faces


def uv_sphere(rings: int, segments: int):
    points: List[Tuple[float, float, float]] = []
    for ring in range(rings):
        for segment in range(segments):
            points.append((float(segment), float(ring), 0.0))
    south = len(points)
    points.append((-1.0, -1.0, 0.0))
    north = len(points)
    points.append((-1.0, float(rings), 0.0))
    faces: List[Face] = []
    for ring in range(rings - 1):
        for segment in range(segments):
            a = ring * segments + segment
            b = ring * segments + (segment + 1) % segments
            c = (ring + 1) * segments + segment
            d = (ring + 1) * segments + (segment + 1) % segments
            faces.append((a, b, d))
            faces.append((a, d, c))
    for segment in range(segments):
        faces.append((segment, (segment + 1) % segments, south))
    for segment in range(segments):
        b = (rings - 1) * segments
        faces.append((b + segment, b + (segment + 1) % segments, north))
    return points, faces


def renumber(positions, faces, permutation):
    """Apply a position permutation: old position `i` becomes `permutation[i]`."""
    out_positions = [None] * len(positions)
    for i, p in enumerate(positions):
        out_positions[permutation[i]] = p
    out_faces = [tuple(permutation[c] for c in f) for f in faces]
    return out_positions, out_faces


def reorder(faces, order):
    return [faces[i] for i in order]


# --------------------------------------------------------------------------------------------
# families


def build() -> Probes:
    p = Probes()
    rng = random.Random(20260908)

    # A -- disconnected triangles.  Nothing is shared, so the whole answer is the restart rule.
    for count in (1, 2, 3, 4, 5, 8, 12, 20):
        coords = [(float(i), float(i % 3), 0.0) for i in range(3 * count)]
        faces = [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(count)]
        p.add("disjoint_%d" % count, coords, faces)
    # the same six, their faces permuted, their vertices renumbered, their winding reversed
    coords = [(float(i), float(i % 3), 0.0) for i in range(18)]
    plain = [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(6)]
    p.add("disjoint_6_faces_reversed", coords, list(reversed(plain)))
    p.add("disjoint_6_faces_rot", coords, reorder(plain, [(i + 2) % 6 for i in range(6)]))
    p.add("disjoint_6_wind", coords, [(f[0], f[2], f[1]) for f in plain])
    flip = {i: 17 - i for i in range(18)}
    cf, ff = renumber(coords, plain, flip)
    p.add("disjoint_6_vertexflip", cf, ff)
    p.add("disjoint_6_far_coords", [(float(i * 1000), 0.0, 0.0) for i in range(18)], plain)

    # B -- strips.  One long run with no choice at all except where to start and where to stop.
    for count in (2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 32):
        coords, faces = strip(count)
        p.add("strip_%d" % count, coords, faces)
    coords, faces = strip(8)
    p.add("strip_8_reversed", coords, list(reversed(faces)))
    p.add("strip_8_rot1", coords, reorder(faces, [(i + 1) % 16 for i in range(16)]))
    p.add("strip_8_perm5", coords, reorder(faces, [(5 * i) % 16 for i in range(16)]))
    p.add("strip_8_wind", coords, [(f[0], f[2], f[1]) for f in faces])
    flip = {i: (len(coords) - 1 - i) for i in range(len(coords))}
    cf, ff = renumber(coords, faces, flip)
    p.add("strip_8_vertexflip", cf, ff)
    shuffled = list(range(len(coords)))
    rng.shuffle(shuffled)
    cf, ff = renumber(coords, faces, {i: shuffled[i] for i in range(len(coords))})
    p.add("strip_8_vertexshuffle", cf, ff)

    # C -- fans.  One high-valence centre: the place where a valence rule and an index rule part.
    for count in (3, 4, 5, 6, 8, 10, 12, 16):
        coords, faces = closed_fan(count)
        p.add("fan_closed_%d" % count, coords, faces)
    for count in (3, 4, 6, 8, 12):
        coords, faces = open_fan(count)
        p.add("fan_open_%d" % count, coords, faces)
    coords, faces = closed_fan(8)
    p.add("fan_closed_8_reversed", coords, list(reversed(faces)))
    p.add("fan_closed_8_rot3", coords, reorder(faces, [(i + 3) % 8 for i in range(8)]))
    p.add("fan_closed_8_perm3", coords, reorder(faces, [(3 * i) % 8 for i in range(8)]))
    # the centre given the highest index instead of the lowest
    coords8, faces8 = closed_fan(8, centre=8)
    p.add("fan_closed_8_centre_last", coords8, faces8)
    coords8, faces8 = closed_fan(8, centre=4)
    p.add("fan_closed_8_centre_mid", coords8, faces8)

    # D -- grids, square and rectangular.  The rectangular ones are the ones that say whether a run
    # stops because of its own length or because of what is left of the mesh.
    for side in (1, 2, 3, 4, 5, 6, 7):
        coords, faces = grid(side, side)
        p.add("grid_%dx%d" % (side, side), coords, faces)
    for cols in (2, 3, 4, 5, 6):
        for rows in (1, 2, 3, 4, 5):
            if cols == rows:
                continue
            coords, faces = grid(cols, rows)
            p.add("grid_%dx%d" % (cols, rows), coords, faces)
    coords, faces = grid(4, 4)
    p.add("grid_4x4_reversed", coords, list(reversed(faces)))
    p.add("grid_4x4_perm7", coords, reorder(faces, [(7 * i) % 32 for i in range(32)]))
    p.add("grid_4x4_perm3", coords, reorder(faces, [(3 * i) % 32 for i in range(32)]))
    p.add("grid_4x4_rowsbottomup", coords,
          [faces[8 * (3 - r) + i] for r in range(4) for i in range(8)])
    order = list(range(32))
    rng.shuffle(order)
    p.add("grid_4x4_shuffled", coords, reorder(faces, order))
    flip = {i: 24 - i for i in range(25)}
    cf, ff = renumber(coords, faces, flip)
    p.add("grid_4x4_vertexflip", cf, ff)
    shuffled = list(range(25))
    rng.shuffle(shuffled)
    cf, ff = renumber(coords, faces, {i: shuffled[i] for i in range(25)})
    p.add("grid_4x4_vertexshuffle", cf, ff)
    p.add("grid_4x4_far_coords", [(x * 1000.0, y * 1000.0, 0.0) for y in range(5) for x in range(5)],
          faces)
    # the same grid with the two triangles of every quad swapped in the input list
    p.add("grid_4x4_quadswap", coords,
          [faces[i ^ 1] for i in range(32)])
    # both diagonals the other way
    alt: List[Face] = []
    for y in range(4):
        for x in range(4):
            a = y * 5 + x
            b, c, e = a + 1, a + 5, a + 6
            alt.append((a, b, c))
            alt.append((b, e, c))
    p.add("grid_4x4_altdiagonal", coords, alt)

    # E -- an L and a T: a grid with a bite out of it, so that a run meets a boundary early.
    coords, faces = grid(4, 4)
    keep = [i for i in range(32) if not (i // 8 >= 2 and (i % 8) // 2 >= 2)]
    p.add("grid_4x4_L", coords, [faces[i] for i in keep])
    keep = [i for i in range(32) if not (i // 8 == 1 and (i % 8) // 2 in (0, 3))]
    p.add("grid_4x4_T", coords, [faces[i] for i in keep])
    keep = [i for i in range(32) if i not in (14, 15)]
    p.add("grid_4x4_hole_topright", coords, [faces[i] for i in keep])
    keep = [i for i in range(32) if i not in (0, 1)]
    p.add("grid_4x4_hole_origin", coords, [faces[i] for i in keep])

    # F -- closed surfaces with two symmetric poles: the tie the spheres could not settle.
    for rings, segments in ((2, 3), (2, 4), (2, 5), (2, 6), (3, 4), (3, 6), (4, 4), (4, 6)):
        coords, faces = uv_sphere(rings, segments)
        p.add("sphere_%dx%d" % (rings, segments), coords, faces)
    coords, faces = uv_sphere(2, 4)
    p.add("sphere_2x4_reversed", coords, list(reversed(faces)))
    p.add("sphere_2x4_perm7", coords, reorder(faces, [(7 * i) % 16 for i in range(16)]))
    # north declared before south: the poles keep their topology and swap their numbers
    swap = {i: i for i in range(10)}
    swap[8], swap[9] = 9, 8
    cf, ff = renumber(coords, faces, swap)
    p.add("sphere_2x4_poleswap", cf, ff)
    # the north fan handed over before the south one, numbering unchanged
    p.add("sphere_2x4_northfirst", coords, faces[:8] + faces[12:] + faces[8:12])
    # the poles given the two lowest numbers instead of the two highest
    shift = {i: (i + 2) % 10 for i in range(10)}
    cf, ff = renumber(coords, faces, shift)
    p.add("sphere_2x4_polesfirst", cf, ff)
    # an octahedron and a tetrahedron: the smallest closed surfaces there are
    p.add("tetrahedron", [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1)],
          [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)])
    oct_coords = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    oct_faces = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4),
                 (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
    p.add("octahedron", oct_coords, oct_faces)
    p.add("octahedron_reversed", oct_coords, list(reversed(oct_faces)))

    # G -- two components, so that a restart has to leave one of them.
    for count in (2, 3):
        coords, faces = grid(3, 3)
        allc: List[Tuple[float, float, float]] = []
        allf: List[Face] = []
        for k in range(count):
            base = len(allc)
            allc.extend([(x + 10.0 * k, y, 0.0) for (x, y, _) in coords])
            allf.extend([(f[0] + base, f[1] + base, f[2] + base) for f in faces])
        p.add("two_grids_3_%d" % count, allc, allf)
    # a grid and a strip side by side, the strip declared first and then second
    gc, gf = grid(3, 3)
    sc, sf = strip(4)
    base = len(gc)
    p.add("grid3_then_strip4", list(gc) + [(x + 20.0, y, 0.0) for (x, y, _) in sc],
          list(gf) + [(f[0] + base, f[1] + base, f[2] + base) for f in sf])
    base = len(sc)
    p.add("strip4_then_grid3", list(sc) + [(x + 20.0, y, 0.0) for (x, y, _) in gc],
          list(sf) + [(f[0] + base, f[1] + base, f[2] + base) for f in gf])

    # H -- one-variable branch probes.  A short strip whose last face has two legal continuations,
    # with the two candidates differing in exactly one thing.
    #
    #   0---1---2      faces: (0,1,4) (1,5,4) are the run; (1,2,5) and (4,5,7) are the two
    #   | \ | \ |      candidates, one reached by keeping the newer vertex and one by keeping the
    #   3---4---5      older.  Which of the two carries the higher face index, the higher vertex
    #                  index and the lower valence is varied one at a time.
    def branch(name, faces, positions=None):
        coords = positions or [(float(i % 3), float(i // 3), 0.0) for i in range(9)]
        p.add(name, coords, faces)

    base_faces = [(0, 1, 4), (0, 4, 3), (1, 2, 5), (1, 5, 4), (3, 4, 7), (3, 7, 6),
                  (4, 5, 8), (4, 8, 7)]
    branch("branch_base", base_faces)
    for k, order in enumerate([[0, 1, 4, 5, 2, 3, 6, 7], [2, 3, 6, 7, 0, 1, 4, 5],
                               [6, 7, 4, 5, 2, 3, 0, 1], [1, 0, 3, 2, 5, 4, 7, 6]]):
        branch("branch_order%d" % k, reorder(base_faces, order))
    for k, perm in enumerate([{i: 8 - i for i in range(9)},
                              {0: 0, 1: 3, 2: 6, 3: 1, 4: 4, 5: 7, 6: 2, 7: 5, 8: 8},
                              {0: 4, 1: 5, 2: 6, 3: 7, 4: 8, 5: 0, 6: 1, 7: 2, 8: 3}]):
        cf, ff = renumber([(float(i % 3), float(i // 3), 0.0) for i in range(9)], base_faces, perm)
        p.add("branch_vertex%d" % k, cf, ff)

    return p


def write(probes: Probes, text_path: str, json_path: str) -> None:
    with open(text_path, "w") as out:
        for item in probes.items:
            out.write("P %s %d %d %d\n" % (item["name"], 1 if item["shared"] else 0,
                                           len(item["positions"]), len(item["faces"])))
            for x, y, z in item["positions"]:
                out.write("V %r %r %r\n" % (x, y, z))
            for a, b, c in item["faces"]:
                out.write("F %d %d %d\n" % (a, b, c))
    with open(json_path, "w") as out:
        json.dump({"probes": probes.items}, out, indent=1)


def main() -> int:
    text_path = sys.argv[1] if len(sys.argv) > 1 else "build/xna-sample-sweep/optimize/probes.txt"
    json_path = sys.argv[2] if len(sys.argv) > 2 else "build/xna-sample-sweep/optimize/probes.json"
    probes = build()
    write(probes, text_path, json_path)
    print("generate: %d probes, %d faces total"
          % (len(probes.items), sum(len(i["faces"]) for i in probes.items)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


def build_targeted() -> Probes:
    """The second batch: probes built to answer the questions the first batch raised.

    Batch one established that the answer is a strip/fan walk, that it ignores coordinates,
    vertex numbering and winding, and that it depends on the input face order.  What it did not
    settle is when a run ends: a 4-column grid's first run walks its whole top row and then stops
    where its second run turns into the row below, from a state that is identical in every feature
    the walk can read.  These probes vary exactly one thing at a time around that.
    """
    p = Probes()
    rng = random.Random(20260908)

    # 1. History.  The same grid, preceded by disjoint triangles that carry the higher indices and
    #    are therefore consumed first: the grid's own state when its first run starts is unchanged,
    #    only what came before it.
    for count in (1, 2, 3, 6, 10):
        coords, faces = grid(4, 2)
        base = len(coords)
        allc = list(coords) + [(100.0 + i, float(i % 3), 0.0) for i in range(3 * count)]
        allf = list(faces) + [(base + 3 * i, base + 3 * i + 1, base + 3 * i + 2)
                              for i in range(count)]
        p.add("hist_grid42_soup%d_after" % count, allc, allf)
        allc2 = [(100.0 + i, float(i % 3), 0.0) for i in range(3 * count)] + \
                [(x, y, z) for (x, y, z) in coords]
        allf2 = [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(count)] + \
                [(f[0] + 3 * count, f[1] + 3 * count, f[2] + 3 * count) for f in faces]
        p.add("hist_grid42_soup%d_before" % count, allc2, allf2)
    # the same, on the 4x4 whose two runs disagree
    for count in (2, 6):
        coords, faces = grid(4, 4)
        base = len(coords)
        allc = list(coords) + [(100.0 + i, float(i % 3), 0.0) for i in range(3 * count)]
        allf = list(faces) + [(base + 3 * i, base + 3 * i + 1, base + 3 * i + 2)
                              for i in range(count)]
        p.add("hist_grid44_soup%d_after" % count, allc, allf)
    # a whole grid consumed before another, disjoint one
    gc, gf = grid(4, 2)
    base = len(gc)
    p.add("hist_grid42_twice", list(gc) + [(x + 50.0, y, z) for (x, y, z) in gc],
          list(gf) + [(f[0] + base, f[1] + base, f[2] + base) for f in gf])

    # 2. Width sweep for a two-row grid: where the first run stops as the row grows.
    for cols in (2, 3, 4, 5, 6, 7, 8, 9, 10, 12):
        coords, faces = grid(cols, 2)
        p.add("wide_%dx2" % cols, coords, faces)
    for cols in (4, 5, 6, 8):
        coords, faces = grid(cols, 3)
        p.add("wide_%dx3" % cols, coords, faces)

    # 3. Ladders: a top row of `cols` quads with the row below present only under one quad, so that
    #    the walk can turn at exactly one place and nowhere else.
    for cols in (6, 8):
        for at in range(cols):
            coords, faces = grid(cols, 2)
            keep = [i for i in range(len(faces))
                    if i >= 2 * cols or i // 2 == at]     # the whole top row, one quad below
            p.add("ladder_%d_at%d" % (cols, at), coords, [faces[i] for i in keep])

    # 4. Rows of a single strip with one branch triangle glued to a chosen edge: the smallest mesh
    #    where a run has exactly one chance to turn.
    for count in (8, 12):
        for at in range(0, 2 * count, 2):
            coords, faces = strip(count)
            extra = len(coords)
            allc = list(coords) + [(float(at), -1.0, 0.0)]
            a, b, _ = faces[at]
            allf = list(faces) + [(a, extra, b)]
            p.add("branchstrip_%d_at%d" % (count, at), allc, allf)

    # 5. Closed fans of every size from 3 to 20: the run length as the only variable.
    for count in range(3, 21):
        coords, faces = closed_fan(count)
        p.add("cfan_%d" % count, coords, faces)
    for count in range(3, 17):
        coords, faces = open_fan(count)
        p.add("ofan_%d" % count, coords, faces)

    # 6. Cylinders: a closed ring of quads with no poles, so a run can go round for ever.
    for rings, segments in ((2, 4), (2, 6), (2, 8), (3, 6), (3, 8), (4, 8)):
        points: List[Tuple[float, float, float]] = []
        for ring in range(rings):
            for segment in range(segments):
                points.append((float(segment), float(ring), 0.0))
        faces: List[Face] = []
        for ring in range(rings - 1):
            for segment in range(segments):
                a = ring * segments + segment
                b = ring * segments + (segment + 1) % segments
                c = (ring + 1) * segments + segment
                d = (ring + 1) * segments + (segment + 1) % segments
                faces.append((a, b, d))
                faces.append((a, d, c))
        p.add("cyl_%dx%d" % (rings, segments), points, faces)

    # 7. Single rows of every length: a run with no choice at all, to bound its length.
    for count in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 20, 24):
        coords, faces = grid(count, 1)
        p.add("row_%d" % count, coords, faces)

    return p


def build_rules() -> Probes:
    """The third batch: index against topology, on the two decisions still open.

    `branchstrip_8_at0` continues from its seed into the face next to it and `branchstrip_8_at2`
    restarts, from states that differ only in which index the neighbouring face carries. These
    reverse and rotate the same strip so that the topology is fixed and the index moves, and vary
    the closed fan the seed rule also fails on.  XNASWEEP-149.
    """
    p = Probes()

    def strip_with_branch(name, count, at, order=None):
        coords, faces = strip(count)
        faces = list(faces)
        if order is not None:
            index = {old: new for new, old in enumerate(order)}
            at = index[at]
            faces = [faces[i] for i in order]
        extra = len(coords)
        a, b, _ = faces[at]
        p.add(name, list(coords) + [(float(at), -1.0, 0.0)], faces + [(a, extra, b)])

    n = 16
    for at in (0, 2, 14):
        strip_with_branch("bstrip_plain_at%d" % at, 8, at)
        strip_with_branch("bstrip_rev_at%d" % at, 8, at, list(reversed(range(n))))
        strip_with_branch("bstrip_rot4_at%d" % at, 8, at, [(i + 4) % n for i in range(n)])
        strip_with_branch("bstrip_perm3_at%d" % at, 8, at, [(3 * i) % n for i in range(n)])

    # A closed fan whose faces are rotated: the seed's tie-break with the topology held fixed.
    for shift in range(0, 9, 2):
        coords, faces = closed_fan(9)
        p.add("cfan9_rot%d" % shift, coords,
              [faces[(i + shift) % 9] for i in range(9)])
    for shift in (0, 3, 6):
        coords, faces = closed_fan(12)
        p.add("cfan12_rot%d" % shift, coords,
              [faces[(i + shift) % 12] for i in range(12)])
    # ... and with the centre carrying a different vertex number.
    for centre in (0, 5, 9):
        coords, faces = closed_fan(9, centre=centre)
        p.add("cfan9_centre%d" % centre, coords, faces)

    # Two disjoint pieces whose minimum-live vertices sit on faces whose indices can be swapped.
    for swap in (False, True):
        one_c, one_f = strip(3)
        two_c, two_f = closed_fan(6)
        base = len(one_c)
        coords = list(one_c) + [(x + 20.0, y, z) for (x, y, z) in two_c]
        shifted = [(f[0] + base, f[1] + base, f[2] + base) for f in two_f]
        faces = (shifted + list(one_f)) if swap else (list(one_f) + shifted)
        p.add("pair_fan_strip_%s" % ("fanfirst" if swap else "stripfirst"), coords, faces)

    # A two-row grid whose rows are handed over interleaved rather than row by row: the run/restart
    # boundary with the topology fixed and the index order changed.
    for label, mix in (("interleaved", lambda i, n: (i % 2) * (n // 2) + i // 2),
                       ("evensfirst", lambda i, n: (i * 2) % n + (1 if i * 2 >= n else 0))):
        coords, faces = grid(4, 2)
        n2 = len(faces)
        order = [mix(i, n2) for i in range(n2)]
        if sorted(order) == list(range(n2)):
            p.add("grid42_%s" % label, coords, [faces[i] for i in order])

    return p
