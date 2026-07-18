# Audit: examples/easygl_billboard_shader_test.cpp

## Metadata

- Source file: `examples/easygl_billboard_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — HLSL→GLSL shader-conversion proof for the XNA
  `BillboardSample`'s `Billboard.fx` (view-facing billboard expansion + wind sway + alpha-tested
  diffuse lighting)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_billboard_shader …)` /
  `cna_register_backend_test(NAME EasyGL_Billboard_Shader …)`, `cmake/Tests/EasyGLTests.cmake:454-456`).
- XNA/FNA relevance: indirect — this is a **NOXNA** custom-shader (`ShaderEffect`) port of a real XNA
  4.0 sample's own `.fx` content, not a stock `Microsoft::Xna` effect; correctness is judged against
  the original `Billboard.fx` HLSL source (reproduced verbatim in the file's own header comment) and
  CNA's own custom-vertex-layout / `ShaderEffect` extension points, both of which are legitimate NOXNA
  surface per `CLAUDE.md`.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp`/`.cpp`
  (custom GLSL effect + `IEffectMatrices`), `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`ExtractMatrices`), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`BindCustomEffectMatrices()` lines 4507-4524, `DrawIndexedPrimitivesEx`'s `customEffectBackend`
  branch).

## Purpose

Ports `BillboardSample`'s `Billboard.fx` 1:1 to GLSL (embedded as raw string literals, `kVertSrc`/
`kFragSrc`, lines 160-223) and pixel-tests the result: Check A (opaque texture at the billboard's own
screen footprint, expects the lit+textured color), Check B (a sample point outside that footprint,
expects the clear color, proving the view-facing expansion is genuinely bounded), and Check C
(identical to A but with a translucent texture whose alpha fails the alpha test, expects the fragment
to be discarded). This shader's own vertex format (`Position+Normal+TexCoord+Random`, stride 36)
matches none of CNA's 5 built-in strides, so it exercises the custom-vertex-layout capability
(Task 1080) and `ShaderEffect`'s `IEffectMatrices` + `GraphicsDevice::ExtractMatrices()` +
`customEffectBackend` binding path (Task 1079) rather than any stock-effect dispatch.

## Executive Verdict

**Healthy** — the GLSL port is a faithful, line-by-line translation of the HLSL source reproduced in
the header comment, the `_m02_m12_m22`→`transpose(mat3(View))[2]` column-extraction claim is
consistent with the project's established `ToColumnMajor()` upload convention (independently
confirmed against `BindCustomEffectMatrices()`), and this audit's own hand re-derivation of the
expected pixel colors for all 3 checks matches the file's stated expectations.

## Checklist Results

### API / XNA / FNA parity
N/A for `Microsoft::Xna` stock-effect parity (this is a custom `ShaderEffect`, a `NOXNA` extension
point per `ShaderEffect.hpp`'s own class-level Doxygen note). The *original sample content* fidelity
is the relevant correctness bar here, and was checked line-by-line against the HLSL reproduced in the
header comment (lines 9-37) — every vertex-shader statement (squish factor, width/height sign flip,
right-vector cross product, position offset, wind sway, lighting) and the fragment shader's
`tex2D`/`clip()` pair are present in the GLSL with the stated, correct HLSL→GLSL idiom translations
(`clip(x)` → `if (x<0.0) discard;`, matching HLSL's own "discard if operand < 0" semantics exactly).

### Behavioral correctness
Re-derived the test's own worked example independently: anchor `(0,-0.5,0)`, `Normal=(0,1,0)`,
`Random=0.5` (`squishFactor=0.75+0.25=1.0` exactly, so `width=height=BillboardWidth=BillboardHeight=1`
unchanged), camera `eye=(0,0,3)`/`target=origin`. `viewDirection` (camera backward axis) `=(0,0,1)`
(consistent with `normalize(eye-target)` for this particular camera). `rightVector =
normalize(cross((0,0,1),(0,1,0))) = normalize((-1,0,0)) = (-1,0,0)` — matches the comment's derivation
exactly. Re-computed the 4 corner offsets independently and got the identical unit square centred at
the world origin `{(0.5,0.5,0),(-0.5,0.5,0),(-0.5,-0.5,0),(0.5,-0.5,0)}` the comment states (lines
77-80) — confirms the `rightVector*(u-0.5)*width + Normal*(1-v)*height` expansion formula is applied
correctly in the GLSL (`kVertSrc` lines 190-192).
Lighting: `LightDirection=(0,-1,0)` against `Normal=(0,1,0)`: `max(-dot(N,L),0)=max(1,0)=1`;
`Color.rgb=1*(0.4,0.4,0.4)+(0.2,0.2,0.2)=(0.6,0.6,0.6)` — matches the comment exactly, and matches the
GLSL (`kVertSrc` line 205: `float diffuseLight=max(-dot(aNormal,LightDirection),0.0); vColor=
vec4(diffuseLight*LightColor+AmbientColor,1.0);`), a correct, direct translation of the HLSL's
`diffuseLight = max(-dot(input.Normal, LightDirection), 0)` / `output.Color.rgb = diffuseLight *
LightColor + AmbientColor`.
Check A: `Color*texture = (0.6*0.7843,0.6*0.3922,0.6*0.1961) = (0.4706,0.2353,0.1176)`×255 =
`(120,60,30)` — matches the test's own `close(...,120/60/30)` assertions (lines 353-354) and this
audit's independent recomputation.
Check C: texture alpha `128/255≈0.502`; `color.a=1.0*0.502=0.502`; `(0.502-0.95)*1=-0.448<0` →
`discard` — correctly triggers the alpha test's `if ((color.a-AlphaTestThreshold)*AlphaTestDirection
< 0.0) discard;` (`kFragSrc` line 220), matching HLSL's `clip((color.a-AlphaTestThreshold)*
AlphaTestDirection)` (values `<0` discarded) exactly.
`WindAmount=0.0` (line 314) is confirmed to gate the entire wind term to zero
(`wind=sin(...)*WindAmount`, `kVertSrc` line 196), consistent with the header comment's documented,
deliberate scope reduction not to independently verify the wind-sway term's own math.

### Logic
The `View._m02_m12_m22` → `transpose(mat3(View))[2]` claim (comment lines 44-54) was cross-checked
against `BindCustomEffectMatrices()` (`EasyGLGraphicsBackend.cpp` lines 4512-4522):
`view.ToColumnMajor(viewCM); backend.SetUniformMat4("View", viewCM);` — the same column-major upload
convention this session's other shader ports rely on (per the comment's own citation of the
`NormalMapping.fx` port's established convention). For this test's specific rotation-free `LookAt`
camera (`eye=(0,0,3)`, `target=origin`, `up=(0,1,0)`), the comment's own scope note (lines 52-54) that
`transpose()` is a no-op here (not independently exercised) is accurate — this reduces confidence in
the general `transpose(mat3(View))` claim for a *rotated* camera specifically, but does not affect
this file's own 3 checks, which only need the correct answer for its own axis-aligned camera. This is
a legitimate, self-disclosed scope limitation rather than a hidden gap.

### Memory/resource lifetime
`Initialize()` writes 3 files (`bb.vert.glsl`/`bb.frag.glsl`/`bb.cnj`) to a per-instance temp directory
(`std::filesystem::temp_directory_path() / ("cna_billboard_test_" + <this pointer>)`, lines 243-245)
and never removes them on exit (no `Dispose()`/destructor cleanup, no `std::filesystem::remove_all`
anywhere in the file). Each test run leaves 3 small files behind in the OS temp directory. `vb_`/`ib_`
are `std::unique_ptr`s with normal RAII lifetime; `opaqueTex_`/`transparentTex_` are plain `Texture2D`
value members, default-constructed then reassigned via `Texture2D::CreateFromPixels` — consistent
with other tests in this shard that use the same pattern.

### C++ correctness
`BillboardVertex` (lines 150-157) is `#pragma pack(push,1)`-packed with a `static_assert(sizeof(...)
== 36)` (line 158) — correctly guards against padding-induced stride mismatches with the
`VertexDeclaration`'s own hardcoded offsets (0/12/24/32). `reinterpret_cast<std::uintptr_t>(this)`
(line 244) used only to build a unique-ish temp directory name — a reasonable, non-UB use of the cast
for this narrow purpose (not dereferenced, not used as an actual pointer).

### Performance
N/A — a one-shot test, not a hot path; the 20-vertex/4-vertex draw and 3 sequential `DrawOnce()` calls
are negligible.

### Architecture
Correctly demonstrates the intended `ShaderEffect` extension boundary: a fully custom vertex layout
and shader pair driving a real `GraphicsDevice::DrawIndexedPrimitives()` call, with `World`/`View`/
`Projection` uniform binding handled generically by the backend's `customEffectBackend` path rather
than any BasicEffect-specific dispatch — the right way to prove this NOXNA capability without
special-casing it into the stock-effect code paths.

### Robustness
`Draw()` checks `fx->IsEffectValid()` before proceeding (lines 336-341) and fails cleanly with a
`[FAIL]` message + `Exit()` if `.cnj` load or GLSL compile failed, rather than crashing on a null
dereference — correct defensive structure for a test whose entire premise depends on successful
shader compilation.

### Testing
Genuinely exercises 3 independent hypotheses (bounded billboard expansion via check B, correct
lit+textured color via check A, and alpha-test discard via check C) rather than a single "it renders
something" assertion — satisfies the anti-boilerplate bar. The `close(...,±6)` tolerance (line 352)
is tighter than most of this shard's `BasicEffect` tests (`±8`/`±10`), appropriate given all 3 checks'
expected values here have wide separation from any plausible wrong-formula alternative (e.g. check B's
expected clear color `(10,10,10)` vs. check A's lit color `(120,60,30)`).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Per-test-run temp files under the OS temp directory are never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: robustness / resource hygiene
- Location/symbol: `Initialize()` lines 242-254 (`WriteFile(root / "bb.vert.glsl", ...)` etc.); no
  corresponding cleanup anywhere in the file (checked destructor — implicit, and `Draw()`/`Exit()`
  paths — no `std::filesystem::remove_all` call found)
- Evidence: `root` is built once per test-process invocation with a unique
  (`this`-pointer-derived) directory name and never removed; each CI/local test run leaves 3 small
  text files (a `.vert.glsl`, a `.frag.glsl`, a `.cnj` manifest) behind under
  `std::filesystem::temp_directory_path()`.
- Why it matters: harmless in isolation (a few hundred bytes per run), but across a long-lived CI
  machine or repeated local test runs this accumulates unbounded small files in `/tmp` with no
  eventual cleanup — a minor but real resource-hygiene gap, not a correctness defect.
- FNA/XNA comparison: N/A (test-infrastructure concern, not an XNA behavior question).
- Related files: none — purely local to this file's own `Initialize()`.
- Suggested future action (not implemented by this audit): add a destructor or `Unload()`/`Draw()`-exit
  path that calls `std::filesystem::remove_all(root)` once the effect has been loaded (or compilation
  has failed and the files are no longer needed).

## Cross-File Observations

- Confirmed `GraphicsDevice::ExtractMatrices()` (`GraphicsDevice.cpp` line 550) is the mechanism that
  reads back `ShaderEffect::getWorldProperty()`/`getViewProperty()`/`getProjectionProperty()` after
  `fx->Apply()`, and that `BindCustomEffectMatrices()` pushes them by the literal uniform names
  `"World"`/`"View"`/`"Projection"` — this three-hop path (`setViewProperty()` → `ExtractMatrices()`
  → `BindCustomEffectMatrices()`) is not obvious from reading this test file alone (it never calls
  `SetUniformMat4("View", ...)` directly), and is worth documenting explicitly in whatever shard
  audits `ShaderEffect.hpp`/`GraphicsDevice.cpp`'s draw-call path, since a future custom-shader test
  author unfamiliar with this indirection could easily assume `View`/`Projection` need to be pushed
  manually via `SetUniformMat4`.
- This is one of several `easygl_*_shader_test.cpp` files in this shard porting real XNA sample `.fx`
  content via the same `ShaderEffect`/custom-vertex-layout mechanism (the comment cites
  `easygl_normalmapping_shader_test.cpp` by name as an established-convention sibling) — worth
  aggregating this family's coverage in a single cross-cutting note once the whole shard is audited,
  rather than re-deriving the shared `ToColumnMajor()`/`transpose(mat3(...))` convention independently
  in each file's report.

## Missing or Weak Tests

The wind-sway term (`waveOffset`/`wind`) is ported line-by-line but only ever tested with
`WindAmount=0.0`, i.e. structurally gated to zero — the file's own comment (lines 58-66) explicitly
and correctly discloses this as a deliberate scope reduction rather than an oversight, citing the
genuine complexity of hand-deriving a per-vertex, position-dependent wind offset as the reason. A
follow-up test exercising a small nonzero `WindAmount` against a hand- or script-derived expected
vertex displacement would close this specific, self-identified gap.

## Positive Findings

- The header comment's reproduction of the original HLSL source (lines 9-37) makes line-by-line
  fidelity checking straightforward and was directly useful for this audit's own verification.
- Explicit, load-bearing use of `static_assert(sizeof(BillboardVertex) == 36)` to guard the custom
  vertex layout against silent padding drift is good defensive test design.
- The 3-check structure (bounded expansion / correct lit color / alpha-test discard) cleanly separates
  three independent failure modes rather than conflating them into one pixel comparison.
- Self-disclosed scope limits (wind term untested beyond zero-gating; `transpose()` not independently
  exercised by this specific camera) are exactly the kind of honest scope documentation this audit
  values over an unqualified "fully covered" claim.

## Final Assessment

A careful, well-documented shader-port test whose 3 pixel checks were independently re-derived and
confirmed correct against both the original HLSL and the current GLSL/backend binding code; only
issue found is a minor resource-hygiene gap (uncleaned temp files) and an already-acknowledged,
narrow wind-term coverage gap.
