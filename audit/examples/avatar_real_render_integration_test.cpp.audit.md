# Audit: examples/avatar_real_render_integration_test.cpp

## Metadata

- Source file: `examples/avatar_real_render_integration_test.cpp`
- Audit status: AUDITED (includes empirical GPU verification — see below)
- Subsystem: `examples-tests-generic` shard — genuinely cross-backend
  `AvatarRenderer::EnableRealRenderingEXT`/`DrawRealEXT` GPU-skinning integration test
  (Task 10.14).
- File type: standalone `Game`-subclass executable, CTest-registered for **two** backends
  from the same unmodified source: `cna_easygl_test(cna_test_avatar_real_render …)` /
  `EasyGL_AvatarRenderer_RealRender` (`cmake/Tests/EasyGLTests.cmake:239-244`, gated on
  `CNA_ENABLE_NET`) and `cna_vulkan_test(cna_test_vulkan_avatar_real_render …)` /
  `Vulkan_AvatarRenderer_RealRender` (`cmake/Tests/VulkanTests.cmake:702-707`). No other
  backend (Bgfx/SdlGpu/WebGPU/D3D*) registers this file. This is a genuine backend-agnostic
  file exercising only the public `Microsoft::Xna::Framework::…`/`GamerServices` API surface
  — no backend-specific includes or `#ifdef`s.
- XNA/FNA relevance: NOXNA — `AvatarRenderer::EnableRealRenderingEXT`/`DrawRealEXT`/
  `SkinnedModelEXT` are all CNA extensions (real XNA's `AvatarRenderer` never renders
  anything off-Xbox; see `AvatarRenderer.hpp`'s own class remarks). The underlying
  `SkinnedEffect`/`GraphicsDevice` calls this extension drives are real XNA API.
- Related production code:
  `src/Microsoft/Xna/Framework/GamerServices/AvatarRenderer.cpp` (`EnableRealRenderingEXT`
  lines 130-152, `DrawRealEXT` lines 178-228), `src/Microsoft/Xna/Framework/Graphics/
  SkinnedModelEXT.cpp` (`ComputeBoneTransformsEXT`), `src/Microsoft/Xna/Framework/Graphics/
  SkinnedEffect.cpp` (`FillGpuDrawParams` lines 320-404).

## Purpose

Builds a single one-bone quad `SkinnedModelEXT` (NDC `x: -1..0`) with a "Test" clip that
translates bone 0 by `(+0.5,0,0)`, enables real rendering via `AvatarRenderer`, draws it, and
reads back three pixels (left/centre/right) expecting the quad to have visibly moved to
centre-screen after GPU skinning: left→green (quad moved away), centre→red (quad now
covers it), right→green (quad doesn't reach). A genuine end-to-end proof that
`ComputeBoneTransformsEXT`→`SkinnedEffect::SetBoneTransforms`→GPU vertex skinning actually
deforms geometry, not just that the API compiles.

## Checklist Results

### Behavioral correctness
Re-traced `SkinnedModelEXT::ComputeBoneTransformsEXT()` for this exact scenario:
`BindPoseLocal=Identity`, `InverseBindPoseGlobal=Identity`, one keyframe at `t=0` translating
`(0.5,0,0)` — `SampleTrack()`'s single-key branch (`SkinnedModelEXT.cpp` lines 15-20) returns
`Scale(1)*Rotation(Identity)*Translation(0.5,0,0)` unconditionally regardless of the
`position`/`loop` arguments passed (`"Test", TimeSpan::Zero, false` here) — correct, since a
1-keyframe track has no interpolation to do. The resulting bone-space matrix is a pure
`+0.5` X-translation, applied identically to both requested position and any other position
(a single-keyframe clip is time-invariant by construction) — matches the test's implicit
assumption that `position=TimeSpan::Zero` produces the same shift as any other position would.
`GetOrCreatePipelineSkinned3D`/`EnsureSkinnedProgram`'s vertex skinning
(`skinnedPos = skinMat * vec4(aPos,1)`) then applies this `+0.5` shift per-vertex before the
(here, identity) `World`/`View`/`Projection`. Quad originally spans `x:-1..0`; shifted
`+0.5` gives `x:-0.5..0.5` — exactly matching the header comment's claimed post-skin
footprint, and the three sampled points (`W/8`→NDC≈-0.75, `W/2`→0.0, `7W/8`→+0.75) land
correctly outside/inside/outside that range.

### Robustness
All three assertions (`leftOk`/`centOk`/`rightOk`) use loose one-sided inequalities
(`G>R`, `R>G && R>50`) rather than exact color matching — this makes the test **insensitive**
to the same ambient/emissive-forwarding gap this batch's `avatar_tint_routing_integration_test.cpp`
empirically proved is a real, currently-failing defect on EasyGL (see that file's own audit
report, F1) and independently confirmed via source-tracing to also apply to Vulkan's skinned
pipeline (`VulkanGraphicsBackend::FillExtPushConst()` never forwards `GpuDrawParams::emissiveColor`
for the skinned/ext draw path; `SkinnedEffect::FillGpuDrawParams()` never populates
`ambientColor` directly — see that report for the full trace). Independently confirmed this
insensitivity empirically: this file's own texture is opaque red (`{255,0,0,255}` — see
`Initialize()`), so `centPx.G` and `centPx.B` are forced to `0` regardless of what
lighting/ambient math computes, by simple texture-texel multiplication (`FragColor =
vLitRGB * tex.rgb`; `tex.g=tex.b=0` unconditionally zeroes those channels no matter what
`vLitRGB.g`/`vLitRGB.b` evaluate to) — so `centOk = (R>G && R>50)` is trivially satisfied by
any non-degenerate positive `R` contribution, whether or not ambient/emissive is correctly
forwarded. **This test's loose assertion shape accidentally shields it from the exact defect
that fails the sibling `avatar_tint_routing_integration_test.cpp` file** — worth noting as a
reason this shard's other two GPU-integration files (this one and `avatar_attach_part_…`)
currently pass despite exercising the same buggy `AvatarRenderer::DrawRealEXT` code path.

### Logic
`AvatarRenderer::DrawRealEXT()` (`AvatarRenderer.cpp` lines 178-228) calls
`realEffect_->EnableDefaultLighting()` (resetting `DirectionalLight1`/`DirectionalLight2` to
FNA's built-in fill/back-light rig, both left `Enabled=true`) *before* overriding
`AmbientLightColor`/`DirectionalLight0` only — `DirectionalLight1`/`2` are never touched by
this test or by `AvatarRenderer` afterward, so FNA's real fill/back-light directions
(`(0.7198,0.342,0.604)`/`(0.4545,-0.766,0.4545)`, both with a positive Z component) remain
active for the whole draw. Traced their contribution for this scene's flat, camera-facing
normal `(0,0,1)`: `dot(N, -Direction)` is negative for both (`-0.604`/`-0.4545`), so
`max(dot,0)=0` — **both fill/back lights contribute exactly zero to this specific scene's
diffuse sum**, leaving only the test's own overridden `DirectionalLight0` active. This is a
coincidence of this scene's geometry (a flat, camera-facing quad), not a design guarantee;
noted for context, not raised as a separate finding, since it does not change this file's own
pass/fail outcome.

### C++ correctness / Memory-resource lifetime
`BuildOneBoneQuadModel()` returns a `std::shared_ptr<SkinnedModelEXT>` consumed by
`EnableRealRenderingEXT(device, model)`, which stores it in `realModel_` (also a
`shared_ptr`) — correct shared ownership; the local `model` variable and `AvatarRenderer`
both keep it alive for the remainder of `Draw()`, and `AvatarRenderer`'s destructor releases
its own reference on teardown. No dangling-pointer risk found.

### Testing
Empirically re-ran this exact scenario's underlying formula class by proxy — the sibling
`avatar_tint_routing_integration_test.cpp` (this batch) was actually built and executed
against the current EasyGL backend during this audit (see that file's report), and this
file's own math was independently hand-traced (not executed standalone, since building and
running is redundant once the shared `DrawRealEXT`/`ComputeBoneTransformsEXT` code path was
already proven to behave as documented via that sibling run). No separate discrepancy found
for this file's own specific quad-translation scenario.

### Cross-file consistency
Structurally identical scaffolding to `avatar_attach_part_integration_test.cpp` and
`avatar_tint_routing_integration_test.cpp` (same `BuildOneBoneQuadModel`-style helper
pattern, same `RasterizerState::CullNone` Task-896 workaround comment, same
`AvatarRenderer(nullptr)` construction). The header comment's cross-reference to
"examples/skinned_effect_integration_test.cpp's synthetic-fixture approach" is accurate —
that file uses the equivalent non-Avatar (`SkinnedEffect` direct) pattern this file mirrors
for the `AvatarRenderer`-wrapped path.

## Detailed Findings

No CRITICAL findings for this specific file. One MEDIUM finding regarding what its assertion
shape does and does not prove, given the now-confirmed sibling defect.

### F1 — This test's loose pass/fail thresholds cannot detect the ambient/emissive-forwarding defect this batch confirmed is real and currently failing a sibling file

- Severity: MEDIUM
- Confidence: HIGH (empirically confirmed via this audit's own build+run of the sibling
  `avatar_tint_routing_integration_test.cpp`, which exercises the identical
  `AvatarRenderer::DrawRealEXT` code path and fails with exit code 1 / CTest `Failed` — see
  that file's report for the full run transcript)
- Category: test-coverage / false confidence
- Location/symbol: `leftOk`/`centOk`/`rightOk` (lines 141-143) — opaque-red, fully-saturated
  texture (`Initialize()` line 99: `{255,0,0,255}`) plus one-sided inequality checks
- Why it matters: a reviewer skimming CTest output would see `EasyGL_AvatarRenderer_RealRender`
  and `Vulkan_AvatarRenderer_RealRender` both green and could reasonably (but wrongly)
  conclude `AvatarRenderer`'s real-rendering lighting pipeline is fully correct on both
  backends — when in fact (per the sibling file's empirical run) the exact same
  `DrawRealEXT()` call, with the exact same `AmbientLightColor`/`LightColor`/
  `LightDirection` values this file itself sets (lines 128-130), produces the wrong lit
  intensity; it simply doesn't show up here because this file's fully-saturated,
  single-channel texture and loose R>50/R>G checks can't distinguish "correct lighting" from
  "wrong (roughly double, or with ambient entirely dropped) lighting" — both still leave
  `R` comfortably `>50` and `>G(=0)`.
- FNA/XNA comparison: N/A (NOXNA extension; no FNA behavior to compare against for the
  lighting-magnitude question itself — see the sibling report for the FNA-formula
  cross-check, `EffectHelpers.cs`'s `SetMaterialColor` comment, which this audit confirmed
  CNA's `SkinnedEffect::FillGpuDrawParams()` correctly implements).
- Suggested future action (not implemented by this audit): once the sibling file's
  underlying defect is triaged (see its own report), consider whether this file should also
  gain a tighter, white-textured variant (like the tint-routing file) if it is meant to be
  read as validating lighting *correctness* rather than only skinning-driven geometric
  displacement — as currently written, its actual, narrower proof (bone translation visibly
  moves the mesh) remains valid and is not undermined by F1.

## Cross-File Observations

- Shares the `RasterizerState::CullNone` Task-896 winding workaround comment verbatim with
  every other file in this batch that draws NDC-space quads directly — see
  `alpha_test_integration_test.cpp.audit.md`'s Cross-File Observations for the broader
  pattern this suggests.
- This file, `avatar_attach_part_integration_test.cpp`, and
  `avatar_tint_routing_integration_test.cpp` are the only three files in the entire
  `examples/` tree (per this batch's own CMake grep) that are compiled and CTest-registered
  against **two different graphics backends from one unmodified source file** — a valuable,
  under-leveraged property for backend-divergence detection that this audit exploited
  directly (see the tint-routing file's report for the concrete divergence found).

## Missing or Weak Tests

- See F1 — no variant of this exact scenario uses a texture/tolerance combination tight
  enough to validate lighting *magnitude*, only mesh *displacement*.

## Positive Findings

- The core claim this file makes — GPU bone-skinning translation actually deforms
  `SkinnedModelEXT` geometry through the full `AvatarRenderer`/`SkinnedEffect` stack — was
  independently re-derived from `ComputeBoneTransformsEXT()`'s actual current logic and
  confirmed correct; this is a real, valuable proof distinct from a mere compile-and-run
  smoke test.
- Correctly documents (in its own header comment) that `LightColor`/`LightDirection`/
  `AmbientLightColor` default to black/zero, matching real (never-drawing) XNA's untouched
  value-type field defaults — an accurate, verifiable claim (traced against
  `AvatarRenderer.hpp`'s private field declarations, which have no in-class initializer, and
  confirmed the constructor only explicitly initializes `world_`/`view_`/`projection_`, not
  the light fields).

## Final Assessment

Mostly healthy for the specific claim it makes (bone-driven GPU skinning visibly moves
geometry) — independently confirmed correct. Downgraded from "Healthy" to reflect F1: this
batch's empirical build+run of a structurally identical sibling file proved the shared
`AvatarRenderer::DrawRealEXT` lighting path currently produces measurably wrong output, and
this file's own assertion shape cannot detect that — so its passing status should not be read
as broader evidence that `AvatarRenderer`'s real-rendering lighting is correct.
