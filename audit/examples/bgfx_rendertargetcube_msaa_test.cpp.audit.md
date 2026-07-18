# Audit: examples/bgfx_rendertargetcube_msaa_test.cpp

## Metadata

- Source file: `examples/bgfx_rendertargetcube_msaa_test.cpp` (194 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `RenderTargetCube` MSAA property-fidelity + no-corruption test
- File type: standalone `Game`-subclass executable, CTest-registered as `Bgfx_RenderTargetCube_MsaaResolve`
  (`cmake/Tests/BgfxTests.cmake:505-508`)
- XNA/FNA relevance: direct — `RenderTargetCube`'s `preferredMultiSampleCount` constructor parameter and
  its device-clamped `MultiSampleCount` getter.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/RenderTargetCube.cpp` (`ClosestMSAAPower()`
  lines 22-36, constructor lines 38-61); `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` —
  `BgfxMsaaRtFlag()` (597-605), `BgfxRenderTargetCubeBackend` ctor (800-835).

## Purpose

Three checks: (1) `MultiSampleCount` request `0` must report applied `0`; (2) request `8` must report a
real applied value `>1`; (3) rendering solid blue into all 6 faces of an 8x-MSAA cube, then sampling it
back via `EnvironmentMapEffect` after unbinding, must not corrupt or crash and must read back blue. The
file's own header (lines 1-24) explicitly scopes this as "property fidelity + no-corruption," not a
genuine sub-pixel anti-aliasing differential — citing a prior task's finding (Task 907) that such a
differential technique can't discriminate cube-face content, "independently re-confirmed while
investigating this exact task."

## Executive Verdict

**Healthy** — all three checks are correctly wired against the actual production `RenderTargetCube` /
`BgfxRenderTargetCubeBackend` code, the file avoids the sibling depth-format test's Task 952 bug by
deliberately using `DepthFormat::None`, and its own documented Task 910 view-id workaround was
independently verified to actually force the frame boundary it depends on.

## Checklist Results

### API / XNA / FNA parity
`RenderTargetCube(device, kCubeSize, mipMap, format, depthFormat, preferredMultiSampleCount, usage)`
6/7-arg overload (lines 92-94, 101-103) matches FNA's full constructor shape, including
`RenderTargetUsage::DiscardContents` as the trailing usage argument.

### Behavioral correctness
Independently re-derived both property checks against `RenderTargetCube.cpp`:
- `ClosestMSAAPower(0)` (lines 22-36): `value <= 0` returns `0` directly (line 26) — the constructor
  then calls `device.backend_->CreateRenderTargetCube(..., ClosestMSAAPower(0))` = `CreateRenderTargetCube(...,
  0)`, and `BgfxMsaaRtFlag(0, appliedOut)` (`BgfxGraphicsBackend.cpp:597-605`) falls through to
  `appliedOut=0; return BGFX_TEXTURE_RT;` (line 603-604) — `rtcNoMsaa.getMultiSampleCountProperty()`
  therefore genuinely returns `0`, matching check 1's expectation exactly (not merely by chance).
- `ClosestMSAAPower(8)`: `8` is already a power of two so the `result == value` branch (line 34) returns
  `8` unchanged; `BgfxMsaaRtFlag(8, appliedOut)` matches the `>= 8` branch, `appliedOut = 8`. Check 2's
  `msaaApplied > 1` (line 105) is satisfied.
- `multiSampleCount_` is confirmed to be overwritten from the *backend's* real applied value
  (`RenderTargetCube.cpp:60`, `if (rtCubeBackend_) multiSampleCount_ = rtCubeBackend_->GetMultiSampleCount();`)
  rather than echoing back the raw constructor argument — this is the correct XNA-matching behaviour
  ("reflects the backend's real, device-clamped value... not the raw constructor argument," per that
  file's own comment, independently confirmed here rather than just trusted).

### Logic
Check 3's per-face fill loop (lines 115-131) uses the identical Task 910 view-id-sharing workaround
verified in this batch's `bgfx_rendertargetcube_mip_test.cpp` report (a dummy `GetBackBufferData()` read
between each face's fill to force a `bgfx::frame()` boundary) — the header comment (lines 21-24)
explicitly credits that file as the pattern's origin ("established by
`bgfx_rendertargetcube_mip_test.cpp`"), and this audit's independent trace of `ReadBackbuffer()`'s
`bgfx::frame()` call (verified once, applies identically here) confirms the workaround is real, not a
placebo.

### C++ correctness
No ownership/lifetime issues; `rtcNoMsaa`/`rtcMsaa` are stack-local `RenderTargetCube` objects that
outlive their use within `Draw()`.

### Robustness
Correctly and deliberately avoids the separately-audited `bgfx_rendertargetcube_depthformat_test.cpp`'s
open Task 952 depth-attachment bug by using `DepthFormat::None` throughout (lines 92-93, 101-102) — this
means check 3's "no corruption" claim is not actually exercising a depth-attached cube face, which is
consistent with (and does not contradict) this file's own explicitly narrow scope.

### Testing
The three checks correctly separate "does the property pipeline apply/report MSAA counts correctly"
(checks 1-2, pure metadata, no GPU-driver dependency) from "does rendering-then-sampling survive with an
MSAA cube bound" (check 3, real GPU work) — a sensible layering that isolates likely failure domains.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings — this file's assertions were all independently re-derived against
current production code and found to match exactly.

## Cross-File Observations

- Directly mirrors `bgfx_rendertarget2d_msaa_test.cpp`'s "unlike Vulkan, Bgfx's per-RT MSAA is fully
  independent of backbuffer MSAA state" architectural claim (this file's lines 9-12) — independently
  verified once via `BgfxRenderTargetBackend`'s constructor for the 2D sibling; the identical claim here
  for `BgfxRenderTargetCubeBackend` is corroborated by the same `BgfxMsaaRtFlag()` helper being shared
  between both backend classes (`BgfxGraphicsBackend.cpp:597-605`, used at both line 673 and line 811).
- Does NOT need the `CNA_BGFX_RENDERER=VULKAN` CTest environment override that
  `bgfx_rendertarget2d_msaa_test.cpp` requires (`BgfxTests.cmake:505-508` has no such override) — since
  this file's own scope is explicitly "property fidelity + no visible corruption," not a genuine
  sub-pixel AA differential, it does not depend on whichever legacy-GL-context MSAA-resolve limitation
  motivated that override for the 2D test. This is a consistent, intentional scope difference, not an
  oversight.

## Missing or Weak Tests

None beyond the explicitly-disclosed and well-justified "not a genuine AA differential" scope limitation
already stated in the file's own header.

## Positive Findings

- Clean separation of concerns across the 3 checks (pure metadata vs. real render-and-sample) makes
  failures easy to localize.
- Explicit, falsifiable justification (citing Task 907's prior finding, re-confirmed for this task) for
  why a stronger sub-pixel differential technique was not attempted, rather than silently omitting it.

## Final Assessment

No defects found. All three checks were independently re-derived against the real `RenderTargetCube`/
`BgfxRenderTargetCubeBackend` code paths and match exactly what the file asserts.
