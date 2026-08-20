# Audit: examples/d3d9_draw_test.cpp

## Metadata

- Source file: `examples/d3d9_draw_test.cpp` (358 lines)
- Audit status: AUDITED (STATIC/SOURCE-READING ONLY — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — this backend's first real 3D triangle
  (`plans/plan_dx9.md` D9-8 / D9-82): `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` and all 4
  `PrimitiveType` values, through the real public `Game`/`GraphicsDeviceManager`/`GraphicsDevice`
  API.
- File type: `Game`-subclass executable (`D3D9DrawTest : public Game`), CTest-registered as
  `D3D9_Draw` (`cmake/Tests/D3D9Tests.cmake:54-57`, `TIMEOUT 60`).
- XNA/FNA relevance: direct — `BasicEffect` `VertexColorEnabled`-only path (ShaderIndex 3,
  `BasicEffect_VSBasicVcNoFog`/`BasicEffect_PSBasicNoFog`), `PrimitiveType.TriangleList/
  TriangleStrip/LineList/LineStrip`, `GraphicsDevice.GetBackBufferData`.
- Related production code: `src/CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.cpp`
  (`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`ToD3D9Topology`, lines 54-64, 654-763),
  `D3D9EffectDraw.cpp`, `D3D9VertexDeclarations.cpp` — all read in full or in the relevant part.

**Environment note (per D-P4/audit instructions):** D3D9 is Windows-only and requires a live device
(via Wine+DXVK per this project's own test infrastructure). No build or execution was attempted in
this Linux sandbox; this report is entirely static-source-reading, cross-checked line-by-line
against the current `D3D9GraphicsBackend.cpp` production implementation.

## Purpose

Six pixel-readback checks proving the "legacy," `BasicEffect`-`VertexColorEnabled`-only draw path
(`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`, stride-16 `VertexPositionColor` only) is
real end-to-end through the actual public API, not merely "didn't crash": (A) non-indexed vertex
color paints over a `Clear()`'d background at a known location; (B) same proof, indexed path;
(C) a `World` translation far outside the NDC cube leaves the background genuinely unpainted,
proving the `WorldViewProj` constant upload is real (not a hardcoded/ignored no-op); (D)
`PrimitiveType.TriangleStrip` with a 4-vertex quad, sampling both the always-covered corner and the
corner covered only by the second triangle, discriminating a `primitiveCount` that silently stayed
wrong from one that resolved correctly; (E) `PrimitiveType.LineList`, two independent segments
proving no unwanted connecting geometry; (F) `PrimitiveType.LineStrip`, a 3-vertex "L" polyline
proving the shared-vertex-connected-segments semantics distinct from LineList's pairwise semantics.

## Executive Verdict

**Healthy** — every check's numeric/geometric claim was independently traced against the current
`D3D9GraphicsBackend.cpp` and matches. One documentation-precision nuance (Check D/F's framing of
"vertex-count↔primitiveCount conversion") is noted below but does not affect correctness of the
check itself.

## Checklist Results

### API / XNA / FNA parity
`PrimitiveType::TriangleList/TriangleStrip/LineList/LineStrip` (used at lines 162, 241, 293, 323)
are the real XNA enum values, and `ToD3D9Topology()` (`D3D9GraphicsBackend.cpp` lines 54-64) maps
all 4 to the correct `D3DPRIMITIVETYPE` (`D3DPT_TRIANGLELIST`/`_TRIANGLESTRIP`/`_LINELIST`/
`_LINESTRIP`) — confirmed by direct reading, a simple, exhaustive, correctly-defaulted `switch`.

### Behavioral correctness
- Checks A/B (lines 152-207): `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` are called
  with `Matrix::getIdentityProperty()` for World/View/Projection and a full-NDC "oversized
  triangle" (`kTri`, vertices at `(-1,-1)`,`(3,-1)`,`(-1,3)` — deliberately extending past the
  `[-1,1]` clip cube on two sides so the entire viewport is covered with no boundary-precision
  ambiguity, the same trick this project's `D3D11_Smoke`'s own Check P established per the file's
  header comment). Confirmed against `D3D9GraphicsBackend.cpp`'s real
  `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (lines 654-763): stride is validated
  (`!= 16` throws), `ShaderIndex 3` is hardcoded (`ComputeBasicEffectShaderIndex(false, true, false,
  false, false, false)`), `WorldViewProj` and `DiffuseColor=white` are uploaded, then
  `DrawPrimitive`/`DrawIndexedPrimitive` is called with the caller's `primitiveCount` passed straight
  through — the vertex color (`0xFF0000FFu`, red under CNA's R,G,B,A ascending-byte convention) is
  the only source of the rendered color since `DiffuseColor` is hardcoded white (a no-op multiply).
- Check C (lines 209-232): `Matrix::CreateTranslation(1000.0f, 0.0f, 0.0f)` as `World` — with the
  same full-NDC triangle geometry, if `WorldViewProj` were a no-op upload (wrong register, or never
  called), the triangle would still cover the full viewport regardless of `World` and this check
  would (correctly) fail. Confirmed the upload is real: `UploadMatrixConstantVS(...,"WorldViewProj",
  world * view * projection)` at `DrawColoredPrimitives` — wait, this specific overload used by
  Check A-C is `DrawColoredPrimitives` (not `DrawPrimitivesEx`), which uploads `WorldViewProj` via
  the same real register-table path (`Shaders::kBasicEffect_VSBasicVcNoFog_Registers`,
  `D3D9GraphicsBackend.cpp` lines 691-696) — confirmed this is a genuine, non-hardcoded upload of
  `world*view*projection`, so translating `World` by 1000 units genuinely moves clip-space geometry
  entirely off-screen. The check is a real discriminator, not a tautology.
- Check D (lines 234-262): `kStrip` (4 vertices in canonical Z-order TL/TR/BL/BR, extended to
  `±2.0` — past the `[-1,1]` cube on all sides) with `PrimitiveType::TriangleStrip,
  primitiveCount=2`. Samples the top-left corner (region `(4,4,4,4)`, covered by triangle 0 alone)
  and the bottom-right corner (region `(56,56,4,4)`, covered only by triangle 1) — both must be red
  for the check to pass. Confirmed `ToD3D9Topology(TriangleStrip) == D3DPT_TRIANGLESTRIP` and that
  `primitiveCount` is passed through to `DrawPrimitive` unmodified (line 707) — D3D9's own
  `DrawPrimitive` API takes a primitive count directly (not a vertex count needing conversion,
  unlike some other backends per this file's own header comment lines 50-53), so this check
  genuinely proves D3D9 (and CNA's pass-through of the caller-supplied count) renders a full
  2-triangle strip, not a degenerate 1-triangle draw.
- Checks E/F (lines 282-333): `kLineList` (two disjoint horizontal segments at y=±0.5, different
  colors — red/green) and `kLineStrip` (a 3-vertex "L," shared middle vertex). Check E's
  "row-between-stays-background" assertion (`betweenRegion` at y≈32, lines 297-304) is the
  specific proof that `LineList` draws disjoint segments rather than being mistakenly treated as one
  connected polyline. Check F's two-leg sampling (`horizontalFound`/`verticalFound`, lines 325-328)
  proves `LineStrip`'s n-1-connected-segments semantics resolved correctly rather than degenerating
  to a single segment. Both are real, non-tautological checks; the file's own comment (lines
  264-269) correctly notes a small search-region + "any pixel matches" tolerance is the right
  robustness margin for 1px-wide line rasterization on a 64×64 canvas, deferring pixel-exact
  proof to the checked-in XNA-oracle scene comparison (`tools/xna-oracle/scenes/
  colored_linelist_quad.scene`/`colored_linestrip_quad.scene`) rather than over-claiming
  pixel-exactness from this offline CTest alone.

### Logic
Minor documentation-precision nuance (not a defect): Check D's own comment (lines 17-27) and Check
F's comment (lines 34-40) both frame their proof as validating "the vertex-count↔primitiveCount
conversion," language evidently carried over from this project's D3D11/Vulkan-family test
comments, where such a backend-internal conversion genuinely exists (this file's own header,
lines 50-53, correctly notes D3D9's `DrawPrimitive`/`DrawIndexedPrimitive` already take a
primitive count directly, with no such conversion needed in this backend). In this test,
`primitiveCount` is a literal (`2`) supplied directly by the *test itself* at the call site (lines
241, 293, 323), not derived internally from a vertex count by any D3D9-backend code path. What the
checks actually and genuinely prove is: (1) `ToD3D9Topology()` maps each `PrimitiveType` to the
correct `D3DPRIMITIVETYPE`, and (2) CNA does not silently coerce/ignore the caller's
`primitiveCount` (e.g. hardcoding `1`) — both real and valuable properties, just not literally a
"conversion" bug class specific to this backend. This is a copy-drift wording issue in the
comments, not a testing gap or a production defect.

### C++ correctness
`VPC` (line 87) is a tightly-packed `{float,float,float,uint32_t}` struct matching stride-16
`VertexPositionColor` exactly (no padding surprises given all 4 members are 4-byte-aligned
4-byte types). `regionContains` lambda (lines 270-280) correctly captures `dev`/`backend` by
reference and performs a linear scan without any out-of-bounds risk (`px` sized exactly `w*h`).

### Memory/resource lifetime
Vertex/index buffers (`backend.CreateVertexBuffer`/`CreateIndexBuffer16`) are held in local
`unique_ptr`s scoped to each check's own block — correctly released before the next check
allocates a new one, no accumulation across the 6 checks.

### Thread safety
N/A — single-threaded `Game::Draw()` override.

### Performance
N/A — a one-shot diagnostic; per-check buffer reallocation (rather than reuse) is a theoretical,
not practical, inefficiency for a test with 6 tiny (3-4 vertex) draws.

### Architecture
Correctly exercises the real public `Game`/`GraphicsDeviceManager`/`GraphicsDevice` API for the
draw/readback path, while reaching into `D3D9GraphicsBackend`-specific `ApplyBlendState`/
`ApplyDepthStencilState`/`ApplyRasterizerState`/`CreateVertexBuffer`/`DrawColoredPrimitives`/
`DrawIndexedColoredPrimitives` directly (via `static_cast<D3D9GraphicsBackend&>(dev.GetBackend())`)
— an intentional, explicitly-backend-specific test (this legacy draw path has no
`Microsoft::Xna`-level entry point of its own; it is CNA backend-internal plumbing), consistent
with this file's stated scope.

### Maintainability
The header comment (lines 1-53) is detailed and specifically documents a real, previously-live bug
this file's own Check A/B readback would have caught (the `D3DDECLTYPE_D3DCOLOR`→
`D3DDECLTYPE_UBYTE4N` R/B-swizzle fix, cross-referenced against `d3d9_common_test.cpp`'s D9-82
check and `D3D9VertexDeclarations.cpp`'s own header) — good cross-file self-documentation.

### Robustness
The `CullMode::None`/opaque-blend/no-depth-stencil baseline (lines 146-148) deliberately avoids
depending on `D9-21`'s "still-open D3DCULL winding proof" per the file's own comment — a reasonable
choice to keep this file's own checks independent of an unrelated, separately-tracked concern.

### Testing
This file is the primary direct test of `D3D9GraphicsBackend::DrawColoredPrimitives`/
`DrawIndexedColoredPrimitives` and `ToD3D9Topology()`'s dispatch for all 4 `PrimitiveType` values.
Coverage is good for this narrow, explicitly-scoped legacy path; the broader effect-aware
`DrawPrimitivesEx` dispatch (BasicEffect/AlphaTestEffect/DualTextureEffect/EnvironmentMapEffect/
SkinnedEffect) is this batch's sibling file's job (`d3d9_drawex_test.cpp`), correctly not
duplicated here.

### Cross-file consistency
`D3D9GraphicsBackend.cpp`'s `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (lines 654-763)
were read in full; both correctly reject `stride != 16` with a clear, named `std::runtime_error`
(matching this file's own scope note, lines 660-663, that "other strides and full effect-aware
dispatch are DrawPrimitivesEx's job, not yet implemented (D9-82b/c)" — this specific claim, unlike
the analogous stale claim found in `d3d9_drawex_test.cpp`'s own header (see that file's report,
F1), is accurate as written here: this file only ever calls the two colored-primitives entry
points, which genuinely remain stride-16-only by design, not because the effect-aware path is
unimplemented).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

## Missing or Weak Tests

- **LOW (confidence MEDIUM):** no check exercises `DrawColoredPrimitives`/
  `DrawIndexedColoredPrimitives` with a non-identity `View`/`Projection` (only `World` is varied, in
  Check C). A regression that swapped the multiplication order of `world*view*projection` (e.g.
  `projection*view*world`) would not necessarily be caught by an identity-View/Projection setup,
  since `Matrix::getIdentityProperty()` is the multiplicative identity regardless of order. This is
  a pre-existing gap shared with several sibling backend tests in this project (not specific to
  this file), and is deferred to the XNA-oracle scene corpus (`tools/xna-oracle/scenes/*`) which
  presumably (not verified in this batch) uses non-trivial camera transforms.

## Positive Findings

- Check D's "sample the corner covered only by the second triangle" design and Check E's
  "sample the row between two segments to prove no connecting geometry" design are both genuinely
  discriminating tests, not just "did anything render" smoke checks — both would catch a real,
  specific, plausible regression class (`primitiveCount` silently clamped/miscounted; `LineList`
  mistakenly dispatched as `LineStrip` or vice versa).
- Consistent, disciplined reuse of the same "oversized full-NDC triangle" trick this project
  established in `D3D11_Smoke`'s Check P, correctly cross-referenced in this file's own comments
  rather than reinvented independently.
- The header comment's own documented real-bug narrative (D3DDECLTYPE R/B swizzle) is corroborated
  by direct reading of `D3D9VertexDeclarations.cpp`'s own, independently-written header comment —
  two files converging on the same historical account strengthens confidence this is a real,
  accurately-recorded finding rather than a retrofitted narrative.

## Final Assessment

A well-constructed, accurate pixel-readback test for D3D9's legacy colored-primitives draw path and
all 4 `PrimitiveType` values. The only issue found is a minor wording carryover ("vertex-count
↔primitiveCount conversion") in two check comments that doesn't match this specific backend's actual
architecture (no such conversion exists in D3D9's `DrawPrimitive` call) — harmless to the test's
correctness, worth a wording tidy-up but not a functional defect.
