# Audit: examples/easygl_depth_format_test.cpp

## Metadata

- Source file: `examples/easygl_depth_format_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_DepthFormat`
  (`cmake/Tests/EasyGLTests.cmake:1022-1024`, target `cna_test_easygl_depth_format`)
- Related production code: `GraphicsDeviceManager::setPreferredDepthStencilFormatProperty`/
  `ApplyChanges`, `GraphicsDevice::SetPresentationParameters`, `PresentationParameters::Clone`/
  `getDepthStencilFormatProperty`
- XNA/FNA relevance: `PresentationParameters.DepthStencilFormat`,
  `GraphicsDeviceManager.PreferredDepthStencilFormat`
- Main related tests: `easygl_depthstencilstate_stencil_enable_test.cpp` (this batch) is the file that
  actually exercises a real stencil-testing behavior difference gated on `DepthFormat`; this file only
  tests the field/round-trip contract.

## Purpose

`DepthFormatTest` (Task 228) verifies that setting `DepthFormat` via both the `GraphicsDeviceManager`
path (`setPreferredDepthStencilFormatProperty` + `ApplyChanges()`) and the direct path
(`GraphicsDevice::SetPresentationParameters`) round-trips correctly through
`PresentationParameters.DepthStencilFormat` for all four `DepthFormat` values, and that neither path
throws. The file's own header comment (lines 3-11) is explicit and honest about the narrow scope: the
EasyGL backend does not recreate the depth buffer at runtime (the window-system framebuffer has a fixed
depth allocation), so the "important invariants" tested are deliberately limited to storage + no-throw.

## Executive Verdict

**Healthy for its stated (narrow) scope**, but the scope itself is a real, self-disclosed limitation:
this file validates that a `DepthFormat` value is stored and doesn't crash, not that it changes any
observable GPU behavior — a genuinely different (and weaker) claim than its filename suggests.

## Checklist Results

### API / XNA / FNA parity
`getDepthStencilFormatProperty()`/`setPreferredDepthStencilFormatProperty()`/`ApplyChanges()`/
`SetPresentationParameters()` are all correct XNA-style names matching
`GraphicsDeviceManager.PreferredDepthStencilFormat`/`ApplyChanges()` and
`GraphicsDevice.PresentationParameters`/`Reset()` semantics respectively (CNA routes
`SetPresentationParameters` through a comparable path — confirmed `PresentationParameters::Clone()`
(line 47) is used before mutating, correctly avoiding aliasing the live `PresentationParameters` the
device currently holds).

### Behavioral correctness
Confirmed the constructor-time default check (lines 61-63, `DepthFormat::Depth24`) against
`GraphicsDeviceManager`'s own default member initializer
(`preferredDepthStencilFormat_(Graphics::DepthFormat::Depth24)`,
`GraphicsDeviceManager.cpp:52`) — correct, matches. Both `gdmSet()`/`directSet()` helpers
(lines 36-53) correctly read back through `dev.getPresentationParametersProperty().
getDepthStencilFormatProperty()` after each mutation, which is the right observable to assert on given
this file's stated scope.

One notable, correctly-resolved ordering subtlety: `Initialize()` (lines 56-78) performs all 8 checks
*before* calling `Game::Initialize()` (line 77, at the very end, not the start as is conventional for
an override). This audit traced this is not a bug: `GraphicsDeviceManager`'s constructor
(`GraphicsDeviceManager.cpp:59-84`) already binds `graphicsDevice_ = &game_->getGraphicsDeviceProperty()`
at construction time (i.e., before `Initialize()` is ever called), and `Game::DoInitialize()`
(`Game.cpp:644-662`) calls `graphicsDeviceManager_->CreateDevice()` **before** invoking the virtual
`Initialize()` override — so by the time this test's `Initialize()` body runs, the device is already
fully constructed and usable regardless of when (or whether) the base `Game::Initialize()` is called.
The unconventional ordering is harmless given this call graph, not a latent bug.

### Logic
`gdmSet`/`directSet` (lines 36-53) are correctly parameterized helpers avoiding duplicated check logic
across the 8 format/path combinations — good, low-risk structure for what is fundamentally a
round-trip check repeated 8 times.

### Memory/resource lifetime
`gdm_` is a `unique_ptr`, standard pattern; no other owned resources.

### C++ correctness
No unusual casts or lifetime concerns; `PresentationParameters::Clone()` is used correctly to avoid
mutating the device's live parameters object in place before the explicit `SetPresentationParameters`
call (line 47-49).

### Performance / Thread safety
N/A — single-frame test performing 9 total format-set operations.

### Architecture
Correct XNA API surface only.

### Maintainability
Header comment (lines 1-11) is unusually candid about its own scope limitation — an example of good
practice (disclosed narrow scope) rather than a silently over-claiming test name.

### Portability
No platform-specific code.

### Robustness
`check()` (lines 30-34) accumulates pass/fail, consistent with the suite pattern.

### Testing
This is the entire dedicated test for `DepthFormat` field plumbing. It deliberately does **not** verify
that a `Depth24Stencil8` vs. `Depth24` vs. `None` selection has any actual runtime effect on rendering
(e.g., whether a stencil test genuinely works only under `Depth24Stencil8`) — that behavioral
verification is instead carried, separately, by
`easygl_depthstencilstate_stencil_enable_test.cpp` in this same batch (which explicitly requests
`Depth24Stencil8` in its own constructor and is the file that actually proves the stencil aspect is
real). Read together, the two files' combined coverage is reasonable; read in isolation, this file's
name ("depth format test") could be mistaken for testing more than it does. See Finding F1.

### Cross-file consistency
Consistent with `GraphicsDeviceManager`'s actual default (`Depth24`) and with
`easygl_depthstencilstate_stencil_enable_test.cpp`'s own header comment, which explicitly explains why
*that* file (not this one) needs to request `Depth24Stencil8` for a real stencil-behavior check —
the two files' scopes are complementary, not overlapping, by design.

## Detailed Findings

No HIGH/MEDIUM findings — the disclosed scope limitation is a documented trade-off, not a hidden gap.

### F1 — Test name implies stronger coverage than is actually verified

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / naming clarity
- Location/symbol: whole file; `DepthFormatTest` class name and `EasyGL_DepthFormat` CTest name
- Evidence: none of the 9 checks in this file observe any actual rendering/depth-test/stencil-test
  behavior difference between the four `DepthFormat` values — every check is a field getter comparison
  after a setter call. The file's own header comment (lines 3-11) already discloses this, so it is not
  a silent gap, but the CTest name `EasyGL_DepthFormat` alone (without reading the source) would suggest
  broader behavioral coverage than exists.
- Why it matters: a reader relying on CTest names/coverage reports alone (rather than opening every
  source file) could reasonably assume `DepthFormat`'s actual effect on rendering is verified somewhere
  under this name, when the real behavioral proof lives in a differently-named sibling file
  (`depthstencilstate_stencil_enable_test.cpp`). This is purely a discoverability/naming concern, not a
  functional defect — the underlying combined coverage across both files is reasonable.
- Suggested action (not implemented by this audit): none required; noted for anyone assembling a
  capability matrix across this shard so "DepthFormat" isn't double-counted as behaviorally verified by
  this file alone.

## Cross-File Observations

- This file and `easygl_depthstencilstate_stencil_enable_test.cpp` together cover what a single
  "DepthFormat" test might be expected to cover alone (field plumbing + one real behavioral
  consequence) — worth treating as a matched pair when assessing `DepthFormat`/`DepthStencilState`
  capability-matrix completeness for this shard.

## Missing or Weak Tests

- No behavioral check tied to `DepthFormat` itself within this file (by design — see F1). No check for
  an *invalid*/out-of-enum-range `DepthFormat` value (arguably out of scope, since the enum's type
  system already prevents that at the C++ call site).

## Positive Findings

- Refreshingly honest, specific header comment about the test's own scope boundary — exactly the kind
  of self-aware documentation the project's broader conventions favor over an implicit, undisclosed
  narrowing of scope.
- Clean, DRY helper-function structure (`gdmSet`/`directSet`) avoiding 8x duplicated assertion logic.
- Correctly traced and confirmed the unconventional `Initialize()`/`Game::Initialize()` ordering is
  harmless given the actual `Game`/`GraphicsDeviceManager` construction sequence.

## Final Assessment

A correctly-implemented, honestly-scoped round-trip test for `DepthFormat` field plumbing; its only
real weakness is that its name could mislead a reader who doesn't also find and read the sibling
`depthstencilstate_stencil_enable_test.cpp` that actually proves `DepthFormat` has a real runtime effect.
