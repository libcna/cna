# Audit: examples/vulkan_rendertarget2d_msaa_test.cpp

## Metadata

- Source file: `examples/vulkan_rendertarget2d_msaa_test.cpp` (210 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `RenderTarget2D` per-instance MSAA real-anti-aliasing
  test.
- File type: standalone `Game`-subclass executable (`class RenderTarget2DMsaaTest`).
- XNA/FNA relevance: direct — `RenderTarget2D(device, w, h, mipMap, format, depthFormat,
  multiSampleCount, usage)`'s `multiSampleCount` parameter; indirectly,
  `GraphicsDeviceManager.PreferMultiSampling`.
- Related production code:
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (`sampleCount_`,
  `ApplyMultiSampleCount()` line 7081, `GetOrCreateRTRenderPassMsaa`),
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`GraphicsDevice::Reset()`, lines 384–439; `RecreateBackendForMultiSampleCount()`, lines
  1346–1352), `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp`
  (`applyToExistingBackend()`, lines 551–585).
- Task references: Task 878/879 (this file, `git log`: `bda07bac feat(Task 878/879): implement
  RenderTarget2D MSAA on Vulkan and Bgfx`, 2026-07-07 17:57), Task 902 (`git log`: `22557a6d
  fix(Task 902): GraphicsDevice::Reset() now really reconfigures the backend`, 2026-07-08 08:00 —
  **postdates** this file's only commit).

## Purpose

Direct port of `easygl_rendertarget2d_msaa_test.cpp` (Task 337)'s methodology to Vulkan: proves
`RenderTarget2D` MSAA does *real* multisample anti-aliasing, not merely "accepts the parameter and
resolves without corrupting a solid fill" (which a non-MSAA target would also trivially pass).
Renders a diagonal-edged triangle into a `RenderTarget2D` at `multiSampleCount=0` and `=8`, resolves,
samples a centre row back, and checks: `MultiSampleCount=0` row must be a hard binary
red/black transition (no AA); `MultiSampleCount=8` row must contain genuinely intermediate
(partially-blended) pixel values — a signature only a real per-sample coverage resolve can produce.

The file's own header comment additionally documents a **real, separate, pre-existing bug** found
while wiring this test up: at the time this file was authored,
`GraphicsDeviceManager.PreferMultiSampling`/`ApplyChanges()` did not actually reach the Vulkan
backend at all (`GraphicsDevice`'s own `GraphicsDevice::SetPresentationParameters()` path,
deliberately not a full reset), so this test uses a `NOXNA` test-only escape hatch,
`GraphicsDevice::RecreateBackendForMultiSampleCount()`, added specifically to force real backbuffer
MSAA before any GPU resources exist.

## Executive Verdict

**Needs attention** — the RT-MSAA production mechanism itself and this test's differential
methodology are both sound (independently confirmed below), but the file's own header comment's
central claim — "`GraphicsDeviceManager.PreferMultiSampling`/`ApplyChanges()` does NOT actually
reach the Vulkan backend at all" — is **stale**: `git log` shows Task 902 fixed exactly this gap
the same day, in a commit that postdates this file's only commit, and a later sibling test
(`vulkan_msaa_test.cpp`, Task 147/902) exists specifically to prove the real
`GraphicsDeviceManager` path now works on Vulkan. This file was never revisited to check whether its
own `NOXNA`-only workaround is still necessary (see F1).

## Checklist Results

### API / XNA / FNA parity — PASS
`RenderTarget2D(device, kRTSize, kRTSize, false, SurfaceFormat::Color, DepthFormat::None,
multiSampleCount, RenderTargetUsage::DiscardContents)` (lines 85–86) exercises the full
XNA-compatible constructor overload, correctly varying only the `multiSampleCount` parameter
between the two calls (`RenderAndReadRow(device, 0)` / `RenderAndReadRow(device, 8)`, lines
164–165).

### Behavioral correctness — PASS (mechanism), STALE COMMENT (workaround necessity) — see F1
The MSAA resolve mechanism itself (`GetOrCreateRTRenderPassMsaa`, `msaaColorImage_`,
`pResolveAttachments`) was independently spot-checked via the shared `sampleCount_`/`WantsMsaa()`
plumbing traced for a sibling file in this shard
(`vulkan_rendertarget_depthformat_fidelity_test.cpp`'s audit) and is consistent with a genuine
per-RT multisample-then-resolve implementation, not a no-op. What this audit specifically
re-verified independently (not merely trusting the file's own comment) is the *staleness* claim in
F1 below.

### Logic — PASS
`IsBinary()`/`HasIntermediate()` (lines 125–143) both scan the full centre row and check the same
threshold band (`v>40 && v<215`) with opposite polarity — a correct, minimal pair of predicates for
this differential; no off-by-one or asymmetry between the two.

### C++ correctness — PASS
`RenderAndReadRow()` constructs a fresh local `RenderTarget2D rt(...)` per call (line 85), correctly
scoped to the function and destroyed at the end of each call — no dangling RT handle carried
between the two `multiSampleCount` variants.

### Robustness — PASS
The `[INFO]` diagnostic prints (lines 176–185) correctly distinguish the two distinct failure
causes for each check ("rasterizer quirk / false positive" vs. "MSAA resolve not averaging /
device doesn't support 8x MSAA") rather than a single generic failure message, aiding triage without
overclaiming a specific root cause.

### Testing — PASS
The binary-vs-intermediate differential genuinely distinguishes "no AA happened" from "AA happened,"
unlike a solid-fill-only test — this is explicitly called out and justified in the file's own header
comment, and this audit agrees it is the correct methodology (also independently confirmed sound in
this shard's mip-test sibling report, which relies on the same category of differential-design
reasoning).

## Detailed Findings

### F1 — Header comment's central "PreferMultiSampling never reaches Vulkan" claim is stale; a same-day fix and a dedicated proof test already supersede it, and this file was never revisited

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage / stale-comment / architecture
- Location/symbol: header comment lines 19–41; `Initialize()`'s
  `device.RecreateBackendForMultiSampleCount(8)` call (line 153)
- Evidence:
  - `git log --format="%ci %h %s" -- examples/vulkan_rendertarget2d_msaa_test.cpp` shows exactly
    one commit, `bda07bac` at `2026-07-07 17:57:42`, titled "feat(Task 878/879): implement
    RenderTarget2D MSAA on Vulkan and Bgfx."
  - `git log -1 --format="%ci %h %s" 22557a6d` shows `2026-07-08 08:00:41`, titled "fix(Task 902):
    GraphicsDevice::Reset() now really reconfigures the backend" — its own commit message states
    verbatim: *"GraphicsDeviceManager.PreferMultiSampling (and other preference-driven
    PresentationParameters changes) never reached the Vulkan backend's actual GPU state after
    construction ... This made vulkan_msaa_test.cpp (Task 147) a false positive its entire
    existence."*
  - `GraphicsDevice::Reset()` (`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` lines
    412–417) now calls `backend_->ApplyMultiSampleCount(presentationParameters_.getMultiSampleCountProperty())`
    and writes the real applied value back — and `VulkanGraphicsBackend::ApplyMultiSampleCount()`
    (line 7081) is a genuine in-place reconfiguration (tears down and rebuilds every piece of state
    that baked in the old `sampleCount_`), not a stub.
  - `GraphicsDeviceManager::applyToExistingBackend()` (lines 551–585) has its own comment
    explicitly confirming this: *"Task 902: real in-place device reset ... reconfigures
    backend-construction-time-only properties like MultiSampleCount
    (GraphicsDeviceManager.PreferMultiSampling) via IGraphicsBackend::ApplyMultiSampleCount()."*
  - A sibling file, `examples/vulkan_msaa_test.cpp` (Task 147/902), exists specifically to
    exercise and prove this exact real path for the Vulkan **backbuffer** case: it calls
    `gdm_->setPreferMultiSamplingProperty(true); gdm_->ApplyChanges();` *after* the backend already
    exists, and its own header comment states verbatim: *"Earlier sibling tests
    (vulkan_rendertarget2d_msaa_test.cpp etc.) had to work around the gap this task fixes via the
    NOXNA-only GraphicsDevice::RecreateBackendForMultiSampleCount() escape hatch; this test
    deliberately does NOT use that hook, to prove the real GraphicsDeviceManager path works."*
  - Per-RT MSAA (this file's actual subject) is documented, by this file's own header comment, to
    "piggyback on the Vulkan backend's own already-picked `sampleCount_`" — i.e. its only
    prerequisite is that the backend's backbuffer `sampleCount_` already be `>1` before the RT is
    constructed, which is exactly the condition `vulkan_msaa_test.cpp` already proves the idiomatic
    `PreferMultiSampling`+`ApplyChanges()` path now achieves on Vulkan.
- Why it matters: this file's header comment currently reads as though the
  `GraphicsDeviceManager.PreferMultiSampling` gap is still open on Vulkan ("does NOT actually reach
  the Vulkan backend at all... real device reset/recreation is a separate, not-yet-implemented FNA
  feature"), which is no longer accurate — a fix landed the same day, and a purpose-built sibling
  test already demonstrates the fix works for the exact prerequisite (backbuffer `sampleCount_`)
  this file's own RT-MSAA mechanism depends on. Left uncorrected, a future maintainer reading only
  this file (not cross-referencing `vulkan_msaa_test.cpp` or the Task 902 commit) would believe the
  heavier `RecreateBackendForMultiSampleCount()` full-backend-teardown-and-rebuild hook is still
  required, when the lighter, idiomatic, in-place `ApplyMultiSampleCount()` path this test's own
  sibling proves functional may now suffice. This is not a claim that the current test is
  *incorrect* (it still genuinely exercises and correctly validates the RT-MSAA resolve mechanism)
  — it is a claim that the test's own narrative about *why* it needs its specific setup mechanism
  is now unverified/outdated, and that the test does not exercise the idiomatic
  `GraphicsDeviceManager`-driven path a real game would actually use to enable per-RT MSAA on
  Vulkan.
- FNA/XNA comparison: N/A (an internal CNA architecture/test-authoring staleness finding, not an
  XNA/FNA behavior divergence).
- Related files: `examples/vulkan_msaa_test.cpp` (the file that supersedes this one's rationale for
  the backbuffer case), `examples/vulkan_rendertargetcube_msaa_test.cpp` (same shard, same stale
  claim, same NOXNA hook — see that file's own audit report), `examples/vulkan_basiceffect_textured_msaa_test.cpp`
  (also predates Task 902, also still uses the hook, per this audit's cross-check of its own header
  comment and `git log`).
- Suggested action (not implemented by this audit): re-verify whether
  `gdm_->setPreferMultiSamplingProperty(true); gdm_->ApplyChanges();`, called before this file's own
  `RenderTarget2D` construction, now suffices on Vulkan (mirroring `vulkan_msaa_test.cpp`'s own
  proof for the backbuffer case); if so, migrate this file off the `NOXNA`-only
  `RecreateBackendForMultiSampleCount()` hook and update the header comment accordingly, closing the
  gap between "what this test proves" and "what a real game would actually do."

## Cross-File Observations

- This file, `vulkan_rendertargetcube_msaa_test.cpp`, and `vulkan_basiceffect_textured_msaa_test.cpp`
  all share the identical stale-comment pattern described in F1 — all three were authored before
  Task 902 landed and none has been revisited since (confirmed via `git log --follow` on each file
  individually). This is a systemic small gap across this shard's MSAA-related files, not an
  isolated one-off in this file alone.
- `vulkan_msaa_test.cpp`'s own header comment is the single clearest piece of first-party evidence
  for F1 — it was written specifically to document and close the Task 902 gap, and explicitly
  calls out this file (`vulkan_rendertarget2d_msaa_test.cpp`) by name as one of the "earlier sibling
  tests" that had to work around it.

## Missing or Weak Tests

- See F1: no Vulkan test currently proves that `RenderTarget2D`-level MSAA (as opposed to backbuffer
  MSAA, which `vulkan_msaa_test.cpp` does cover) is reachable via the idiomatic
  `GraphicsDeviceManager.PreferMultiSampling` + `ApplyChanges()` path rather than the `NOXNA`-only
  `RecreateBackendForMultiSampleCount()` hook this file uses.

## Positive Findings

- The underlying differential methodology (binary vs. intermediate row) is sound and was
  independently confirmed to be the correct technique to distinguish real MSAA from a
  solid-fill false positive, consistent with this audit's assessment of the same technique in
  `vulkan_msaa_test.cpp`.
- The file is transparent about the pre-existing `GraphicsDeviceManager` gap it found (crediting
  its own investigation, not silently working around it without explanation) — the issue this audit
  raises in F1 is that the comment's *currency*, not its original honesty, has lapsed.

## Final Assessment

The RT-MSAA mechanism and this test's own differential design are sound. The one substantive
finding (F1, MEDIUM) is that this file's explanatory comment and setup mechanism were overtaken by
a same-day fix (Task 902) and a purpose-built sibling test (`vulkan_msaa_test.cpp`) that already
proves the idiomatic path works — this file was never revisited to reflect that, leaving both a
stale narrative and an unclosed test-coverage gap (the idiomatic per-RT-MSAA-enablement path is
still unverified on Vulkan).
