# Audit: examples/easygl_graphicsdevice_reference_stencil_test.cpp

## Metadata

- Source file: `examples/easygl_graphicsdevice_reference_stencil_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard) — **shared source**, also
  compiled/registered as a Vulkan test (see Cross-file consistency).
- File type: C++ example/integration test, registered via `cmake/Tests/EasyGLTests.cmake:1420`
  (`cna_test_easygl_graphicsdevice_reference_stencil`, `EasyGL_GraphicsDevice_ReferenceStencil`) and
  `cmake/Tests/VulkanTests.cmake:203` (`cna_test_vulkan_graphicsdevice_reference_stencil`).
- Related production code: `GraphicsDevice::getReferenceStencilProperty`/`setReferenceStencilProperty`
  (`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:1750-1756`),
  `IGraphicsBackend::SetReferenceStencil` (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp:664-669`),
  backend overrides in `Bgfx`, `D3D9`, `D3D11`, `SdlGpu`, `Vulkan`, `WebGPU` — **not** `EasyGL`.
- XNA/FNA relevance: `GraphicsDevice.ReferenceStencil` is a real, independent XNA 4.0 device property
  (FNA's `FNA3D_Get/SetReferenceStencil`), analogous to `GraphicsDevice.BlendFactor`.
- Main related tests: `easygl_graphicsdevice_clear_stencil_test.cpp` (same batch) covers the
  adjacent-but-distinct stencil-*clear* behavior.

## Purpose

`GraphicsDeviceReferenceStencilTest` verifies that `GraphicsDevice.setReferenceStencilProperty()` — a
device-level property independent of any one `DepthStencilState` object — actually overrides the
*active* stencil-compare reference used by the next draw call, even when the currently-assigned
`DepthStencilState` object has its own, different, baked-in `ReferenceStencil`. The test's own header
comment states plainly that it expects this to **FAIL** on every backend it can currently run on
(EasyGL here), documenting a known, tracked gap (Task 872). Correct placement as a
backend-shared integration test that intentionally encodes an expected-failure regression marker.

## Executive Verdict

**Needs attention** — not because the test's own EasyGL-specific behavior/expectation is wrong (it
isn't: confirmed by source inspection that EasyGL genuinely has no live path for this override), but
because the test file's own header comment makes a **stale, overly broad factual claim** about the
current state of the codebase that this audit found to be false: it says there is "no
`SetReferenceStencil` method anywhere in `IGraphicsBackend` at all" and that the gap is "universal ...
not just Vulkan." Both are demonstrably false as of the current tree — `IGraphicsBackend::SetReferenceStencil`
exists (with its own "Task 870/319" doc comment) and is genuinely implemented, all the way to the
draw-time pipeline state, on six backends including Vulkan.

## Checklist Results

### API / XNA / FNA parity
`GraphicsDevice::getReferenceStencilProperty`/`setReferenceStencilProperty` match FNA's
`GraphicsDevice.ReferenceStencil` property name and semantics (an independent device-level override,
not merely a mirror of the active `DepthStencilState`'s own field) — this is exactly the FNA behavior
the test's header comment (lines 11-14) describes and is correct as a description of intended XNA
semantics.

### Behavioral correctness
Traced the full call chain for **EasyGL** specifically:
1. `dev.setDepthStencilStateProperty(stamp)` (with `ReferenceStencil=0x05`) →
   `GraphicsDevice.cpp:1687-1710`'s `ApplyDepthStencilState(...)` call forwards `0x05` as the
   `referenceStencil` *parameter* to `EasyGLGraphicsBackend::ApplyDepthStencilState`
   (`EasyGLGraphicsBackend.cpp:1924-1969`), which threads it straight into
   `device.set_stencil_func(...)`/`set_stencil_func_separate(...)` — i.e., EasyGL's stencil-compare
   reference is set **directly from the `DepthStencilState` object's own field**, not from a separate
   device-level override slot.
2. `dev.setReferenceStencilProperty(0x99)` → `GraphicsDevice.cpp:1751-1756` sets `referenceStencil_ =
   0x99` and calls `backend_->SetReferenceStencil(0x99)`.
3. **Confirmed via `grep`**: `EasyGLGraphicsBackend.cpp` contains **no** `SetReferenceStencil` override
   at all — so this call resolves to `IGraphicsBackend::SetReferenceStencil`'s default no-op body
   (`IGraphicsBackend.hpp:669`, `virtual void SetReferenceStencil(int /*value*/) {}`). The 0x99 value
   is stored in `GraphicsDevice::referenceStencil_` (a device-side field) but never reaches EasyGL's
   actual stencil-compare state.
4. The subsequent `DrawQuad(dev, kGreen)` therefore still uses the `compare` `DepthStencilState` object's
   own baked-in `ReferenceStencil=0x05` (set in step 1's *original* assignment, whose own
   `ApplyDepthStencilState` call happened when `compare` was assigned, separately from step 2's
   no-effect override) — `Equal(0x05, buffer=0x05)` passes, the quad draws green, and the test correctly
   reports **FAIL** (its own `ok = !isGreen` check, line 137, produces `false`), matching the header
   comment's prediction for EasyGL.
- **This specific EasyGL-scoped prediction is accurate and well-verified.** The problem is the header
  comment's *broader* claim, addressed in Finding F1 below.

### Logic
Single-shot `Draw()` (line 88-149), no branching beyond the final pass/fail print — logic is a linear
stamp → reassign-state → override → draw → readback → compare sequence, correctly ordered to isolate
exactly the one variable under test (whether the *override* — as opposed to the *state object's own
field* — takes effect).

### Memory/resource lifetime
`gdm_` constructed with `DepthFormat::Depth24Stencil8` in the constructor (line 154-156), matching
the same necessary precondition documented in this test's sibling clear-stencil test. `BasicEffect fx(dev);`
(line 99) is a local stack object here (unlike the sibling clear-stencil test's function-local static
pattern) — cleaner, no cross-call lifetime concern.

### C++ correctness
No raw-pointer aliasing or undefined-behavior risk; straightforward value-type usage throughout.

### Performance
N/A — single draw-pair, single frame, single readback.

### Thread safety
N/A.

### Architecture
Correctly scoped to public `GraphicsDevice`/`DepthStencilState` API; the test's own INFO print on
failure (line 143-145) is a genuinely useful piece of self-documentation ("the compare still used the
state's own baked-in ReferenceStencil") that helps a reader interpret an expected-failure result
correctly rather than mistaking it for a spurious/flaky failure.

### Maintainability
167 lines, clear structure, thorough header comment — **but see F1**: the header comment's specific
factual claims about IGraphicsBackend's current interface surface are stale and should be corrected or
re-verified, since they no longer match the code as of this audit.

### Portability
The test explicitly reminds (header comment, lines 6-9) that `PresentationParameters.DepthStencilFormat`
defaults to `Depth24Stencil8`-less `Depth24`, and that this test's constructor must request
`Depth24Stencil8` explicitly, and that `GraphicsDevice::Clear` ignores `ClearOptions::Stencil`
entirely (Task 871) — correctly avoiding `Clear()` for baseline setup and using a real stamp draw
instead (line 106-114), matching its own stated rationale precisely.

### Robustness
The test correctly reports "FAIL" as the expected outcome for its own target backend, with a clear
diagnostic — appropriate handling of a known, tracked, deliberately-not-yet-fixed (for this backend)
condition rather than silently hiding it.

### Testing
This file is the dedicated regression/documentation test for the `ReferenceStencil`-override gap
(Task 872) on backends that have not yet implemented `IGraphicsBackend::SetReferenceStencil`. Since
Task 870 (evidenced by the doc comment at `IGraphicsBackend.hpp:664`) has already implemented and wired
this method on six backends, this specific EasyGL-scoped instance is exactly correct — but see F1 for
why the underlying claim needs updating and Missing/Weak Tests below for what should exist to properly
close out Task 872 given the now-partial state.

### Cross-file consistency
**Confirmed** this exact source file also backs the Vulkan CTest registration
(`VulkanTests.cmake:200-205`), whose own comment reads: *"Task 319: GraphicsDevice.ReferenceStencil
independent-override behavior on Vulkan (Task 872, universal gap, reuses the backend-agnostic EasyGL
source)"* and the EasyGL registration's own comment (`EasyGLTests.cmake:1417-1418`): *"confirmed a
universal, not-Vulkan-specific gap; registered as a documented known failure."* **Both of these cmake
comments repeat the same now-stale claim as this test file's own header comment.** Tracing
`VulkanGraphicsBackend.cpp` directly: it **does** override `SetReferenceStencil` (line 7998-8001,
`referenceStencil_ = value;`), and that field is threaded into the real Vulkan pipeline dynamic state
at **five** separate draw-time call sites (`d.referenceStencil = referenceStencil_;` at lines 7296,
7342, 7401, 7644, 7881) — meaning on Vulkan, `setReferenceStencilProperty()`'s override genuinely
reaches the draw-time stencil-compare reference, independently of whatever `DepthStencilState` object
is currently assigned. This strongly suggests the Vulkan instance of this exact same test
(`cna_test_vulkan_graphicsdevice_reference_stencil`) **now passes**, contradicting three separate
comments (this file's own header, and both cmake registration comments) that still describe the gap as
a "universal," "not-Vulkan-specific," "confirmed" failure. See Finding F1.

## Detailed Findings

### F1 — Test file's header comment (and both cmake registration comments reusing it) make a stale, falsified "universal gap" claim

- Severity: MEDIUM
- Confidence: HIGH
- Category: documentation / maintainability / test-suite accuracy
- Location/symbol: `easygl_graphicsdevice_reference_stencil_test.cpp` lines 25-31 ("this project's
  `GraphicsDevice::setReferenceStencilProperty` is confirmed ... to be a pure local no-op with ZERO
  backend connection on ALL THREE backends -- there is no `SetReferenceStencil` method anywhere in
  `IGraphicsBackend` at all ... not just Vulkan"); `cmake/Tests/VulkanTests.cmake:200-201`; `cmake/Tests/EasyGLTests.cmake:1417-1418`.
- Evidence: `IGraphicsBackend::SetReferenceStencil` exists today (`IGraphicsBackend.hpp:664-669`,
  itself carrying a "Task 870/319" doc comment describing this exact property), and is overridden with
  a real, functioning implementation in `Bgfx`, `D3D9`, `D3D11`, `SdlGpu`, `Vulkan`, and `WebGPU`
  backends (confirmed via `grep -rl "SetReferenceStencil"` across `src/CNA/Internal/Backends/`).
  Tracing Vulkan's own implementation shows the override value is genuinely threaded into draw-time
  pipeline state at five call sites, not merely stored and ignored.
- Why it matters: a reader (human or another audit pass) taking this test's header comment or either
  cmake comment at face value would conclude the `ReferenceStencil`-override bug is still present and
  identical on every backend, including Vulkan — which appears to no longer be true. If the Vulkan
  variant of this test now silently passes, that's a genuine fix landing without its own tracking
  documentation being updated (a "done but not marked done" gap); if it still somehow fails on Vulkan
  for a different, subtler reason (e.g. a pipeline-cache/dynamic-state wiring issue not visible from
  this trace alone), then the comment's specific mechanism ("no such method exists") is still wrong
  even though the top-line "still fails" conclusion might coincidentally hold. Either way, the stated
  *reason* for the expected failure is now inaccurate for at least the Vulkan case, and the *scope*
  claim ("ALL THREE backends," "universal," "not just Vulkan") actively misdirects anyone trying to
  scope out remaining Task 872 work — 8 backends (including EasyGL) still lack the override, but 6
  others do not, and this file's own comment does not reflect that split at all.
- FNA/XNA comparison: N/A — this concerns internal documentation accuracy, not FNA behavior.
- Related files: `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` (defines the now-existing
  method), `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (a working override),
  `cmake/Tests/VulkanTests.cmake`, `cmake/Tests/EasyGLTests.cmake` (both repeat the stale claim).
- Suggested future action (not implemented by this audit — this is an AUDIT-ONLY task): (1) actually
  run `cna_test_vulkan_graphicsdevice_reference_stencil` to determine its current real pass/fail result;
  (2) update this test file's header comment and both cmake comments to reflect the true, current,
  per-backend split (fixed on Bgfx/D3D9/D3D11/SdlGpu/Vulkan/WebGPU; still open on
  EasyGL/SdlRenderer/Ascii/Software/Headless/Canvas/Dx3/D3D12); (3) if Vulkan's test now passes, that is
  good news that should be reflected in whatever tracking document owns Task 872's status, not left
  looking like an unfixed universal gap.

## Cross-File Observations

- The same stale "universal gap" framing appears independently in three places (this test file, and
  two separate `cmake/Tests/*.cmake` registration comments) — worth a single cross-cutting
  `AUDIT_CROSS_CUTTING_FINDINGS.md` entry rather than three separate per-file fixes, since correcting
  one without the others would leave an inconsistency between the test source and its own build
  registration comments.
- This is the clearest concrete example encountered so far in this shard of the general risk
  `AUDIT_CHECKLIST.md`'s "Testing" section warns about: a test whose own claimed defect scope needs
  periodic re-verification against the current tree, not just against the tree as of whichever task
  originally wrote it.

## Missing or Weak Tests

- No per-backend enumeration/matrix test exists that would make Task 872's true current status
  (fixed on 6 backends, open on 8) visible in one place — today it requires manually grepping every
  backend's `.cpp` for `SetReferenceStencil`, exactly as this audit had to do. A lightweight capability
  matrix (even just a comment block enumerating backend → fixed/not-fixed) would prevent this kind of
  staleness from recurring.

## Positive Findings

- The test's *own, EasyGL-specific* technical claim (the actual mechanism: `ApplyDepthStencilState`
  bakes the reference in from the state object, `SetReferenceStencil` is a no-op on this backend, so
  the override never reaches the draw) is accurate and was independently re-derived and confirmed by
  this audit via direct source tracing — the bug it describes for EasyGL is real, not stale.
- The `[INFO]` diagnostic printed on failure (line 143-145) is a thoughtful touch that correctly
  explains *why* the expected-failure result looks the way it does, reducing the chance a future reader
  mistakes this documented known-failure for a flaky/spurious one.

## Final Assessment

The test's concrete, backend-specific prediction for EasyGL is correct and well-verified by this audit.
However, its header comment (and two cmake registration comments that repeat it verbatim in spirit)
overclaim the scope and mechanism of the underlying bug in a way that is now demonstrably false for at
least the Vulkan backend, which has a real, working `SetReferenceStencil` override wired all the way to
draw-time pipeline state. This is exactly the kind of "confirmed" claim this audit's own instructions
warn against trusting without re-verification, and it warrants a documentation correction pass even
though the test's own runtime behavior for its EasyGL target is not in question.
