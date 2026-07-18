# Audit: examples/sdlgpu_rendertargetcube_test.cpp

## Metadata

- Source file: `examples/sdlgpu_rendertargetcube_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `RenderTargetCube` proof for the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_RenderTargetCube`,
  `cmake/Tests/SdlGpuTests.cmake:45-48`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: direct — `RenderTargetCube` ctor overload, `CubeMapFace`, `GraphicsDevice.
  SetRenderTarget(RenderTargetCube, CubeMapFace)`, `RenderTargetCube.MultiSampleCount`,
  `TextureCube.GetData(face, level, rect, data, startIndex, elementCount)`.
- FNA reference: `Graphics/RenderTargetCube.cs`, `Graphics/TextureCube.cs`.
- Related production code: `include/Microsoft/Xna/Framework/Graphics/TextureCube.hpp` (`GetData`
  overload set, lines 96-124), `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`SdlGpuRenderTargetCubeBackend` ctor/`GetData`/`BindAsRenderTargetFace`, lines 4443-4637;
  `RenderToTargetCubeFace`, lines 853-912; frame-end mip regeneration, lines 734-749).

## Purpose

Five-check, single-frame pixel-readback test proving a real `SDL_GPU_TEXTURETYPE_CUBE` render
target: (A) all 6 faces individually bound/cleared/read back with distinct colors, (B) a real
`BasicEffect` colored-triangle draw into one face, (C) a per-face dedicated depth buffer (near
quad wins over far quad), (D) `MultiSampleCount` fidelity plus an actual MSAA fill/resolve/
readback round-trip, (E) mip-chain regeneration (level-1 of a uniform-color fill reads back the
source color). Placement (`examples/`, `CNA::Internal::Backends::SdlGpu` reference-only include)
is correct for a backend integration test.

## Executive Verdict

**Healthy.** All 5 checks map onto real, distinctly-implemented backend code paths (verified by
reading the production `SdlGpuRenderTargetCubeBackend`, not inferred from the test alone); no
correctness defect found in the file itself. One coverage gap (E's mip check cannot actually
distinguish a working mip chain from mip generation being skipped, given SDL_GPU's own resolve
semantics — see F1) and one masked-but-real design decision worth flagging for future test design
(depth format/MSAA/mip combinations are only exercised individually, never combined).

## Checklist Results

### API / XNA / FNA parity
`RenderTargetCube(dev, size, mipMap, format, depthFormat, multiSampleCount, usage)` (lines 110-111,
137-138, 169-170, 212-213, 217-218, 237-238) matches FNA's `RenderTargetCube` constructor overload
taking `(GraphicsDevice, int, bool, SurfaceFormat, DepthFormat, int, RenderTargetUsage)` in argument
order and count. `TextureCube::GetData(CubeMapFace, int level, Rectangle*, T*, int startIndex, int
elementCount)` (line 74, via the file's own `ReadCentrePixel` helper) matches the six-argument FNA
overload exactly (`TextureCube.hpp` lines 118-124). `getMultiSampleCountProperty()` (lines 214, 219)
correctly reflects XNA's documented behavior that `RenderTargetCube.MultiSampleCount` returns the
actual clamped value, not the raw constructor request — confirmed against
`SdlGpuRenderTargetCubeBackend`'s ctor, which runs the requested count through `ClampSampleCount()`
before storing it (`SdlGpuGraphicsBackend.cpp` lines 4466-4468).

### Behavioral correctness
- Check A: `BindAsRenderTargetFace` accumulates `(cube, face)` pairs into
  `usedRenderTargetCubeFacesThisFrame_` with a de-dup guard (lines 4557-4561), and
  `RenderToTargetCubeFace` (lines 853-912) issues one render pass per stored face at
  `EnsureFrameRendered()` time, targeting `layer_or_depth_plane = face` on the real 6-layer cube
  texture (non-MSAA branch, lines 866-868) — six independent solid fills backed by six genuinely
  distinct GPU sub-resources, not a shared scratch texture. `GetData(face, level, ...)` reads back
  via `region.layer = face` (line 4606), so the six checks in the loop (lines 120-131) are reading
  the six real layers, not re-sampling the same one.
- Check B: a real `BasicEffect`+`VertexPositionColor` draw through `dev.DrawPrimitives`, confirming
  the cube-face draw path is a genuine render pass, not a `Clear()`-only fast path — this
  discriminates the case (plausible in an immature cube-target implementation) where only
  `Clear()` is wired to the per-face target and actual draws silently no-op or fall through to the
  swapchain.
- Check C: depth test correctly re-enabled/disabled around the draw pair
  (`SetDepthTestEnabled(true)`→draws→`SetDepthTestEnabled(false)`, lines 186, 199) and the
  `Depth24Stencil8` format is requested per-cube (line 170) — `SdlGpuRenderTargetCubeBackend`'s ctor
  only allocates `depthTexture` when `depthFormat != 0` (line 4518), so this genuinely exercises a
  dedicated depth attachment path distinct from Check A/B/D/E's `DepthFormat::None` cubes.
- Check D: `ClampSampleCount()` (lines 268-282) walks 8→4→2→1 querying
  `SDL_GPUTextureSupportsSampleCount`, so `applied > 1` (line 220) is a real hardware/driver-queried
  value, not a hardcoded expectation — appropriately tolerant of environments where 4x MSAA isn't
  supported (would still pass at 2x). The MSAA color-target-info wiring (`msaaTexture` is a single
  shared 2D texture, resolved into the correct cube layer via `resolve_texture`/`resolve_layer` at
  pass end, lines 880-888) is a real, non-trivial resolve path, not a same-texture no-op.
- Check E: `mipMap_ = mipMap_ && multiSampleCount_ == 0` (line 4471) correctly encodes the
  MSAA/mip mutual-exclusion the header comment documents; `CalculateMipLevels(size,size)` sizes
  `num_levels` for the cube texture (line 4481); regeneration happens once per frame via
  `SDL_GenerateMipmapsForGPUTexture(cmd, cube->cubeTexture)` **before** `SDL_SubmitGPUCommandBuffer`
  (lines 739-748), matching the `SDL_gpu.h` contract the comment cites (must not be called inside a
  pass, must precede submission) — this ordering detail is easy to get wrong (submitting first,
  then trying to regenerate on an already-submitted buffer) and is verified correct here.

### Logic
`ReadCentrePixel`'s `size = std::max(1, kCubeSize >> level)` (line 72) correctly derives the level-1
sample point (`8/2=4,4`) for `kCubeSize=16`; consistent with `CalculateMipLevels`'s halving loop
(lines 258-261).

### C++ correctness
`static bool done` guard in `Draw()` (lines 94-96) is the same single-shot idiom used throughout
this shard; safe here since the class has no other mutable state read before this flag is set.
`ReadCentrePixel` takes `RenderTargetCube&` by non-const reference for a `const`-semantics read
(`GetData` is logically an accessor) — a minor API-shape nit, not a defect (matches
`TextureCube::GetData`'s own non-const-qualified declaration in the production header, so this is
inherited from the XNA-facing API shape, not introduced by the test).

### Robustness
Check A's per-face mismatch printout (lines 124-129) prints actual vs. expected RGB for a failing
face before the aggregate `Check()` call — useful failure diagnostics, better than the shard's
median which just reports pass/fail.

### Performance
Five `RenderTargetCube` allocations per run (one per check, Check D allocates two) is a lot of GPU
texture churn for a single-frame test, but this is a test-only cost with a 60s CTest timeout
budget, not a runtime concern in the production code being exercised.

### Testing
Strong, `RenderTarget2D`/`RenderTargetCube::GetData()`-based readback throughout — the header
comment's own claim that this is "a stronger, per-face-precise check than the reflection-based
'some face came back non-garbage' bar other backends settle for" is accurate: unlike
`vulkan_rendertargetcube_msaa_test.cpp` (per this file's own citation), every check here reads an
exact face+level via direct GPU download, not an indirect reflection-vector sample.

## Detailed Findings

### F1 — Check E's mip assertion cannot distinguish "mip chain correctly regenerated" from "mip chain never regenerated but the transfer read level 0 anyway" in the SDL_GPU backend specifically

- Severity: LOW
- Confidence: MEDIUM (traced the code path; the ambiguity is real, but I could not fully rule out
  that `SDL_DownloadFromGPUTexture` with `region.mip_level=1` on a texture whose `num_levels==1`
  (a hypothetical regression where `mipMap_` silently became false) would itself fail loudly rather
  than silently reading level-0 data)
- Category: test-coverage
- Location/symbol: `ReadCentrePixel(rt, CubeMapFace::PositiveY, 1)` (line 243); Check E block
  (lines 236-248)
- Evidence: Check E fills the entire face with one solid color (`Color::Orange`) and then asserts
  that mip level 1 reads back the *same* color. Because a uniform source color downsamples to
  itself at every mip level regardless of whether real box-filtering, nearest-neighbor mip
  generation, or (in a hypothetical regression) no mip generation at all happened, this check would
  pass identically whether `SDL_GenerateMipmapsForGPUTexture` actually ran, ran with a degenerate
  filter, or (if `SDL_DownloadFromGPUTexture` tolerates an out-of-range `mip_level` by clamping
  rather than erroring) silently read back level-0 data reinterpreted as level 1.
- Why it matters: a regression that broke mip generation for non-uniform content (the only case
  that actually matters for texture-cube reflections/environment maps sampled at a distance) would
  not be caught by this test, even though the test's own header claims it is "a solid-color fill
  regenerates a real mip chain."
- FNA/XNA comparison: N/A — this is a test-design gap, not an FNA/XNA behavioral question (FNA
  itself has no cross-mip-level content-based conformance test either).
- Suggested future action (not implemented by this audit): use a non-uniform per-texel pattern
  (e.g. a 2x2 checkerboard) at level 0 and assert the *expected averaged* value at level 1, which
  would only pass if real box-filter downsampling actually executed.

## Cross-File Observations

- Consistent with `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "silent-default-degradation" and
  "mutate-before-validate" watch-items: `SdlGpuRenderTargetCubeBackend`'s ctor does *not* exhibit
  either pattern — `mipMap_` is derived (`mipMap_ && multiSampleCount_ == 0`) and stored
  consistently before any texture is created, and every `SDL_CreateGPUTexture` failure throws with
  proper partial-resource cleanup of the textures already created in that same ctor call (lines
  4484-4536) rather than leaving a half-constructed object.
- This backend has no `EnvironmentMapEffect` yet (SDLGPU-33, deferred per the file's own header
  comment, confirmed still true — no `env_map3d.*` reference from `RenderTargetCube` or
  `TextureCube` sampling code in this file's own draw path), so the cross-cutting
  `EnvironmentMapEffect` EmissiveColor*DiffuseColor bug (Bgfx/Vulkan) and the Vulkan-specific
  `env_map3d.vert.glsl` Y-flip omission are **not applicable** to this file or this backend's
  render-target-cube feature area at all — there is no envmap-consuming shader here to share the
  defect.
- The fog-formula cross-cutting bug (EasyGL fixed, Bgfx/Vulkan still wrong) is likewise not
  applicable: none of this backend's 3D shaders (including `colored3d.frag.glsl`, used by Check
  B/C's `BasicEffect` draws) implement fog at all yet — confirmed via
  `FillLitLightUniforms`'s/`FillColoredUniforms`'s own doc comments ("minus fog, deliberately
  deferred") in `SdlGpuGraphicsBackend.cpp` lines 326-327 and 295-296 — so there is no formula to be
  wrong.
- Git history confirms the file's own header claims are current, not stale: `3d248aa7`/`68c6fd33`
  ("close SDLGPU-36 -- SDL_GPU RenderTargetCube (real MSAA + mips)") is this file's sole authoring
  commit; no later commit touches `SdlGpuRenderTargetCubeBackend` in a way that would contradict the
  header's SDLGPU-39-pulled-forward `GetData()` claim or the "no EnvironmentMapEffect yet" claim.

## Missing or Weak Tests

- See F1 (mip check uses a content-blind uniform-color oracle).
- No check combines two of {MSAA, mip-mapping, dedicated depth format} on the same
  `RenderTargetCube` in one test (each of B/C/D/E uses a fresh single-feature cube) — reasonable for
  a focused proof-of-each-feature test, but leaves the "MSAA cube + mip cube" interaction (already
  mutually exclusive by the ctor's own `mipMap_ && multiSampleCount_==0` guard, so combined
  behavior would silently downgrade to non-mipped rather than error) untested for whether that
  silent downgrade is itself surfaced anywhere the caller could observe (e.g.
  `getMultiSampleCountProperty()` staying correct while mip levels silently collapse to 1) — not a
  found defect, just an unexercised interaction.

## Positive Findings

- All 5 checks are backed by independently-verified, genuinely-distinct backend code paths (traced
  through `SdlGpuRenderTargetCubeBackend`'s ctor, `BindAsRenderTargetFace`, `RenderToTargetCubeFace`,
  and `GetData`) — this is not a "smoke test dressed up as a feature test."
- The MSAA cube-face resolve path (single shared 2D MSAA scratch texture, resolved per-face into
  the real 6-layer cube texture) correctly works around SDL_GPU's lack of a multisampled cube
  texture type, and the accompanying comment (lines 4489-4500) transparently documents a real
  Vulkan-validation bug this project found and fixed (`sample_count>1` forbidden on array
  textures) — good engineering discipline, and independently corroborated by reading the actual
  `RenderToTargetCubeFace` cycling logic (lines 876-888).
- Mip regeneration ordering relative to command-buffer submission (lines 734-749) correctly follows
  the `SDL_gpu.h` contract cited in the comment; this is a subtle ordering requirement that a naive
  port could easily get backwards.

## Final Assessment

A well-constructed, genuinely discriminating 5-check test with real per-face/per-level GPU readback
throughout; the underlying `SdlGpuRenderTargetCubeBackend` production code was independently traced
and found correct for every behavior this file exercises. Only gap found is the mip check's
content-blind oracle (F1, LOW) — worth tightening if this file is revisited, but not a live defect.
