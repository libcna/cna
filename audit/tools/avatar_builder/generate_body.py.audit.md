# Audit: tools/avatar_builder/generate_body.py

## Metadata
- Source file: `tools/avatar_builder/generate_body.py` (286 lines)
- Audit status: AUDITED (full read) — **priority target for this fork's "infinite slab" bone-weight
  investigation (see project persistent memory)**
- Subsystem: `tools-avatar-builder` shard
- File type: Python script (Blender `bpy`)
- XNA/FNA relevance: content-generation tooling for CNA's avatar rendering system, not XNA API surface
- Main related tests: self-contained `assert`-based `__main__` check

## Purpose
Builds the original (pre-Phase-7) procedural body mesh from per-bone cylinder+joint-sphere
primitives, joined via `bpy.ops.object.join()`, then applies automatic (heat-map) vertex weights
and `fix_automatic_weights()` — the shared weight-correction function every other garment/hair
generator in this batch also calls.

## Executive Verdict — HIGH-PRIORITY FINDING: the "infinite slab" bone-weight defect is CONFIRMED
## ALREADY FIXED, with the fix, its root cause, and its measured symptom all directly documented
## in this file's own source
This file's `fix_automatic_weights()` (lines 109-208) is exactly where this project's persistent
memory's "infinite slab" defect (a joint weighted to one bone incorrectly picking up an entire
other body part's transform, e.g. Pants weighted to Shoulders) originated and was fixed — confirmed
by direct reading, not inference. The bug and its fix are documented inline (lines 182-194):

```python
for v in mesh.vertices:
    offset = obj.matrix_world @ v.co - joint_pos
    signed_dist = offset.dot(axis)
    if abs(signed_dist) > blend_radius:
        continue
    # audit_net.md remediation (2026-07-18, fifth round): the axial test alone selects an
    # INFINITE SLAB perpendicular to the bone axis, so it forced parent/child weights onto
    # vertices arbitrarily far from the joint sideways. For a laterally-pointing arm bone
    # that slab sweeps straight down through the torso, hips and legs - which is how
    # CNAAvatarPants ended up weighted to Shoulder.L/Shoulder.R (measured: 108 and 107
    # vertices, via tools/avatar_builder/diagnose_avatar_mesh.py's `weights` check). ...
    # Gate on perpendicular distance too, turning the region into a bounded cylinder around
    # the joint axis instead of a slab.
    perpendicular = (offset - axis * signed_dist).length
    if perpendicular > blend_radius:
        continue
    t = max(0.0, min(1.0, (signed_dist / blend_radius + 1.0) * 0.5))
    t = t * t * (3.0 - 2.0 * t)  # smoothstep
    parent_group.add([v.index], 1.0 - t, "REPLACE")
    child_group.add([v.index], t, "REPLACE")
```

**Root cause, confirmed by direct reading**: the original (buggy) version of this loop tested only
`abs(signed_dist) > blend_radius` (distance along the bone's own axis from the joint) before
forcing parent/child weight blending onto a vertex. For a bend joint on a laterally-pointing bone
(e.g. an arm's shoulder joint, whose local axis points sideways/outward from the spine), that
axial-only test describes an *infinite slab* perpendicular to the axis — extending forever in the
two directions perpendicular to the bone, i.e. straight through the torso, hips, and legs for a
shoulder joint. Any vertex on any OTHER body part that happened to fall within `blend_radius` of
the joint's position along that one axis (regardless of how far away it was in the other two
dimensions) got spuriously blended toward the shoulder/arm bones. This is confirmed as the exact,
measured cause of `CNAAvatarPants` (which has vertices near the Hips, spatially close to the
Spine1/Shoulder joint along one axis) ending up weighted to `Shoulder.L`/`Shoulder.R` (108/107
vertices, measured via `diagnose_avatar_mesh.py`'s `weights` check) — those spurious weights made
hip/leg geometry follow the shoulders during arm animations like `Wave`, visibly deforming the
pants away from the body they're meant to cover.

**The fix, confirmed correct**: adding the `perpendicular = (offset - axis * signed_dist).length`
computation and gating on `perpendicular > blend_radius` too turns the weighted region from an
infinite slab into a genuinely bounded cylinder around the joint axis — this is the standard,
correct way to decompose a 3D offset into axial and radial (perpendicular) components relative to
a line, and the fix is applied consistently: both the axial and perpendicular checks use the same
`blend_radius`, so only vertices within a cylinder of radius `blend_radius` and half-length
`blend_radius` around the joint position, oriented along the child bone's own axis, are affected.
This is a **correct, complete, currently-active fix** for the described defect, not a partial or
regressed one.

**Consumers confirmed to inherit the fix, not bypass it**: `generate_body_meshcraft.py` (the
production body path since Phase 7 — see that file's own audit report), `generate_clothes.py`, and
`generate_clothes_meshcraft.py` (the production clothes path) all call this exact
`fix_automatic_weights()` function (imported, not reimplemented) for their own weight-blending, so
none of them could have silently reintroduced the infinite-slab bug via a separate, un-fixed
implementation.

## Checklist Results
- `BEND_JOINTS` (lines 100-106) is a deliberately curated list of (parent, child) bend-joint pairs,
  with its own inline history of two prior additions found necessary (Hips/Spine + Spine/Spine1
  after a torso-bend animation revealed the same tear class; LowerLeg/Foot after the audit found
  the ankle was the single darkest, most pose-independent artifact of everything reported) — a
  real, iteratively-hardened list, not a fixed guess.
- The zero-weight-vertex fix (lines 150-164) correctly assigns each such vertex to its nearest bone
  *segment* (point-to-segment distance, not just nearest bone head) via `_closest_point_on_segment()`
  — a geometrically correct nearest-point-on-line-segment computation (clamps the projection
  parameter `t` to `[0,1]`).
- The `__main__` block's independent re-verification (lines 278-285) — re-checking zero-weight and
  over-4-influence counts directly from `body_obj.data.vertices` rather than trusting
  `fix_automatic_weights()`'s own return value — is a genuinely good defensive-testing practice
  (matches this file's own comment: "independently re-check ... rather than trusting its own
  return value").

## Detailed Findings
None beyond the (already-fixed, historical) infinite-slab defect documented above — included as the
Executive Verdict's primary finding since it directly answers this fork's priority investigation,
not because it represents a currently-active bug.

## Cross-File Observations
The exact same `fix_automatic_weights()` function (and therefore the same infinite-slab fix) is
reused by `generate_clothes.py`/`generate_clothes_meshcraft.py`/`generate_hair.py` — confirmed via
direct import (`generate_body.fix_automatic_weights(...)`) in each, not a separate, potentially
divergent reimplementation.

## Missing or Weak Tests
The `__main__` self-check verifies zero-weight/over-limit counts but does not itself re-verify the
absence of cross-body-part weight bleeding (the specific infinite-slab symptom) — that verification
lives in the separate `diagnose_avatar_mesh.py` tool (audited separately in this batch), not as an
automated assertion in this file's own `__main__` block. A future hardening could add an assertion
here that no bone's vertex-weight influence set includes a bone from a disjoint body region, to
catch a regression of this specific class automatically rather than relying on manual
`diagnose_avatar_mesh.py` runs.

## Positive Findings
This is one of the clearest, most valuable pieces of self-documented bug history found in this
entire audit — the exact defect, its measured symptom (with specific vertex counts), its root
cause (in precise geometric terms), and its fix are all stated in one place, directly in the code
that was actually changed.

## Final Assessment
No currently-active findings. The "infinite slab" bone-weight-blend defect this fork was tasked
with investigating is CONFIRMED to have been a real, previously-existing bug in this exact file,
and is CONFIRMED currently fixed via the `perpendicular`-distance gate added in
`fix_automatic_weights()`.
