# Audit: examples/vulkan_rendertargetcube_msaa_test.cpp

## Metadata

- Source file: `examples/vulkan_rendertargetcube_msaa_test.cpp` (205 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RenderTargetCube` MSAA property-fidelity +
  no-corruption test.
- File type: standalone `Game`-subclass executable (`class VulkanRenderTargetCubeMsaaTest`).
- XNA/FNA relevance: direct — `RenderTargetCube(device, size, mipMap, format, depthFormat,
  preferredMultiSampleCount, usage)`'s `MultiSampleCount`/`getMultiSampleCountProperty()` surface.
- Related production code:
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`VulkanRenderTargetCubeBackend`'s `appliedMultiSampleCount_` computation, line ~8772;
  `GraphicsDevice::RecreateBackendForMultiSampleCount()`, `GraphicsDevice.cpp` lines 1346–1352).
- Task references: Task 903 (this file, `git log`: `6896a408 feat(Task 903): implement
  RenderTargetCube MSAA on Vulkan and Bgfx`), Task 902 (`22557a6d fix(Task 902):
  GraphicsDevice::Reset() now really reconfigures the backend` — postdates this file's only
  commit by ~5 hours the same day), Task 907 (mip-chain sibling test in this shard).

## Purpose

Two-part verification of `RenderTargetCube` MSAA support: **(1) property fidelity** —
`RenderTargetCube` constructed with `preferredMultiSampleCount=0` must report `0` via
`getMultiSampleCountProperty()`, while one constructed with `8` (with real backbuffer MSAA already
engaged) must report a real, nonzero, device-clamped value — proving the request reaches the
backend at all (before Task 903, `VulkanRenderTargetCubeBackend` never overrode
`GetMultiSampleCount()`, always returning the base interface's hardcoded `0`). **(2)
no-corruption/no-crash** — fill all 6 faces of the MSAA-enabled cube solid Blue via a real draw
call, unbind, sample the centre via `EnvironmentMapEffect` — must read back Blue, not
black/garbage.

The file's own header comment explicitly declines a genuine sub-pixel anti-aliasing differential
test (the diagonal-edge technique the `RenderTarget2D` MSAA sibling test uses), citing the same
"reflection-vector sampling can't be forced to a specific face/LOD the way direct 2D sampling can"
limitation independently confirmed in this shard's `vulkan_rendertargetcube_mip_test.cpp` audit.

## Executive Verdict

**Needs attention** — the property-fidelity check (this file's primary, genuinely novel
contribution per its own stated purpose) was independently confirmed sound against the production
`GetMultiSampleCount()` override. However, this file shares the exact same stale-comment/workaround
issue this audit identified in the sibling `vulkan_rendertarget2d_msaa_test.cpp` (see F1): its
header comment's claim that `GraphicsDeviceManager.PreferMultiSampling` "doesn't actually reach the
Vulkan backend at all" is stale post-Task-902, and the file was never revisited since.

## Checklist Results

### API / XNA / FNA parity — PASS
`RenderTargetCube(device, kCubeSize, /*mipMap=*/false, SurfaceFormat::Color, DepthFormat::None,
/*preferredMultiSampleCount=*/0, RenderTargetUsage::DiscardContents)` /
`.../*preferredMultiSampleCount=*/8, ...)` (lines 116–118, 125–127) exercise the full XNA
constructor overload with only the MSAA parameter varied; `getMultiSampleCountProperty()`
(lines 119, 128) matches this codebase's established C# `MultiSampleCount` property → CNA getter
convention.

### Behavioral correctness — PASS (property check), STALE COMMENT (workaround necessity) — see F1
Check 1 (`noMsaaApplied == 0`, line 120) and Check 2 (`msaaApplied > 1`, line 129) together
correctly verify both directions of the property-fidelity claim — a hardcoded-`0` regression would
fail Check 2; a hardcoded-nonzero regression would fail Check 1. This is a genuine, minimal,
correctly-designed pair, not a redundant single-direction check.

Check 3 (the render-and-sample-back check, lines 133–183) reuses the identical
`EnvironmentMapEffect`-centre-sample technique this audit already traced and confirmed sound in
the sibling `vulkan_rendertargetcube_mip_test.cpp` report (same flat-quad/fixed-normal/Identity-matrix
setup, same `RasterizerState::CullNone` requirement, same "overwhelmingly samples the side faces"
caveat) — no new concerns beyond what that report already documents.

### Logic — PASS
Both `RenderTargetCube` instances (`rtcNoMsaa`, `rtcMsaa`) are constructed as function-local stack
objects (lines 116, 125), each exercised for its own specific check without cross-contamination —
`rtcNoMsaa` is only ever used for the property check, never rendered into, so Check 1 cannot be
accidentally influenced by any rendering side effect.

### C++ correctness — PASS
No lifetime concerns — both `RenderTargetCube` locals live for the whole `Draw()` call and are
correctly destroyed at scope exit after all checks complete.

### Robustness — PASS
`msaaPropertyOk = (msaaApplied > 1)` (line 129) rather than an exact `==8` check correctly allows
for legitimate device-clamped sample counts (the file's own comment explicitly calls out
"device-clamped value" as the expected/acceptable outcome, not necessarily exactly 8) — this is the
right tolerance for a property that FNA/XNA documents as being clamped to what the device actually
supports.

### Testing — PASS (within stated scope, same caveat as the mip sibling)
The deliberate avoidance of a genuine per-pixel AA differential (as done for `RenderTarget2D`) is
explicitly justified and, per this audit's independent assessment in the sibling mip-test report,
technically well-founded for cube-map reflection sampling — not an unexplained coverage gap.

## Detailed Findings

### F1 — Header comment's "PreferMultiSampling never reaches Vulkan" claim is stale (same defect class as `vulkan_rendertarget2d_msaa_test.cpp`); this file was also never revisited after the Task 902 fix

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage / stale-comment / architecture
- Location/symbol: header comment lines 38–43; `Initialize()`'s
  `device.RecreateBackendForMultiSampleCount(8)` call (line 92)
- Evidence: `git log --format="%ci %h %s" -- examples/vulkan_rendertargetcube_msaa_test.cpp` shows
  exactly one commit, `6896a408` at `2026-07-08 02:54:41`, titled "feat(Task 903): implement
  RenderTargetCube MSAA on Vulkan and Bgfx" — several hours **before** `22557a6d fix(Task 902):
  GraphicsDevice::Reset() now really reconfigures the backend` (`2026-07-08 08:00:41`), which its
  own commit message states fixed exactly the gap this file's header comment describes ("this test
  uses the same narrow NOXNA test-only GraphicsDevice::RecreateBackendForMultiSampleCount() hook
  Task 878/879 added to unblock this," line 43). As independently traced in this audit's
  `vulkan_rendertarget2d_msaa_test.cpp` report (F1 there, same evidence chain):
  `GraphicsDevice::Reset()` now calls a real `VulkanGraphicsBackend::ApplyMultiSampleCount()`
  override via the standard `GraphicsDeviceManager.PreferMultiSampling`+`ApplyChanges()` path, and
  a dedicated sibling test (`vulkan_msaa_test.cpp`, Task 147/902) exists specifically to prove this
  idiomatic path now works on Vulkan for the backbuffer case this file's own per-cube MSAA
  mechanism piggybacks on ("per-RT MSAA piggybacks on the backend's own already-picked
  sampleCount_," line 39-41 of this file's own header comment).
- Why it matters: identical reasoning to the sibling finding — this file's narrative currently
  reads as though the underlying `GraphicsDeviceManager` gap is still open on Vulkan, when a
  same-day fix and a purpose-built proof test (`vulkan_msaa_test.cpp`) already supersede it. The
  test itself still correctly validates the RenderTargetCube-MSAA property/no-corruption behaviour
  it targets (not a false pass), but it does not exercise the idiomatic
  `GraphicsDeviceManager.PreferMultiSampling` path a real game would use to enable per-cube MSAA,
  and its own explanatory comment overstates a limitation that no longer exists.
- FNA/XNA comparison: N/A — internal CNA test-authoring staleness, not an XNA/FNA behavior
  divergence.
- Related files: `examples/vulkan_rendertarget2d_msaa_test.cpp` (identical finding, see that file's
  own F1 for the full evidence chain), `examples/vulkan_msaa_test.cpp` (the proof-of-fix sibling).
- Suggested action (not implemented by this audit): same as the `RenderTarget2D` sibling — re-verify
  whether `gdm_->setPreferMultiSamplingProperty(true); gdm_->ApplyChanges();` now suffices before
  constructing an MSAA `RenderTargetCube` on Vulkan, and if so, retire the `NOXNA`-only
  `RecreateBackendForMultiSampleCount()` hook here too and refresh the header comment.

## Cross-File Observations

- This file is the third (of at least three: this one, `vulkan_rendertarget2d_msaa_test.cpp`, and
  `vulkan_basiceffect_textured_msaa_test.cpp`) file in this shard sharing the exact same
  pre-Task-902 staleness pattern — a systemic, not isolated, small gap across this shard's MSAA test
  family, as already noted in this audit's `vulkan_rendertarget2d_msaa_test.cpp` report.
- Shares its `EnvironmentMapEffect`-centre-sample verification technique and its accompanying
  scope-limitation reasoning (cannot force a specific face/LOD for reflection-vector sampling) with
  `vulkan_rendertargetcube_mip_test.cpp` in this same shard — consistent, non-duplicated
  documentation of the same underlying technique limitation rather than each file re-deriving (or
  worse, re-litigating) it independently.

## Missing or Weak Tests

See F1: no Vulkan test proves `RenderTargetCube` MSAA is reachable via the idiomatic
`GraphicsDeviceManager.PreferMultiSampling` path rather than the `NOXNA`-only escape hatch.
Additionally (mirroring the mip-test sibling's F1), this file's Check 3 only samples one point via
reflection-vector geometry that likely favours the cube's side faces, so it does not independently
confirm every one of the 6 faces individually engaged MSAA without corruption — an acknowledged,
proportionate limitation given the shared technique's well-documented constraints, not a fresh
concern specific to this file.

## Positive Findings

- The property-fidelity checks (Checks 1 and 2) are this file's strongest, most novel contribution
  and were independently confirmed to correctly verify both directions of the
  `GetMultiSampleCount()` fix without redundancy or a hardcoded-value assumption that would be too
  strict for legitimate device clamping.
- The file is consistent and honest about reusing an already-scoped-and-justified verification
  technique (`EnvironmentMapEffect` centre-sample) from its mip-test sibling rather than
  re-implementing or re-arguing the same limitation independently.

## Final Assessment

The property-fidelity portion of this test is sound and well-designed. As with its
`RenderTarget2D` sibling, the file's explanatory comment and setup mechanism for enabling MSAA are
stale relative to a same-day production fix (Task 902) that a dedicated sibling test already proves
resolves the described gap — this file was never revisited to reflect that (F1, MEDIUM).
