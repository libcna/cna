# Audit: examples/d3d11_smoke_test.cpp

## Metadata
- Source file: `examples/d3d11_smoke_test.cpp` (3601 lines; representative sampling — full header
  comment (Checks A-AA, lines 1-292), skinned3d/DX-135/DX-137/DX-150 section (lines 1846-2158),
  fog/NPOT/sampler-slot section (lines 2586-2730), tail Clear*-variant/depth-stencil section and
  final `totalChecks` tally (lines 3480-3601), plus targeted `grep` sweeps for `SpriteFont`/
  `SpriteEffects` usage across the whole file)
- Audit status: AUDITED (representative sampling of this exceptionally large, single-file backend
  smoke test)
- Subsystem: `examples-tests-d3d11` shard
- File type: standalone real-GPU integration-test executable (`Game` subclass, Wine+DXVK)
- XNA/FNA relevance: exercises the full public `GraphicsDevice`/`SpriteBatch`/`SpriteFont`/Effects
  API surface against the D3D11 backend's real device/shader/buffer/texture/render-target pipeline

## Purpose
The single largest and most comprehensive integration test in the D3D11 backend: device/swap-chain
creation, vertex/index buffers, input layouts, 2D/cube/3D textures, render targets (incl. MSAA and
mip-chain generation), samplers (all 16 slots simultaneously), occlusion queries (both completion
and visible-vs-occluded discrimination), blend/depth-stencil/rasterizer state, real triangle draws
across colored/textured/lit/alpha-test/dual-texture/env-map/skinned/instanced shader variants,
custom `ShaderEffect`/`Effect` draws, `SpriteBatch` (rotation/origin/scale/crop-rect/address-mode/
custom-effect), backbuffer resize, fog on/off, per-light diffuse/specular discrimination, and
`SpriteFont` glyph placement/spacing/newline/flip — around 90 individually-labeled checks.

## Executive Verdict
Exceptionally rigorous test engineering, consistent with the general quality bar this audit has
found throughout the D3D-family backends. Nearly every check is byte/pixel-exact rather than
"didn't throw," and several checks go out of their way to rule out false positives (e.g. DX-135's
weightsPerVertex discrimination deliberately picks a probe point and bone-weight combination where
a bug would produce a *different* observable result, not just "no crash"; DX-142's 16-simultaneous-
sampler-slot test re-queries every slot after all 16 are bound specifically to catch an
off-by-one/aliasing bug that a single-slot-at-a-time test would miss). One MEDIUM test-design gap
identified below regarding the fog tests' World/View matrices.

## Checklist Results
- Check D-Z (device/buffers/textures/render-targets/samplers/occlusion/MRT/state caches/first
  triangle/textured/lit/alpha-test/dual-texture/env-map/skinned/instanced/custom-shader/
  SpriteBatch/address-mode/custom-effect) are each documented with a precise "proven this far,
  honestly, and no further" scope boundary where full behavioral coverage would require more
  machinery than this file provides (e.g. Check N's MRT proof is explicitly bounded to "Clear()
  clears both targets" without a real multi-target-writing shader).
- DX-135's WeightsPerVertex discriminator (lines 1897-1958) is an unusually well-reasoned piece of
  test design: its own comment documents a real, non-obvious math property found empirically while
  writing the test (a single bone's own weight magnitude cancels out via the homogeneous divide,
  so `weightsPerVertex=1` is insensitive to the weight value) and picks a genuinely different
  two-bone scenario (`bone0=Identity` blended with `bone1=Scale(0.1)`) specifically because it's the
  only case that actually discriminates.
- DX-150's directional-light isolation (lines 2010-2072, `qp1`/`qp1off`/`qp2`) includes a
  deliberate "off" control (`qp1off`, light1Diffuse zeroed) to confirm the preceding "on" result
  wasn't a leaked/pre-existing default — the same anti-false-positive discipline found in
  `DX-124`'s multi-light tests elsewhere in this file.
- `SpriteEffects` usage across the whole file (`grep`-confirmed) only ever uses `None`,
  `FlipHorizontally`, or `FlipVertically` individually — never the combined 4th value
  (`FlipHorizontally|FlipVertically`) — so this file does not exercise the confirmed HIGH
  `DrawString()` axis-direction-lookup-table out-of-bounds-stack-read bug (see Cross-File
  Observations).
- The DX-127 `SpriteFont` test (lines 3122-3231) constructs its `SpriteFont` directly from an
  explicit `chars`/glyph-bounds list it controls, rather than exercising the
  `MeasureString()`/`DrawString()` default-character fallback path with a `defaultCharacter` that
  is deliberately absent from the character set — so it does not exercise the confirmed HIGH
  invalid-iterator-dereference bug either (see Cross-File Observations).

## Detailed Findings

### MEDIUM — The fog on/off tests (Check AC/DX-69/DX-81, DX-137's 7 variants, DX-150's directional-light tests) all use `World=View=Projection=Identity`, so they cannot distinguish a correct view-space-Z fog implementation from the already-confirmed EasyGL object-space-only fog bug
Every fog-related draw call sampled in this file (e.g. lines 2615-2616, 2628-2629, and the
skinned3d fog test at lines 1989-1990/2000-2001) passes `Matrix::getIdentityProperty()` for World,
View, *and* Projection. With all three identity, a vertex's object-space Z, view-space Z, and
clip-space Z are numerically identical — so a fog implementation that (correctly) computes its fog
factor from view-space depth and one that (incorrectly, matching the already-confirmed
`feedback_easygl_fog_object_space_only` cross-cutting finding: "fog shader reads raw local vertex Z,
ignores World/View entirely") reads raw object-space Z instead would both produce the exact same
pixel result in every check in this file. This test suite proves fog *blends to the correct color at
the correct object-space Z*, but does not prove *which space* that Z is drawn from — a real,
non-obvious test-design gap directly analogous to the confirmed EasyGL defect. Whether D3D11's own
fog shader has the same object-space-only bug is not determined by this file and would need to be
checked directly against the D3D11 HLSL fog formula (already audited separately in the
`backend-d3d11`/`backend-d3dcommon` shards) or a new test using a non-identity World/View transform
that moves the same vertex to a different distance from the camera than its raw local Z would
suggest.

## Cross-File Observations
- Directly relevant to two standing, already-confirmed HIGH findings in `SpriteFont.cpp`/
  `SpriteBatch.cpp`: (1) the `defaultCharacter`-not-in-character-set invalid-iterator bug, and (2)
  the `SpriteEffects` 4th-combined-value out-of-bounds axis-lookup bug. Neither is exercised by this
  file, per the Checklist Results above — this test does not corroborate either bug, but also does
  not contradict them; it simply doesn't reach those code paths.
- The fog-test World/View-identity gap identified above is the same shape of blind spot as the
  already-confirmed `feedback_easygl_fog_object_space_only` finding (fog reads raw local Z, ignoring
  World/View) — worth flagging to whoever owns the cross-cutting findings doc as a place to check
  whether D3D11 shares that defect, since this test suite as currently written cannot tell either
  way.
- DX-140's NPOT (5x3) texture test explicitly notes this is "the first NPOT texture this suite has
  ever created against D3D11" — a genuine, disclosed historical coverage gap being closed in this
  same file.

## Missing or Weak Tests
The fog on/off tests' lack of a non-identity World/View transform (see Detailed Findings) is the
one identified gap. Everything else sampled is comprehensively covered with precise, discriminating
assertions.

## Positive Findings
This file's discipline around "proven this far, honestly, and no further" scope statements (Checks
N, O, and others) is a consistently valuable pattern throughout this codebase's test suites — it
prevents a reader from over-trusting a smoke test's coverage boundary. DX-135's and DX-142's
test designs (both described above) are genuinely sophisticated pieces of engineering that go out of
their way to construct scenarios where a real bug would produce an observably different result,
rather than settling for "the call didn't throw."

## Final Assessment
One MEDIUM finding: the fog tests' use of identity World/View/Projection matrices throughout means
this suite cannot distinguish correct view-space fog from the same object-space-only bug already
confirmed in EasyGL — worth a follow-up check against D3D11's actual fog shader implementation.
Otherwise, an exceptionally rigorous, byte/pixel-exact smoke test with no other findings in the
sampled portion.
