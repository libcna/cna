# Audit: examples/webgpu_instanced3d_test.cpp

## Metadata

- Source file: `examples/webgpu_instanced3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `DrawInstancedPrimitivesEx()`/instanced-rendering test,
  WebGPU backend (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_instanced3d`, CTest target `WebGPU_Instanced3D`
  (`cmake/Tests/WebGpuTests.cmake:95-96`).
- XNA/FNA relevance: indirect — `GraphicsDevice.DrawInstancedPrimitives()` (this file tests the
  backend-agnostic `IGraphicsBackend::DrawInstancedPrimitivesEx()` entry point directly, bypassing the
  XNA-layer `VertexBufferBinding`/instance-frequency plumbing, which the file's own header comment
  correctly notes is pre-existing and backend-agnostic).
- FNA reference: N/A directly (per-instance vertex streams are an XNA 4.0 feature with no dedicated
  stock-effect `.fx` reference; this is CNA/backend plumbing correctness, not an effect-formula parity
  question).
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreateInstancedResources()` lines 4259-4316, `GetOrCreatePipelineInstanced3D()` lines 4318-4406,
  `DrawInstancedPrimitivesEx()` lines 6166-6227, `RenderInstancedDraws()` lines 4408-4484).

## Purpose

Five-check test proving `DrawInstancedPrimitivesEx()`'s second, per-instance vertex buffer
(`WGPUVertexStepMode_Instance`) is genuinely consumed, not ignored or read once and reused: (A/B/C)
one draw call with `instanceCount=3`, each instance a distinct pure-X-translation applied via the
per-instance buffer, painting a small quad at 3 independently-predicted, distinct screen locations with
the exact shared `DiffuseColor` — a buggy implementation that ignored the per-instance stream (or always
read instance 0) could satisfy at most one of the three checks, never all three simultaneously; (D) a
region far from all three quads stays the clear colour, ruling out an implementation that happens to
overlap by drawing something much larger; (E) `params.instanceVb == nullptr` falls back to a real
(non-instanced) `DrawIndexedPrimitivesEx()` draw instead of throwing or corrupting the frame.

## Executive Verdict

**Healthy.** Independently traced the full instanced-draw pipeline (shader, vertex-buffer-layout
construction, dispatch, and the `instanceVb==nullptr` fallback) against the test's own expectations and
found no divergence — the per-instance mat4 world-transform binding, its column layout, and the
instance-count propagation into `wgpuRenderPassEncoderDrawIndexed`/`Draw` are all correctly wired.

## Checklist Results

### API / XNA / FNA parity

N/A in the strict XNA-surface sense (this file deliberately bypasses `Microsoft::Xna::Framework`
entirely, testing `CNA::Internal::Backends::IGraphicsBackend`/`GpuDrawParams` directly) — a documented,
reasonable choice given this backend's header (`WebGPUGraphicsBackend.hpp`) transitively requires
`wgpu-native`'s `webgpu.h`, whose include path is private to `cna_backend_graphics_webgpu`'s own
translation units (header comment lines 10-15, independently confirmed plausible: unlike
D3D9/D3D11/Vulkan's headers, which don't have this restriction, per the comment's own comparison).

### Behavioral correctness

Re-derived the per-instance vertex-buffer contract against `CreateInstancedResources()`'s WGSL (lines
4272-4305):
```
struct InstanceInput {
    @location(4) instCol0: vec4f, @location(5) instCol1: vec4f,
    @location(6) instCol2: vec4f, @location(7) instCol3: vec4f,
};
let world = mat4x4f(instance.instCol0, instance.instCol1, instance.instCol2, instance.instCol3);
output.position = u.vp * world * vec4f(input.position, 1.0);
```
and `GetOrCreatePipelineInstanced3D()`'s matching `WGPUVertexBufferLayout` for binding 1
(`stepMode = WGPUVertexStepMode_Instance`, 4× `Float32x4` attributes at offsets 0/16/32/48, lines
4342-4364) — this exactly matches the test's own `InstanceColumns`/`TranslationInstance()` helper
(lines 89-99): 4 column-major `float[4]` blocks, with the translation in `col3 = (Tx,Ty,Tz,1)`. Since
`u.vp` is `View*Projection` (not a full MVP — `FillExtUniforms()` is reused verbatim per the production
comment at line ~6219-6222, with the caller's real `World` matrix deliberately absent because
per-instance transforms replace it), and the test passes `Matrix::getIdentityProperty()` for all three
of world/view/projection to `DrawInstancedPrimitivesEx()`, the final clip-space X for each instance is
exactly its own translation column, confirming the test's own "NDC `(tx,0)` → pixel `((tx+1)/2)*64`"
derivation (comment lines 158-161: pixel 16/32/48 for `tx=-0.5/0.0/+0.5`).

`RenderInstancedDraws()` (lines 4454-4457, 4470) correctly binds the per-vertex buffer at slot 0 and the
per-instance buffer at slot 1 (`wgpuRenderPassEncoderSetVertexBuffer(pass, 1, instVertexBuffer, ...)`),
and passes `command.instanceCount` (not a hardcoded `1`) as the instance-count argument to both
`wgpuRenderPassEncoderDrawIndexed`/`wgpuRenderPassEncoderDraw` — confirming checks A/B/C's "all 3
instances actually issued" premise structurally, not just via the pixel-colour outcome.

### Logic

Check E's fallback: `DrawInstancedPrimitivesEx()` (lines 6172-6178) checks `params.instanceVb ==
nullptr` **first**, before any WebGPU-specific casting, and forwards to the ordinary
`DrawIndexedPrimitivesEx()` — the test's own comment (lines 26-31) correctly notes this "matches
`D3D11GraphicsBackend::DrawInstancedPrimitivesEx()`'s own identical fallback contract." This audit
confirms the WebGPU implementation of that fallback is real (a genuine re-dispatch, not a no-op) by
inspection of the early-return structure.

### C++ correctness

The odd-vs-even index-count note in the header comment (lines 70-76: "an odd index count is only 6
bytes, which trips this backend's `wgpuQueueWriteBuffer` `COPY_BUFFER_ALIGNMENT` requirement") is
consistent with `wgpu-native`'s real 4-byte alignment requirement on buffer writes; `kSmallQuadIdx`
(6 `uint16_t` = 12 bytes) and the check-E quad's own 6-index buffer are both already 4-byte-aligned, so
this documented constraint is correctly avoided rather than silently hit.

### Robustness

Check D (a screen region far from all 3 quads must stay the original clear colour) is a good,
easy-to-omit negative check: it rules out an implementation bug where a much larger primitive happens
to cover the expected sample points by accident (e.g. an off-by-one in `instanceCount` that
happens to still touch pixel 16/32/48 while also covering the rest of the screen).

### Testing

Good coverage of the instanced-draw path itself (per-instance buffer genuinely read per-instance, not
just for instance 0) and its `instanceVb==nullptr` fallback. Not covered by this file (no claim
otherwise): instance counts other than exactly 3, a non-identity `World`/`View`/`Projection` combined
with instancing (this test uses identity throughout, so whether the instance transform composes
correctly with a non-trivial base transform is untested), and non-translation per-instance transforms
(e.g. per-instance rotation/scale) — all instances here use a pure-translation matrix, so a bug that
only manifested for a non-axis-aligned instance transform (e.g. a column/row-major mixup beyond simple
translation) would not be caught.

### Architecture / Memory / Performance / Thread safety / Portability

No file-specific concerns. Deliberately tests at the `IGraphicsBackend` layer rather than through
`GraphicsDevice`, a reasonable and explicitly-justified architectural choice for this specific
capability given the header-visibility constraint described above.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- Per this audit's cross-cutting mandate: this file uses a plain untextured/unlit instanced shader with
  no skinning or normal transform at all (`instanced3d.wgsl` reads only `position`, discarding any
  colour/normal/UV present in the buffer's real stride per the header comment's own note, lines 9-11),
  so neither the confirmed `CreateSkinnedResources()` normal-transform bug nor the
  `EnvironmentMapEffect` emissive/diffuse bug (see `webgpu_envmap3d_test.cpp`'s own audit) applies here
  — this shader has no lighting math to be wrong in.
- Consistent with `VulkanGraphicsBackend`'s own `instanced3d.{vert,frag}.glsl` per the header comment's
  explicit porting note (line 3) — this audit did not independently re-check the Vulkan source but has
  no reason to doubt the comment given how precisely the WebGPU side matches its own stated contract.

## Missing or Weak Tests

- No coverage of non-translation per-instance transforms (rotation/scale), or of instancing combined
  with a non-identity base `World`/`View`/`Projection`.
- No coverage of instance counts other than 3 (e.g. 1, a large count, or 0).

## Positive Findings

- The three-instance differential (A/B/C) is a rigorous design: a broken implementation could satisfy
  at most one of the three checks by accident, never all three, making this a genuine proof rather than
  a single-sample coincidence.
- Check D's "painted only the small quads, not the whole screen" negative check is an easy-to-omit but
  valuable safeguard against a false-positive from an oversized draw.
- The header comment's transparent explanation of *why* this file tests at the `IGraphicsBackend` layer
  (a genuine header-visibility constraint, not an arbitrary choice) and *why* the index buffer uses an
  even count (a genuine, documented `wgpu-native` alignment requirement) both reflect good engineering
  discipline — this audit verified both claims and found them accurate.

## Final Assessment

A rigorous, well-designed test with no defects found in either its own logic or the
`WebGPUGraphicsBackend::DrawInstancedPrimitivesEx()`/`GetOrCreatePipelineInstanced3D()` production code
it exercises. The per-instance vertex-buffer plumbing this file exists to prove is genuinely correct on
this backend.
