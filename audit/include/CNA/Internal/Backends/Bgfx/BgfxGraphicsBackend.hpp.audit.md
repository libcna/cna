# Audit: include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.hpp`
- Audit status: AUDITED (scoped-depth review — see below; matches the standard already applied to this audit's
  other largest backend headers)
- Subsystem: `backend-bgfx` shard
- File type: C++ header, 695 lines
- Related implementation: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (same shard, 3443 lines,
  scoped-depth review — see that report)
- XNA/FNA relevance: implements the full `IGraphicsBackend` contract via bgfx (a multi-API abstraction layer:
  OpenGL/Vulkan/D3D11/D3D12/Metal, selected at runtime)
- Graphics backend relevance: this is the backend's central class
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Declares `BgfxGraphicsBackend`: device/view/framebuffer lifecycle, cached blend/depth/stencil/rasterizer state
(bgfx bakes most render state into per-draw flags rather than separate state objects, unlike D3D11), `SpriteBatch`/
`EffectBackend` factories, and the full draw-dispatch surface mirroring every other backend's own variant matrix.

## Executive Verdict

**Needs attention — well-engineered overall, with 2 already-confirmed HIGH findings (dead-code vertex-format
helper file it doesn't use, and 2 known-failing CTests tracked elsewhere), plus positive confirmations that this
backend fully implements Stencil/Scissor/DepthBias (unlike D3D12's confirmed gaps).**

## Checklist Results

### Confirmed clean: window-registry
**No `RegisterForWindow` call anywhere in this file or its `.cpp`** (confirmed via grep) — this backend does not
participate in `IGraphicsBackend`'s static window registry at all, matching D3D11/D3D12's own confirmed absence;
cannot share the EasyGL-class dangling-pointer bug.

### Confirmed correct: SetTransformMatrix
`BgfxSpriteBatchBackend::SetTransformMatrix()` is declared here (line 353) and confirmed a real, working override
in the `.cpp` (already independently verified while auditing D3D11's own `SpriteBatch`, whose header comment
specifically cited Bgfx as one of the backends that gets this right, unlike Vulkan's confirmed no-op bug).

### Architecture — genuine Stencil/Scissor/DepthBias functionality (contrast with D3D12's confirmed gaps)
The cached-state design (`stencilFront_`/`stencilBack_`/`scissorEnabled_`/`depthBias_` members, rebuilt via
`RebuildStencilState()`) is confirmed, in the `.cpp`, to be genuinely functional — not merely tracked-and-ignored
like D3D12's confirmed complete non-functionality of both Stencil and Scissor. `DepthBias` is emulated via a
per-draw vertex-shader Z-offset (`u_depthBias`, present in every 3D vertex shader in this shard) since bgfx has
no native polygon-offset mechanism — a real, functional workaround, not a silent gap. `SlopeScaleDepthBias` is
the one honestly-disclosed gap here (deliberately not emulated, per an explicit, dated 2026-07-10 project-owner
decision documented in the `.cpp`).

### Systematic FNA parity gaps — already recorded, corroborated here
`BasicEffect::VertexColorEnabled`'s bare-public-field issue (already a confirmed cross-cutting finding, discovered
independently via this backend's own `bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp`) is consistent with
this header's own declarations, which correctly follow the project's real (if occasionally violated elsewhere)
property convention for every other XNA-facing member.

## Detailed Findings

None new in this file — see the `.cpp` report and `AUDIT_CROSS_CUTTING_FINDINGS.md` for the findings this
backend contributes (dead-code `BgfxVertexFormatHelper.hpp`, 2 known-failing CTests, the `EnvironmentMapEffect`
emissive-remultiply bug, the skinned-normal-transform bug's final confirming instance, the fog-formula bug).

## Cross-File Observations

Correctly declares accessors/state matching every already-audited shader file's own uniform expectations
(`u_depthBias`, `u_vertexColorEnabled3D`, stencil front/back state) — no mismatch found between this header's
declared state and what the shaders actually consume.

## Missing or Weak Tests

No dedicated test found in this audit so far exercising this backend's device-lost recovery or resize handling.

## Positive Findings

Confirmed absence of the EasyGL-class window-registry bug; confirmed correct `SetTransformMatrix`; genuinely
functional Stencil, Scissor, and DepthBias support (the most complete of any backend checked for this specific
combination — D3D12 has none of the three functional, SdlGpu has Stencil+Scissor but not DepthBias).

## Final Assessment

No new defects in this file; corroborates and formally confirms already-recorded cross-cutting findings while
establishing 3 genuine positive results (Stencil/Scissor/DepthBias all functional).
