# Audit: examples/easygl_rendertarget2d_properties_test.cpp

## Metadata

- Source file: `examples/easygl_rendertarget2d_properties_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `RenderTarget2D` constructor/property black-box test (backend-agnostic,
  shared with Vulkan/Bgfx builds per its own header comment)
- File type: C++ example/integration-test executable (`RenderTarget2DPropertiesTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::RenderTarget2D` (`RenderTarget2D.cpp`/`.hpp`)
- XNA/FNA relevance: direct — every property asserted (`Width`, `Height`, `Format`, `DepthStencilFormat`,
  `RenderTargetUsage`, `LevelCount`, `IsContentLost`, `ContentLost`, `MultiSampleCount`) is a real XNA 4.0
  `RenderTarget2D` member; judged against `FNA/src/Graphics/RenderTarget2D.cs`.
- Main related tests: this file (Task 331); the mip-chain fix it references (Task 336) is behaviorally exercised by
  `easygl_rendertarget2d_mip_test.cpp`, and the MSAA fix (Task 337) by `easygl_rendertarget2d_msaa_test.cpp` — this
  file only checks the reported *property values*, not the underlying GPU behavior those two sibling files verify.

## Purpose

A pure black-box property/constructor audit: constructs `RenderTarget2D` via each of its three public constructor
overloads and asserts every getter against the value FNA's `RenderTarget2D.cs` computes or documents, with no
rendering or pixel readback. Also validates the `Task 331`-added `IsContentLost`/`ContentLost` members (previously
present on `RenderTargetCube` but missing from `RenderTarget2D`). Correctly placed per `AUDIT_SCOPE.md`.

## Executive Verdict

**Healthy** — every assertion in this file was independently checked against both `RenderTarget2D.cpp`'s actual
implementation and FNA's `RenderTarget2D.cs` reference semantics and found accurate; the `MultiSampleCount`
assertions are deliberately (and correctly) written to be valid across three different backends' current
implementation states rather than asserting one hardcoded expectation.

## Checklist Results

### API / XNA / FNA parity
Confirmed against `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/RenderTarget2D.cs`:
- Ctor1 `RenderTarget2D(device, width, height)` (line 53) delegates in both FNA (lines 87-100) and CNA
  (`RenderTarget2D.cpp:39-42`) to the full overload with `mipMap=false, format=Color, depthFormat=None,
  multiSampleCount=0, usage=DiscardContents` — the test's expected defaults (lines 54-63) match exactly on both
  sides.
- `DepthStencilFormat`, `MultiSampleCount`, `RenderTargetUsage` are all real FNA public properties
  (`RenderTarget2D.cs` lines 20-35) with `private set` — the test only reads them via getters, correct usage.
- `IsContentLost` (line 62, checked `== false`) matches FNA's hardcoded `get { return false; }` (`RenderTarget2D.cs`
  lines 37-42, "We never lose data, but lol XNA4 compliance -flibit") — CNA's
  `getIsContentLostProperty()` returning a hardcoded `false` (`RenderTarget2D.hpp:89`) is a faithful, intentional
  match of FNA's own behavior, not a CNA shortcut.
- `ContentLost` event (line 63, `rt.ContentLost.Empty()`) matches FNA's declared-but-never-raised
  `EventHandler<EventArgs> ContentLost` (`RenderTarget2D.cs` lines 74-78) — correct parity; FNA itself never raises
  this event either, so CNA's identical inertness is not a gap.

### Behavioral correctness — verified against production code
- **`LevelCount == 1` (no mipMap)** (line 61): traced `RenderTarget2D::RenderTarget2D` (`RenderTarget2D.cpp:44-58`),
  passes `mipMap ? CalculateMipLevels(width,height) : 1` to `Texture2D`'s levelCount ctor arg — `mipMap=false` (Ctor1
  default) always yields `1`. Confirmed.
- **`LevelCount == 7` for 64×64 mipMap=true** (line 92-93): independently re-computed `CalculateMipLevels(64,64)`
  (`RenderTarget2D.cpp:15-20`): levels start at 1, loop halves w/h with `std::max(1,...)` floor until both are 1 —
  `64→32→16→8→4→2→1` is 6 halvings, so `levels = 1 + 6 = 7`. Matches the test's expected value exactly, and matches
  FNA's standard `log2(64)+1 = 7` mip-count convention for a power-of-two texture.
- **`MultiSampleCount` defaults to 0** (line 74-75): confirmed default arg `preferredMultiSampleCount = 0`
  (`RenderTarget2D.hpp:49`), and the ctor's post-construction clamp (`RenderTarget2D.cpp:64-66`,
  `if (rtBackend_) multiSampleCount_ = rtBackend_->GetMultiSampleCount();`) — for a genuinely-0 request, any
  reasonable backend's `GetMultiSampleCount()` returns 0, consistent with the test's expectation.
- **`MultiSampleCount == 4 || == 0` for a requested 4** (lines 102-108): this is the test's most interesting
  assertion — it deliberately accepts either the real clamped value (EasyGL, which the audit confirmed via
  `EasyGLRenderTargetBackend::CreateResources`'s `GLint maxSamples` clamp, `EasyGLGraphicsBackend.cpp:598-604`) or a
  flat `0` (Vulkan/Bgfx, documented as "not yet implemented," Task 879) — a deliberately backend-permissive assertion
  that is still a real regression guard, since it explicitly rules out "9999 unchanged" in the next sub-test.
- **`multiSample=9999` is never a blind pass-through** (lines 109-119): `valid = (v==0) || (v>0 && v<9999 &&
  (v&(v-1))==0)` — checks the result is either the honest "not implemented" 0, or a real power-of-two value strictly
  less than the absurd request. This is the actual regression guard for the pre-Task-337 bug (a raw pass-through of
  the caller's argument with no device-capability clamping) — correctly designed to catch that specific class of
  regression regardless of which backend the test runs under.

### Logic
Three independently-scoped `{ ... }` blocks (lines 52-64, 66-76, 78-86) construct and destroy each `RenderTarget2D`
before moving to the next sub-test, avoiding any possibility of state leaking between the different constructor
overload checks. `check()` (lines 37-41) accumulates `pass_`/`fail_` counters and prints a per-assertion PASS/FAIL
line plus a final summary (line 121) — a consistent, greppable output format shared across this whole shard.

### Memory/resource lifetime
Each `RenderTarget2D rt` is stack-local and scoped to its enclosing `{ }` block — destroyed (and disposed, since
`~RenderTarget2D` triggers `Dispose(false)` → `Texture2D::Dispose` → `backend_.reset()`) before the next construction,
so no accumulation of live GPU handles across the 6 sub-tests. No `SetRenderTarget` is ever called in this file
(unlike the sibling rendering tests), so `RenderTarget2D::Dispose`'s "still bound" guard (`RenderTarget2D.cpp:86-93`)
is never exercised here — correctly out of scope for a pure property test.

### C++ correctness
No raw pointers, no casts, no UB risk — this file is entirely straight-line construction + getter calls.

### Performance
N/A — one-shot property test, negligible cost.

### Thread safety
N/A.

### Architecture
Pure public-XNA-API usage; correctly backend-agnostic per its own header comment ("This assertion holds on
Vulkan/Bgfx too") — confirmed accurate since `CalculateMipLevels`/`ClosestMSAAPower` (`RenderTarget2D.cpp:15-37`) are
shared, non-backend-specific C++ that runs identically regardless of which `IGraphicsBackend` is active; only the
backend-reported `GetMultiSampleCount()` value differs, which the test correctly accounts for.

### Maintainability
Header comment (lines 1-18) is accurate and specifically dated to the tasks (331/336/337) whose fixes it validates —
each claim was independently checked against the actual code rather than taken at face value, and each held up.

### Portability
N/A for this file's own code; the test's `MultiSampleCount` assertions are explicitly designed to be portable across
backends (see above) — a positive, not a gap.

### Robustness
`fail_ > 0` (line 133) is the correct pass/fail aggregation; no PASS is silently swallowed since every `check()` call
prints its own line regardless of overall outcome.

### Cross-file consistency
Directly complements `easygl_rendertarget2d_mip_test.cpp` (behavioral proof the `LevelCount==7` mip chain this file
only checks the *count* for is actually GL-complete) and `easygl_rendertarget2d_msaa_test.cpp` (behavioral proof the
`MultiSampleCount` this file only checks the *reported value* for produces real AA). The three files together give
full coverage (declared value + underlying GPU behavior) for both the mip and MSAA features — a well-organized split
of responsibilities across the shard, not overlapping redundancy.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — `Ctor3`'s `DepthStencilFormat`/`RenderTargetUsage`-only assertions leave `MultiSampleCount=0` explicit-arg case unchecked for that overload

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `Ctor3` sub-test (lines 78-86)
- Evidence: the full-overload constructor test (`RenderTarget2D(device, 8, 8, false, Color, Depth16, 0,
  PreserveContents)`, lines 80-81) only asserts `RenderTargetUsage` and `DepthStencilFormat`; it does not also assert
  `getMultiSampleCountProperty() == 0` for this specific overload/argument combination (that combination is checked
  separately under Ctor2, lines 74-75, using a different depth format and usage).
- Why it matters: purely a coverage completeness nit — the 0-multisample path is exercised elsewhere in the file, so
  this is not a real gap in what's ultimately verified, just a missed opportunity to assert every property on every
  constructed instance for maximal regression-locality.
- FNA/XNA comparison: N/A.
- Related files: none.
- Suggested future action (not implemented by this audit): add the one extra `check()` line if this file is touched
  again; not worth a standalone change.

## Missing or Weak Tests

- No test asserts `Width`/`Height`/`Format` for the Ctor2/Ctor3 overloads (only Ctor1 checks these) — reasonable
  since those getters are inherited unchanged from `Texture2D` regardless of overload and are unlikely to vary by
  constructor path, but strictly speaking unverified for Ctor2/Ctor3 specifically.
- No test constructs a `RenderTarget2D` with a non-power-of-two size to confirm `LevelCount`'s mip-chain math for an
  irregular dimension (e.g. `LevelCount` for `mipMap=true, 100x60`) — only the clean 64×64 case is checked.

## Positive Findings

- Every property assertion was independently re-derived from FNA's `RenderTarget2D.cs` and CNA's own implementation
  rather than assumed correct because "the test says so" — all held up under scrutiny.
- The `MultiSampleCount` assertions are a genuinely well-designed, deliberately backend-portable regression guard
  that still catches the specific historical bug (unclamped pass-through) it targets.
- Explicit acknowledgment in comments of exactly which backends currently satisfy which assertions and why (Vulkan/
  Bgfx honestly reporting `0` rather than lying about unimplemented MSAA/mip support) — accurate, not aspirational.

## Final Assessment

A precise, accurately-targeted black-box property test whose every assertion checks out against both the CNA
implementation and the FNA reference it claims to mirror. Only trivial, non-blocking coverage-completeness gaps
remain (F1 and the two minor gaps above).
