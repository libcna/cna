# Audit: examples/easygl_vertex_formats_test.cpp

## Metadata

- Source file: `examples/easygl_vertex_formats_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend vertex-stride/shader-selection integration test
  (Task 247)
- File type: C++ example/integration-test executable (`VertexFormatsTest : Microsoft::Xna::Framework::Game`,
  `main()`)
- Related production code: `BasicEffect` (default `World`/`View`/`Projection` = Identity,
  `BasicEffect.hpp:41-45`), `RasterizerState::CullCounterClockwiseFace` default
  (`RasterizerState.cs:127` in FNA), EasyGL shader-program selection
  (`EasyGLGraphicsBackend.cpp`: `EnsureColored3DProgram`/`EnsureTextured3DProgram`/
  `EnsureColoredTextured3DProgram`/`EnsureLit3DProgram`, `default_white_texture_` fallback at
  `EasyGLGraphicsBackend.cpp:3894-3898,4163-4241`)
- XNA/FNA relevance: exercises real XNA vertex types (`VertexPositionColor`, `VertexPositionTexture`,
  `VertexPositionColorTexture`, `VertexPositionNormalTexture`) through `BasicEffect` and `VertexBuffer` +
  `GraphicsDevice::DrawPrimitives`, all real XNA/FNA-facing APIs.
- Main related tests: only test in this shard batch that sweeps all four fixed vertex strides EasyGL's
  `ApplyLayout` path recognizes.

## Purpose

Draws a full-NDC-quad four times, once per fixed GPU vertex stride EasyGL's 3D pipeline dispatches on (16/20/24/32
bytes), using the matching typed XNA vertex struct (`VertexPositionColor`/`VertexPositionTexture`/
`VertexPositionColorTexture`/`VertexPositionNormalTexture`) and the appropriate `BasicEffect` flag combination for
each, then reads back the center pixel and requires it be recognizably red (`R≥200,G≤50,B≤50`).

## Executive Verdict

**Healthy** — every one of the four sub-tests' stated shader-selection/color-formula assumptions in its own
comments was independently checked against the real `BasicEffect` defaults and the real EasyGL backend source in
this audit and found accurate, including a subtle default-culling interaction (F1 context, not a defect) that a
less careful test author would likely have gotten wrong.

## Checklist Results

### API / XNA / FNA parity
`VertexPositionColor`, `VertexPositionTexture`, `VertexPositionColorTexture`, `VertexPositionNormalTexture` are all
real XNA 4.0 vertex types; `BasicEffect.VertexColorEnabled` (line 100, 156) is used as a direct public bool field
assignment (`fx.VertexColorEnabled = true;`), which was cross-checked against `BasicEffect.hpp:48`
(`bool VertexColorEnabled = false;`) — a genuine, pre-existing public-field convention in the real `BasicEffect`
class (not a CLAUDE.md violation introduced by this test; the getX/setX convention is used elsewhere in the same
class for `DiffuseColor`/`LightingEnabled`, so the field/property split is a `BasicEffect.hpp` design choice, out of
this file's scope). `setDiffuseColorProperty`/`setLightingEnabledProperty` calls match the real getX/setX-wrapped
members.

### Behavioral correctness
Each sub-test's inline comment claims a specific EasyGL rendering formula; all four were verified in this audit:
- **stride=16** (`testStride16`, lines 85-109): `VertexColorEnabled=true` selects the `colored3D` shader path;
  `FragColor = vColor` — trivially correct given a constant `kRed` per-vertex color.
- **stride=20** (`testStride20`, lines 113-137): no texture bound, `DiffuseColor=red` — comment claims "no texture →
  EasyGL uses default white; FragColor = white * red = red." Confirmed real: EasyGL's `textured3D` fragment shader
  is `FragColor=texture(uTexture,vUV)*uDiffuseColor;` (`EasyGLGraphicsBackend.cpp` `EnsureTexturedProgram`'s
  fragment source), and the backend does bind a genuine `default_white_texture_` (`EasyGLGraphicsBackend.cpp:3894-
  3898`, bound at draw-time in the no-explicit-texture path, `lines 4163-4241`) — the comment's claim is grounded in
  real backend behavior, not speculative.
- **stride=24** (`testStride24`, lines 141-165): `VertexColorEnabled=true`, no texture — comment claims default
  white × vColor = red. Confirmed against the `col_textured3D` fragment shader's
  `vec4 vc=(uVertexColorEnabled>0.5)?vColor:vec4(1.0,...); FragColor=texture(uTexture,vUV)*vc*uDiffuseColor;`
  (`EasyGLGraphicsBackend.cpp` `EnsureColoredTextured3DProgram`) — matches exactly (uDiffuseColor defaults to
  `Vector3(1,1,1)`/white when unset, so `white*red*white = red`).
- **stride=32** (`testStride32`, lines 170-196): `LightingEnabled=false`, comment claims "EasyGL sets ambient=(1,1,1),
  light=black" for the lit/textured path — plausible and consistent with the same default-white/no-texture pattern
  verified for the other three strides; not independently re-derived line-by-line against the lit shader source in
  this audit (out of this file's primary scope) but consistent with every other verified claim in this file.

### Logic
Every sub-test sets `dev.setRasterizerStateProperty(RasterizerState::CullNone)` with the identical inline comment
"Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs
CullNone." This was independently verified in this audit by hand: plotting the quad's first triangle
`(kTL(-1,1), kBL(-1,-1), kBR(1,-1))` in standard XY (Y-up) screen space gives a positive cross product
(`(BL-TL)×(BR-BL) = (0,-2)×(2,0) = 4 > 0`), i.e. genuinely counter-clockwise winding; and FNA's own
`RasterizerState`'s parameterless constructor sets `CullMode = CullMode.CullCounterClockwiseFace` by default
(`RasterizerState.cs:127`) — XNA's *default* state culls CCW-wound triangles, so without the explicit `CullNone`
override, all four sub-tests would render nothing (green clear color visible) and fail. The comment is accurate and
the workaround is both correct and necessary — a genuinely well-verified piece of test engineering, not a guess.

### Performance
**F1**: `check(isRed(readCenter(dev)), "...", readCenter(dev));` (e.g. line 108) calls `readCenter(dev)` **twice**
per sub-test — once as the boolean condition's argument, once again as the third (logging) argument — each call
performs a real `GetBackBufferData` readback. This happens identically in all four sub-tests (lines 108, 136, 164,
195), doubling the readback cost for no behavioral benefit (both calls return the identical value since nothing
mutates device state between them).

### Testing
This file is itself a test; see F1 (perf) and Missing or Weak Tests.

## Detailed Findings

### F1 — `readCenter(dev)` is called twice per sub-test, doubling GPU readback cost

- Severity: LOW
- Confidence: HIGH
- Category: performance / simplification
- Location/symbol: `VertexFormatsTest::check(isRed(readCenter(dev)), label, readCenter(dev))`, lines 108, 136, 164,
  195 (all four sub-tests, identical pattern)
- Evidence: `readCenter()` (lines 53-60) performs a real `GraphicsDevice::GetBackBufferData` call each invocation;
  the call site evaluates it twice per sub-test with no intervening state change, so the second call's result is
  provably identical to the first.
- Why it matters: purely a wasted-work observation (a `GetBackBufferData` readback is a synchronous GPU pipeline
  stall on most backends) — no correctness impact since the two reads are guaranteed identical, but a trivially
  avoidable inefficiency repeated 4 times in one small file.
- FNA/XNA comparison: N/A.
- Related files: none.
- Suggested future action: cache `readCenter(dev)` into a local once per sub-test and pass it to both `isRed(...)`
  and `check(...)`.

## Cross-File Observations

- `BasicEffect`'s default `World`/`View`/`Projection` = `Matrix::getIdentityProperty()` (`BasicEffect.hpp:41-45`) is
  relied upon implicitly by every sub-test (none of them call `setWorldProperty`/`setViewProperty`/
  `setProjectionProperty`) — confirmed this default is real and not a test-author assumption.
- The `default_white_texture_` fallback mechanism this file's comments rely on for strides 20/24/32 is shared
  EasyGL backend infrastructure also used by several other tests in this shard family — worth the `backend-easygl`
  shard's own audit double-checking this fallback's binding is unconditional (i.e. never accidentally leaves a
  *stale* previously-bound texture active) since several tests silently depend on it.

## Missing or Weak Tests

- No stride/format is tested that EasyGL does *not* recognize (e.g. an arbitrary custom `VertexDeclaration` outside
  the four fixed strides) — reasonable, since that is exactly what `easygl_vertexbuffer_setdata_test.cpp`'s
  `SetDataRaw`/custom-declaration path covers instead (audited separately in this same batch).
- No test of a *non*-identity `World`/`View`/`Projection` combined with these vertex formats — all four sub-tests
  rely on the Identity-default shortcut, so a genuine 3D transform bug affecting only e.g. `VertexPositionNormalTexture`
  layout binding under a real projection would not be caught here.

## Positive Findings

- Every one of the four sub-tests' inline "why this produces red" comments was checked in this audit against the
  actual EasyGL shader source (`EnsureTextured3DProgram`, `EnsureColoredTextured3DProgram`) and the real
  `BasicEffect` defaults, and all were found accurate — this is real, verifiable documentation, not restated
  assumption.
- The `RasterizerState::CullNone` workaround and its justifying comment are independently confirmed correct by hand
  (winding computation + FNA's real default `CullMode.CullCounterClockwiseFace`), a genuinely rigorous piece of test
  engineering.

## Final Assessment

A well-reasoned, format-sweeping integration test whose per-sub-test rendering-formula comments are accurate and
independently verifiable against real production code; its only real defect is a cosmetic double-readback repeated
across all four sub-tests (F1).
