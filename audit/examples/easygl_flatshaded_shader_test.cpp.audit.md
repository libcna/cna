# Audit: examples/easygl_flatshaded_shader_test.cpp

## Metadata

- Source file: `examples/easygl_flatshaded_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest via `cmake/Tests/EasyGLTests.cmake:361`
  (`cna_test_easygl_flatshaded_shader`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect`
  (`include/.../ShaderEffect.hpp`, `src/.../ShaderEffect.cpp`), `Effect`/`ContentManager::Load` (the
  `.cnj` custom-shader content pipeline), `GraphicsDevice::DrawIndexedPrimitives`/`GetBackBufferData`.
- XNA/FNA relevance: shader-conversion proof for FNA's `VertexLightingSample_4_0/VertexLighting/
  Content/FlatShaded.fx` (technique `FlatShaded`) — not a `Microsoft::Xna`-namespace type itself
  (`ShaderEffect` is `NOXNA`), but the World/View/Projection semantics it exercises are XNA-facing
  (`Effect`/`IEffectMatrices`).
- Main related tests: this is itself an integration test; no separate unit test targets
  `ShaderEffect`'s GLSL-compile/uniform path directly other than the many other
  `easygl_*_shader_test.cpp` siblings referenced in the header comment.

## Purpose

`EasyGLFlatShadedTest` is a shader-conversion proof: it hand-writes GLSL translated 1:1 from FNA's
`FlatShaded.fx` (`SimpleVertexShader`/`SimplePixelShader`) into a temp-directory `.cnj` content
descriptor, loads it through the real content pipeline (`ContentManager::Load<std::shared_ptr<Effect>>`),
and verifies that `World`/`View`/`Projection` uniforms genuinely drive vertex position rather than
being silently ignored. Correct placement — a backend-integration example, not XNA API source.

## Executive Verdict

**Healthy.** The test correctly isolates the one thing this trivial shader can prove (transform
plumbing) given a pixel shader that is a hardcoded constant, and its own Check A/B design (on-screen
vs. off-screen quad) is a valid, non-trivial way to prove that. One `LOW`-severity portability
observation about the hardcoded 50-unit offscreen assumption is worth recording.

## Checklist Results

### API / XNA / FNA parity
Uses `ShaderEffect::setWorldProperty`/`setViewProperty`/`setProjectionProperty` (from `IEffectMatrices`,
matching FNA's `Effect`-derived `World`/`View`/`Projection` XNA properties) and `Apply()` — correct
XNA-style names. `dynamic_cast<ShaderEffect*>(fxBase_.get())` and `IsEffectValid()` are `NOXNA`
extensions specific to CNA's non-XNA custom-shader path (correctly not XNA-namespace API surface).
The GLSL itself (lines 77-95) is a faithful, line-by-line port of the quoted FNA HLSL (lines 10-16 of
the file's own header comment): `mul(mul(world, view), projection)` in row-vector HLSL convention
becomes `Projection * View * World * vec4(aPosition,1.0)` in GLSL's column-vector convention — the
standard, correct transpose-order flip this project's other shader tests use consistently (verified
against `easygl_instancedmodel_shader_test.cpp`'s own explicit statement of the same identity).

### Behavioral correctness
`Draw()` (line 174) runs exactly once (`done_` latch), computes two colors via `DrawOnce()`, and
exits — normal single-shot example-test shape. `DrawOnce()` correctly re-clears
(`Color(10,10,10,255)`), re-applies state (`SetDepthTestEnabled(false)`, `BlendState::Opaque`,
`RasterizerState::CullNone`), and re-applies the effect's matrices before each of the two draws, so
Check A and Check B are independent, not order-dependent. `IsEffectValid()` guard (line 180) correctly
fails loudly (prints `[FAIL]`, `Exit()`) if the `.cnj`/GLSL load fails, rather than silently drawing
garbage and reporting a false pass/fail.

### Logic
Check A tolerance (`>= 250` per channel, line 190) and Check B tolerance (`<= 15` per channel, line
191) are asymmetric on purpose and correctly so: Check A's expected value is opaque white
`(255,255,255)` from a hardcoded fragment shader (no blending/AA to soften it, so a tight `>=250`
bound is appropriate), Check B's expected value is the clear color `(10,10,10)` (allowing `<=15` for
any driver-level clear-color rounding). No off-by-one or inverted-comparison bugs found.

### Memory/resource lifetime
`vb_`/`ib_` (`std::unique_ptr<VertexBuffer>`/`std::unique_ptr<IndexBuffer>`) and `fxBase_`
(`std::shared_ptr<Effect>`) are constructed once in `Initialize()` and implicitly destroyed via the
`Game` subclass's own destruction — no manual `Dispose()`/double-free risk in this file itself.
Temp-directory content (`root`) is created via `std::filesystem::create_directories` but never
cleaned up after the test exits (matches the same convention used by every other `.cnj`-generating
example test in this shard — not a defect specific to this file).

### C++ correctness
`dynamic_cast<ShaderEffect*>(fxBase_.get())` (lines 156, 179) is checked for null via `!fx` before
dereference in `Draw()` (line 180) — but **not** re-checked inside `DrawOnce()` (line 156): `DrawOnce()`
is only ever called from `Draw()` after the `fx`/`IsEffectValid()` guard has already passed, so this is
not a live null-deref risk today, but `DrawOnce()`'s own local `dynamic_cast` result is used
unconditionally on the very next line with no local guard — a latent fragility if `DrawOnce()` is ever
called from a different call site. `LOW` severity, `MEDIUM` confidence (no reproducing call path
found; a structural observation, not a confirmed defect).

### Performance
N/A — this is a single-shot test, not a hot-path production file. Two draws per process lifetime.

### Thread safety
N/A — single-threaded `Game` loop, matching the rest of this shard.

### Architecture
Correctly layered: the test only talks to `Microsoft::Xna::Framework::Graphics` public API plus the
one `NOXNA` extension point (`ShaderEffect`) the CNA content pipeline is built around — no direct
backend (`EasyGLGraphicsBackend`) symbols referenced from this file, which is the correct boundary for
an `examples/` integration test (it proves the abstraction works, not the backend internals).

### Maintainability
212 lines, single responsibility, no dead code, no `TODO`/`FIXME`. The header comment (lines 1-38) is
thorough and traceable (task number, FNA file quoted verbatim, explicit statement of what is and isn't
being tested) — a genuine strength, not padding.

### Portability
**F1**: Check B relies on `Matrix::CreateTranslation(50.0f, 0.0f, 0.0f)` moving the quad "comfortably
outside the view frustum," justified in the header comment as "confirmed empirically, not just
assumed" (line 34). This is correct math for the specific camera setup used here (distance 3, `PiOver4`
vertical FOV, viewport aspect ratio whatever the default backbuffer resolves to) — at 50 world units of
lateral offset vs. ~3 units of depth, the quad is off-frustum by roughly an order of magnitude of
margin, so this is not fragile in practice. Still, the claim is validated against a fixed default
window/backbuffer size and FOV; if either default (`GraphicsDeviceManager`'s default preferred back
buffer dimensions or aspect ratio) were changed elsewhere in the codebase, this test's implicit
assumption would need re-verification. `LOW` severity, `LOW` confidence (no evidence of an actual
regression, just a structural fragility worth flagging for the "cross-cutting: default-resolution
assumptions" note other similar tests in this shard share).

### Robustness
`IsEffectValid()` guard (discussed above) correctly turns a `.cnj`/GLSL compile failure into a `[FAIL]`
exit rather than an unchecked null-pointer crash or a false pass. No other external-input handling is
relevant (all shader/content-descriptor text is inline, compiled into the binary, not user-supplied).

### Testing
This file *is* a test; there is no separate unit test of its own. As an integration test it correctly
covers the "does a hand-authored `.cnj`-loaded GLSL effect actually receive World/View/Projection and
apply them to vertex position" scenario end-to-end (content pipeline → `ShaderEffect` → EasyGL backend
→ real GPU rasterization → readback) — genuinely more than a "compiles and doesn't crash" check, since
Check B specifically catches a regression where `World` is accepted but ignored (the quad would then
still cover the center pixel and both checks would spuriously agree, but Check A's own pass/fail
depends on Check B's independent confirmation that translation is honored, and the two together do
distinguish "geometry moved" from "some other coincidental match").

### Cross-file consistency
Reuses the same `VertexPositionNormalTexture` (stride 32) vertex layout as sibling shader tests in this
shard for consistency, per the header comment — verified: `Normal`/`TexCoord` are declared at GLSL
locations 1/2 implicitly via the vertex layout but genuinely unread by `kVertSrc` (line 79 only
declares `layout(location = 0) in vec3 aPosition;`), matching the comment's own claim that `FlatShaded.fx`'s
real XNA vertex declaration only reads the position stream.

## Detailed Findings

### F1 — Off-screen assumption for Check B depends on unstated default viewport/backbuffer size

- Severity: LOW
- Confidence: LOW
- Category: portability / test fragility
- Location/symbol: `DrawOnce()` (line 144-172), specifically the `Matrix::CreatePerspectiveFieldOfView`
  call using `vp.getAspectRatioProperty()` (line 161) with no explicit `GraphicsDeviceManager`
  back-buffer size set anywhere in this file (unlike several sibling tests, e.g.
  `easygl_instancedmodel_shader_test.cpp`, which explicitly set a 64x64 backbuffer).
- Evidence: no `GraphicsDeviceManager` is even constructed in this file (`Game`'s own default device is
  used implicitly), so the aspect ratio and default resolution are whatever `Game`'s built-in default
  presentation parameters resolve to — not pinned down in this file itself.
- Why it matters: a future change to `Game`'s default back-buffer size/aspect ratio (or to
  `MathHelper::PiOver4`) could, in principle, shrink the effective off-frustum margin; at a 50-unit
  lateral offset vs. ~3 units of camera distance the margin is large enough that this is very unlikely
  to actually break, so this is recorded as a structural note rather than a live risk.
- FNA/XNA comparison: N/A — CNA test-authoring convention, not an FNA behavior question.
- Suggested future action (not implemented by this audit): none required; noted for completeness only.

## Cross-File Observations

- Shares the "off-screen via a translation large enough to clear the frustum with margin" test pattern
  with several sibling shader tests in this shard; none of them pin down an explicit backbuffer size,
  which is a minor, low-risk convention worth a single cross-cutting note in `AUDIT_CROSS_CUTTING_FINDINGS.md`
  rather than a per-file fix.

## Missing or Weak Tests

None beyond what's already covered — this file thoroughly exercises the one behavior its own header
comment claims to test (transform plumbing through a custom GLSL effect), and explicitly does not
overclaim coverage of pixel-shader/lighting correctness (the header comment itself states "this test
can't verify any lighting formula").

## Positive Findings

- Exceptionally clear, load-bearing header comment: quotes the actual FNA HLSL source verbatim, states
  the exact behavior under test and why the trivial pixel shader limits what can be verified, and gives
  the concrete Check A/B rationale including why the specific translation magnitude was chosen.
- Genuine end-to-end verification (content pipeline → GLSL compile → GPU raster → readback), not a
  "compiles and doesn't crash" style smoke test.

## Final Assessment

A well-targeted, honestly-scoped integration test that verifies exactly the one thing its trivial
shader can prove (World/View/Projection reaching vertex position) via two independently meaningful
checks, with only a minor, low-risk portability observation (F1) and a latent structural fragility
(unchecked `dynamic_cast` reuse in `DrawOnce()`) worth a mention but not a fix.
