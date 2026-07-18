# Audit: examples/vulkan_rendertargetcube_mip_test.cpp

## Metadata

- Source file: `examples/vulkan_rendertargetcube_mip_test.cpp` (187 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RenderTargetCube` mip-chain generation
  no-corruption/no-crash test.
- File type: standalone `Game`-subclass executable (`class VulkanRenderTargetCubeMipTest`).
- XNA/FNA relevance: direct — `RenderTargetCube(device, size, mipMap=true, SurfaceFormat,
  DepthFormat)`; sampled indirectly via `EnvironmentMapEffect` (an XNA stock effect).
- Related production code:
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`,
  `VulkanRenderTargetCubeBackend::FaceProxy::MaybeGenerateMips` (line ~8875), the
  construction-time full-range initial-layout-transition fix (lines 8657–8684).
- Task references: Task 907 (`git log`: `5ac03715 feat(Task 907): implement RenderTargetCube mip
  chains on Vulkan and Bgfx`), Task 878 (the `RenderTarget2D` mip mechanism this ports),
  Task 875 (`140b2e05 fix(Task 875): Vulkan Clear()-only render targets now record a render pass`
  — cross-referenced by this file's own "must be a real draw call, not Clear() alone" comment).

## Purpose

Direct port of `vulkan_rendertargetcube_sample_test.cpp` (Task 334) with `mipMap=true` added,
proving `VulkanRenderTargetCubeBackend::FaceProxy::MaybeGenerateMips`'s per-face `vkCmdBlitImage`
cascade runs without corrupting level 0's content or crashing. Fills all 6 faces solid Blue (a real
draw call via `SpriteBatch`, not `Clear()` alone — Task 875's finding), unbinds, then samples the
cube via `EnvironmentMapEffect` at the exact backbuffer centre — must read back Blue.

The file's own header comment explicitly documents a **narrower scope decision** than the sibling
`RenderTarget2D` mip test in this shard: it does *not* attempt to assert on coarser mip *levels'*
content specifically, because (a) an earlier attempt at a per-face asymmetric split + forced-coarse-LOD
technique was tried and found non-discriminating (`git stash`-reverting the production fix still
passed identically), and (b) cube-map mip levels never blend across faces, so a uniform-per-face
solid-colour pattern structurally cannot discriminate at any LOD. It also documents catching a
**real, separate bug** while writing this narrower test: the per-face blit cascade's first barrier
for each destination level assumed it started in `SHADER_READ_ONLY_OPTIMAL`, which was false for
never-rendered-into levels, producing live `VUID-vkCmdDraw-None-09600` validation errors — fixed
with a construction-time full-range initial transition.

## Executive Verdict

**Healthy** — this file's own "narrower scope, deliberately not X" framing was independently
verified against the actual production code and found to be an accurate, load-bearing description
(not an excuse for weaker coverage): the construction-time initial-transition fix it credits itself
with catching is present in the live source exactly as described, and the reasoning for why a
per-level content assertion is structurally impossible for this technique holds up under
independent analysis.

## Checklist Results

### API / XNA / FNA parity — PASS
`RenderTargetCube(device, kCubeSize, /*mipMap=*/true, SurfaceFormat::Color, DepthFormat::None)`
(line 82–83) is the standard XNA-compatible constructor overload. `EnvironmentMapEffect`'s
`setDiffuseColorProperty`/`setEmissiveColorProperty`/`setEnvironmentMapAmountProperty`/
`setEnvironmentMapSpecularProperty`/`setTextureProperty`/`setEnvironmentMapProperty` (lines
130–135) match FNA's `EnvironmentMapEffect` public surface (`DiffuseColor`, `EmissiveColor`,
`EnvironmentMapAmount`, `EnvironmentMapSpecular`, `Texture`, `Texture` overload for the cube map).

### Behavioral correctness — PASS
Independently traced the construction-time fix this file's header comment credits itself with
motivating: `VulkanRenderTargetCubeBackend`'s constructor (lines 8657–8684) transitions "every
level of every face to SHADER_READ_ONLY_OPTIMAL up front" via a single `vkCmdPipelineBarrier` whose
`subresourceRange` spans `{0, levelCount_, 0, 6}` (all levels, all 6 array layers) — exactly
matching the comment's description, and exactly the fix needed for `FaceProxy::MaybeGenerateMips`'s
first per-destination-level barrier (which assumes `SHADER_READ_ONLY_OPTIMAL` as its `oldLayout`)
to be valid even for mip levels that no render pass has ever touched. This is gated by `if
(levelCount_ > 1)` (line 8666), correctly scoped to only run when mips were actually requested.

Independently verified the "cube-map mips can't blend across faces" reasoning: `EnvironmentMapEffect`'s
reflection-vector sampling of a cube map fundamentally selects one of 6 independent 2D mip chains
per fragment (there is no cross-face blending in the cube-sampling hardware path itself), so a
per-face-uniform fill pattern (as used here, all 6 faces solid Blue) is, as the comment states,
structurally incapable of producing a within-face gradient a coarser-LOD assertion could
distinguish from level 0 — this is a correct, not merely asserted, limitation of the chosen
verification technique, not a gap the test authors overlooked.

### Logic — PASS
The comment for the reflection-vector geometry ("`normalize(vertexPosition.xy, 0)` has a zero Z
component almost everywhere... overwhelmingly samples the cube's four side faces") is not
independently re-derived in this audit (it belongs to the *sibling* MSAA cube test's header
comment, cross-referenced here only implicitly via the shared technique) but is consistent with
the flat-quad, fixed-`(0,0,1)`-normal geometry both files use (`n(0.0f, 0.0f, 1.0f)`, line 141) and
`World=View=Projection=Identity` (lines 136–138) — under those exact conditions the reflection
vector is genuinely determined mostly by the vertex's own `(x,y)` position, making this a
structurally sound (if narrow) technique.

### C++ correctness — PASS
`rtc_`, `whiteTex_`, `sb_`, `blueTex_` are all `unique_ptr` members constructed once in
`Initialize()` (lines 82–90); `done_` guards `Draw()` against re-entry (line 95) consistent with
this shard's established single-shot test pattern.

### Robustness — PASS
The pass condition (`centPx.R<=50 && G<=50 && B>=200`, lines 159–161) requires both a positive
signal (high blue) and negative signals (low red/green) — correctly rejects an ambiguous
partially-blue-tinted result as a failure rather than a weak pass.

### Testing — PASS (within its own explicitly narrower stated scope)
Given the file's own honestly-declared scope ("confirm the identical mechanism... still produces a
valid, correctly-populated level 0 and doesn't crash or corrupt anything when mipMap=true" — not a
per-level content assertion), the single centre-pixel check is proportionate and sufficient for
that stated goal; it is not over-claiming a stronger verification than it actually performs.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — This test cannot, by its own admission, detect a mip-generation regression that corrupts level 0's *own* content in a way EnvironmentMapEffect's centre-sample wouldn't reach

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: the single centre-pixel `pass` check (lines 155–161)
- Evidence: this file only ever samples via `EnvironmentMapEffect`'s reflection-vector geometry,
  which — per this file's own header comment and this audit's independent confirmation above —
  overwhelmingly samples the cube's *side* faces for this exact flat-quad/fixed-normal setup, not
  necessarily every one of the 6 faces on every run (device/driver-dependent reflection-vector
  rounding at the geometric centre vertex could shift which face(s) actually get sampled).
- Why it matters: this is not a defect in the code under test, but it does mean a hypothetical
  regression that corrupted, say, only the `PositiveZ`/`NegativeZ` faces' mip chain specifically
  could plausibly go undetected by this test if the reflection geometry never samples those faces
  at the one point checked — a narrower residual gap than the file's own comment (which focuses on
  the "coarser LOD content" limitation) explicitly discusses.
- FNA/XNA comparison: N/A — a test-coverage observation about verification technique, not an
  XNA/FNA behavior question.
- Suggested action (not implemented by this audit): not urgent given the shared mip-generation
  mechanism (`vkCmdBlitImage` cascade) is already rigorously verified per-face-independent-of-content
  by the `RenderTarget2D` sibling test in this shard (which this audit separately confirmed
  performs genuine per-level content verification); a future test could fill each face a distinct
  colour to widen this file's own face coverage, but this is a marginal, non-blocking improvement.

## Cross-File Observations

- Shares its `MaybeGenerateMips` blit-cascade mechanism with `vulkan_rendertarget2d_mip_test.cpp`'s
  `VulkanRenderTargetBackend::MaybeGenerateMips` — this audit's independent box-filter re-derivation
  for that sibling file (confirming the cascade exactly preserves a solid-region's weighted average
  through every level) applies equally to the per-face cascade here, since both are structurally
  identical `vkCmdBlitImage`/`VK_FILTER_LINEAR` halvings.
- The construction-time full-range initial-transition fix this file credits itself with motivating
  is present with near-identical phrasing on the `RenderTarget2D` side too, per this shard's
  `vulkan_rendertarget_depthformat_fidelity_test.cpp` audit's cross-file note on the matching
  Task 911 per-instance-depth-format comment — this project consistently documents matching fixes
  applied in parallel across the 2D and cube-map render-target backends, a good sign of
  deliberate cross-backend consistency rather than one-off patching.
- Uses `RasterizerState::CullNone` (line 152) for the identical Task 896 reason as every other file
  in this shard — consistent, not independently reinvented.

## Missing or Weak Tests

See F1 — no test in this shard fills each of the 6 cube faces with a *distinct* colour to broaden
per-face mip-generation coverage beyond the single reflection-sampled point this file (and its MSAA
sibling) checks. This is an acknowledged, deliberate scope limitation (per the file's own comment)
rather than an oversight, and the underlying mechanism is independently verified elsewhere in this
shard.

## Positive Findings

- The file's own "tried harder first, abandoned for a documented reason" scope narrative
  (attempting and then rejecting a per-face asymmetric-split differential) was independently
  assessed as technically sound reasoning, not an excuse for under-testing.
- The construction-time initial-layout-transition bug this file's own narrower test caught is a
  genuine, real Vulkan validation-layer bug (confirmed present via `git stash` per the comment, and
  the fix is confirmed present and correctly scoped in the live source) — a good example of a
  "narrower goal" test still finding a real defect its own primary pixel assertion couldn't have
  caught on its own (the comment is explicit that the pixel output was correct even with the bug
  present; only the validation layer caught it).

## Final Assessment

An honestly-scoped test whose stated limitations were independently verified as genuine (not
under-testing dressed up as a deliberate choice), and whose credited bug-find (the initial-layout
barrier fix) is confirmed present in the live production code exactly as described.
