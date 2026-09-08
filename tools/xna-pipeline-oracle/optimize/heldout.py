"""Held-out probes: fresh topologies, generated to test an equivalence rather than to fit a rule.

plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149.  The 376 frozen probes were designed, one family
at a time, to separate hypotheses about `MeshHelper.OptimizeForCache`; a rule fitted to them proves
little on them.  These are different: pseudo-random triangulations, random face orders and random
corner rotations, generated from a fixed seed and never used to choose a rule.  They exist to be
handed to *both* Microsoft black boxes -- genuine XNA's method and the documented public
`D3DXOptimizeFaces` -- and to a reconstruction, and to make a disagreement possible.

    python3 tools/xna-pipeline-oracle/optimize/heldout.py <out.txt> <out.json> [count]
"""

from __future__ import annotations

import random
import sys
from typing import List, Sequence, Tuple

sys.path.insert(0, __file__.rsplit("/", 1)[0])

import generate

Face = Tuple[int, int, int]


def _grid(cols: int, rows: int):
    positions = [(float(x), float(y), 0.0) for y in range(rows + 1) for x in range(cols + 1)]
    faces: List[Face] = []
    for y in range(rows):
        for x in range(cols):
            a = y * (cols + 1) + x
            b = a + 1
            c = a + cols + 1
            d = c + 1
            faces.append((a, b, c))
            faces.append((b, d, c))
    return positions, faces


def _cylinder(rings: int, segments: int):
    positions = [(float(s), float(r), 0.0) for r in range(rings + 1) for s in range(segments)]
    faces: List[Face] = []
    for r in range(rings):
        for s in range(segments):
            a = r * segments + s
            b = r * segments + (s + 1) % segments
            c = a + segments
            d = b + segments
            faces.append((a, b, c))
            faces.append((b, d, c))
    return positions, faces


def _fan(count: int, closed: bool):
    positions = [(0.0, 0.0, 0.0)] + [(float(i), 1.0, 0.0) for i in range(count + (0 if closed else 1))]
    faces: List[Face] = []
    n = count + (0 if closed else 1)
    for i in range(count):
        faces.append((0, 1 + i, 1 + ((i + 1) % n if closed else i + 1)))
    return positions, faces


def _random_soup(rng: random.Random, triangles: int, points: int):
    """A pseudo-random triangulation over a shared point set: arbitrary, connected by accident."""
    positions = [(rng.uniform(-8, 8), rng.uniform(-8, 8), rng.uniform(-8, 8)) for _ in range(points)]
    faces: List[Face] = []
    seen = set()
    while len(faces) < triangles:
        a, b, c = rng.sample(range(points), 3)
        if frozenset((a, b, c)) in seen:
            continue
        seen.add(frozenset((a, b, c)))
        faces.append((a, b, c))
    return positions, faces


def _sphere(stacks: int, slices: int):
    positions = [(0.0, 1.0, 0.0)]
    for st in range(1, stacks):
        for sl in range(slices):
            positions.append((float(sl), float(st), 0.0))
    positions.append((0.0, -1.0, 0.0))
    bottom = len(positions) - 1
    faces: List[Face] = []
    for sl in range(slices):
        faces.append((0, 1 + sl, 1 + (sl + 1) % slices))
    for st in range(stacks - 2):
        base = 1 + st * slices
        for sl in range(slices):
            a = base + sl
            b = base + (sl + 1) % slices
            faces.append((a, b, a + slices))
            faces.append((b, b + slices, a + slices))
    base = 1 + (stacks - 2) * slices
    for sl in range(slices):
        faces.append((base + sl, bottom, base + (sl + 1) % slices))
    return positions, faces


def build(count: int = 240, seed: int = 20260908) -> generate.Probes:
    rng = random.Random(seed)
    p = generate.Probes()

    shapes = []
    for cols, rows in ((2, 2), (3, 5), (4, 4), (5, 3), (6, 6), (7, 2), (8, 5), (9, 9)):
        shapes.append(("grid%dx%d" % (cols, rows), _grid(cols, rows)))
    for rings, segs in ((2, 5), (3, 7), (4, 4), (5, 9), (6, 3)):
        shapes.append(("cyl%dx%d" % (rings, segs), _cylinder(rings, segs)))
    for n in (3, 5, 7, 8, 9, 11, 14, 17, 23):
        shapes.append(("cfan%d" % n, _fan(n, True)))
        shapes.append(("ofan%d" % n, _fan(n, False)))
    for stacks, slices in ((3, 5), (4, 6), (5, 8), (6, 11)):
        shapes.append(("sph%dx%d" % (stacks, slices), _sphere(stacks, slices)))
    for k, (tris, pts) in enumerate(((9, 7), (17, 11), (26, 13), (33, 19), (48, 21), (60, 30))):
        shapes.append(("soup%d" % k, _random_soup(rng, tris, pts)))

    made = 0
    while made < count:
        for label, (positions, faces) in shapes:
            if made >= count:
                break
            mode = rng.randrange(4)
            faces = list(faces)
            if mode == 1:                       # a random face order
                rng.shuffle(faces)
            elif mode == 2:                     # random corner rotations, order kept
                faces = [tuple(f[r:] + f[:r]) for f, r in
                         ((f, rng.randrange(3)) for f in faces)]
            elif mode == 3:                     # both, and half the triangles reversed
                rng.shuffle(faces)
                faces = [tuple(reversed(f)) if rng.random() < 0.5 else f for f in faces]
            shared = rng.random() < 0.85
            p.add("ho_%s_%d" % (label, made), positions, faces, shared=shared)
            made += 1
    return p


def main() -> int:
    text_path = sys.argv[1] if len(sys.argv) > 1 else "build/xna-sample-sweep/optimize/heldout.txt"
    json_path = sys.argv[2] if len(sys.argv) > 2 else "build/xna-sample-sweep/optimize/heldout.json"
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 240
    probes = build(count)
    generate.write(probes, text_path, json_path)
    print("heldout: %d probes, %d faces total"
          % (len(probes.items), sum(len(i["faces"]) for i in probes.items)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
