# Audit: include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp`
- Audit status: AUDITED
- Subsystem: `backend-easygl` shard
- File type: C++ header (629 lines) — full class declarations for the EasyGL backend
- Related header/implementation: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (audited
  separately — the three substantive findings F1/F2/F3 all live in that report, as they are implementation-level
  issues; this report covers the header's own declared API surface and design)
- XNA/FNA relevance: declares the backend's public surface; see `.cpp` report for the FNA-parity findings
- Graphics backend relevance: declares the default Linux/Emscripten backend, linking the external `easy-gl`
  sibling library (out of scope per D-6)
- FNA reference: N/A directly
- Main related tests: `examples-tests-easygl` (218 files, already audited via mechanical batch)

## Purpose

Declares every `EasyGL*Backend` resource class (`Texture`, `RenderTarget`, `RenderTargetCube`, `Texture3D`,
`TextureCube`, `Effect`, `OcclusionQuery`, `SpriteBatch`, `VertexBuffer`, `IndexBuffer`) plus the top-level
`EasyGLGraphicsBackend` itself, including its large internal `Prog3D` per-shader-variant uniform-location cache
struct (11 program variants) and the `Ensure*Program()` shader-compilation method declarations. This is the
complete, accurate public+private surface for the implementation audited separately.

## Executive Verdict

**Mostly healthy.** Accurate, complete declarations matching the `.cpp` exactly for every method checked. The
`RecoverableResource` interface is consistently applied to every GL-resource-owning class. No independent defects
found in this file beyond a design observation about the `Prog3D` struct's size (see below) — the substantive
findings (F1 constructor exception safety, F2/F3 skinned normal-transform bugs) are implementation-level and
documented in the `.cpp` report.

## Checklist Results

### API / XNA / FNA parity
N/A directly (see `.cpp` report).

### Behavioral correctness / Logic
`GetMultiSampleCount()`'s override (line 552, `return sampleCount_ > 1 ? sampleCount_ : 0;`) correctly reports 0
when no real MSAA is active rather than a raw internal `sampleCount_` value of 1 — consistent with
`IGraphicsBackend.hpp`'s documented convention that 0 means "no MSAA."

### Memory/resource lifetime
Every GL-resource-owning class (`EasyGLTextureBackend`, `EasyGLRenderTargetBackend`,
`EasyGLRenderTargetCubeBackend`, `EasyGLVertexBufferBackend`, `EasyGLIndexBufferBackend`,
`EasyGLOcclusionQueryBackend`, `EasyGLSpriteBatchBackend`) consistently inherits `::easygl::RecoverableResource`
and declares both `release_gl_handle_only()`/`recreate_gl_resource()` — a uniform, complete application of the
context-loss-recovery pattern with no class silently omitted. `EasyGLTexture3DBackend`/`EasyGLTextureCubeBackend`
are the two exceptions (no `RecoverableResource`) — worth a one-line check during any future context-loss-recovery
audit pass on whether 3D/cube textures are intentionally excluded from recovery or simply not yet covered (not
assessed further here, out of this report's scope).

### C++ correctness
`EasyGLGraphicsBackend` itself is *not* declared `final` — an exception to the fairly consistent `final`-where-
appropriate pattern seen in other backends, though (like SdlRenderer's own header) nothing currently derives from
it.

### Performance
The `Prog3D` struct (lines 404-447) has ~35 `int loc_*` fields — at roughly 4 bytes each, each of the 11 `Prog3D`
instances (`prog_colored_` through `prog_pbr_skinned_`) costs on the order of 150+ bytes just for cached uniform
locations, on top of the `::easygl::Program` itself — a design cost proportional to the shader-variant count, not
a bug, but worth noting as a data point for the file-size observation already made in the `.cpp` report.

### Thread safety / Portability
N/A — see `.cpp` report.

### Architecture
The per-shader-variant `Prog3D` design (11 variants, declared explicitly rather than via any kind of shader
permutation/preprocessor system) trades verbosity for simplicity and directness — a reasonable choice for a
GLES3-target backend without a more elaborate shader-variant infrastructure, though it does mean every new
effect/vertex-format combination requires a new `Prog3D` member and a new `Ensure*Program()` method (as seen
literally happening across this file's own history, e.g. PBR and SkinnedPbr being later additions per their
`plans/plan_cnj.md` citations).

### Maintainability
629 lines for 11+ resource-class declarations plus the large `EasyGLGraphicsBackend` class itself is
proportionate to the `.cpp`'s own scale — see that report's Maintainability section for the overall file-size
observation.

### Robustness / Testing
See `.cpp` report.

## Detailed Findings

None specific to this file beyond the minor `final`-consistency and `RecoverableResource`-coverage notes above,
neither of which rises to a reportable finding on its own (no derived classes exist; the 3D/cube-texture
recovery-coverage question needs the actual recovery-path code to judge, which lives in the `.cpp`).

## Cross-File Observations

See `.cpp` report for the full findings (F1, F2, F3) and their cross-file connections to the already-audited
`examples-tests-easygl` shard.

## Missing or Weak Tests

See `.cpp` report.

## Positive Findings

- Complete, accurate, and consistently-applied `RecoverableResource` pattern across nearly every resource class.
- Doxygen-quality comments on the more subtle design points (e.g. `EasyGLVertexBufferBackend::GetDeclarationElements()`'s
  comment explaining exactly why hardware instancing needs it, lines 315-319).

## Final Assessment

Accurate, well-organized header for a large, mature backend; no independent defects, all substantive findings
live in the paired `.cpp` report.
