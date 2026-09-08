"""Load the generated probes and the genuine-XNA answers they were measured against."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

Face = Tuple[int, int, int]

DEFAULT_PROBES = "build/xna-sample-sweep/optimize/probes.json"
DEFAULT_ANSWERS = "build/xna-sample-sweep/optimize/answers.txt"


@dataclass
class Case:
    name: str
    shared: bool
    positions: Tuple[Tuple[float, float, float], ...]
    faces: Tuple[Face, ...]
    position_indices: Tuple[int, ...]
    index_buffer: Tuple[int, ...]
    answer_faces: Tuple[Face, ...]
    order: Optional[List[int]]
    rotations: Optional[List[Optional[int]]]


def _order_of(answer_faces, inputs):
    remaining: Dict[frozenset, List[int]] = {}
    for i, face in enumerate(inputs):
        remaining.setdefault(frozenset(face), []).append(i)
    order = []
    for face in answer_faces:
        bucket = remaining.get(frozenset(face))
        if not bucket:
            return None
        order.append(bucket.pop(0))
    return order


def _rotation(answer_face, input_face):
    for r in range(3):
        if (input_face[r], input_face[(r + 1) % 3], input_face[(r + 2) % 3]) == tuple(answer_face):
            return r
    return None


def load(probes_path: str = DEFAULT_PROBES, answers_path: str = DEFAULT_ANSWERS) -> Dict[str, Case]:
    probes = {p["name"]: p for p in json.load(open(probes_path))["probes"]}
    out: Dict[str, Case] = {}
    for line in open(answers_path):
        line = line.strip()
        if not line:
            continue
        name, _, rest = line.partition("|")
        if rest.startswith("ERROR"):
            continue
        positions_text, _, indices_text = rest.partition("|")
        position_indices = [int(v) for v in positions_text.split(",") if v != ""]
        index_buffer = [int(v) for v in indices_text.split(",") if v != ""]
        probe = probes[name]
        faces = tuple(tuple(f) for f in probe["faces"])
        answer_faces = tuple(
            (position_indices[index_buffer[i]], position_indices[index_buffer[i + 1]],
             position_indices[index_buffer[i + 2]])
            for i in range(0, len(index_buffer), 3))
        order = _order_of(answer_faces, faces)
        rotations = ([_rotation(answer_faces[i], faces[order[i]]) for i in range(len(order))]
                     if order is not None else None)
        out[name] = Case(name, probe["shared"], tuple(map(tuple, probe["positions"])), faces,
                         tuple(position_indices), tuple(index_buffer), answer_faces, order,
                         rotations)
    return out
