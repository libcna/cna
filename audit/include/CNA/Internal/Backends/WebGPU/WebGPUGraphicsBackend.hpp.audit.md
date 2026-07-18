# Audit: include/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp`
- Audit status: AUDITED (scoped-depth review, per the paired `.cpp` report's methodology note)
- Subsystem: `backend-webgpu` shard
- File type: C++ header (1606 lines) — the second-largest header in the audit after none (largest backend header
  overall)
- Related header/implementation: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` (audited
  separately — the substantive F1 finding lives in that report; this report adds one important piece of
  corroborating evidence found only in this header)
- XNA/FNA relevance: declares the backend's public surface; see `.cpp` report
- Graphics backend relevance: declares the experimental WebGPU backend (`CLAUDE.md`-confirmed status)
- FNA reference: N/A directly
- Main related tests: `examples-tests-webgpu` (22 files, not yet audited)

## Purpose

Declares every `WebGPU*Backend` resource class and the large `WebGPUGraphicsBackend` class itself, including its
per-draw-command-queue design (`SkinnedDrawCommand`/`SkinnedPbrDrawCommand`/etc. structs that buffer draw
parameters for later batched pipeline creation/dispatch — a materially different architecture from EasyGL's
immediate-mode `Prog3D`/`BindDrawParams()` approach, reflecting WebGPU's own pipeline-object-heavy API design).

## Executive Verdict

**Needs attention**, for the same reason as the `.cpp` report: this header's own comments provide the clearest,
most self-aware documentation found anywhere in this audit of the `SkinnedPbrEffect` variant of the
world-space-normal-transform bug (see Detailed Findings) — valuable corroborating evidence, but confirming the
defect is real and (for that one variant) already known internally without having been fixed.

## Checklist Results

### API / XNA / FNA parity
N/A directly (see `.cpp` report for the confirmed finding).

### Behavioral correctness / Logic
The draw-command-queueing pattern (`SkinnedDrawCommand`/`SkinnedPbrDrawCommand`, each capturing a full snapshot of
vertex/index data, uniforms, and state at `Queue*Draw()` time for later `Render*Draws()` dispatch) is a
sensible design for a modern explicit-pipeline API like WebGPU, where pipeline objects are comparatively expensive
to create/bind compared to EasyGL's immediate `glUniform*`-per-draw model.

### Memory/resource lifetime
See `.cpp` report for the constructor/destructor analysis (this header only declares the fields those methods
manage).

### C++ correctness / Performance
`SkinnedPbrDrawCommand` (lines 1552-1586) stores `std::array<float, 4 + 72*16> skinningParams{}` **by value**
inside every queued draw command — at 1156 floats (~4.6 KB) per command, this is the same
"`GpuDrawParams::boneTransforms` zero-inits a large array" cost pattern this audit's `IGraphicsBackend.hpp` report
(Finding F2) already flagged as needing call-frequency corroboration — here the cost is paid once per *queued*
skinned-PBR draw command (not necessarily once per frame if commands are queued across multiple draws before a
single flush), which could be a real, measurable cost for scenes with many skinned-PBR objects. Not elevated to
its own finding given the scoped-review methodology, but worth flagging as a data point for whenever
`GpuDrawParams`' F2 is followed up on.

### Thread safety / Portability
N/A — see `.cpp` report.

### Architecture
The per-pipeline-variant `std::unordered_map<std::uint64_t, WGPURenderPipeline>` caches (e.g. `skinnedPipelines_`,
`skinnedColorPipelines_`, `skinnedVertexLitPipelines_`, `skinnedVertexLitColorPipelines_` — four separate maps for
one effect) reflect WebGPU's requirement that pipeline state (blend/depth/cull/wireframe/etc.) be baked into
immutable pipeline objects rather than set as loose per-draw GL-style state — a real, unavoidable architectural
consequence of the target API, not a design smell.

### Maintainability
1606 lines, proportionate to the `.cpp`'s own scale — see that report's file-size observation.

### Robustness / Testing
See `.cpp` report.

## Detailed Findings

### F1 — This header's own comment explicitly documents (without having fixed) the `SkinnedPbrEffect` variant of the world-space-normal-transform bug

- Severity: N/A (this is corroborating documentation evidence for the `.cpp` report's F1, not a separate defect)
- Confidence: HIGH
- Category: documentation / correctness (cross-reference)
- Location/symbol: comment immediately preceding `SkinnedPbrDrawCommand` (lines 1544-1551)
- Evidence: "Bone-palette skinning... feeding pbr3d.wgsl's own fragment BRDF unchanged, matching
  `EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()`'s combination exactly — **including its own (different from
  unskinned PbrEffect) normal/tangent transform, which uses the raw World rotation directly
  (`mat3(uWorld)*(skinMat3*normal)`) rather than the precomputed inverse-transpose normal matrix `pbr3d.wgsl`'s
  own unskinned vertex shader uses.**"
- Why it matters: this is valuable, honest self-documentation of a real limitation — the authors were aware this
  specific shader diverges from the (correct) unskinned sibling's normal-matrix handling, and wrote that down
  rather than leaving it to be discovered. It does not change the defect's real-world impact (still wrong lighting
  for non-uniformly-scaled skinned-PBR models under a rotated World transform), but it is a meaningfully different
  situation from the plain `SkinnedEffect` case (`.cpp` report's main F1 finding), where no equivalent disclosure
  was found.
- Suggested future action: none beyond what the `.cpp` report's F1 already recommends (fix both alongside
  EasyGL's own F2/F3).

## Cross-File Observations

See `.cpp` report.

## Missing or Weak Tests

See `.cpp` report.

## Positive Findings

- Exceptionally candid self-documentation of a known limitation (the `SkinnedPbrEffect` normal-matrix gap) —
  exactly the kind of "document intentional deviations" discipline this project's own `CLAUDE.md` asks for,
  applied here to something that reads more like an acknowledged-but-not-yet-fixed bug than an intentional
  simplification, which is itself valuable honesty.
- Clear, consistent per-effect structuring (dedicated `Create*Resources()`/`Destroy*Resources()`/
  `GetOrCreatePipeline*()`/`Queue*Draw()`/`Render*Draws()` method groups per effect) that makes the header
  navigable despite its size.

## Final Assessment

A large, clearly-organized header whose most notable contribution to this audit is corroborating evidence for the
`.cpp` report's central finding — including proof that at least one variant of the bug was already known
internally, not purely a fresh discovery.
