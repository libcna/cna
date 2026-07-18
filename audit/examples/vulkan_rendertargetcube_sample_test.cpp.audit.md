# Audit: examples/vulkan_rendertargetcube_sample_test.cpp

## Metadata

- Source file: `examples/vulkan_rendertargetcube_sample_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RenderTargetCube` sampled back as `TextureCube` via
  `EnvironmentMapEffect`, Vulkan backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_rendertargetcube_sample …)` /
  `cna_register_backend_test(NAME Vulkan_RenderTargetCube_SampleAfterUnbind …)`,
  `cmake/Tests/VulkanTests.cmake:343-346`).
- XNA/FNA relevance: direct — `RenderTargetCube`, `CubeMapFace`, `EnvironmentMapEffect`
  (`EnvironmentMapAmount`/`EmissiveColor`/`EnvironmentMapSpecular`/`DiffuseColor`).
- FNA reference: `Graphics/RenderTargetCube.cs` (ctor signature), `Graphics/Effect/StockEffects/
  HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`: `color.rgb = lerp(color.rgb, envmap.rgb,
  pin.Specular.rgb)` with `pin.Specular.rgb = EnvironmentMapAmount` in the non-fresnel path),
  `Graphics/Effect/StockEffects/EnvironmentMapEffect.cs` (ctor: `DirectionalLight0.Enabled =
  true`).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`RecordCommandBuffer()` lines 6224-6920, `clearedRTs_`/`usedRTs` construction lines 6694-6708,
  `VulkanRenderTargetCubeBackend` lines 8574-8875, `IVulkanCubeSamplable::GetVkCubeImageView()`
  override line 510), `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp` (lines
  65-72, 492-544), `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.{vert,frag}.glsl`,
  `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`.

## Purpose

Two-phase Vulkan integration test proving that a `RenderTargetCube`'s *actual rendered content*
(not stale/garbage image data) can be sampled back as a `TextureCube` through
`EnvironmentMapEffect::setEnvironmentMapProperty()` after the cube target is unbound. Phase 1
`SpriteBatch`-draws solid blue into each of the 6 cube faces (`CubeMapFace::PositiveX` …
`NegativeZ`); Phase 2 draws a full-screen NDC quad with `EnvironmentMapAmount=1`,
`EmissiveColor=0`, `EnvironmentMapSpecular=0` (isolating the pure env-map term) sampling `rtc_`.
File placement (`examples/`, non-XNA `Game` subclass with plain data members) is correct for this
shard.

## Executive Verdict

**Mostly healthy, with a stale self-documented comment** — the test's own rendering logic and its
independently-verified derivation of the expected blue-centre pixel are correct, and the specific
`IVulkanCubeSamplable`/`dynamic_cast` mechanism it targets is real, present, and exercised as
described. However, its header comment's claim that the Task 875 Clear()-only-RT bug is "tracked as
Task 875, not fixed here" is now **stale**: Task 875 landed the next day (commit `140b2e05`,
2026-07-08) and the fix is live in the current `RecordCommandBuffer()` (see F1). This does not
affect this file's own correctness (it never relies on the fixed behavior, by design), but it is
exactly the kind of drifted documentation this audit has been asked to independently re-verify
rather than trust.

## Checklist Results

### API / XNA / FNA parity
`RenderTargetCube(device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None)` matches
FNA's 5-argument `RenderTargetCube` constructor (`RenderTargetCube.cs` lines 132-147, which forwards
to the 7-arg ctor with `multiSampleCount=0`/`RenderTargetUsage.DiscardContents`).
`EnvironmentMapEffect::setDiffuseColorProperty`/`setEmissiveColorProperty` take `Vector3`, matching
FNA (`EnvironmentMapEffect.cs`: `public Vector3 DiffuseColor`/`public Vector3 EmissiveColor` —
**not** `Color`, confirmed by reading the FNA source directly rather than assuming). The
constructor-time default `DirectionalLight0.Enabled = true` this test relies on implicitly (it
never disables light0, but light0's own Direction/DiffuseColor are left at `Vector3::Zero`, so it
contributes nothing) is independently confirmed present in both FNA
(`EnvironmentMapEffect.cs:366`) and CNA (`EnvironmentMapEffect.cpp:40`).

### Behavioral correctness
Independently re-derived the expected result against FNA's `PSEnvMap` formula and CNA's
`env_map3d.frag.glsl`: with `EmissiveColor=0` and no enabled light contributing non-zero diffuse,
`litRGB=(0+0)*diffuseColor=0`; `baseColor=0`; `blendFactor=ep.emissive_em.w=EnvironmentMapAmount=1`
(fresnel disabled by default); `combinedAlpha=diffuseColor.a(1)*texColor.a(1)=1`; final
`rgb=mix(0, envSample.rgb*1, 1)=envSample.rgb` — i.e. the rendered pixel is exactly whatever the
cube face sampled contains, with zero interference from diffuse/emissive/ambient. Since all 6
faces were filled solid blue in Phase 1, any sampled direction reads blue, making the test robust
to which face `reflect(-E,N)` actually resolves to for this exact quad/eye/normal configuration —
a deliberately low-fragility design.

### Logic
The dynamic dispatch path this test exercises
(`VulkanGraphicsBackend`'s `dynamic_cast<const IVulkanCubeSamplable*>(params.envMap)` at
`VulkanGraphicsBackend.cpp:7736`, falling back to `defaultWhiteCubeView_` only if the cast fails)
is real and present; `VulkanRenderTargetCubeBackend` (`include/.../VulkanGraphicsBackend.hpp:492`)
publicly inherits `IVulkanCubeSamplable` and overrides `GetVkCubeImageView()` to return
`cubeView_`, a `VK_IMAGE_VIEW_TYPE_CUBE` view constructed once at line 8640 covering all 6 array
layers — so a genuinely wrong sampled face (rather than a hardcoded/garbage value) is what this
test's PASS criterion depends on.

### C++ correctness
`device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr))` (lines 117) is the established
project idiom for unbinding via the `RenderTarget2D*` overload (an unbind is not
cube-face-specific); consistent with other files in this shard (`vulkan_rt2d_test.cpp`,
`vulkan_rtcube_test.cpp`).

### Testing
Single PASS/FAIL criterion (`R<=50 && G<=50 && B>=200`) at the exact backbuffer centre. Reasonably
tight tolerance for a solid-fill scene; adequate given the deliberately simple 6-solid-faces setup.

### Cross-file consistency
Explicitly built to combine two prior single-purpose Vulkan tests
(`vulkan_rtcube_test.cpp`'s per-face fill mechanics and an implied `vulkan_env_map_test.cpp`'s
`EnvironmentMapEffect` sampling), per its own header. See F1 for the one place its narrative and
the current repository state have diverged.

## Detailed Findings

### F1 — Header comment's Task 875 status ("not fixed here") is stale; the fix landed the next day and is live in the current backend

- Severity: LOW
- Confidence: HIGH (confirmed via git log timestamps and direct inspection of the current
  `RecordCommandBuffer()`/`Clear()` implementation)
- Category: documentation / stale-comment
- Location/symbol: header comment lines 21-28 ("tracked as Task 875, not fixed here")
- Evidence: `git log` shows this file's Task 334 authorship at `6c834671` (2026-07-07 19:39:24
  +0200); `git log -1 --format=%ci -- examples/vulkan_rt_roundtrip_test.cpp` shows Task 875's
  own test landed at `140b2e05` (2026-07-08 00:59:34 +0200) — titled *"fix(Task 875): Vulkan
  Clear()-only render targets now record a render pass"*. Reading the current
  `VulkanGraphicsBackend::Clear()` (line 6214) confirms the fix is live:
  `if (currentRT_ && … ) clearedRTs_.push_back(currentRT_);`, and `RecordCommandBuffer()`
  (line 6698-6701) now folds `clearedRTs_` into `usedRTs` before the Phase-1 RT-pass loop — exactly
  undoing the gap this file's comment describes.
- Why it matters: a future reader of this file (which predates the fix) would reasonably believe
  the described Clear()-only gap is still open on Vulkan, when it has in fact been fixed and has
  its own dedicated regression test (`vulkan_rt_roundtrip_test.cpp`, audited separately in this
  batch). This is purely a documentation-staleness risk, not a functional defect in this file —
  the file's own Phase 1 correctly uses a real `SpriteBatch` draw (not Clear()-only) regardless of
  whether the underlying bug is fixed, so its own PASS/FAIL behavior is unaffected either way.
- FNA/XNA comparison: N/A (CNA-internal Vulkan backend implementation detail, not an XNA-facing
  behavior question).
- Related files: `examples/vulkan_rt_roundtrip_test.cpp` (the file that actually fixed and now
  regression-tests this), `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`.
- Suggested future action (not implemented by this audit): update the header comment to note the
  gap was fixed by Task 875 (`vulkan_rt_roundtrip_test.cpp`), matching the "confirmed... tracked as
  Task 875" framing but past-tense.

## Missing or Weak Tests

None specific to this file beyond F1's documentation note — the pixel assertion itself is sound
and appropriately isolates the env-map term from diffuse/emissive/ambient contributions.

## Positive Findings

- The test correctly isolates the env-map contribution (`EnvironmentMapAmount=1`,
  `EmissiveColor=0`, `EnvironmentMapSpecular=0`) so that a wrong/garbage cube sample would be
  immediately visible as a wrong colour rather than being masked by other lighting terms — this
  audit's independent re-derivation of the shader math confirms the isolation is mathematically
  complete (the non-env-map terms genuinely reduce to exactly zero contribution to `rgb`, not just
  approximately).
- Its own header comment proactively documents a real, separately-tracked gap (Task 875) rather
  than silently working around it — good project hygiene, only let down by not being updated once
  that gap was closed (F1).
- Explicitly notes and fixes the CCW/back-face culling gotcha for this quad's winding under CNA's
  real default `RasterizerState` (Task 896), rather than silently disabling culling everywhere.

## Final Assessment

A sound, well-reasoned integration test whose pixel-level assertion holds up under independent
re-derivation against both FNA's `PSEnvMap` formula and CNA's actual `env_map3d.frag.glsl`. The
only defect found is a stale narrative comment (F1) about a bug that has since been fixed by a
sibling file in this same shard — worth a one-line comment update, not a behavior change.
