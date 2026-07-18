# Audit: examples/easygl_cartooneffect_normaldepth_shader_test.cpp

## Metadata

- Source file: `examples/easygl_cartooneffect_normaldepth_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_CartoonEffect_NormalDepth_Shader`
  (`cmake/Tests/EasyGLTests.cmake:411-413`, target `cna_test_easygl_cartooneffect_normaldepth_shader`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`.hpp`/`.cpp`),
  `GraphicsDevice::ExtractMatrices`/`DrawIndexedPrimitives` (`GraphicsDevice.cpp:550ff`),
  EasyGL's `BindCustomEffectMatrices` (`EasyGLGraphicsBackend.cpp:4512-4523`)
- XNA/FNA relevance: ports `NonPhotoRealisticSample_4_0/NonPhotoRealistic/Content/CartoonEffect.Fx`'s
  `NormalDepth` technique (`NormalDepthVertexShader`/`NormalDepthPixelShader`) from HLSL to GLSL —
  a NOXNA content-authoring proof, not an `Microsoft::Xna` API surface itself, but judged against the
  FNA sample's shader semantics.
- Main related tests: sibling `easygl_cartooneffect_lambert_shader_test.cpp` (Lambert technique) and
  `easygl_cartooneffect_toon_shader_test.cpp` (this batch), all three completing CartoonEffect.Fx's
  three techniques (Task 947).

## Purpose

`EasyGLCartoonEffectNormalDepthTest` proves the EasyGL backend can compile and correctly execute a
hand-ported GLSL translation of `CartoonEffect.Fx`'s `NormalDepth` technique, which encodes the
world-space normal into the RGB channels (`(n+1)/2`) and clip-space depth into alpha
(`gl_Position.z / gl_Position.w`) — used by `NonPhotoRealisticSample`'s `Game.cs:233` to feed a
(not-yet-ported) edge-detection post-process. It writes the vertex/fragment GLSL source and a `.cnj`
descriptor to a per-instance temp directory, loads it via `ContentManager::Load<shared_ptr<Effect>>`,
and draws a single quad twice (`World=Identity`, `World=RotationY(180°)`) reading back the center pixel
each time.

## Executive Verdict

**Healthy.** The vertex/fragment shader is a faithful 1:1 translation of the FNA `.Fx` source (verified
below), the two checks genuinely discriminate a per-component `(n+1)/2` remap from a broken/partial one
(distinct Blue channel between the two draws), and the deliberate choice to only range-check the
alpha/depth channel (not derive an exact expected value) is disclosed and justified in the file's own
header comment rather than silently under-tested.

## Checklist Results

### API / XNA / FNA parity
N/A for the GLSL content itself (not an `Microsoft::Xna` API), but the CNA-side calls
(`ShaderEffect::setWorldProperty`/`setViewProperty`/`setProjectionProperty`, `Effect::Apply()`,
`GraphicsDevice::DrawIndexedPrimitives`) are all correct XNA-style names. Confirmed by reading
`ShaderEffect.hpp:95-115`: `World`/`View`/`Projection` set through `IEffectMatrices` are extracted by
`GraphicsDevice::ExtractMatrices()` and forwarded to the backend, which (`EasyGLGraphicsBackend.cpp:
4512-4523`, `BindCustomEffectMatrices`) uploads them as column-major `mat4` uniforms literally named
`World`/`View`/`Projection` — exactly the uniform names this test's `kVertSrc` declares (lines 84-86).

### Behavioral correctness
Hand-verified the vertex shader (lines 79-93) against the FNA reference quoted in the header comment
(lines 8-18): `gl_Position = Projection * View * World * vec4(aPosition, 1.0)` matches
`mul(mul(mul(input.Position, World), View), Projection)` under HLSL's row-vector/GLSL's
column-vector convention duality (same multiplication order class as every other test in this batch);
`vec3 worldNormal = mat3(World) * aNormal; vColor.rgb = (worldNormal + 1.0) / 2.0;` is a direct,
correct per-component port of `(worldNormal + 1) / 2`; `vColor.a = gl_Position.z / gl_Position.w`
matches `output.Position.z / output.Position.w` exactly. Fragment shader (lines 96-103) is a pure
passthrough, matching `NormalDepthPixelShader`'s `return color`.

Check A (`World=Identity`): normal stays `(0,0,1)` → expected `(0.5,0.5,1.0)` ≈ `(128,128,255)`.
Check B (`World=RotationY(π)`): a 180° Y-rotation flips `(0,0,1)` to `(0,0,-1)` → expected
`(0.5,0.5,0.0)` ≈ `(128,128,0)`. Both are correctly derived and the Blue-channel divergence
(255 vs. 0) is a strong, well-chosen discriminator that would catch a normal-transform bug (e.g. only
translating instead of rotating the normal, or applying the remap to the wrong axis).

### Logic
`close()` (line 199) uses a ±6 tolerance per channel — reasonable for 8-bit-quantized GPU output.
Alpha is only range-checked (`>= 0 && <= 255`, lines 204-205) — this is a tautology for a `bytecs`-backed
channel (any `Color` alpha value is always in `[0,255]` by construction), so Check A/B's `aAlphaOk`/
`bAlphaOk` **cannot fail** regardless of what the shader actually renders into alpha. The header
comment (lines 20-27) frames this as "checked for a plausible non-degenerate range," but the range
`[0,255]` is not actually a meaningful assertion for an already-clamped 8-bit channel — it would only
have caught a NaN/undefined-behavior-driven wraparound that a `Color`'s own byte storage cannot
represent in the first place. See Finding F1.

### Memory/resource lifetime
`vb_`/`ib_` are `unique_ptr`-owned, `fxBase_` is a `shared_ptr<Effect>` from `ContentManager::Load` —
consistent, unambiguous ownership. Per-instance temp directory (line 121-124, keyed by `this` pointer)
avoids collisions with concurrently-run sibling tests; not explicitly cleaned up afterward, but this
matches the established pattern across the whole test suite (temp dirs are process/OS-lifetime, not a
regression specific to this file).

### C++ correctness
`dynamic_cast<ShaderEffect*>(fxBase_.get())` is performed twice per draw — once in `Draw()` (line 188,
guarded by a null/`IsEffectValid()` check) and again, redundantly, inside `DrawOnce()` (line 165) with
no null check at all. Since `DrawOnce()` is only ever called after `Draw()`'s own successful cast, this
is not a live null-deref risk today, but `DrawOnce()` has no defensive check of its own — a future
caller of `DrawOnce()` added without first validating `fxBase_`'s dynamic type would dereference a null
`fx` unconditionally on line 166. LOW severity, not exploitable in the current call graph.

### Performance / Thread safety
N/A — single-frame, single-threaded test.

### Architecture
Correctly stays on the public `Microsoft::Xna::Framework` API surface plus documented `NOXNA`
`ShaderEffect`/`SetUniformXxx` extensions; no direct backend-internal symbols referenced.

### Maintainability
Header comment (lines 1-40) is exemplary: quotes the FNA HLSL source directly, explains the test's own
scope-narrowing decision (why alpha isn't exactly checked) instead of leaving it implicit, and states
the exit-code contract. `kVertSrc`/`kFragSrc` are clearly labeled as ports with line-level provenance.

### Portability
No platform-specific code; GLSL ES 3.00 (`#version 300 es`) is the correct target for the EasyGL/GLES
backend.

### Robustness
`Draw()` does check `!fx || !fx->IsEffectValid()` and reports a clean `[FAIL]` + `Exit()` rather than
crashing if the `.cnj`/GLSL compile fails (line 188-194) — appropriate defensive handling for the one
failure mode most likely during iteration on this hand-written shader source.

### Testing
This file is itself a test; there is no unrelated "production file" it's meant to cover other than the
generically-reusable `ShaderEffect`/`ContentManager` machinery (covered separately by other test files
in the suite). Its own coverage is 2 checks (Identity, RotateY180) for the RGB encoding, with the alpha
channel effectively untested (see F1).

### Cross-file consistency
Consistent with the sibling Lambert/Toon tests' shared geometry/camera setup (quad at origin, camera at
`(0,0,3)`, same `.cnj` scaffolding pattern) and with `ShaderEffect`'s documented `IEffectMatrices`
mechanism (Task 1079, `ShaderEffect.hpp:95-104`).

## Detailed Findings

### F1 — Alpha/depth-channel check is a tautology, not a real assertion

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / correctness-of-the-test
- Location/symbol: `EasyGLCartoonEffectNormalDepthTest::Draw()`, lines 204-205
  (`a.getAProperty() >= 0 && a.getAProperty() <= 255`)
- Evidence: `Color::getAProperty()` returns a `bytecs`/`uint8_t`-backed value that is always within
  `[0,255]` by the type's own storage width — there is no code path through which it could read outside
  that range. The check can never observe a FAIL.
- Why it matters: the header comment (lines 20-27) presents this as a deliberate, reasoned trade-off
  ("checked for a plausible non-degenerate range only"), which is the right instinct given the real
  complexity of deriving an exact clip.z/w value by hand — but the specific implementation chosen
  provides **zero** actual discriminating power, so the depth-encoding half of this shader
  (`vColor.a = gl_Position.z / gl_Position.w`) is completely unverified by this test despite the file's
  own stated intent to at least sanity-check it. A shader that wrote `vColor.a = 1.0` unconditionally,
  or forgot the alpha assignment entirely (leaving whatever the GLSL default/undefined value is,
  clamped into `[0,255]` at readback), would still pass both checks.
- FNA/XNA comparison: N/A (CNA-internal shader-port test).
- Suggested action (not implemented by this audit): a genuinely weak-but-real check would be easy to
  add without deriving the exact clip-space value by hand — e.g. assert the two draws' alpha values
  actually *differ* (or don't, if geometry is coplanar) from each other, or from a hardcoded sentinel
  written by `Color(0,0,0,0)`'s own initial value before `GetBackBufferData` overwrites it, to at least
  prove the alpha channel is being written by the shader at all rather than left at its clear/init
  value.

## Cross-File Observations

- Shares the exact geometry/camera setup and `.cnj`-scaffolding idiom with
  `easygl_cartooneffect_toon_shader_test.cpp` and the (not in this batch)
  `easygl_cartooneffect_lambert_shader_test.cpp` — all three complete the same FNA sample's shader
  effect and should be read together for full Task 947 coverage.

## Missing or Weak Tests

- No boundary case for the normal encoding at a non-axis-aligned rotation (e.g. 90°) — only 0° and
  180° are exercised, both of which happen to land on axis-aligned normals. This is an acceptable
  scope choice for a "does the port work at all" proof rather than an exhaustive geometric test, but
  worth noting as the actual coverage boundary.
- See F1 — the alpha/depth channel has no real assertion at all.

## Positive Findings

- Excellent, evidence-first header comment that quotes the FNA source directly and explains its own
  scope-narrowing decision rather than leaving it implicit — exactly the kind of self-aware
  documentation the project's own conventions ask for elsewhere.
- The RGB-encoding check itself (the file's stated primary goal) is correctly derived and has genuine
  discriminating power via the Blue-channel divergence between the two draws.
- Clean `IsEffectValid()` failure path instead of crashing on a shader-compile regression.

## Final Assessment

A well-targeted, mostly correct shader-port proof whose primary claim (RGB normal-encoding fidelity) is
genuinely verified with good discriminating power, but whose secondary claim (depth/alpha channel
sanity) is an unconditionally-true tautology rather than a real check — a LOW-severity, easily-fixed
gap that does not undermine the file's main purpose.
