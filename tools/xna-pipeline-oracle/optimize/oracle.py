"""Read the recorded genuine-XNA answers for the `OptimizeForCache` probes.

The graphics oracle prints a mesh as one line; the two fields this campaign needs are
`positionIndices=[...]`, the vertex-to-position map after the reorder, and `indices=[...]`, the
index buffer.  Triangle `t` of the answer is therefore
`(positionIndices[indices[3t]], positionIndices[indices[3t+1]], positionIndices[indices[3t+2]])`,
a triple of *position* indices directly comparable with the probe's input faces.  XNASWEEP-149.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

Face = Tuple[int, int, int]

DEFAULT_ORACLE = "tests/reference/xna40/graphics/graphics-content-oracle.json"

_POSITION_INDICES = re.compile(r"positionIndices=\[([0-9,\s]*)\]")
_INDICES = re.compile(r"\bindices=\[([0-9,\s]*)\]")


@dataclass(frozen=True)
class Answer:
    """One recorded answer, decoded."""

    case: str
    position_indices: Tuple[int, ...]
    index_buffer: Tuple[int, ...]
    faces: Tuple[Face, ...]

    @property
    def face_count(self) -> int:
        return len(self.faces)


def _ints(text: str) -> List[int]:
    text = text.strip()
    if not text:
        return []
    return [int(part) for part in text.split(",")]


def decode(case: str, result: str) -> Optional[Answer]:
    position_match = _POSITION_INDICES.search(result)
    if position_match is None:
        return None
    rest = result[position_match.end():]
    index_match = _INDICES.search(rest)
    if index_match is None:
        return None
    position_indices = _ints(position_match.group(1))
    index_buffer = _ints(index_match.group(1))
    faces = tuple(
        (position_indices[index_buffer[i]],
         position_indices[index_buffer[i + 1]],
         position_indices[index_buffer[i + 2]])
        for i in range(0, len(index_buffer), 3))
    return Answer(case, tuple(position_indices), tuple(index_buffer), faces)


def load(path: str = DEFAULT_ORACLE) -> Dict[str, Answer]:
    document = json.load(open(path))
    out: Dict[str, Answer] = {}
    for record in document["cases"]:
        case = record.get("case")
        result = record.get("result")
        if not case or not result or "optimize" not in case:
            continue
        answer = decode(case, result)
        if answer is not None:
            out[case] = answer
    return out


def face_order(answer: Answer, inputs: Sequence[Face]) -> Optional[List[int]]:
    """The answer as a permutation of the input face list, or None where the faces do not match.

    A face is matched by its *corner set*, because the walk is free to rotate a triangle; the
    rotation itself is kept separately by `face_rotation`.
    """
    remaining: Dict[frozenset, List[int]] = {}
    for i, face in enumerate(inputs):
        remaining.setdefault(frozenset(face), []).append(i)
    order: List[int] = []
    for face in answer.faces:
        key = frozenset(face)
        bucket = remaining.get(key)
        if not bucket:
            return None
        order.append(bucket.pop(0))
    return order


def face_rotation(answer_face: Face, input_face: Face) -> Optional[int]:
    """How far the emitted triangle is rotated from the input's, or None if it is reversed."""
    for r in range(3):
        if (input_face[r], input_face[(r + 1) % 3], input_face[(r + 2) % 3]) == answer_face:
            return r
    return None
