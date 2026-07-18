# Audit: include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp`
- Audit status: AUDITED
- Subsystem: `backend-software` shard
- File type: C++ header (337 lines)
- Related header/implementation: `src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp` (audited
  separately — the substantive findings F1/F2, about depth-write/depth-function fidelity, live in that report
  since they're implementation gaps; this report covers the header's own declared API surface)
- XNA/FNA relevance: declares the backend's public surface; see the `.cpp` report for FNA-parity detail
- Graphics backend relevance: declares one of the 14 confirmed backends
- FNA reference: N/A directly (see `.cpp` report)
- Main related tests: `examples-tests-software` (6 files, not yet audited)

## Purpose

Declares `SoftwareFramebuffer` (real CPU-owned RGBA8 color + float32 depth storage) and every
`Software*Backend` resource class plus `SoftwareGraphicsBackend` itself. The header's own doc comments are
explicit and accurate about the intentional v1 scope boundary: line 152-154 states "Cube-map render targets,
Texture3D, and hardware occlusion queries remain out of scope for v1... all keep IGraphicsBackend's own shared
default (returns nullptr)" — verified this matches the `.cpp`: no `CreateRenderTargetCube`/`CreateTexture3D`/
`CreateOcclusionQuery`/`DrawInstancedPrimitivesEx` overrides exist anywhere in the implementation, so callers get
`IGraphicsBackend`'s honest `nullptr`/throw defaults rather than a silent wrong-behavior fallback — the *correct*
way to leave a feature unimplemented (see `IGraphicsBackend.hpp`'s own audit report, Finding F1, for the general
risk this backend correctly avoids).

## Executive Verdict

**Healthy.** Accurate, well-scoped declarations with clearly and correctly documented boundaries. The one gap this
report adds beyond the `.cpp` findings is that the *depth-write/depth-function* limitation (that report's F1/F2)
is conspicuously **not** documented here the same way the cube/3D-texture/occlusion-query/instancing boundary is
— see Finding F1 below.

## Checklist Results

### API / XNA / FNA parity
N/A directly — see `.cpp` report.

### Behavioral correctness / Logic
`SoftwareFramebuffer`'s `Resize`/`ClearColor`/`ClearDepthValue` (lines 20-30) are a clean, minimal, correctly-scoped
value type — real backing storage, no bookkeeping-only fields (matching this backend's stated "not a bookkeeping
fiction" design philosophy, line 18).

### Memory/resource lifetime
No `shared_ptr`/registry indirection here (unlike Headless) — resource classes own their data directly
(`std::vector<std::uint8_t>` members), which is correct and simpler for a backend with no cross-resource shared
state to coordinate (Headless needs the shared registry for leak detection; Software has no equivalent feature and
doesn't need one).

### C++ correctness
Every concrete resource class is `final` (`SoftwareVertexBufferBackend`, `IndexBufferBackend`,
`RenderTargetBackend`, `TextureCubeBackend`, `EffectBackend`, `SpriteBatchBackend`, `GraphicsBackend` itself) —
consistent, unlike Headless's one inconsistent `HeadlessTextureBackend` (see that file's own audit finding).
`SoftwareTextureBackend` (line 80) is the one non-`final` class here too, for the same reason (no destructor
declared at all, actually — relies on the implicit one, which is fine since it holds only a `std::vector` with no
special cleanup needed, and nothing derives from it either) — same minor stylistic inconsistency noted for
Headless applies here too, at lower severity since this class doesn't even declare a virtual destructor override
explicitly (it inherits `ITextureBackend`'s, correctly).

### Performance / Thread safety / Portability
N/A — see `.cpp` report.

### Architecture
Clean; `SoftwareSpriteBatchBackend` correctly takes a `SoftwareGraphicsBackend&` owner reference (line 190) to
reach `CurrentFramebuffer()`/`IsBlendEnabled()`/`IsDepthTestEnabled()`/`GetCullMode()` — a sensible way to share
backend-wide rasterizer state with sprite draws without duplicating it.

### Maintainability
337 lines, proportionate; comments are substantive and specific (e.g. the cube-map-face-selection convention note
on `SoftwareTextureCubeBackend`, lines 129-131; the `GetCullMode()` comment explaining exactly why SpriteBatch
needs it, lines 314-318).

### Robustness / Testing
See `.cpp` report.

## Detailed Findings

### F1 — The header documents the cube/3D-texture/occlusion-query/instancing v1 boundary explicitly, but not the depth-write/depth-function boundary the `.cpp` report found

- Severity: LOW (documentation gap, not a code defect — the code defect itself is `.cpp` F1/F2)
- Confidence: HIGH
- Category: maintainability / documentation
- Location/symbol: compare lines 152-154 (explicit, accurate boundary documentation) against the complete absence
  of any comment near `ApplyDepthStencilState`'s declaration (line 264-268) or `SetDepthWriteEnabled`'s (line 283)
  mentioning that depth-write and depth-compare-function are not honored.
- Why it matters: this header is clearly capable of (and does, elsewhere) documenting intentional v1 boundaries
  well — the omission here reads as an actual gap in awareness rather than a deliberate choice to leave it
  undocumented, unlike the cube/3D-texture/occlusion/instancing gaps which are both correctly implemented (honest
  `nullptr`/throw defaults) *and* documented.
- FNA/XNA comparison: N/A.
- Related files: `src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp` (`.cpp` report F1/F2);
  `docs/software-backend.md`.
- Suggested future action (not implemented by this audit): add a comment near `ApplyDepthStencilState`'s
  declaration analogous to line 152-154's, once F1/F2 are either fixed or consciously accepted as permanent v1
  scope.

## Cross-File Observations

See `.cpp` report for the full cross-file list (SpriteBatch cull-mode coupling, DualTextureEffect same-UV
precedent).

## Missing or Weak Tests

See `.cpp` report.

## Positive Findings

- Explicit, accurate scope-boundary documentation for the features that genuinely are out of scope (line 152-154)
  — a good model other backend headers' own "not yet supported" declarations should match.
- Consistent, minimal, ownership-clean resource class design with no unnecessary indirection.

## Final Assessment

A clean, accurate header let down only by one documentation omission (F1) that mirrors the `.cpp` file's own two
substantive findings.
