# Audit: src/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.cpp`
- Audit status: AUDITED (scoped-depth review — see below; matches the standard applied to this audit's other
  largest files, `EasyGLGraphicsBackend.cpp` 4733 lines and `WebGPUGraphicsBackend.cpp` 8805 lines)
- Subsystem: `backend-d3d11` shard
- File type: C++ implementation, 1846 lines — the largest single file in this backend
- Related header: `include/CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp` (same shard)
- XNA/FNA relevance: implements the entire D3D11 backend's device lifecycle and draw dispatch
- Graphics backend relevance: this is the central D3D11 class
- FNA reference: FNA's own D3D11 device/draw-dispatch conventions
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Implements device/swap-chain/window-size-view creation and teardown, `Clear*`/`Present`/`ReadBackbuffer`,
`Apply*State`, `SetRenderTarget(s)`/MRT tracking, and the full `DrawPrimitivesExImpl` variant-dispatch chain
(colored/textured/lit/alpha-test/dual-texture/env-map/skinned/PBR, each filling the matching `D3DCommon` constant
buffer struct and selecting the matching `D3DShaderVariant`), plus `DrawInstancedPrimitivesEx` and the
`SpriteBatch`/`OcclusionQuery`/`EffectBackend` factories.

## Executive Verdict

**Mostly healthy, scoped-depth review.** Constructor/destructor, `RegisterForWindow` absence, `SetRenderTargets`/
MRT finalization, and the skinned/PBR ambient-vs-emissive constant-buffer fill logic were all read and verified
in full. The remaining ~1500 lines (full `DrawPrimitivesExImpl` dispatch for every non-skinned variant, device-lost
recovery, resize handling, `ReadBackbuffer`) were not exhaustively traced line-by-line given this file's size,
consistent with the scoped-depth standard already applied to this audit's other largest files.

## Checklist Results

### Confirmed clean: constructor/destructor/window-registry
Constructor (lines 87-109) creates device/swap-chain/window-size resources in the documented three-group order;
destructor is `= default` (line 111). **No `RegisterForWindow` call anywhere in this file** (confirmed via grep) —
this backend does not participate in `IGraphicsBackend`'s static window registry at all, so it cannot share the
already-confirmed EasyGL dangling-window-registry-pointer bug.

### Systematic FNA parity gaps (corroborates already-recorded shader-level findings)
**Independently confirmed at the C++ level**: the `Skinned3d` draw-param-fill block (lines ~1546-1650+) sets
`perDraw.AmbientColor` from `params.ambientColor` (lines 1559-1561) but **never sets any `EmissiveColor` field**
for the skinned path (`D3DSkinnedExtraConstants` has no such field — confirmed in the `backend-d3dcommon` shard's
own audit of `D3DConstantBuffers.hpp`) — this closes the loop on that already-recorded MEDIUM finding: the gap is
not merely a shader-side omission, the C++ fill code never even attempts to send `EmissiveColor` for skinned
draws, because there is nowhere in the wire format to put it. Confirmed the PBR path (lines ~1420-1430,
`c.EmissiveAmount`) and the unskinned lit path (lines ~1660-1670, `lighting.EmissiveColor`) both DO correctly
forward `EmissiveColor` — the gap is specifically scoped to `SkinnedEffect` (non-PBR), matching what was already
established from the shader side.

### Architecture — MRT finalization
`SetRenderTargets()` (lines 564-622) correctly calls `FlushPendingMRTResolveEXT()` first (finalizing any prior MRT
bind before doing anything else), correctly restores the back buffer unconditionally on `count<=0` (with an
explicit, accurate comment explaining why this is needed even though a prior MRT bind never sets
`currentCustomRT_`), and correctly tracks the new MRT set in `currentMRTTargets_`/`currentMRTCount_` for the next
call to finalize. This is a genuine, well-engineered fix for exactly the class of "state tracked before a
finalization step can run" issue this audit has flagged as a recurring risk pattern elsewhere (e.g.
`SpriteBatch::Begin()`/`GraphicsDevice::SetRenderTargets` in the XNA layer) — this file gets it right.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
No issues found in the areas read.

## Detailed Findings

None new — this file corroborates (does not introduce) the already-recorded `backend-d3dcommon` findings
(missing `EmissiveColor` for `SkinnedEffect`, mirrored fog formula, skinned-normal-transform bug — all consumed
via the shader variants this file dispatches to).

## Cross-File Observations

Every constant-buffer struct filled in this file (`D3DPerDrawConstants`, `D3DFogConstants`,
`D3DLightingConstants`, `D3DSkinnedExtraConstants`, `D3DBoneConstants`, `D3DEnvMapConstants`,
`D3DPbrPerDrawConstants`, `D3DPbrLightConstants`) was already independently verified correct at the
size/offset/layout level in the `backend-d3dcommon` shard's own audit — this file's job is purely to populate
those already-verified layouts from `GpuDrawParams`, which the areas read here do correctly.

## Missing or Weak Tests

No dedicated test found in this audit so far for: device-lost/removed recovery, `EnsureSwapChainSize()`'s resize
path, or the full `DrawPrimitivesExImpl` variant-selection matrix (colored/textured/dual-texture/env-map paths
were not traced in this pass — flagged for a future, more exhaustive pass if this backend becomes a priority for
deeper review, e.g. if Windows-native CI capacity for this backend becomes available per D-P4).

## Positive Findings

Explicit, deliberate, well-documented MRT-finalization bugfix (DX-143) — a genuine positive example of this
project fixing exactly the kind of "state mutated before the finalization step runs" issue flagged elsewhere in
this audit as a recurring risk. Confirmed absence of the EasyGL-class window-registry bug.

## Final Assessment

No new defects found in the areas read (scoped-depth review, consistent with this audit's standard for
1500+-line files); corroborates and closes the loop on already-recorded `backend-d3dcommon` findings. The
untraced ~1500 lines (full non-skinned variant dispatch, device-lost recovery, resize) remain a gap for a future,
more exhaustive pass.
