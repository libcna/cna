# Audit: examples/webgpu_graphicsstate_test.cpp

## Metadata

- Source file: `examples/webgpu_graphicsstate_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — cull mode / blend state / scissor / viewport / fill mode
  pipeline-state test, WebGPU backend (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_graphicsstate`, CTest target `WebGPU_GraphicsState`
  (`cmake/Tests/WebGpuTests.cmake:122-123`).
- XNA/FNA relevance: direct — `RasterizerState.CullMode`/`FillMode`/`ScissorTestEnable`,
  `BlendState.NonPremultiplied`/`Opaque`, `GraphicsDevice.ScissorRectangle`/`Viewport`.
- FNA reference: no single `.fx` file — this exercises `GraphicsDevice`-level state objects
  (`RasterizerState`, `BlendState`, `Viewport`), so parity is judged against FNA's
  `GraphicsDevice`/`FNA3D` state-application semantics rather than a stock effect shader.
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`ToWGPUCullMode()` lines 301-309, `ApplyBlendState()`/`ApplyRasterizerState()` lines 4919-4945,
  `SetScissorRect()`/`SetViewport()` lines 4968-4985, `FillWGPUBlendState()`/`ToWGPUBlendFactor()`/
  `ToWGPUBlendOperation()` lines 239-289).

## Purpose

Seven-check test proving this backend's `BlendState`/`RasterizerState`/scissor/viewport wiring is real,
not a silent no-op fallback to `IGraphicsBackend`'s defaults (per the header comment, this was
previously the case — "every 3D draw silently ignored these... confirmed by grepping this backend's own
.cpp before this task"): (A/B) a differential cull-mode pair proving direction-correct winding-based
culling; (C/D) a differential blend-state pair (`NonPremultiplied` genuinely blends, `Opaque` on the
identical draw is a pure overwrite) proving real blend factors reach the pipeline, not just an
enabled/disabled bit; (E) `ScissorRectangle` clips exactly at its boundary; (F) `Viewport` confines
rendering to a sub-rectangle; (G) `FillMode.WireFrame` is a documented smoke-only check (WEBGPU-115:
`wgpu-native` has no polygon-mode API).

## Executive Verdict

**Healthy.** Every check's underlying formula/derivation was independently re-verified against the
current production code and found correct, including the non-obvious, empirically-derived cull-mode
mapping (`CullClockwiseFace` → `WGPUCullMode_Front`) that the codebase's own comment explains was
counter-intuitive on first reasoning and had to be verified empirically rather than derived. Check G is
honestly scoped as a smoke check, not a real wireframe-rendering assertion, matching the actual (absent)
capability.

## Checklist Results

### API / XNA / FNA parity

`RasterizerState.setCullModeProperty`/`setScissorTestEnableProperty`/`setFillModeProperty`,
`BlendState::NonPremultiplied`/`Opaque`, `GraphicsDevice.setScissorRectangleProperty`/
`setViewportProperty` (lines 135, 146, 160, 174, 187-189, 203, 211, 217) all map directly to FNA's
public `GraphicsDevice`/state-object surface, with correct defaults reset between independent checks
(`RasterizerState::CullNone` at lines 156, 197, 211, 225; `BlendState::Opaque` at line 182).

### Behavioral correctness

Re-derived the winding-culling math (`DrawWindingQuad()`'s comment, lines 84-92, and
`ToWGPUCullMode()`'s comment, lines 291-309): the quad's own NDC (math-convention, Y-up) signed area is
`+4` (CCW in NDC), but WGPU/D3D determine front/back winding in raster space (Y-down), which mirrors
across X and reverses chirality — making this quad clockwise in raster space, i.e. an ordinary
XNA-front-facing quad under `CullCounterClockwiseFace` (XNA's real default). `ToWGPUCullMode()`'s own
comment documents that this mapping was **empirically verified**, not purely derived, because a naive
NDC-vs-raster-space argument predicted the opposite pairing on a first pass — the comment explicitly
frames this as "this project's established 'empirically verify, don't just derive' rule for this exact
class of winding/orientation subtlety," referencing an analogous precedent in `VulkanGraphicsBackend`.
This audit independently confirms `ToWGPUCullMode()`'s actual mapping (`CullClockwiseFace(1) →
WGPUCullMode_Front`, `CullCounterClockwiseFace(2) → WGPUCullMode_Back`, both combined with
`pipeline.primitive.frontFace = WGPUFrontFace_CCW` set consistently at every 3D pipeline call site) is
self-consistent with the checks' own expected outcomes (A culls, B keeps visible) — a differential pair
that would fail if the mapping were backwards, so this is a genuine proof, not a coincidence.

Check C/D's blend-state differential: `ApplyBlendState()` (lines 4919-4933) derives `blendEnabled_` as
"not exactly the Opaque preset" (`colorSrcBlend==0 && colorDstBlend==1 && alphaSrcBlend==0 &&
alphaDstBlend==1`, i.e. `Blend::One`/`Blend::Zero`), and `FillWGPUBlendState()`/`ToWGPUBlendFactor()`
(lines 239-289) correctly map every XNA `Blend` enum value to its `WGPUBlendFactor` equivalent
(`SourceAlpha=4→WGPUBlendFactor_SrcAlpha`, `InverseSourceAlpha=5→WGPUBlendFactor_OneMinusSrcAlpha`,
matching `BlendState::NonPremultiplied`'s real XNA definition of `SourceAlpha`/`InverseSourceAlpha`).
`GetOrCreatePipelineColored3D()`'s call site (implied by cache-key composition in
`Make3DPipelineKey()`) correctly sets `target.blend = blend ? &blendState : nullptr` — an absent
(`nullptr`) blend state is WGPU's own "no blending, pure overwrite" semantics, confirming Check D's
"pure unblended overwrite" expectation is not a coincidence of factor values but a structurally
different (blend-disabled) pipeline object from Check C's.

### Logic

Check G's `FillMode.WireFrame` path: `ApplyRasterizerState()` stores `fillModeWireframe_ = (fillMode ==
1)` (line 4941) and every draw command carries `command.wireframe = fillModeWireframe_` into its
pipeline-cache key (`Make3DPipelineKey()`'s `salt`/wireframe parameter), but no `GetOrCreatePipeline*()`
call site was found that maps `wireframe` to `WGPUPrimitiveState`'s topology or any polygon-mode
equivalent — consistent with the header comment's own honest disclosure (WEBGPU-115: `wgpu-native` has
no polygon-mode API at all) and with `wgpu-native v29.0.1.1`'s actual `webgpu.h` surface (no
`polygonMode` field on `WGPUPrimitiveState`, unlike Vulkan's `VkPipelineRasterizationStateCreateInfo`).
This is a genuine, documented capability gap, not a silently-broken feature — the test correctly asserts
only "does not crash," not a rendered wireframe result.

### C++ correctness

`ApplyBasicEffect()`'s file-local `static BasicEffect* fx = nullptr; if (fx == nullptr) fx = new
BasicEffect(dev);` (lines 74-75) leaks the one allocated `BasicEffect` for the lifetime of the process —
trivial in a short-lived, single-`Draw()`-call test executable that exits immediately after, but
inconsistent with RAII/no-manual-`new` conventions used elsewhere in this same shard's test files (most
allocate `BasicEffect fx(dev);` on the stack per-check). Severity: LOW (test-code style only, no
production-code implication, no observable effect given the process lifetime).

### Robustness

Check E/F's "inside vs. outside" differential (reading one pixel just inside and one just outside the
scissor rectangle / viewport sub-rectangle) is the correct technique to prove the state is applied per
render pass, not accidentally left over from — or leaking into — the next check; both are reset to
full-backbuffer values immediately after (lines 197-198, 211) so no check's state contaminates a later
one.

### Testing

Good, direct coverage of `CullMode`, `BlendState` (2 of the 3 built-in presets — `AlphaBlend` is not
exercised), scissor, and viewport. Not covered by this file (no claim otherwise): `BlendState.Additive`,
custom (non-preset) `BlendState` construction, `DepthBias`/`SlopeScaleDepthBias` (present in the
pipeline-key plumbing per `Make3DPipelineKey()` but not exercised by any check here), and stencil state
(`stencilEnable_`/`referenceStencil_` etc. are stored per `ApplyDepthStencilState()` but this file never
enables stencil testing).

### Architecture / Memory / Performance / Thread safety / Portability

No file-specific concerns beyond the trivial static-leak note above. Follows the same one-shot
`Game`/`Draw()`-guard/`Exit()` idiom as every other file in this shard.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- This file is the direct evidentiary basis for `ToWGPUCullMode()`'s own header comment's empirical
  claim ("Empirically verified via `WebGPU_GraphicsState`'s differential cull-mode checks") — i.e. this
  audit confirms the comment is not merely asserting an untested claim about itself; the differential
  checks A/B it describes do exist in this exact file and do prove the direction, not just "a pipeline
  was created."
- Per this audit's cross-cutting mandate: this file uses `BasicEffect` with no skinning, so the
  confirmed `CreateSkinnedResources()` normal-transform bug does not apply. No fog is exercised
  (irrelevant to this file's scope — it tests pipeline state, not lighting/fog formulas).

## Missing or Weak Tests

- No `BlendState.Additive` or custom-factor `BlendState` coverage (only `NonPremultiplied`/`Opaque`).
- No `DepthBias`/`SlopeScaleDepthBias` or stencil-state coverage, despite both being wired into the
  same `ApplyRasterizerState()`/`ApplyDepthStencilState()` state-tracking this file otherwise exercises.

## Positive Findings

- The cull-mode differential (A/B) is a genuinely rigorous proof of direction-correct winding, not just
  "culling exists" — this audit independently re-derived the NDC/raster-space mirroring argument and
  confirms the test's own comment's empirical-verification claim is accurate.
- The blend-state differential (C/D) correctly isolates "blend factors are real" from "blending is
  enabled/disabled," a distinction a weaker test could conflate.
- Check G's honest framing as a smoke check for a real, documented `wgpu-native` capability gap
  (WEBGPU-115) is good practice — it does not overclaim wireframe rendering that does not exist.

## Final Assessment

A well-designed, differential-proof-based test with no defects found in either its own logic or the
`WebGPUGraphicsBackend` state-application code paths it exercises (cull mode, blend state, scissor,
viewport). The one honestly-disclosed gap (wireframe fill mode) is a genuine, documented upstream
`wgpu-native` limitation, not a hidden regression.
