# SPDX-License-Identifier: MS-PL
"""Geometry shared by several fixtures.

Fixtures state their own values explicitly wherever the value *is* the thing under test. Only
genuinely incidental geometry -- the carrier triangle whose job is merely to exist so a node
transform or a material has something to apply to -- is shared here, so a change to it cannot
silently mean two different things in two fixtures.
"""

from __future__ import annotations

#: The carrier triangle. One unit along +X and one along +Y, so an axis-aligned transform is
#: readable straight off the expected values.
TRIANGLE_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)]
TRIANGLE_NORMALS = [(0.0, 0.0, 1.0), (0.0, 0.0, 1.0), (0.0, 0.0, 1.0)]
TRIANGLE_INDICES = [0, 1, 2]
TRIANGLE_MIN = [0.0, 0.0, 0.0]
TRIANGLE_MAX = [1.0, 1.0, 0.0]

#: The carrier quad, wound so it is a valid two-triangle strip in vertex order 0,1,2,3.
QUAD_STRIP_POSITIONS = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (1.0, 1.0, 0.0)]
QUAD_STRIP_MIN = [0.0, 0.0, 0.0]
QUAD_STRIP_MAX = [1.0, 1.0, 0.0]
