# Audit: src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
- Audit status: AUDITED (**scoped-depth review** — see Methodology note below; this is the largest single file
  in the entire audit, 4733 lines, ~6× the next-largest backend file)
- Subsystem: `backend-easygl` shard
- File type: C++ implementation — the entire EasyGL (OpenGL ES 3.0) backend in one file
- Related header/implementation: `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp` (audited
  separately, same shard)
- XNA/FNA relevance: implements the full 3D+2D XNA rendering pipeline (`BasicEffect`, `SkinnedEffect`,
  `DualTextureEffect`, `EnvironmentMapEffect`, `PbrEffect`/`SkinnedPbrEffect` (NOXNA), `SpriteBatch`, all
  render-state objects) — the most XNA-behaviorally-complete backend in the project per `CLAUDE.md`'s own framing.
- Graphics backend relevance: the default backend on Linux/Emscripten (`cmake/BackendSelection.cmake`); links the
  external `easy-gl` sibling library (out of audit scope per D-6) for its actual GL object wrappers
  (`easygl::Texture`/`Buffer`/`Program`/`Framebuffer`/etc.) — this file is the CNA-side adapter/effect-shader layer
  built on top of that library.
- FNA reference: cross-checked the `SkinnedEffect`/`SkinnedPbrEffect` normal-transform pipeline against FNA's
  `Skinning.fxh` (`Skin()` step) and the general lit-shader normal-matrix handling against `EffectHelpers.cs`'s
  `WorldInverseTranspose` computation.
- Main related tests: `examples-tests-easygl` (218 files — **already audited via a prior mechanical-batch
  Workflow**, see that batch's commit and `AUDIT_CROSS_CUTTING_FINDINGS.md`). That batch's own finding (three test
  files independently reporting EasyGL's skinned shaders skip the world-space normal transform) is **independently
  confirmed here by direct production-code reading** — see F2/F3 below, which supersede those test-report
  observations with first-hand verification of the actual defect mechanism.

## Methodology note

Given this file's exceptional size (4733 lines — larger than the next four biggest backend files combined), this
audit read the file's header contract in full, the constructor/destructor and MSAA-buffer lifecycle in full, and
all six lighting/skinning/PBR shader-generation functions (`EnsureLit3DProgram`, `EnsureLit3DVertexLitProgram`,
`EnsureSkinnedProgram`, `EnsureSkinnedVertexLitProgram`, `EnsurePbrProgram`, `EnsurePbrSkinnedProgram` — roughly
1000 of the file's 4733 lines) plus the shared `BindDrawParams`/`SelectProgram` uniform-binding logic, specifically
to independently verify the world-space-normal-transform finding the prior `examples-tests-easygl` mechanical batch
surfaced from three separate test files. This is a targeted, evidence-driven deep-dive on a flagged area plus a
structural overview of the rest (resource-recovery pattern, MSAA, wireframe emulation, sampler/state application),
not an exhaustive line-by-line read of all 4733 lines. Given the mechanical-batch pass already produced 218
genuine, evidence-based per-file reports touching most of this backend's effect/state surface from the test side,
this direct pass deliberately prioritized *production-code* confirmation of the highest-value finding over
re-covering ground the test-side batch already covered well.

## Executive Verdict

**Significant correctness risk** in two related, independently-confirmed areas, set against an otherwise
exceptionally mature, well-documented, heavily-iterated codebase (the single richest "Task NNN fix" comment
history seen in this entire audit so far — real bugs found and fixed via genuine investigation, not assumed away).
F1 (constructor exception-safety / dangling window-registry entry) is the more severe of the two: a plausible,
real-world-reachable failure path (`SDL_GL_CreateContext` failing) leaves a dangling pointer in
`IGraphicsBackend`'s static window registry, which `SdlInputBridge`/`Mouse` would then dereference. F2/F3
(skinned-effect normal transforms) are confirmed, direct production defects affecting any rotated skinned model's
lighting — exactly what the sibling test-batch flagged, now root-caused.

## Checklist Results

### API / XNA / FNA parity

**F2/F3 (Detailed Findings)** are the substantive parity gaps confirmed here. By contrast, the *non-skinned* lit
paths are correct and were independently verified: `EnsureLit3DProgram`/`EnsureLit3DVertexLitProgram`/
`EnsureEnvMapped3DProgram`/`EnsurePbrProgram` all correctly use a `uNormalMatrix` uniform, and `BindDrawParams`
(lines 3993-4011) computes it as the true `transpose(inverse(world3x3))` via the cofactor/determinant method —
its own comment cites a prior fix ("Task 398... the raw upper-left 3x3 used before was only correct for
rotation/uniform-scale/translation World matrices"), meaning this exact class of bug (raw World vs.
inverse-transpose for the normal) was already found and fixed once for the *non-skinned* shaders — making its
survival in the *skinned* shader family (F2) and partial survival in the PBR-skinned shader (F3) a case of a
known-and-fixed defect class not being propagated to sibling code paths added later.

The Task 1102/1102b vertex-lit shader family (`EnsureLit3DVertexLitProgram`/`EnsureSkinnedVertexLitProgram`) is a
carefully-reasoned XNA-parity correction: real XNA/FNA defaults `PreferPerPixelLighting=false`, selecting a
per-vertex-lit (Gouraud) shader family, not the per-pixel one — this file implements both families and selects
between them via `SelectProgram()`, matching the real default. The GLSL ES cross-stage uniform precision-qualifier
issue (`uDiffuseColor` needing identical `highp` qualification in both stages) is documented as a real link failure
that was hit and fixed, not a hypothetical.

### Behavioral correctness

**F2/F3** below. The Task 895 bone-weight-count gating (`uWeightsPerVertex >= 2`/`>= 4`, summing only the first
`N` weight/index pairs) correctly mirrors FNA's own `Skin(vin, boneCount)` behavior. The degenerate-blend-normal
NaN guard (lines 3305-3318, `skinnedNormalLen > 1e-6` fallback to the untransformed bind-pose normal) is a
genuinely thoughtful numerical-safety addition with a concrete, plausible root-cause explanation (two
near-opposite bone rotations blended near-evenly can make the linearly-interpolated skin matrix's rotational part
nearly cancel for a specific vertex normal) — a real defensive fix, not defensive-programming theater.

### Logic

**F2/F3.** Also of note: the Task 1111 fog-vector-formula fix comment (present verbatim across at least 5 of the
six shader functions read) documents a specific, previously-wrong naive `(fogEnd-z)/(fogEnd-fogStart)` falloff
that silently inverted/collapsed once `FogEnd < FogStart`, replaced with a formula matching FNA's real
`ComputeFogFactor` exactly — a good example of an FNA-fidelity bug that was actually root-caused (not just
patched to pass a specific test case).

### Memory/resource lifetime

**F1 (Detailed Findings)** — the substantive finding. Beyond that: the `RecoverableResource`/`ResourceRegistry`
pattern (used by `EasyGLTextureBackend`, `EasyGLRenderTargetBackend`, `EasyGLRenderTargetCubeBackend`,
`EasyGLVertexBufferBackend`, `EasyGLIndexBufferBackend`, `EasyGLOcclusionQueryBackend`,
`EasyGLSpriteBatchBackend` — every GL-object-owning class, per the header) is a coherent, deliberate design for
GL-context-loss recovery (`release_gl_handle_only()`/`recreate_gl_resource()` pairs), consistent with the
constructor's own `RegistryPtr()` opt-out mechanism (`contextRecoveryEnabled_`) documented in
`GraphicsBackendCreateArgs::contextRecoveryEnabled`'s own doc comment (audited in `IGraphicsBackend.hpp`'s report).

### C++ correctness

The destructor (lines 1409-1415) correctly reverses construction-order concerns it's responsible for
(`UnregisterForWindow` then `SDL_GL_DestroyContext`, both null-guarded) — but see F1 for the *constructor's* gap,
which this destructor cannot compensate for since it's never called on a construction failure.

### Performance

Not assessed in depth given the scoped-review methodology — `CreateMsaaBuffers`'s resize-on-demand check inside
`BindDefaultFramebuffer` (lines 1379-1395, comparing `physW/physH` against cached `msaaW_/msaaH_` every frame) is
a cheap, reasonable per-frame cost.

### Thread safety

N/A — consistent with every other backend audited so far.

### Architecture

The per-vertex-layout `Prog3D` struct-of-uniform-locations pattern (11+ distinct program variants, one per
stride/feature combination) is a clear, if verbose, way to avoid runtime uniform-name lookups on every draw call
— a reasonable design given GLES3's lack of a more structured shader-permutation system. `SelectProgram()`
dispatches correctly by `stride` first, then by feature flags (`dualTexture`/`envMapping`/`skinned`/`pbr`/
`lightingEnabled`/`preferPerPixelLighting`) — not independently re-verified for every branch given the scoped
review, but the branches read for the skinned/lit paths were internally consistent with the six functions this
audit did fully read.

### Maintainability

4733 lines for a single file is large by any measure — this is a genuine maintainability concern independent of
correctness: the sheer size makes it harder to review holistically (as this audit's own scoped-methodology note
had to acknowledge) and likely harder to onboard new contributors to, compared to a codebase organized like
Vulkan/D3D9/Bgfx/SdlGpu (dozens of smaller files). This is a defensible historical/organic-growth outcome, not
obviously a mistake (splitting it now would be a large, risky refactor for a working, well-tested file) — flagged
as an architectural observation for `AUDIT_CROSS_CUTTING_FINDINGS.md`, not a defect to fix.

### Portability

N/A beyond EasyGL's own GLES3-target scope (already well-documented in the file's own comments, e.g. "GLES3 has no
wireframe fill mode at all").

### Robustness

**F1.**

### Testing

Extensively covered by the already-audited `examples-tests-easygl` shard (218 files) — this audit's own direct
reading corroborates rather than duplicates that work; see the Cross-File Observations below for how the two
passes connect.

## Detailed Findings

### F1 — A constructor failure after `RegisterForWindow()` but before the constructor completes leaves a dangling entry in `IGraphicsBackend`'s static window registry

- Severity: HIGH
- Confidence: HIGH
- Category: memory safety / resource lifecycle
- Location/symbol: `EasyGLGraphicsBackend::EasyGLGraphicsBackend` (lines 1277-1348), specifically
  `IGraphicsBackend::RegisterForWindow(window, this)` at line 1292 followed by a confirmed throwing path at
  lines 1305-1309 (`SDL_GL_CreateContext` failure)
- Evidence: the constructor calls `RegisterForWindow(window, this)` very early (line 1292) — before GL context
  creation, before `device.initialize()`, before MSAA buffer setup. Immediately after, `SDL_GL_CreateContext(window)`
  is called (line 1305) and explicitly throws `std::runtime_error` on failure (lines 1306-1309). If that happens
  (a real, plausible failure mode: unsupported GLES 3.0 context on the given driver/platform, exhausted GL
  contexts, a headless/CI environment without a real GPU), the constructor's exception propagates out, the
  `EasyGLGraphicsBackend` object itself is never fully constructed and its storage is released by the failed
  `new`/`make_unique` expression in the caller — but `IGraphicsBackend::windowRegistry()`'s static map still
  contains `window → this`, where `this` now points at freed/invalid memory. `IGraphicsBackend.hpp`'s own audit
  report (Finding F1 in that file) already flagged the *general* risk that this registry's lifecycle is enforced
  only by convention (register in ctor, unregister in dtor) with no compiler/runtime enforcement — this is the
  **concrete instance** of that risk actually manifesting, since this constructor's own destructor (which would
  call `UnregisterForWindow`) is never invoked when the constructor itself throws.
- Why it matters: `SdlInputBridge.cpp:524` and `Mouse.cpp:48` both call `IGraphicsBackend::GetForWindow(window)`
  and dereference the result — if a game retries creating an `EasyGLGraphicsBackend` for the same window after a
  transient `SDL_GL_CreateContext` failure (or even just continues running with a different backend after
  catching the exception, while the same `SDL_Window*` is reused), the very next mouse-position query or input
  event on that window would dereference a dangling pointer — undefined behavior, plausibly a crash, in a path
  that's otherwise completely unrelated-looking (mouse input) to the actual root cause (a GL context creation
  failure during backend construction).
- FNA/XNA comparison: N/A (CNA-internal resource-lifecycle issue).
- Related files: `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` (`windowRegistry()`,
  `RegisterForWindow`/`UnregisterForWindow`); `src/CNA/Internal/Input/SdlInputBridge.cpp`;
  `src/Microsoft/Xna/Framework/Input/Mouse.cpp` (both confirmed call sites of `GetForWindow`).
- Suggested future action (not implemented by this audit): move `RegisterForWindow(window, this)` to the very end
  of the constructor (after every fallible step has succeeded), or wrap the fallible prefix in a try/catch that
  calls `UnregisterForWindow(window)` before rethrowing. The same audit should check whether `Canvas`/`SdlGpu`/
  `WebGPU` (the other three backends that call `RegisterForWindow`, per the `IGraphicsBackend.hpp` report) have
  the same ordering risk when their own shards are audited.

### F2 — `SkinnedEffect`'s shaders (`EnsureSkinnedProgram`/`EnsureSkinnedVertexLitProgram`) never apply the object's World transform to lighting normals at all — only the bone-skinning matrix

- Severity: HIGH
- Confidence: HIGH (confirmed by direct source reading, not inference)
- Category: correctness / FNA parity
- Location/symbol: `EnsureSkinnedProgram` vertex shader (lines 3300-3318), `EnsureSkinnedVertexLitProgram` vertex
  shader (lines 3489-3512); confirmed by absence in `BindDrawParams`/uniform-registration: neither
  `prog_skinned_` nor `prog_skinned_vertexlit_` registers a `loc_normalmat` (`uNormalMatrix`) uniform location
  anywhere in their setup code (grep-confirmed: `loc_normalmat` is registered only for `prog_lit_textured_`,
  `prog_lit_textured_vertexlit_`, `prog_env_mapped_`, and `prog_pbr_` — four programs, not six)
- Evidence: both skinned vertex shaders compute `vec3 skinnedNormal = mat3(skinMat) * aNormal;` (normalized with a
  NaN guard) — using **only** the linearly-blended bone matrix (`skinMat`, itself object/model-space per FNA's own
  `Skin()` convention). The object's own `uWorld` matrix is applied to *position* (`vWorldPos = (uWorld *
  skinnedPos).xyz`) but is **never composed into the normal transform at all** — contrast with
  `EnsureLit3DProgram`'s non-skinned equivalent, which correctly does `vNormal = uNormalMatrix * aNormal` (the
  true world-space-correct inverse-transpose of World's 3×3, per `BindDrawParams`'s own Task-398-fixed
  computation).
- Why it matters: any skinned model placed in the world with a rotation (the overwhelmingly common case for any
  character that isn't facing exactly the bind pose's forward axis) will be lit using normals still expressed in
  the *bind pose's local orientation*, not rotated to match the model's actual world-facing direction — light and
  camera-relative specular highlights would appear to come from the wrong direction relative to the visibly-rotated
  character. This is invisible in every existing EasyGL skinned-effect test (confirmed via the prior mechanical
  batch's own finding: all of them use `World=Identity`, where the raw skin-space normal and world-space normal
  coincide).
- FNA/XNA comparison: FNA's `Skinning.fxh` (the real HLSL `Skin()` step used by `SkinnedEffect`) composes the
  bone's rotational part with `WorldInverseTranspose` for the normal, exactly mirroring how the non-skinned
  `BasicEffect` shader does it — this file's own non-skinned lit shader gets this right; only the skinned variant
  regressed or never received the fix.
- Related files: `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp` (`Prog3D::loc_normalmat`
  field, already present and used elsewhere in this same struct — the fix is adding it to
  `prog_skinned_`/`prog_skinned_vertexlit_` too); `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp` (to
  confirm `FillGpuDrawParams()` doesn't already fold World into `boneTransforms` some other way — not found in
  this audit's reading, but the shader-side absence of any `uNormalMatrix`/`uWorld`-for-normal usage is conclusive
  on its own regardless).
- Suggested future action (not implemented by this audit): change the normal line in both shaders to
  `mat3(uNormalMatrix) * (mat3(skinMat) * aNormal)` (composing the object-level inverse-transpose normal matrix
  with the per-vertex skin-space normal), matching the non-skinned shader's already-correct pattern, and add the
  corresponding `loc_normalmat` uniform registration + `BindDrawParams` upload for both programs.

### F3 — `SkinnedPbrEffect`'s shader (`EnsurePbrSkinnedProgram`) uses the raw `uWorld` matrix (not the inverse-transpose) for its normal/tangent transform

- Severity: MEDIUM
- Confidence: HIGH
- Category: correctness / FNA parity (NOXNA extension — no direct FNA equivalent, but the same
  correctness bar this file already applies to `PbrEffect`'s non-skinned sibling)
- Location/symbol: `EnsurePbrSkinnedProgram` vertex shader (lines 3777-3779):
  `vNormal=normalize(mat3(uWorld)*(skinNormalMat*aNormal)); vTangent=mat3(uWorld)*(skinNormalMat*aTangent.xyz);`
- Evidence: unlike F2 (where World is entirely absent from the normal transform), here `uWorld` genuinely is
  applied — but as the *raw* World matrix, not the `uNormalMatrix` inverse-transpose `EnsurePbrProgram` (the
  non-skinned PBR sibling, confirmed via grep to register `loc_normalmat` at line 3714) correctly uses. Raw World
  is correct only for rotation/uniform-scale/translation transforms; a non-uniform-scale World matrix would skew
  the transformed normal and tangent, producing incorrect BRDF lighting/normal-mapping results specifically for
  non-uniformly-scaled skinned+PBR models.
- Why it matters: a narrower-scope version of F2's problem (World is at least partially applied here, unlike the
  SkinnedEffect case), but still a real, confirmed divergence from this same file's own established correct
  pattern for every other lit shader.
- FNA/XNA comparison: N/A directly (PbrEffect/SkinnedPbrEffect are NOXNA extensions per `plans/plan_cnj.md` CNB-58), but
  the correctness bar is the same real-time-graphics standard (inverse-transpose for normals under non-uniform
  scale) this file already applies correctly to its own non-skinned PBR program.
- Related files: same as F2.
- Suggested future action (not implemented by this audit): replace `mat3(uWorld)` with a properly-registered
  `uNormalMatrix` uniform for this program too, composed with `skinNormalMat` the same way F2's fix would apply it.

## Cross-File Observations

- F2/F3 directly confirm and root-cause the finding three separate `examples-tests-easygl` audit reports
  independently surfaced from the test side (`easygl_skinnedeffect_preferperpixellighting_test.cpp.audit.md`,
  `easygl_skinnedeffect_specular_test.cpp.audit.md`, `easygl_skinnedpbreffect_golden_test.cpp.audit.md`) — this is
  a good example of the hybrid audit strategy (mechanical test-side batch + direct production-code verification)
  converging on the same real defect from two independent angles, which is exactly the kind of corroboration this
  audit's methodology is meant to produce.
- F1 is a *new* finding not surfaced by the test-side batch (since none of those 218 tests exercise a
  constructor-failure path) — a good illustration of why direct backend-source auditing remains necessary even
  after a thorough test-side mechanical pass: some defect classes (constructor exception safety) are essentially
  invisible from the test-file side entirely.
- Recommend checking, when `backend-canvas`/`backend-sdlgpu`/`backend-webgpu` are audited, whether their own
  `RegisterForWindow` call sites have the same F1-shaped ordering risk (call before every fallible construction
  step has completed).

## Missing or Weak Tests

No existing test (per the already-completed `examples-tests-easygl` audit) exercises a rotated-World skinned
model's lighting direction specifically — which is exactly the scenario that would have caught F2/F3. A test
using a non-identity, rotated `World` matrix with `SkinnedEffect`/`SkinnedPbrEffect` and checking the lit result
against an independently-computed expected color would directly catch both.

## Positive Findings

- The richest, most detailed "found a real bug, root-caused it, fixed it, documented why" comment history of any
  file in this audit so far: the normal-matrix inverse-transpose fix (Task 398), the vertex-lit shader family
  addition matching XNA's real default (Task 1102/1102b), the fog-vector formula fix (Task 1111), the GLSL
  cross-stage precision-qualifier link-failure fix, the degenerate-blend-normal NaN guard, the emissive-multiplied-
  twice dark-material lighting bug (found via direct pixel sampling of an avatar's shoes, per the in-shader
  comment) — this is a codebase that has clearly been through multiple genuine rounds of empirical
  bug-hunting, not just written once and left alone.
- The `RecoverableResource`/`ResourceRegistry` GL-context-loss-recovery pattern is a coherent, well-integrated
  design applied consistently across every GL-object-owning class in this file.
- Despite F2/F3, the *non-skinned* lighting pipeline (BasicEffect lit/vertex-lit, EnvironmentMapEffect, PbrEffect)
  is correctly implemented with proper inverse-transpose normal matrices throughout.

## Final Assessment

An exceptionally mature, well-tested, heavily-iterated backend implementation whose few remaining defects (F1-F3)
are precise, well-evidenced, and — notably — each one is a *regression relative to a correctness bar this same
file has already met elsewhere* (the non-skinned lit shader's correct normal matrix; the destructor's correct
window-unregistration that the constructor doesn't symmetrically guarantee) rather than a sign of general
carelessness. This file would benefit most from: (1) moving `RegisterForWindow` to the end of the constructor
(F1), and (2) propagating the already-proven-correct `uNormalMatrix` pattern to the three skinned/PBR-skinned
programs that currently lack it (F2/F3).
