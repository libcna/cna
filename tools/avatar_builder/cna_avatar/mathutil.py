# SPDX-License-Identifier: MS-PL
"""Small vector/quaternion helpers on plain tuples (no numpy, so output is reproducible)."""
import math


def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def lerp(a, b, t): return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def normalize(a):
    n = length(a)
    return (0.0, 0.0, 0.0) if n == 0 else (a[0] / n, a[1] / n, a[2] / n)


def clamp(x, lo, hi): return lo if x < lo else hi if x > hi else x


def smoothstep(e0, e1, x):
    t = clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def frame_from_axis(axis, hint=(0.0, 0.0, 1.0)):
    """Orthonormal (u, v, axis) frame; u/v span the plane perpendicular to axis."""
    w = normalize(axis)
    if abs(dot(w, normalize(hint))) > 0.95:
        hint = (1.0, 0.0, 0.0)
    u = normalize(cross(hint, w))
    v = cross(w, u)
    return u, v, w


def quat_axis_angle(axis, degrees):
    a = normalize(axis)
    h = math.radians(degrees) * 0.5
    s = math.sin(h)
    return (a[0] * s, a[1] * s, a[2] * s, math.cos(h))


def quat_mul(a, b):
    """Hamilton product a*b (apply b first, then a) on (x, y, z, w)."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def quat_normalize(q):
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)


def quat_euler(x=0.0, y=0.0, z=0.0):
    """Rotation about X, then Y, then Z (degrees, fixed model axes)."""
    q = quat_axis_angle((1, 0, 0), x)
    q = quat_mul(quat_axis_angle((0, 1, 0), y), q)
    return quat_normalize(quat_mul(quat_axis_angle((0, 0, 1), z), q))


def quat_rotate(q, v):
    x, y, z, w = q
    t = cross((x, y, z), v)
    t = (2 * t[0], 2 * t[1], 2 * t[2])
    c = cross((x, y, z), t)
    return (v[0] + w * t[0] + c[0], v[1] + w * t[1] + c[1], v[2] + w * t[2] + c[2])


def quantize(value, step=1e-5):
    """Round to a fixed grid so libm last-bit differences cannot change the written bytes."""
    return round(value / step) * step
