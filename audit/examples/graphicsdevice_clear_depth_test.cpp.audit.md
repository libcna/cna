# Audit: examples/graphicsdevice_clear_depth_test.cpp

## Metadata

- Source file: `examples/graphicsdevice_clear_depth_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — `GraphicsDevice::Clear()`'s `depth` parameter
  end-to-end verification, shared verbatim source registered on all 3 3D-capable backends: EasyGL
  (`cmake/Tests/EasyGLTests.cmake:1432-1435`, `EasyGL_GraphicsDevice_ClearDepth`), Bgfx
  (`cmake/Tests/BgfxTests.cmake:650-654`, `Bgfx_GraphicsDevice_ClearDepth`), Vulkan
  (`cmake/Tests/VulkanTests.cmake:215-219`, `Vulkan_GraphicsDevice_ClearDepth`) — confirmed by
  direct `grep` across all `cmake/Tests/*.cmake` files.
- XNA/FNA relevance: direct — `GraphicsDevice.Clear(ClearOptions, Color, float, int)`,
  `DepthStencilState.DepthBufferFunction`/`DepthBufferWriteEnable`.
- FNA reference: `Graphics/GraphicsDevice.cs`'s `Clear(ClearOptions, Vector4, float, int)` — the
  depth value forwarded to the underlying `FNA3D_Clear` must be the caller-requested value, not a
  hardcoded constant.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`Clear(ClearOptions, const Color&, float, int)`, lines 284-365),
  `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`ClearColorAndDepth`/`ClearColorDepthAndStencil`, lines 7194-7246),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ClearColorAndDepth`/`ClearColorDepthAndStencil`, lines 1963-2000).

## Purpose

Regression test for Task 950: a real, pre-existing bug where Vulkan and Bgfx silently hardcoded the
depth-buffer clear value to `1.0` regardless of what `GraphicsDevice::Clear()`'s `depth` argument
actually requested, found incidentally while implementing Task 871's stencil-clearing fix. Method:
clear to a deliberately non-default depth, then draw a single quad at a `z` value with
`DepthBufferFunction::Less` and `DepthBufferWriteEnable=false` (so the draw itself cannot perturb
the buffer). If the clear correctly wrote the requested depth, the new fragment's `z` fails the
`Less` test against it and the pixel stays background (REJECTED — correct); if the bug is present
(depth silently hardcoded to `1.0`), the fragment's `z` (chosen to be `<1.0` but `>` the correctly-
requested clear depth) wrongly passes and overwrites the pixel (ACCEPTED — bug reproduced). Check A
exercises `ClearOptions::Target|DepthBuffer` (→ `IGraphicsBackend::ClearColorAndDepth`, clear
depth=0.3, draw z=0.6); Check B exercises `ClearOptions::Target|DepthBuffer|Stencil` (→
`ClearColorDepthAndStencil`, clear depth=0.7 — deliberately different from Check A so a broken
dispatch that leaves depth at some *other* single hardcoded constant can't coincidentally still pass
both — draw z=0.9, still `<1.0`/still not `<0.7`). The file's own comment explicitly and correctly
scopes out `ClearOptions::DepthBuffer` alone (`IGraphicsBackend::ClearDepth`) as deliberately
untested here, citing Task 871's separate, unrelated, already-accepted "doesn't visibly apply until
the next real clear" limitation for that specific entry point.

## Executive Verdict

**Healthy** — this audit independently traced the entire dispatch chain
(`GraphicsDevice::Clear(ClearOptions,...)` → `ClearColorAndDepth`/`ClearColorDepthAndStencil` →
the stored `clearDepth_`/`clearDepthValue_` actually being consumed in the real clear-value
construction, not just stored and dropped) on both Vulkan and Bgfx, and confirmed the Task 950 fix
is genuinely present, correctly wired end-to-end, and the test's own rejection-threshold logic
correctly distinguishes the fixed behavior from the specific historical bug it targets.

## Checklist Results

### API / XNA / FNA parity
`dev.Clear(clearOptions, kBackground, clearDepthValue, 0)` (line 93) matches FNA's real
`GraphicsDevice.Clear(ClearOptions, Color, float, int)` 4-argument overload exactly.
`DepthStencilState` built inline (lines 97-101,
`setDepthBufferEnableProperty`/`setDepthBufferWriteEnableProperty`/`setDepthBufferFunctionProperty`)
correctly uses the property-setter convention. `ClearOptions::Target | ClearOptions::DepthBuffer` /
`| ClearOptions::Stencil` bitwise-OR composition (lines 133, 144) matches FNA's `[Flags] enum
ClearOptions` semantics.

### Behavioral correctness
Traced `GraphicsDevice::Clear(ClearOptions options, const Color& color, float depth, int stencil)`
(`GraphicsDevice.cpp:284-365`) in full:
- Line 291-297: `depth` is range-validated (`< 0.0f || > 1.0f` throws
  `ArgumentOutOfRangeException`) *before* any backend dispatch — this test's `0.3`/`0.7` values are
  both safely in-range, so this guard is not incidentally exercised, but confirms the API surface is
  correctly defensive.
- Lines 308-323: `options` is masked down to `ClearOptions::Target` only if the currently-bound
  target lacks a *real* depth-stencil buffer — this test's `GraphicsDeviceManager` explicitly sets
  `setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8)` (line 168), so
  `hasRealDepthBuffer` is expected to be `true` and this masking path should not fire, letting the
  test's requested `DepthBuffer`/`Stencil` flags reach the backend dispatch as intended.
- Lines 337-364: the exact dispatch table was read and confirmed to match this test's own two
  checks precisely: `clearTarget && clearDepth && clearStencil` → `ClearColorDepthAndStencil` (Check
  B's path); `clearTarget && clearDepth` (without stencil) → `ClearColorAndDepth` (Check A's path).
  Both are exactly the entry points this test's header comment claims to exercise.
- Vulkan: `ClearColorAndDepth`/`ClearColorDepthAndStencil` (`VulkanGraphicsBackend.cpp:7194-7202,
  7238-7246`) both store the caller's `depth` into `clearDepth_`; this audit further traced
  `clearDepth_`'s only consumers (`RecordCommandBuffer`'s `VkClearValue` construction at lines
  6719/6723/6762/6764) and confirmed it is read there, not overwritten with a hardcoded value
  anywhere else in the file — the fix is genuinely wired end-to-end, not merely stored-and-dropped.
- Bgfx: `ClearColorAndDepth`/`ClearColorDepthAndStencil` (`BgfxGraphicsBackend.cpp:1963-1967,
  1995-2000`) store into `clearDepthValue_`; this audit confirmed (line 1379-1381, with the file's
  own comment explicitly noting "Task 950: `clearDepthValue_` replaces a previously-hardcoded
  `1.0f`") that this value is passed directly into `bgfx::setViewClear(...)`'s depth argument — again
  genuinely fixed end-to-end, not just superficially renamed.
- The rejection-check helper (`ClearThenExpectRejected`, lines 90-109) reads back the center pixel
  and checks `c.getGProperty() <= 60 && c.getRProperty() <= 60 && c.getBProperty() <= 60` (line 108)
  against `kBackground=(20,20,20)` (well under the `60` threshold) vs. `kTestColor=(0,255,0)` (G=255,
  far over) — a correctly-separated pass/fail threshold with generous margin on both sides, not a
  borderline tolerance that could accidentally pass either way.

### Logic
The `step_`/`switch` state machine (lines 127-159) correctly runs Check A on frame 0 and Check B on
frame 1, guarded by `done_` (line 123) to make the whole sequence a clean one-shot regardless of how
many additional `Draw()` calls occur after `Exit()` is requested. `passCount_` accumulates across
both checks and the final `result_ = (passCount_ == 2) ? 0 : 1` (line 151) requires **both** checks
to pass, not just one — correct for a test claiming to validate 2 independent entry points.

### C++ correctness
`ApplyBasicEffect()` (lines 77-86) uses a function-local `static BasicEffect* fx = nullptr;` that is
heap-allocated once (`new BasicEffect(dev)`) and never deleted — see F1. `DrawQuad()`/
`ApplyBasicEffect()` both take `GraphicsDevice&` by reference with no lifetime concerns (the
`GraphicsDeviceManager`/`Game` own the device for the process's whole lifetime). No unsafe casts,
no signed/unsigned issues in the reviewed arithmetic.

### Memory/resource lifetime
See F1 — the static leaked `BasicEffect*` is a real, if practically inconsequential (single-process,
short-lived diagnostic binary that exits immediately after the two checks), resource-lifetime
finding.

### Performance
N/A — 64×64, single quad per check, two total frames; trivial cost appropriate for a correctness
test.

### Thread safety
N/A — single-threaded `Game` harness.

### Architecture
Exercises only the public `GraphicsDevice`/`DepthStencilState`/`BasicEffect`/`ClearOptions` API
surface with no backend-specific code — genuinely shared, backend-agnostic source, confirmed by
its identical byte-for-byte registration across 3 separate CMake files.

### Maintainability
Extensive, precise header comment (lines 1-30) correctly explaining both the historical bug, the
specific mechanism this test uses to distinguish fixed-vs-broken behavior, and an explicit,
well-reasoned scope exclusion (the `ClearDepth`-alone entry point) rather than silently omitting it.
A strong example of self-documenting test-authoring in this codebase.

### Portability
No platform-specific code.

### Robustness
The choice of *different* clear-depth values between Check A (`0.3`) and Check B (`0.7`) is a
deliberately-designed robustness property, explicitly called out in the header comment: it prevents
a partially-broken fix (e.g. one that clears to some other single hardcoded constant instead of
`1.0`) from coincidentally passing both checks. This audit confirms the reasoning is sound: any
single hardcoded depth constant other than the two actually-requested values would fail at least one
of the two checks' specific `draw z</> clear depth` relationship.

### Testing
Directly and effectively tests the exact regression it claims to (Task 950), with the underlying
fix independently confirmed wired end-to-end on both Vulkan and Bgfx by this audit (not merely
re-stated from the header comment). One minor scope gap already explicitly and correctly disclosed
by the file itself (`ClearOptions::DepthBuffer` alone, i.e. `ClearDepth()`, is intentionally
untested here per the Task 871 precedent it cites) — a transparent, well-justified exclusion, not an
oversight.

### Cross-file consistency
`GraphicsDevice::Clear()`'s dispatch table, and both Vulkan's and Bgfx's `ClearColorAndDepth`/
`ClearColorDepthAndStencil` implementations plus their downstream consumers of the stored clear-depth
value, were all read in full for this report — genuine cross-file verification of the fix's
end-to-end correctness, not assumed from the test's own passing expectation.

## Detailed Findings

### F1 — `ApplyBasicEffect()`'s function-local `static BasicEffect* fx` is heap-allocated once and never deleted

- Severity: LOW
- Confidence: HIGH (directly read; no destructor call, no smart pointer, no cleanup path anywhere
  in the file)
- Category: memory-lifetime
- Location/symbol: `ApplyBasicEffect()`, lines 77-86: `static BasicEffect* fx = nullptr; if (fx ==
  nullptr) fx = new BasicEffect(dev);`
- Evidence: this is a classic "leak on purpose because the process exits anyway" pattern — the
  `BasicEffect` is allocated exactly once (guarded by the `nullptr` check) across however many times
  `ApplyBasicEffect()` is called (twice, once per check, in this file's actual usage) and is never
  freed via `delete` or wrapped in a `std::unique_ptr`.
- Why it matters: in this specific file the practical impact is negligible — the process calls
  `Run()` for exactly 2 frames total and then exits, so the OS reclaims the leaked `BasicEffect`
  (and whatever GPU-side resources its constructor/`Apply()` may have allocated through the backend)
  at process teardown regardless. It is flagged at LOW severity specifically because it is a
  pattern that would be a genuine concern if copy-pasted into a longer-lived context (e.g. a real
  game loop calling an equivalent helper every frame across thousands of frames with a *different*
  `GraphicsDevice&` each time, which would each leak their own effect instance) — this file itself
  does not do that (the `dev` reference is stable across both calls), so no accumulation actually
  occurs here.
- FNA/XNA comparison: N/A — this is a CNA-side test-authoring pattern, not an XNA API behavior
  question; real XNA/FNA code would typically own a `BasicEffect` as a class member with normal
  destructor-based cleanup, which this test's own production-adjacent sibling files (e.g.
  `dualtextureeffect_vertexcolor_test.cpp`, `environmentmapeffect_alphascaledlerp_test.cpp` in this
  same batch) do correctly via stack-local, function-scoped effect construction instead of a leaked
  static.
- Related files: none outside this file — self-contained.
- Suggested future action (not implemented by this audit): replace the leaked raw `static
  BasicEffect*` with a `static std::unique_ptr<BasicEffect>` (still function-local `static`, but with
  automatic cleanup at static-destruction time), or simply construct a fresh
  stack-local `BasicEffect fx(dev);` inside `ApplyBasicEffect()` each call — the latter would match
  the pattern already used by this batch's other example files and would remove the "why is this
  static" question entirely, at the cost of re-doing the effect's parameter caching each of the 2
  calls (negligible for a 2-frame diagnostic test).

## Cross-File Observations

- Shares the identical `RasterizerState::CullNone`/"Task 896" CCW-winding-culling workaround comment
  with the other 3 files in this audit batch that draw explicit-winding geometry
  (`dualtextureeffect_vertexcolor_test.cpp`, `environmentmapeffect_alphascaledlerp_test.cpp`) — a
  consistent, correctly-applied idiom across this shard.
- This file's own header comment explicitly cross-references
  `dualtextureeffect_vertexcolor_test.cpp` ("mirroring Task 950's own... pattern" appears in that
  file; this file is the one Task 950 actually introduced) for the "one shared source, three
  backend registrations" structure — verified accurate in both directions.
- Unlike this file's `static BasicEffect*` leak (F1), the sibling files in this same batch
  (`dualtextureeffect_vertexcolor_test.cpp`, `environmentmapeffect_alphascaledlerp_test.cpp`) both
  construct their effect objects as ordinary stack locals inside their per-case render function —
  worth noting as a minor internal inconsistency in an otherwise very similar set of test files, not
  a functional defect.

## Missing or Weak Tests

- `ClearOptions::DepthBuffer` alone (`IGraphicsBackend::ClearDepth`) is explicitly and correctly
  disclosed as out of scope for this file (citing the pre-existing, unrelated Task 871
  "doesn't visibly apply until the next real clear" limitation) — not a gap in this file's own
  design, but noted here per `AUDIT_CHECKLIST.md`'s instruction to record intentional scope
  exclusions rather than silently pass over them.
- No test in this file (or, as far as this audit found, anywhere in this shard) exercises
  `ClearOptions::DepthBuffer | ClearOptions::Stencil` (i.e. `ClearDepthAndStencil`, without
  `Target`) or `ClearOptions::Target | ClearOptions::Stencil` (i.e. `ClearColorAndStencil`, without
  `DepthBuffer`) for the same depth-value-actually-takes-effect property this file checks for the
  two combinations it does cover — a plausible, if narrower, extension of this exact test's own
  technique that isn't currently exercised.

## Positive Findings

- The Task 950 fix was independently traced end-to-end on both Vulkan and Bgfx (store → actual
  consumption in the real clear-value construction) and confirmed genuinely correct, not merely
  superficially renamed.
- The two-different-clear-depth-values design (`0.3` vs. `0.7`) is a genuinely thoughtful robustness
  property that would catch a wider class of broken fixes than a single-value test would.
- Explicit, well-reasoned scope exclusion for `ClearDepth()` alone, correctly citing the relevant
  prior task rather than silently omitting that entry point.
- Correctly requires both checks to pass (`passCount_ == 2`), not treating either as optional.

## Final Assessment

A well-designed, thoroughly-verified regression test whose underlying fix this audit independently
confirmed is genuinely wired end-to-end on both backends it targets. The only defect found (F1, a
leaked static `BasicEffect*`) is real but practically inconsequential in this file's own short-lived,
two-frame execution context — flagged for hygiene/pattern-consistency reasons rather than any actual
observed-or-plausible runtime harm.
