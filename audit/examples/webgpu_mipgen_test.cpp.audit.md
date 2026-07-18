# Audit: examples/webgpu_mipgen_test.cpp

## Metadata

- Source file: `examples/webgpu_mipgen_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — real, render-pass-based mip generation test
  (`GenerateMipsForLayer()`/`GenerateMips2D()`/`GenerateMipsCubeFace()`), WebGPU backend (experimental,
  per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_mipgen`, CTest target `WebGPU_MipGen`
  (`cmake/Tests/WebGpuTests.cmake:186-187`).
- XNA/FNA relevance: indirect/deliberately divergent — this file's own header comment (lines 10-17)
  explicitly documents that this WebGPU-only behaviour (auto-generating real mip content for a plain
  `Texture2D`/`TextureCube` whenever `mipMap=true` and level-0 content is written) is **not** real
  FNA/XNA semantics (a plain `Texture2D`/`TextureCube` never auto-generates mip content from level 0 in
  FNA/XNA itself — only `VulkanGraphicsBackend`/`EasyGLGraphicsBackend`'s matching, FNA-faithful
  behaviour of regenerating mips for a render target being *unbound* is the real XNA-parity case).
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`EnsureMipBlitPipeline()` lines 4517-4617, `GenerateMipsForLayer()` lines 4628-4722,
  `GenerateMips2D()`/`GenerateMipsCubeFace()` lines 4724-4731, trigger sites
  `WebGPUTextureBackend::UpdatePixels()` lines 651-662 and `WebGPUTextureCubeBackend::SetData()`
  (referenced at line 882's comment), `WebGPUTextureBackend::GetData()` lines 690-780).

## Purpose

Four-check test using `IGraphicsBackend`/`ImageData` directly (bypassing the XNA `Texture2D`/
`TextureCube` layer, matching this shard's established convention for testing a backend capability with
no dedicated XNA-layer entry point): (A) a hard-edged, deliberately non-power-of-2-aligned red/blue
stripe uploaded to level 0 of a `mipLevels=4` `Texture2D`; level 1 read back and checked for a genuine
linear blend at the boundary texel pair (not a hard nearest-neighbor edge); (B) level 2 (chain-
downsampled from level 1, not directly from level 0) has real, plausible, non-garbage content; (C) the
same blending proof for one face of a `TextureCube`, proving `GenerateMipsCubeFace()` also filters
correctly and is correctly triggered by `TextureCube::SetData()`'s per-face level-0 write; (D)
`mipMap=false` construction with non-empty pixel data does not crash (the `mipLevels<=1` no-op guard).

## Executive Verdict

**Healthy**, with the file's own claims about the intentional FNA-divergent trigger condition
independently verified true against the current production code — including one subtlety the header
comment does not spell out (see Cross-File Observations): the mip generation is retriggered on *every*
subsequent level-0 write, not just at construction, which the test does not itself exercise but which
this audit confirmed by direct inspection.

## Checklist Results

### API / XNA / FNA parity

N/A in the strict sense (this file tests `CNA::Internal::Backends::IGraphicsBackend`/`ImageData`
directly, matching `examples/webgpu_texture2d_getdata_test.cpp`'s own established convention per the
header comment, lines 19-21) — the entire point of this file is a **documented, deliberate** deviation
from FNA/XNA semantics, correctly disclosed as such rather than silently introduced.

### Behavioral correctness

Independently re-derived the mip-blit technique against `EnsureMipBlitPipeline()`/
`GenerateMipsForLayer()`:
- `EnsureMipBlitPipeline()` (lines 4517-4617) builds a standard 3-vertex full-screen-triangle vertex
  shader (`vs_main`, deriving clip position/UV purely from `@builtin(vertex_index)`, no vertex buffer)
  and a fragment shader that samples the single-mip-level source view through a real
  `WGPUFilterMode_Linear` min/mag-filter sampler (lines 4609-4611) — since the bound source view has
  exactly one mip level (`srcViewDescriptor.mipLevelCount = 1`, line 4655), `mipmapFilter` cannot
  matter (no cross-level LOD blending is possible within a single-level view), so `minFilter=Linear`
  alone is what produces the genuine 2×2 bilinear downsample this test's Check A proves.
- `GenerateMipsForLayer()` (lines 4628-4722) records one render pass per level (`for (int level = 1;
  level < mipLevels; ++level)`), each sourcing `level-1`'s view and rendering into `level`'s own
  render-attachment view, with `srcW`/`srcH` correctly halved (`std::max(1, srcW/2)`) each iteration —
  confirming Check B's "level 2 sources level 1, not level 0" premise: since all levels' render passes
  are recorded into the **same** command encoder and submitted together, and GPU command buffers
  execute recorded render passes in program order, level 2's render pass is guaranteed to observe
  level 1's fully-written content by the time it begins, without needing an explicit barrier (the
  encoder's own sequential-recording order is sufficient).
- Hand-verified the stripe geometry: source width 8, `redColumns=3` (deliberately not power-of-2-aligned
  with the 2:1 downsample boundary, per the test's own comment) — level 1's dest pixel 1 draws from
  source texels (2,3), i.e. one red one blue, correctly producing the "genuine blend, not a hard edge"
  signal Check A's `IsBlend()` (tolerant range `[70,190]` on both R and B channels) is designed to catch;
  dest pixels 0 and 2/3 draw from same-colour texel pairs (0,1)/(4,5)/(6,7), correctly expected to stay
  solid red/blue.

### Logic

`WebGPUTextureBackend::GetData()` (lines 690-780) correctly reads the requested `level` parameter
(`source.mipLevel = level`, line 717) with per-level dimensions computed via `MipDim(width_, level)` —
not always level 0 — confirming this is a real per-level readback path, not a level-0-only
implementation that happens to satisfy Check A's `level=1` request by coincidence. Row addressing
(`sy=y+row`, `sx=x+col`, lines 762-774) applies no Y-flip, consistent with this being a direct texel copy
(not a render-target sample subject to a coordinate-convention mismatch).

### Robustness

Check D's guard (`mipLevels<=1` in both `GenerateMipsForLayer()`'s early return, line 4630, and the
caller-side `if (mipLevels_ > 1)` gate in `UpdatePixels()`, line 661) is doubly defensive — either guard
alone would already prevent the crash Check D tests for, so this is a genuinely low-risk path, correctly
confirmed non-crashing.

### Testing

Good, direct proof of genuine linear filtering (vs. nearest-neighbor) via a boundary-straddling texel
pair, correctly distinguishing "some content, plausibly correct" (Check B's weaker "not all-zero, alpha
opaque" check for a level with no simple predictable expected colour) from "provably filtered content"
(Checks A/C's stronger blend assertion). Not covered by this file (no claim otherwise): a
non-power-of-2 source dimension's mip chain terminating correctly at 1×1 (all `mipLevels=4`/size-8 cases
here terminate at 2×2, one level short of the theoretical 1×1 minimum — level 3, the actual 1×1 level,
is never read back or checked), a `TextureCube`'s *other* 5 faces staying unaffected by
`GenerateMipsCubeFace()`'s per-face regeneration (only `PositiveY`, face index 2, is checked), and
whether a later, in-place `SetData()`/`UpdatePixels()` call on an already-mipped texture correctly
regenerates the whole chain again (the header comment for `UpdatePixels()`, lines 651-662, explicitly
documents this re-trigger behaviour, but this test file only exercises the at-construction path for
`Texture2D` and the at-`SetData()` path for `TextureCube` — it does not test a *second*, later
level-0 rewrite of the same `Texture2D` to confirm the mip chain is regenerated rather than left stale).

### Architecture / Memory / Performance / Thread safety / Portability

No file-specific concerns. `GenerateMipsForLayer()`'s hardcoded `WGPUTextureFormat_RGBA8Unorm` color
target format (lines 4574, 4652) is consistent with `WebGPUTextureBackend`'s own constructor always
using `RGBA8Unorm` (confirmed at line 602) for the `SurfaceFormat::Color` case this test exercises;
whether a non-`Color` `SurfaceFormat` would be handled correctly is out of this file's scope (and not
independently checked by this audit).

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- This file's header comment (lines 10-17) is a rare, valuable example in this codebase of proactively
  disclosing an intentional, backend-specific behavioural divergence from both FNA and this project's
  own sibling backends, rather than silently introducing one — this audit independently confirmed the
  divergence is real (`VulkanGraphicsBackend`/`EasyGLGraphicsBackend` only regenerate mips when a render
  target is unbound; this backend also regenerates for a plain `Texture2D`/`TextureCube` on any level-0
  write) and correctly limited to this backend, matching the comment's own framing.
- Per this audit's cross-cutting mandate: this file has no lighting/effect-shader surface at all (it
  operates purely on raw texture upload/readback), so neither the confirmed skinned-normal-transform bug
  nor the `EnvironmentMapEffect` emissive/diffuse bug (see `webgpu_envmap3d_test.cpp`'s own audit)
  applies here.

## Missing or Weak Tests

- No check of the true 1×1 terminal mip level (level 3, for this test's 8×8/`mipLevels=4` source) —
  only levels 1 and 2 are read back.
- No check that a *second*, later `SetData()`/`UpdatePixels()` call on an already-mipped `Texture2D`
  correctly regenerates the full chain (only the at-construction trigger is exercised for `Texture2D`;
  the production code's own comment claims this re-trigger behaviour exists, but this file does not
  independently confirm it at the test level).
- No check that `GenerateMipsCubeFace()`'s per-face regeneration leaves the other 5 cube faces
  untouched (only `PositiveY` is populated and checked).

## Positive Findings

- The deliberately non-power-of-2-aligned stripe boundary (`redColumns=3` of 8, not 4 of 8) is a
  genuinely well-reasoned test-design choice: a boundary aligned exactly with the 2:1 downsample would
  produce a blocky result under real linear filtering too, making it impossible to distinguish from a
  broken nearest-neighbor implementation — this file avoids that trap.
- The chained-downsample proof (Check B, level 2 sourced from level 1 not level 0) correctly targets a
  real, plausible class of bug (a loop that always samples level 0 instead of the previous level) that
  a simpler "does level 1 look right" test would miss entirely.
- The header comment's honest disclosure of this backend's deliberate FNA/sibling-backend divergence is
  exemplary practice, independently confirmed accurate by this audit.

## Final Assessment

A well-designed, honestly-scoped test with no defects found in either its own logic or the
`WebGPUGraphicsBackend::GenerateMipsForLayer()`/`GenerateMips2D()`/`GenerateMipsCubeFace()` production
code it exercises. The genuine-linear-filtering proof technique (a deliberately-unaligned colour
boundary) is a good, transferable pattern; the main gaps are a missing terminal-mip-level check and no
test of the documented re-trigger-on-later-write behaviour.
