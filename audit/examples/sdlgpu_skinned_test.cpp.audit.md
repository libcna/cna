# Audit: examples/sdlgpu_skinned_test.cpp

## Metadata

- Source file: `examples/sdlgpu_skinned_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `SkinnedEffect` proof for the SDL_GPU backend, plus a
  72-bone (4608-byte) push-uniform-capacity spike
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_Skinned`,
  `cmake/Tests/SdlGpuTests.cmake:75-78`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: direct — `SkinnedEffect.SetBoneTransforms`, `WeightsPerVertex`,
  `EnableDefaultLighting`.
- FNA reference: `Graphics/Effect/SkinnedEffect.cs`, the shared stock `SkinnedEffect.fx`
  (`Skin(vin, boneCount)` helper in `Structures.fxh`/`Common.fxh`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`
  (`FillGpuDrawParams`, lines 320-400+), `src/CNA/Internal/Backends/SdlGpu/shaders/
  skinned3d.vert.glsl`, `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`FillSkinnedBoneUniforms`, lines 412-422; `QueueSkinnedDraw`, lines 2986-3036;
  `GetOrCreatePipelineSkinned3D`, lines 2627-2682).

## Purpose

Three-check pixel test proving real `SkinnedEffect` bone-palette skinning on the SDL_GPU backend,
and the empirical spike this task itself required: whether SDL_gpu's push-uniform mechanism can
carry a full 72-bone (4608-byte) palette. (A) a single translation bone shifts a quad right
(left/centre/right sample pattern), (B) a genuine 50/50 two-bone weighted blend (not just the first
bone winning), (C) a translation bone at palette index 71 (the last slot) produces the identical
shift to Check A's index-0 bone, proving the full 72-matrix buffer is transmitted intact and
indexable at the far end. Checks A/B are explicitly stated as ported from this project's own
existing Vulkan equivalents; Check C is this file's own novel empirical-limit proof. Correct
placement for a backend `SkinnedEffect` integration test.

## Executive Verdict

**Needs attention** — the three checks themselves are sound and independently confirmed against
real production code (bone-buffer wiring, weight-count gating, and the storage-buffer-not-push-
uniform architectural decision are all correctly implemented and correctly exercised). However,
this test's `World=Identity` scene means it cannot detect — and does not claim to detect — that the
underlying `skinned3d.vert.glsl` shares this codebase's now cross-cutting-confirmed
World-normal-matrix omission (see F1), a defect already independently found in EasyGL, WebGPU, and
Vulkan's own `SkinnedEffect` shaders.

## Checklist Results

### API / XNA / FNA parity
`SkinnedEffect::SetBoneTransforms(bones)` (line 107), `setWeightsPerVertexProperty(weightsPerVertex)`
(line 108), and `EnableDefaultLighting()` (line 109) all map directly to FNA's `SkinnedEffect`
public surface with matching names/semantics.

### Behavioral correctness
- Check A (lines 147-163): `bones={CreateTranslation(0.5,0,0)}`, weight 1 at index 0.
  `skinned3d.vert.glsl`'s `main()` computes `skinMat = bb.bones[inBoneIndices.x] *
  inBoneWeights.x` (only, since `weightsPerVertex < 2.0`) — a single-bone-weight-1 skin is
  mathematically just that bone's own transform, so the quad (originally spanning x∈[-1,0]) shifts
  to x∈[-0.5,0.5], covering the render target's centre column but not its left/right eighths — the
  test's `left`/`centre`/`right` sample-point assertions (lines 159-161: left/right still show the
  green clear color, centre shows red) are consistent with this geometry.
- Check B (lines 165-185): `bones={CreateTranslation(-0.5,0,0), CreateTranslation(1.5,0,0)}`,
  weights `(0.5,0.5)` at indices `(0,1)`, `weightsPerVertex=2`. `skinMat = bones[0]*0.5 +
  bones[1]*0.5` (the `weightsPerVertex >= 2.0` branch adds the second term, line 65 of the shader) —
  this is a genuine linear combination of two *distinct* 4×4 matrices (not, e.g., two identical
  matrices that would pass even with a "sums only the first weight" bug), and the combined
  translation `(-0.5)*0.5 + (1.5)*0.5 = 0.5` matches Check A's single-bone shift exactly, so the
  same left/centre/right pattern is the mathematically correct expected result — a real proof that
  a two-bone sum is computed, not just the first bone's weight winning (which would incorrectly
  shift by `-0.5`, landing the quad on the *left*, not centre).
- Check C (lines 187-208): `bones(72, Identity)`, `bones[71] =
  CreateTranslation(0.5,0,0)`, vertex `blendIndices=(71,0,0,0)`, weight 1, `weightsPerVertex=1`.
  Traced `FillSkinnedBoneUniforms` (lines 412-422): copies `min(p.boneCount,72)*16` floats from
  `GpuDrawParams::boneTransforms` into a 1152-float (`72*16`) output array, so index 71 genuinely
  occupies bytes `[71*64..72*64)` of the 4608-byte buffer, and the shader's `bb.bones[inBoneIndices.x]`
  with `inBoneIndices.x=71` reads that exact slot via `std430` storage-buffer indexing (not a push
  uniform, per the shader's own doc comment on why: SDL_gpu's push-uniform mechanism was empirically
  found to have a real ~4096-byte cap in this environment, undocumented in `SDL_gpu.h`). This
  reproduces Check A's exact shift, which is the correct differential — if the buffer were silently
  truncated or corrupted past some byte offset less than 4608, index-71 data would either read as
  the zero-initialized tail (`FillSkinnedBoneUniforms`'s own `for (int i = count*16; ...) out[i] =
  0.0f;` zero-fill, line 420-421 — a *degenerate* all-zero 4×4 matrix, not `Identity`, so the quad
  would not render at all rather than merely fail to shift) or garbage GPU memory (an unpredictable
  shift/rotation), either of which would fail this exact-shift-match assertion. The test's own
  documented empirical motivation (SDL_gpu's push-uniform ~4096-byte cap not being well-known/
  documented) is corroborated by this codebase's own shader-level doc comment, not just this test
  file's claim.

### Logic
`SkinnedGpuVertex`'s `static_assert(sizeof(...) == 52, ...)` (line 70) matches
`GetOrCreatePipelineSkinned3D`'s own `vbDesc.pitch = hasVertexColor ? 56 : 52` (line 2643) and its
5-attribute (non-color) layout (lines 2647-2651) exactly — position(12)+normal(12)+uv(8)+
weights(16)+indices(4, `UBYTE4`, offset 48) = 52 bytes, matching the vertex struct's own field
layout byte-for-byte.

### C++ correctness
`RenderAndSample` (lines 93-126) correctly re-clears and rebuilds the full render/effect/draw state
for each of the 3 checks (fresh `SkinnedEffect fx(dev)` per check, lines 102), so there is no
cross-check state leakage risk from a stale bone palette or lighting setting.

### Robustness
No exception-guarding around any of the 3 checks (unlike, e.g., `sdlgpu_samplerstate_test.cpp`'s
frame-1 `try`/`catch`) — an uncaught exception during `RenderAndSample` (e.g. a
`VertexBuffer::SetDataRaw` stride mismatch) would crash the test binary rather than reporting a
clean FAIL. Low practical risk given the fixed 52-byte stride is asserted at compile time via
`static_assert`, but this is a shard-wide inconsistency (some `sdlgpu_*` tests wrap their checks in
`try`/`catch`, this one does not) rather than specific to this file's own logic.

### Testing
3/3 checks are each individually meaningful (not redundant): single-bone shift, genuine two-bone
blend, and full-palette-capacity/indexing. `WeightsPerVertex=4` (the FNA-supported max) is not
exercised by this file — only 1 and 2 are — a real, if minor, coverage gap (see Missing Tests).

## Detailed Findings

### F1 — `skinned3d.vert.glsl` (this backend's `SkinnedEffect` vertex shader) transforms the normal by the skin matrix alone, with no World-space normal-matrix contribution — the same cross-cutting defect already confirmed in EasyGL, WebGPU, and Vulkan

- Severity: MEDIUM (confirmed-present in the shader; **not observable through this specific test**,
  since `World=Identity` throughout, matching the exact masking pattern already documented for the
  other 3 backends)
- Confidence: HIGH (read the shader source directly)
- Category: correctness / FNA-parity / cross-cutting (see `AUDIT_CROSS_CUTTING_FINDINGS.md`,
  "CONFIRMED SYSTEMIC, MULTI-BACKEND: skinned-effect shaders skip the WorldInverseTranspose normal
  transform")
- Location/symbol: `skinned3d.vert.glsl` line 75: `fragNormal = normalize(mat3(skinMat) *
  inNormal);` (no `lp.world`/`WorldInverseTranspose` factor anywhere in the normal computation,
  despite `lp.world` being available in the very same `SkinnedLightParams` uniform block, used only
  for `fragWorldPos` at line 76)
- Evidence: the shader's own comment (lines 71-74) states verbatim: *"The normal is transformed by
  the skin matrix alone, with no additional World normal-matrix contribution -- mirrors
  VulkanGraphicsBackend's own skinned3d.vert.glsl exactly (an established simplification already
  shared by every backend implementing SkinnedEffect in this codebase, not something introduced
  here)."* This audit independently confirms the comment's own characterization is accurate given
  the current `AUDIT_CROSS_CUTTING_FINDINGS.md` record (EasyGL/WebGPU/Vulkan all independently
  confirmed to share this exact simplification) — SdlGpu is a **fourth** confirmed instance of the
  same systemic gap, not a new/different bug.
  This test's own scene (`World=Identity`, lines 104, 140) makes `WorldInverseTranspose == Identity`,
  so the omission produces byte-identical output to a correct implementation *for this specific
  scene* — exactly the masking mechanism the cross-cutting findings doc already describes for the
  other 3 backends.
- Why it matters: any real game rotating or non-uniformly scaling a skinned model's `World` matrix
  (the overwhelmingly common case for actual animated characters, as opposed to a static test quad)
  would see incorrectly-lit normals on this backend — lighting would rotate/scale with the mesh's
  local skin-space orientation rather than staying consistent with the mesh's actual world
  orientation. None of this file's 3 checks could ever surface this, since none varies `World` away
  from `Identity`.
- FNA/XNA comparison: FNA's real `SkinnedEffect.fx` (via `Structures.fxh`'s `Skin()` helper composed
  with the shared `Lighting.fxh` `ComputeLights()`) does apply the full world-space normal transform
  after skinning — this is a genuine parity gap relative to real XNA/FNA behavior, consistent with
  the cross-cutting finding's characterization.
- Related files: `src/CNA/Internal/Backends/SdlGpu/shaders/skinned3d.vert.glsl` (line 75),
  `skinned_colored3d.vert.glsl` (line 66, identical pattern — see
  `sdlgpu_skinnedeffect_vertexcolor_test.cpp.audit.md`), and (with an *additional*, related but
  distinct defect) `pbr_skinned3d.vert.glsl` — see
  `sdlgpu_skinnedpbreffect_test.cpp.audit.md`'s F1 for that shader's own variant of the same root
  issue.
- Suggested future action (not implemented by this audit): add a companion test with a non-identity
  `World` (e.g. a 90° rotation) to at least one existing bone-translation check, which would turn
  this from a masked-but-latent defect into an actively-failing regression test the moment the
  underlying shader is fixed to match FNA (or would immediately demonstrate the bug if run against
  today's shader) — this is the single highest-value test addition across this whole cross-cutting
  defect family, not specific to this file.

## Cross-File Observations

- **This audit independently confirms `SkinnedEffect::FillGpuDrawParams()` (the shared, backend-
  agnostic production source, not the SdlGpu backend itself) does *not* share the Vulkan-specific
  ambient/emissive-color bug** noted in `AUDIT_CROSS_CUTTING_FINDINGS.md` ("`SkinnedEffect::
  FillGpuDrawParams()` never sets `ambientColor`, and Vulkan's skinned shaders never consume
  `emissiveColor`"). Reading `SkinnedEffect.cpp` lines 335-338 shows `p.emissiveColor` is
  deliberately pre-combined with `ambientLightColor_ * diffuseColor_` (comment: *"Emissive
  pre-combined with ambient * diffuse (matches FNA's colour upload)"*) rather than populating a
  separate `p.ambientColor` field — and `SdlGpuGraphicsBackend::QueueSkinnedDraw` correctly forwards
  both `FillExtUniforms` (which reads `p.ambientColor`, currently 0 for skinned draws by design) and
  `FillSkinnedLightUniforms`/`FillLitLightUniforms` (which reads `p.emissiveColor`, lines 3010-3012)
  — since `skinned_colored3d.frag.glsl`'s (and by extension `lit_textured3d.frag.glsl`'s, reused
  unchanged for the non-vertex-color path) `lit = lightSum * fragTint.rgb + emissiveColor` formula
  adds the two contributions separately, the net result (`ambient(=0)*diffuse + light_diffuse_sum*
  diffuse + (emissive+ambient*diffuse)`) is mathematically identical to the alternative convention
  of populating `ambientColor` directly and leaving it out of `emissiveColor`. **This is a genuine
  positive finding, distinguishing SdlGpu from the Vulkan-specific defect** — worth confirming this
  observation propagates to the cross-cutting findings doc as "Vulkan-specific, NOT shared by
  SdlGpu."
- Git history (`7f3a6df2`/`c4a15ed0`, "close SDLGPU-34 -- SDL_GPU SkinnedEffect") is this file's sole
  authoring commit; no later commit touches `skinned3d.vert.glsl`'s normal computation, so F1 is not
  a stale claim about since-fixed code — it is a currently-live characteristic of the shipped
  shader.
- The fog-formula cross-cutting bug does not apply here: `skinned3d.vert.glsl`/
  `skinned_colored3d.vert.glsl` implement no fog term at all (this backend's 3D shaders uniformly
  defer fog, per multiple doc comments already cited in this shard's other reports).

## Missing or Weak Tests

- See F1 — no test in this file exercises a non-identity `World` matrix, so the normal-matrix
  omission is entirely unobservable from this file alone.
- `WeightsPerVertex=4` (the FNA-supported maximum, summing all 4 weight/index pairs) is untested —
  only 1 and 2 are exercised. The shader's own `weightsPerVertex >= 4.0` branch (lines 66-67 of
  `skinned3d.vert.glsl`) is present and plausible by inspection but has no dedicated pixel-level
  proof in this file.
- No test exercises `SkinnedEffect`'s directional-lighting parameters (`DirectionalLight0/1/2`,
  `AmbientLightColor`, `EmissiveColor`) in combination with skinning on this backend within this
  file — `EnableDefaultLighting()` is called (line 109) but its specific numeric output is never
  asserted (checks only look at qualitative red-vs-green channel dominance), so a defect in how
  lighting parameters combine with a skinned normal would not be caught here (though it likely is
  covered by `sdlgpu_skinnedeffect_vertexcolor_test.cpp`'s own checks — see that file's report).

## Positive Findings

- Check C's index-71/full-palette-capacity design is a genuinely well-reasoned test: it does not
  merely assert "no exception was thrown for a big buffer" but proves the *specific, byte-precise*
  slot at the far end of a 4608-byte buffer round-trips correctly, via a real geometric differential
  (same shift as Check A) rather than an easily-satisfied loose bound.
- Check B's two-bone blend uses two *different* translation values whose weighted sum coincidentally
  equals Check A's single-bone shift — a deliberately strong design choice that makes the test
  immune to a "sums only the first weight" bug class, which a naive two-identical-bones test would
  have missed.
- Independently confirmed (Cross-File Observations) that this backend's `SkinnedEffect` ambient/
  emissive handling does **not** share the Vulkan-specific defect already documented in the
  cross-cutting findings — a genuine positive differentiator worth recording.

## Final Assessment

A well-constructed 3-check test with a genuinely novel and rigorous full-palette-capacity proof
(Check C); all 3 checks were independently confirmed correct against real production code. The one
substantive finding (F1) is not a defect in this test file — it is confirmation that this backend's
`SkinnedEffect` shader shares a already-known, cross-backend, `World=Identity`-masked normal-matrix
omission, invisible to this file's scene by construction, consistent with the same pattern already
established in 3 other backends.
