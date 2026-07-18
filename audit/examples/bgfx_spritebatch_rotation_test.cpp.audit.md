# Audit: examples/bgfx_spritebatch_rotation_test.cpp

## Metadata

- Source file: `examples/bgfx_spritebatch_rotation_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SpriteBatch::Draw`'s rotation-around-`origin` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_spritebatch_rotation …)` /
  `cna_register_backend_test(NAME Bgfx_SpriteBatch_Rotation …)`, `cmake/Tests/BgfxTests.cmake:765-767`).
- XNA/FNA relevance: direct — `SpriteBatch.Draw`'s `rotation`/`origin` parameters.
- FNA reference: `Graphics/SpriteBatch.cs`, `GenerateVertexInfo`/corner-computation block
  (`SpriteBatch.cs:1346-1398`): `cornerX = -originX*destinationW` (etc. for the other 3 corners), then
  `X' = destinationX + cornerX*cos(rotation) - cornerY*sin(rotation)`, `Y' = destinationY + cornerX*sin(rotation)
  + cornerY*cos(rotation)`, where FNA's `originX`/`originY` are pre-normalized to
  `origin.X / sourceRectangle.Width` (`SpriteBatch.cs:636-637`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`SubmitSprite`, lines 1421-1529 — corner computation at 1468-1490).

## Purpose

3-check pixel test (Task 804, a Bgfx port of Task 417's EasyGL/Vulkan test) proving that `SpriteBatch::Draw`'s
rotation genuinely pivots around the caller-specified `origin` point, not e.g. always around the destination
rectangle's top-left corner or center. A 100×100 texture (20×20 Red marker top-left, rest Blue) is drawn at
`destinationRectangle=(200,150,100,100)`, `origin=(100,100)` (the texture's own bottom-right corner — diagonally
opposite the marker), rotated 90° (`MathHelper.PiOver2`). The file hand-derives that the marker's centre
(source point (10,10)) must land at screen (290,60) after rotation — nowhere near its unrotated position
(110,60) nor the origin/pivot point itself (200,150), which must stay fixed under any rotation.

## Executive Verdict

**Healthy** — this audit independently re-derived `BgfxGraphicsBackend::SubmitSprite`'s corner/rotation formula
from source and confirmed it is algebraically identical to FNA's real `GenerateVertexInfo` formula (not just
superficially similar), and that the test's own hand-derived expected screen point (290,60) for the marker
follows correctly from that formula.

## Checklist Results

### API / XNA / FNA parity
`Draw(texture, destRect, srcRect, color, rotation, origin, effects, layerDepth)` — signature and semantics
(origin expressed in *source-rectangle* pixel space, matching FNA's own `origin.X / sourceRectangle.Width`
convention) match FNA exactly.

### Behavioral correctness
Derived `SubmitSprite`'s formula independently from source (lines 1443-1490):
```
u1 = srcX/texW, u2 = (srcX+srcW)/texW  (and v1/v2 similarly)         // texcoords, unaffected by rotation
scaleX = destW/srcW, scaleY = destH/srcH
p0 = ((0 - origin.X)*scaleX, (0 - origin.Y)*scaleY)                   // corner for source point (0,0)
p1 = ((srcW - origin.X)*scaleX, (0 - origin.Y)*scaleY)                // (srcW,0)
p2 = ((srcW - origin.X)*scaleX, (srcH - origin.Y)*scaleY)             // (srcW,srcH)
p3 = ((0 - origin.X)*scaleX, (srcH - origin.Y)*scaleY)                // (0,srcH)
v.x = destX + p.x*cos(rotation) - p.y*sin(rotation)
v.y = destY + p.x*sin(rotation) + p.y*cos(rotation)
```
Substituting FNA's own `originX_FNA = origin.X / sourceRectangle.Width` into FNA's `cornerX = -originX_FNA *
destinationW = -(origin.X/srcW)*destW = -origin.X*scaleX` reproduces `p0.x` above **exactly** — the two
implementations are the same formula expressed in different (but equivalent) units (CNA works directly in
source-pixel space rather than FNA's normalized-fraction space, cancelling out identically). The
rotate-and-translate step (`v.x = destX + p.x*cosR - p.y*sinR`) is likewise identical to FNA's
`(-rotationSin*cornerY) + (rotationCos*cornerX) + destinationX`.
- Applied this confirmed-correct formula to the test's own scene: `origin=(100,100)`, `srcRect=(0,0,100,100)`
  (so `scaleX=scaleY=100/100=1`), `destRect=(200,150,100,100)`, `rotation=PiOver2` (`sin=1,cos=0`). For the
  marker's centre in source space (10,10): `p.x = (10-100)*1 = -90`, `p.y = (10-100)*1 = -90`; rotated:
  `v.x = 200 + (-90)*0 - (-90)*1 = 290`, `v.y = 150 + (-90)*1 + (-90)*0 = 60` — **exactly** reproducing the
  file's own claimed (290,60), confirmed independently rather than merely trusting the comment's arithmetic.
- The pivot invariant: substituting the origin's own source point (100,100) gives `p.x=p.y=0` for every
  rotation, so `v = (destX, destY) = (200,150)` unconditionally — confirming check design intent (the origin
  point is invariant, though this specific invariant is not itself one of the 3 sampled checks; see Missing
  Tests).
- Checks 2/3 (interior-but-off-marker → Blue; far outside the rotated footprint → clear color) are
  straightforward containment checks against the rotated AABC footprint `x:[200,300], y:[50,150]` (a 90°
  rotation of a square destination rect about its own centre-ish region keeps it axis-aligned) — verified
  (250,100) is inside, (50,50) is well outside.

### Robustness
`colourMatch`'s `tol=60` cannot conflate Red/Blue/clear-green (`kClear=(0,255,0)`) given their large pairwise
channel differences — no accidental-pass risk from tolerance overlap (the specific failure mode this audit's
brief was primed to look for, per the sibling EasyGL specular-test finding).

### Testing
The 3 checks correctly discriminate "rotation is a no-op" (would place the marker at (110,60) or leave it at
the origin (200,150), not (290,60)) from "rotation is applied but pivots around the wrong point" (e.g. pivoting
around the destination rect's own top-left would place the marker elsewhere again) — a check at the origin
point itself confirming its own invariance under rotation is the one additional assertion that would make this
airtight (see Missing Tests).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM defects found in this file.

### F1 — The origin point's own rotation-invariance is not directly asserted
- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `checks[]`, lines 137-141
- Evidence: none of the 3 check points is `(200,150)` (the origin/pivot point itself, which per the formula
  always maps to `(destinationX, destinationY)` regardless of rotation) or a point designed to distinguish
  "pivots around the given origin" from "pivots around the destination rectangle's centre" (which, for this
  specific 100×100 square destination and a 90° rotation, could coincidentally produce a similar-looking
  rotated footprint for some check points).
- Why it matters: the marker-position check (290,60) is a strong, specific assertion that already rules out
  "no rotation" and "wrong origin" for this particular scene, so the practical risk is low; but a direct check
  at the pivot point itself would make the "genuinely pivots around the caller's `origin`" claim in the file's
  own stated purpose fully self-contained without relying on the reader's trust in the geometry argument.
- FNA/XNA comparison: N/A (test-design suggestion, not a behavior question).
- Suggested future action (not implemented by this audit): add a 4th check sampling near (200,150) confirming
  it renders whatever color the sprite's own origin corner is (the texture's bottom-right pixel, which is Blue)
  regardless of the applied rotation.

## Missing or Weak Tests

See F1.

## Positive Findings

- The rotation/origin formula was independently re-derived from the actual CNA source and shown to be
  algebraically identical to FNA's `SpriteBatch.cs` formula, not merely "looks similar" — this is exactly the
  kind of parity check the audit brief calls for on FNA-facing behavior.
- The test's own hand-derivation in its header comment was independently reproduced and found to be numerically
  correct against the current production code (no stale-comment issue, unlike the pattern flagged in a sibling
  EasyGL specular test).

## Final Assessment

A rigorous, correctly-derived rotation/origin pivot test; production math and test expectations agree exactly,
with only a minor, easy-to-add coverage refinement (a direct pivot-invariance check) left on the table.
