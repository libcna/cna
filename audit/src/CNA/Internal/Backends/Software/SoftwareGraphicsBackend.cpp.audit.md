# Audit: src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-software` shard
- File type: C++ implementation (1403 lines) — a genuine from-scratch CPU triangle rasterizer
- Related header/implementation: `include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp` (audited
  separately, same shard)
- XNA/FNA relevance: implements real (not bookkeeping-only, unlike Headless) rendering semantics for
  `GraphicsDevice`/`Effect`/`SpriteBatch` — the rasterization/blend/depth math needs to match XNA/FNA's documented
  defaults and D3D conventions closely enough to be a trustworthy pixel-level reference.
- Graphics backend relevance: one of the 14 confirmed backends; per its own header doc, deliberately the "genuinely
  correct pixels, no GPU" alternative to Headless's "bookkeeping fiction."
- FNA reference: cross-checked conceptually against FNA's `SpriteBatch.cs`/`BasicEffect.cs`/
  `EnvironmentMapEffect.cs`/`DepthStencilState.cs` semantics for the specific claims below (default cull mode,
  `Skin()` bone-blend step, `PSEnvMap` formula, `DepthStencilState.Default`'s `LessEqual` compare function).
- Main related tests: `examples-tests-software` (6 files) and `src/Microsoft/Xna/Framework/Graphics/`'s own tests
  — not yet audited at time of writing; F1/F2 below are prime candidates to check for existing coverage.

## Purpose

Implements a complete, real CPU rasterization pipeline: clip-space transform, Sutherland-Hodgman near-plane
clipping, perspective-correct barycentric interpolation, depth testing, bilinear texture sampling, a simplified
Opaque/AlphaBlend compositing choice, and NOXNA extensions for `EnvironmentMapEffect` (cube-map reflection) and
`SkinnedEffect` (bone blending) — everything needed to make `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/
`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/`SpriteBatch::Draw` produce genuinely correct pixels without any GPU.
This is a substantially more ambitious file than the other single-file backends (Headless, Dx3, SdlRenderer,
WebGPU) audited so far — it is real 3D-graphics-engine code, not a thin adapter.

## Executive Verdict

**Needs attention** on two specific, well-evidenced, undocumented fidelity gaps in the depth-test path (F1: depth
*write* is unconditional regardless of `DepthBufferWriteEnable`; F2: the depth *compare function* is hardcoded to
a LessEqual-equivalent test, ignoring `DepthStencilState.DepthBufferFunction` entirely) — both silent, both
plausible to hit in a real game/test that relies on either technique. Everything else in this file — the
rasterizer core, perspective-correct interpolation, near-plane clipping, texture sampling, skinning, environment
mapping — is carefully reasoned, well-commented, and (per its own comments) empirically corrected at least once
already (the bilinear-sampling clamp-edge fix, see Positive Findings). This is a case of "mostly excellent work
with two specific, fixable holes," not a systemically weak file.

## Checklist Results

### API / XNA / FNA parity

**F1, F2 (Detailed Findings)** are the two substantive parity gaps found. Everything else checked against FNA
matches:
- `ShouldCullTriangle`'s default (`cullMode==2`, `CullCounterClockwiseFace`) culling positive-area triangles is
  explicitly cross-referenced against real XNA/FNA's own default `RasterizerState.CullCounterClockwise`
  (header comment, `SoftwareGraphicsBackend::cullMode_`'s default-value comment, lines 329-335) — correct default.
- `SampleCubeMap`'s face-selection/UV convention (largest-magnitude axis, sign picks +/-, `s,t` from the other two
  axes) matches the standard D3D/OpenGL cube-map convention FNA itself relies on.
- The `EnvironmentMapEffect` blend formula (lines 598-634) mirrors FNA's `PSEnvMap`/`PSEnvMapSpecular` HLSL
  (reflection vector via `2*dot(N,E)*N - E`, Fresnel-weighted blend factor when enabled) closely, with the explicit,
  documented simplification of omitting the per-light diffuse sum (consistent with "design decision 6: no lighting
  engine in v1").
- `BuildGenericClipVertex`'s skinning path (lines 385-413) mirrors FNA's `Skin(vin, boneCount)` — blends up to
  `weightsPerVertex` (1/2/4) bone matrices by `BlendWeight`, applies to position/normal before World*View*Projection
  — correctly reflects the "only sum the first N pairs" behavior the code comment attributes to Task 895.

### Behavioral correctness

`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` all validate
`primitiveCount > 0`, `primitive == TriangleList` (throwing for any other topology — a deliberate, honest v1 scope
trim per the comment at lines 1150-1152, "other PrimitiveType values throw rather than silently misrendering" —
this is the *correct* choice, unlike Headless's `PrimitiveVertexCount` silently returning 0 for an unhandled type,
see that report's F3), and buffer-capacity bounds before touching any vertex/index data.

`ClipTriangleNearPlane`'s Sutherland-Hodgman near-plane clip (lines 117-136) is a single-half-space clip against
`w > kNearEpsilon`, correctly preserving winding order (iterates edges in order, appends the clipped
intersection point before the still-inside vertex when a boundary is crossed) — verified the 0/3/4-output-vertex
cases are all handled by both draw loops (`if (clippedCount == 0) continue;` / always draws `[0,1,2]` / additionally
draws `[0,2,3]` when `clippedCount==4`, a standard convex-quad fan triangulation, valid here since a single-plane
clip of a triangle is always convex).

### Logic

**F1/F2** are logic gaps in `ApplyDepthStencilState` (see Detailed Findings). Beyond those: `ApplyBlendState`'s
Opaque-vs-AlphaBlend detection (lines 1084-1093, `Blend::One=0`/`Blend::Zero=1` → Opaque) is explicitly
cross-referenced against `EasyGLGraphicsBackend::ApplyBlendState`'s "own exact... formula" — a good example of
intentional, disclosed, cross-backend-consistent simplification (design decision 7), not an oversight.

`SampleBilinear`'s clamp-then-recompute-x1/y1-from-raw-index logic (lines 190-199) has a code comment describing a
*real bug this exact pattern was written to avoid* ("a real bug caught by Software_Effects' own corner-sampling
check") — i.e. this is evidence the file has already been through at least one round of genuine bug-fixing, and
the fix is correctly implemented (`x1`/`y1` computed from `x0raw+1`/`y0raw+1`, clamped independently, not from the
already-clamped `x0`/`y0`).

### Memory/resource lifetime

No dynamic/native resource ownership beyond `std::vector`-backed pixel/depth arrays, all correctly sized in
`SoftwareFramebuffer::Resize` and consistently accessed via `pixelIndex`/`colorIndex` computed the same way
throughout (checked every read/write site in `RasterizeTriangle`/`RasterizeTriangleShaded`/`ReadBackbuffer`/
`ClearColor` — no indexing convention mismatch found).

### C++ correctness

`BuildPositionColorClipVertex`/`BuildGenericClipVertex` both `std::memcpy` a `Vector3`/`float` directly out of a
`const std::uint8_t*` raw vertex buffer at hardcoded byte offsets — correct, standard technique for stride-based
vertex layout inference, and consistent with the file's own documented convention (stride 16/20/24/32/52 each
mapped to a specific, commented byte layout). `readIndex` lambdas (in both indexed-draw methods) correctly
`memcpy` either a `uint16_t` or `uint32_t` based on `IsThirtyTwoBit()`, avoiding unaligned-read UB via `memcpy`
rather than a reinterpret-cast dereference.

### Performance

This is a scalar (non-SIMD), per-pixel bounding-box rasterizer with a `dynamic_cast` per triangle for
`texture0`/`texture1`/`envMap` (lines 505-507) — reasonable for a testing/CI backend whose entire purpose (per its
own header) is correctness over speed; not flagged as a performance defect given the stated design goal.

### Thread safety

N/A — no shared mutable state accessed by more than one thread in this codebase's usage pattern (consistent with
every other backend audited so far).

### Architecture

Clean separation between the anonymous-namespace rasterizer core (transform/clip/rasterize/shade free functions)
and the `IGraphicsBackend`-implementing class methods that drive them — a good internal layering choice that
keeps the actual rasterization math testable/readable independent of the backend-interface plumbing around it.

### Maintainability

1403 lines is large but proportionate to genuinely implementing a real rasterizer (clipping + perspective-correct
interpolation + texturing + two NOXNA effect extensions) from scratch — not padding. Comments consistently explain
*why*, not just *what* (e.g. the bilinear-sampling bug-avoidance comment, the reflect-vector derivation, the
dual-texture same-UV limitation with its Vulkan-precedent citation).

### Portability

N/A — no platform-specific code.

### Robustness

**F1/F2** aside, argument validation is thorough and consistently throws `std::runtime_error` with a specific,
actionable message for every misuse case checked (stride mismatch, capacity overrun, null texture with the
corresponding `*Enabled` flag set, unsupported topology, unsupported stride) — a genuinely defensive, well-designed
validation surface.

### Testing

Not independently assessed (queued for `examples-tests-software`) — but F1/F2 are concrete, easily-testable claims
(render two overlapping quads with `DepthBufferWriteEnable=false` on the far one and check the near one still
depth-tests against the *original* depth value; or set `DepthBufferFunction=Greater` and confirm inverted painter's
order) that a real test suite should be checked against when that shard is audited.

## Detailed Findings

### F1 — `DepthBufferWriteEnable` (and the standalone `SetDepthWriteEnabled`) have no effect; depth is always written whenever the depth test passes

- Severity: MEDIUM
- Confidence: HIGH
- Category: correctness / FNA parity
- Location/symbol: `SoftwareGraphicsBackend::ApplyDepthStencilState` (`.cpp` lines 1095-1099),
  `SoftwareGraphicsBackend::SetDepthWriteEnabled` (line 1133), `RasterizeTriangle`/`RasterizeTriangleShaded`'s
  unconditional `fb.depthBuffer[pixelIndex] = depth;` (lines 354, 636)
- Evidence: `ApplyDepthStencilState`'s second parameter (`depthWriteEnable`) is unnamed and never stored anywhere;
  `SetDepthWriteEnabled(bool)` is a literal empty function body (`{}`). No member field tracking a "should this
  draw write depth" state exists anywhere in `SoftwareGraphicsBackend` (checked the full class — only
  `depthTestEnabled_`, `blendEnabled_`, `cullMode_` exist as rasterizer-state members). Both rasterizer core
  functions write `fb.depthBuffer[pixelIndex]` unconditionally once a pixel passes the (test-only) depth check.
- Why it matters: `DepthStencilState.DepthBufferWriteEnable = false` (test-but-don't-write) is a standard technique
  — e.g. rendering translucent geometry that should be depth-tested against opaque geometry but not occlude other
  translucent geometry behind it. A game or test relying on this would see later-drawn, further-away translucent
  geometry incorrectly depth-rejected by the earlier translucent draw's depth values, which this backend wrote
  even though it was told not to. Not documented anywhere in this file or in `docs/software-backend.md` (checked —
  no mention of depth-write, `DepthBufferFunction`, or stencil limitations), unlike the blend-state simplification
  which *is* explicitly disclosed as "design decision 7" in multiple comments.
- FNA/XNA comparison: real XNA/FNA `GraphicsDevice` honors `DepthStencilState.DepthBufferWriteEnable` independently
  of `DepthBufferEnable` (the test-enable flag) — this is standard D3D/XNA semantics, not an edge case.
- Related files: `include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp` (would need a new
  `depthWriteEnabled_` member); `docs/software-backend.md` (should document this as either a fixed gap or an
  accepted v1 boundary, matching how the blend-state simplification is documented).
- Suggested future action (not implemented by this audit): add a `depthWriteEnabled_` member, set it from both
  `ApplyDepthStencilState`'s `depthWriteEnable` param and `SetDepthWriteEnabled`, and gate the
  `fb.depthBuffer[pixelIndex] = depth;` writes on it in both rasterizer functions.

### F2 — `DepthStencilState.DepthBufferFunction` (`depthFunc`) is completely ignored; the depth test is hardcoded to a LessEqual-equivalent comparison

- Severity: MEDIUM
- Confidence: HIGH
- Category: correctness / FNA parity
- Location/symbol: `SoftwareGraphicsBackend::ApplyDepthStencilState` (`.cpp` lines 1095-1099, third parameter
  `int` unnamed), `RasterizeTriangle`/`RasterizeTriangleShaded`'s `if (depthTestEnabled && depth >
  fb.depthBuffer[pixelIndex]) continue;` (lines 345, 551)
- Evidence: the depth-function parameter to `ApplyDepthStencilState` is discarded (unnamed `int` in the signature);
  the rasterizer's actual comparison is a fixed `reject if depth > stored` (i.e. accept `<=`), which happens to
  match XNA/FNA's own `DepthStencilState.Default.DepthBufferFunction = CompareFunction.LessEqual` — but any game
  or test that explicitly sets a *different* `CompareFunction` (`Greater`, `GreaterEqual`, `Less`, `Equal`,
  `NotEqual`, `Always`, `Never`) on this backend would silently keep getting LessEqual behavior instead.
- Why it matters: `CompareFunction.Always`/`Never` are commonly used for pass-through or fully-occluded debug/
  effect passes; `Greater`/`GreaterEqual` show up in reverse-Z or specific multi-pass effect techniques. A test
  written to exercise `DepthStencilState`'s `CompareFunction` values (analogous to the
  `easygl_depthstencilstate_compare_function_test.cpp` pattern seen in the EasyGL example shard) would fail or
  silently pass for the wrong reason under this backend. Same non-disclosure issue as F1 — not mentioned in
  `docs/software-backend.md`.
- FNA/XNA comparison: XNA/FNA's `DepthStencilState.DepthBufferFunction` (a `CompareFunction`) is a first-class,
  commonly-varied state; hardcoding one comparison is a real fidelity gap for any of the other 6 documented enum
  values.
- Related files: same as F1.
- Suggested future action (not implemented by this audit): store `depthFunc` and branch the rasterizer's
  comparison on it (a small `switch` mapping `CompareFunction`'s raw ordinal to a comparison operator), or — if
  full parity is out of scope for v1 — explicitly document the LessEqual-only limitation the same way the
  blend-state simplification is documented.

## Cross-File Observations

- Stencil support is entirely absent (`SoftwareFramebuffer` has no stencil array; `ClearStencil` is a no-op;
  `ApplyDepthStencilState`'s stencil-related parameters are all discarded) — likely an intentional v1 boundary
  consistent with "no lighting engine"/"Opaque+AlphaBlend only" trims elsewhere in this file, but unlike those, it
  isn't called out with its own "design decision N" comment anywhere found in this file. Worth a documentation
  pass alongside F1/F2 rather than a separate finding, since the *absence* itself (not a wrong depth default) is
  much more obviously a scope trim a reader would expect from a "v1" software rasterizer.
- `SoftwareSpriteBatchBackend::Draw` (line 962) reads `owner_.GetCullMode()` — the *last* `ApplyRasterizerState`
  value — to decide whether to cull its own quads. This is architecturally sound *if* (and only if) the real
  `Microsoft::Xna::Framework::Graphics::SpriteBatch` C++ implementation actually calls
  `GraphicsDevice::setRasterizerStateProperty` (and hence `IGraphicsBackend::ApplyRasterizerState`) during its own
  `Begin()`, matching FNA's documented default (`RasterizerState.CullCounterClockwise` applied unless the caller
  passes a different one to `Begin()`). Flagged for confirmation when the `xna-graphics` shard's `SpriteBatch.cpp`
  is audited — if that assumption doesn't hold, sprite quads could inherit a stale cull mode from whatever 3D draw
  last ran.
- `DualTextureEffect`'s same-UV-for-both-textures simplification (lines 568-582) explicitly cites "established
  precedent already set by this codebase's own Vulkan dual_texture3d shaders" — worth cross-checking during the
  `backend-vulkan` shard audit whether that precedent is itself documented as a real, shared limitation or was
  itself an unverified assumption being propagated forward.

## Missing or Weak Tests

Given F1/F2 are both live, silent behavioral gaps, the most valuable missing tests would be exactly the
`DepthBufferWriteEnable=false` and non-default `DepthBufferFunction` scenarios described in those findings —
flagging for cross-check against `examples-tests-software` (6 files) once that shard is audited.

## Positive Findings

- The perspective-correct interpolation technique (premultiply color/UV/world-position/normal by `invW` before the
  barycentric lerp, un-premultiply once at the end) is textbook-correct and applied consistently everywhere it's
  needed.
- Near-plane clipping (`ClipTriangleNearPlane`) is implemented at all — many toy/test rasterizers skip this
  entirely and produce garbage for any triangle crossing the camera plane; this one handles it correctly via a
  proper Sutherland-Hodgman single-plane clip.
- The bilinear-sampling clamp-order bug-fix comment (lines 190-199) is genuine evidence of iterative correctness
  work, not just a first-draft implementation — a strong positive signal for this file's overall trustworthiness.
- Every unsupported-topology/stride/argument case throws a specific, actionable `std::runtime_error` rather than
  silently misrendering (explicitly called out as the deliberate choice at lines 1150-1152) — the opposite,
  correct-for-a-testing-backend choice from Headless's `PrimitiveVertexCount` silent-zero gap.

## Final Assessment

A genuinely well-built software rasterizer with careful, evidenced attention to perspective-correctness and
clipping — let down by two specific, concrete, silently-wrong depth-state gaps (F1, F2) that should be either fixed
or explicitly documented as v1 boundaries the same way the file's other simplifications already are.
