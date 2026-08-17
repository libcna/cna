# SPDX-License-Identifier: MS-PL
"""Readable diffs for the binary L5 goldens (`GLTF-410`).

The L5 goldens are the bytes CNA hands to ``VertexBuffer::SetData``/``IndexBuffer::SetData``. That
is exactly why they catch what no higher layer can — a field written at the wrong offset, a stride
selected wrongly, a colour channel packed in the wrong order — and exactly why a change to one is
unreviewable as a diff: ``git diff`` reports "Binary files differ" and stops.

Anyone reviewing such a change is then choosing between taking it on trust and decoding 144 bytes by
hand, and the first of those is what makes a golden stop being evidence.

This module decodes both sides using the fixture's **own** ``.expected.json`` layout — the stride,
and each field's name, offset and size — and prints the differences as
``vertex 2 TextureCoordinate.y: 0.25 -> 0.75``. It is deliberately not a general binary differ: the
layout comes from the manifest the golden was generated with, so a diff that cannot be explained
this way means the stride itself changed, which the header line says outright.
"""

from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Any, Iterator

#: How many differing components to print before summarising the rest. A stride change makes every
#: byte differ, and pasting 144 lines into a review is the same as pasting none.
MAX_REPORTED = 24

#: Component names, by how many components a field has. A four-component field is XYZW rather than
#: RGBA even for `Color`, because the golden's own field name already says which it is and inventing
#: a second naming scheme per field would make two rows of the same diff read differently.
_COMPONENT_NAMES = ["x", "y", "z", "w"]


def _decode_field(data: bytes, offset: int, size: int) -> list[float] | list[int]:
    """Decodes one field of a vertex, given its byte size.

    Size 4 is CNA's packed byte quadruple (``Color``, ``BlendIndices``); anything else is a run of
    32-bit floats. That is the whole vertex ABI (plan_gltf.md §2.3) -- no field uses any other form.
    """
    chunk = data[offset:offset + size]
    if len(chunk) < size:
        return []
    if size == 4:
        return list(chunk)
    return list(struct.unpack("<" + "f" * (size // 4), chunk))


def _format(value: float | int) -> str:
    if isinstance(value, int):
        return str(value)
    text = f"{value:.6f}".rstrip("0").rstrip(".")
    return text if text not in ("", "-") else "0"


def _vertex_differences(part: dict[str, Any], old: bytes, new: bytes) -> Iterator[str]:
    stride = int(part["stride"])
    fields = part.get("fields", [])
    count = max(len(old), len(new)) // stride if stride else 0
    for vertex in range(count):
        base = vertex * stride
        for field in fields:
            name = str(field["name"])
            offset = int(field["offset"])
            size = int(field["size"])
            before = _decode_field(old, base + offset, size)
            after = _decode_field(new, base + offset, size)
            if before == after:
                continue
            if not before or not after:
                yield f"  vertex {vertex} {name}: present on one side only"
                continue
            for c, (b, a) in enumerate(zip(before, after)):
                if b == a:
                    continue
                component = _COMPONENT_NAMES[c] if c < len(_COMPONENT_NAMES) else str(c)
                yield f"  vertex {vertex} {name}.{component}: {_format(b)} -> {_format(a)}"


def _index_differences(part: dict[str, Any], old: bytes, new: bytes) -> Iterator[str]:
    element = int(part.get("indexElementSize", 2))
    code = "<H" if element == 2 else "<I"
    count = max(len(old), len(new)) // element
    for i in range(count):
        at = i * element
        before = struct.unpack(code, old[at:at + element])[0] if at + element <= len(old) else None
        after = struct.unpack(code, new[at:at + element])[0] if at + element <= len(new) else None
        if before == after:
            continue
        yield f"  index {i}: {before} -> {after}"


def explain(golden: Path, previous: bytes) -> list[str]:
    """Explains how @p golden differs from the @p previous bytes of the same file.

    :param golden: Path to the golden as it now is, inside the corpus directory.
    :param previous: The bytes it is being compared against (usually the committed version).
    :return: Human-readable lines. Empty when the two are identical.
    """
    current = golden.read_bytes()
    if current == previous:
        return []

    name = golden.name
    fixture = name.split(".")[0]
    expectation = golden.parent / f"{fixture}.expected.json"
    header = f"{name}: {len(previous)} bytes -> {len(current)} bytes"
    if not expectation.is_file():
        return [header, "  (no .expected.json beside it, so the layout is unknown)"]

    l5 = json.loads(expectation.read_text()).get("l5", {})
    parts = l5.get("parts", [])
    part = None
    for candidate in parts:
        if candidate.get("vertexBufferFile") == name or candidate.get("indexBufferFile") == name:
            part = candidate
            break
    if part is None:
        return [header, "  (this file is not named by the fixture's own L5 expectation)"]

    lines = [header]
    if name.endswith(".ib.bin"):
        lines.append(f"  index element size {part.get('indexElementSize')} bytes")
        differences = list(_index_differences(part, previous, current))
    else:
        stride = int(part["stride"])
        lines.append("  stride {} -- {}".format(
            stride, ", ".join(f"{f['name']}@{f['offset']}+{f['size']}" for f in part.get("fields", []))))
        if len(previous) % stride != 0 or len(current) % stride != 0:
            lines.append("  one side is not a whole number of vertices at this stride, so the "
                         "STRIDE itself changed -- review the layout decision, not the values")
            return lines
        differences = list(_vertex_differences(part, previous, current))

    if not differences:
        lines.append("  bytes differ but every decoded field matches -- padding or trailing bytes")
        return lines
    lines.extend(differences[:MAX_REPORTED])
    if len(differences) > MAX_REPORTED:
        lines.append(f"  ... and {len(differences) - MAX_REPORTED} more differing component(s)")
    return lines
