"""Pull a model `.xnb`'s vertex and index buffers out whole, for XNASWEEP-149.

`tools/xnb/xnb_conformance.py` digests a buffer rather than returning it, because a conformance
report wants a fingerprint.  Answering "is XNA's face order what `D3DXOptimizeFaces` answers for
CNA's" needs the bytes, so this wraps the same parser and keeps them.
"""

from __future__ import annotations

import os
import sys
from typing import Dict, List

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "xnb"))

import xnb_conformance as X


def _patch():
    """Keep the payload beside the digest, without changing the conformance report's shape."""
    original = X.Reader.read_object if hasattr(X.Reader, "read_object") else None
    digest = X._digest

    def keeping(data):
        keeping.last = data
        return digest(data)

    keeping.last = b""
    return keeping


def load(path: str) -> Dict:
    """The parsed file, with `payload` added to every vertex and index buffer."""
    payloads: List[bytes] = []
    real_digest = X._digest

    def digest(data):
        payloads.append(data)
        return real_digest(data)

    X._digest = digest
    try:
        report = X.parse(path)
    finally:
        X._digest = real_digest
    index = 0
    for resource in report.get("sharedResources", []):
        if resource.get("reader") in ("Microsoft.Xna.Framework.Content.VertexBufferReader",
                                      "Microsoft.Xna.Framework.Content.IndexBufferReader"):
            if index < len(payloads):
                resource["payload"] = payloads[index]
            index += 1
    return report


def triangles(resource: Dict) -> List[tuple]:
    """An index buffer's triangles, as tuples of vertex indices."""
    payload = resource["payload"]
    width = resource["indexElementSize"]
    order = "little"
    values = [int.from_bytes(payload[i:i + width], order)
              for i in range(0, len(payload), width)]
    return [tuple(values[i:i + 3]) for i in range(0, len(values), 3)]


def vertices(resource: Dict) -> List[bytes]:
    """A vertex buffer's vertices, as raw stride-sized records."""
    payload = resource["payload"]
    stride = resource["declaration"]["stride"]
    return [payload[i:i + stride] for i in range(0, len(payload), stride)]
