# Audit: examples/easygl_render_target_usage_test.cpp

## Metadata

- Source file: `examples/easygl_render_target_usage_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test — `examples-tests-easygl` shard
- File type: C++ example/integration-test executable (Task 177)
- Related production code: `GraphicsDevice::SetRenderTarget(RenderTarget2D*)`
  (`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:1821-1859`), `RenderTargetUsage`
  (`include/Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp`), `RenderTarget2D` constructor
  (`include/Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp:43-50`)
- FNA reference: `GraphicsDevice.SetRenderTargets` clear-on-bind logic, `FNA-XNA/FNA/src/Graphics/GraphicsDevice.cs:1054-1062`
  and its `DiscardColor` constant (lines 300-314)
- XNA/FNA relevance: `RenderTargetUsage.DiscardContents`/`PreserveContents` and their "clear on (re-)bind" semantics
  are real, XNA-documented behavior, not a CNA invention.

## Purpose

Verifies that `RenderTargetUsage` actually controls whether a render target's prior contents survive being unbound
and re-bound: `DiscardContents` should have its contents cleared to black on every bind (including re-binds);
`PreserveContents` should never be auto-cleared. Reads back pixels directly from the render target's own FBO while
it is still bound (via `GetBackBufferData`, which the file's own comment claims reads whatever framebuffer is
currently attached) rather than round-tripping through `SpriteBatch`.

## Executive Verdict

**Healthy** — every claim in this file, including the "reads directly from the RT attachment" comment and the
implicit assumption that `DiscardContents` clears *on every bind, not just re-binds*, was independently traced
against both the CNA production code and the FNA reference source and found accurate. The test also (correctly,
if perhaps unintentionally) exercises a case its own comments don't explicitly call out: the *first* bind of the
`DiscardContents` target is also auto-cleared, which the manual `Clear(Color::Red)` immediately overwrites — this
doesn't affect the test's correctness, but is worth noting for anyone reasoning about the exact clear-count.

## Checklist Results

### API / XNA / FNA parity
`RenderTargetUsage` (`DiscardContents`, `PreserveContents`, `PlatformContents`) matches FNA's enum
(`FNA-XNA/FNA/src/Graphics/RenderTargetUsage.cs`) exactly, including the third `PlatformContents` value this test
does not exercise. The 8-arg `RenderTarget2D` constructor call — `RenderTarget2D(dev, kSize, kSize, false,
SurfaceFormat::Color, DepthFormat::None, 0, RenderTargetUsage::DiscardContents)` — matches the declared parameter
order exactly (`device, width, height, mipMap, preferredFormat, preferredDepthFormat, preferredMultiSampleCount,
usage`, `RenderTarget2D.hpp:43-50`). PASS.

### Behavioral correctness
Traced `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (`GraphicsDevice.cpp:1821-1859`) against FNA's
`SetRenderTargets` (`GraphicsDevice.cs:960-1062`) and found the discard-on-bind semantics equivalent:
- CNA: `if (renderTarget && renderTarget->getRenderTargetUsageProperty() == RenderTargetUsage::DiscardContents) {
  ... Clear(hasDepthBuffer ? (Target|DepthBuffer) : Target, Color(0,0,0,255), 1.0f, 0); }` — runs on **every** call
  to `SetRenderTarget` with a `DiscardContents`-usage target, not conditionally on whether it's a first bind or a
  re-bind.
- FNA: `if (clearTarget == RenderTargetUsage.DiscardContents) { Clear(Target|DepthBuffer|Stencil, DiscardColor,
  Viewport.MaxDepth, 0); }`, with `DiscardColor = Vector4(0,0,0,1)` (Release-mode value,
  `GraphicsDevice.cs:313`, matching CNA's `Color(0,0,0,255)` exactly) — also runs unconditionally whenever a
  `DiscardContents`-usage target is (re-)bound, matching CNA's behavior.
This confirms the test's core assumption — that **re-binding** a `DiscardContents` target clears it — is not a
CNA-only convention but a faithful port of real FNA/XNA behavior. It also means the test's **first**
`dev.SetRenderTarget(&rt)` call for Case A already auto-clears the RT to black before the test's own
`dev.Clear(Color::Red)` overwrites it with red — harmless to the test's outcome (the manual red-clear is the last
write before unbind), but means the "Fill RT with red" comment slightly understates that an implicit black-clear
happens first. PASS, with a documentation-precision note (see F1).

### Logic
Case B (`PreserveContents`) correctly relies on the *absence* of the `usage == DiscardContents` branch — traced and
confirmed `GraphicsDevice::SetRenderTarget` only special-cases `DiscardContents`; both `PreserveContents` and
`PlatformContents` fall through with no auto-clear, so the green fill genuinely survives the unbind/re-bind cycle
because nothing touches the RT's storage in between (`SetRenderTarget(nullptr)` only rebinds the backbuffer and
resets viewport/scissor — it does not resolve, clear, or otherwise mutate the previously-bound RT).

### Robustness
`colourMatch()` deliberately ignores alpha (only compares R/G/B), consistent with this project's stated convention
(also seen in `PixelTestGame::ExpectPixel`) of not asserting on alpha unless a test specifically cares about it.

### Testing
Directly and specifically tests the one axis (`RenderTargetUsage`) that `easygl_render_target_test.cpp` (same
shard) does not exercise — see that file's audit report for the complementary "RT usable as a `SpriteBatch` source
texture" case.

## Detailed Findings

No CRITICAL/HIGH findings. One LOW/documentation-precision finding:

### F1 — Case A's comment doesn't mention that the render target is auto-cleared on its *first* bind too, before the manual red fill

- Severity: LOW
- Confidence: HIGH (directly traced `GraphicsDevice::SetRenderTarget`'s unconditional discard-clear branch)
- Category: maintainability / comment accuracy
- Location/symbol: `RenderTargetUsageTest::Initialize()`, Case A block (lines 71-92), comment "Fill RT with red,
  unbind, re-bind." (lines 68-69)
- Evidence: `dev.SetRenderTarget(&rt)` at line 75 is the RT's *first* bind, and since `rt`'s usage is
  `DiscardContents`, this call already triggers `GraphicsDevice::SetRenderTarget`'s auto-clear-to-black branch
  (`GraphicsDevice.cpp:1843-1857`) before the test's own `dev.Clear(Color::Red)` on the very next line overwrites it.
  The comment ("Fill RT with red...") reads as if the RT goes straight from an untouched/undefined state to red,
  when in fact it passes through an intermediate implicit black clear first.
- Why it matters: purely cosmetic — the final state before unbind is still red either way, so the test's own
  correctness is unaffected — but a reader relying on the comment alone could mistakenly believe
  `RenderTarget2D`'s initial (pre-first-bind) contents are being tested here, when they are not (they're
  immediately overwritten by the implicit discard-clear, then again by the explicit red clear).
- Suggested action (not implemented by this audit): note in the comment that the first bind's implicit
  discard-clear is also exercised (harmlessly) here, or restructure to bind once, clear red, unbind, then re-bind
  as the code already does — no functional change needed, only clarify the sequence for future readers.

## Cross-File Observations

- FNA's `DiscardColor` is `(68,34,136,255)` (a distinctive purple) in `DEBUG` builds specifically so developers can
  visually spot "this pixel was never actually rendered, only discard-cleared" bugs, falling back to
  `(0,0,0,255)` in Release builds for a fast hardware clear path (`GraphicsDevice.cs:300-314`, Intel/Mesa
  clear-color optimization comment). CNA's `Color(0,0,0,255)` matches only the Release-mode value — there is no
  CNA equivalent of FNA's debug-purple discard color. This test's Case A assertion
  (`px.getRProperty() == 0 && px.getGProperty() == 0 && px.getBProperty() == 0`) is therefore tied to CNA's
  single, Release-only discard-color choice; if CNA ever added a debug-mode purple discard color to match FNA's
  developer-diagnostic intent, this exact-black assertion would need to change accordingly. Not a defect today —
  recorded as a forward-compatibility note.
- Complements `easygl_render_target_test.cpp` in the same shard (see that file's own audit report) — together the
  two files cover render-target-as-texture-source correctness and render-target-content-persistence semantics as
  two genuinely separate concerns.

## Missing or Weak Tests

- `RenderTargetUsage::PlatformContents` (the third enum value — "may preserve if the platform can do so without
  penalty") is not tested at all in this file or (as far as this shard's file list shows) anywhere in the EasyGL
  shard; CNA's `GraphicsDevice::SetRenderTarget` treats it identically to `PreserveContents` (falls through the
  same `if (usage == DiscardContents)` check, i.e. never auto-clears) — a reasonable, FNA-consistent interpretation,
  but currently unverified by any test.
- No test of `DiscardContents` behavior when the render target *does* have a real depth buffer (this test uses
  `DepthFormat::None` for both cases) — the `hasDepthBuffer`-conditioned `ClearOptions::DepthBuffer` branch
  (`GraphicsDevice.cpp:1851-1857`) is untested here.

## Positive Findings

- The file's own claim about `GetBackBufferData` reading "directly from the RT attachment" while it remains bound
  was independently verified against `EasyGLGraphicsBackend::ReadBackbuffer`'s `currentRtHeight_`-based
  framebuffer-agnostic read path — an accurate, non-hand-wavy comment.
- Correctly chose the two `RenderTargetUsage` values whose behavior genuinely differs (`DiscardContents` vs.
  `PreserveContents`) and designed a fill-unbind-rebind-read cycle that would fail for either value if the
  discard/preserve logic were swapped or removed — a real contrast test, not a same-outcome-every-check design.
- Cross-referencing FNA's actual `SetRenderTargets` implementation confirmed this isn't a CNA-invented behavior
  being tested for its own sake — it's a faithful, verified port of real XNA/FNA semantics (including the exact
  discard-color constant).

## Final Assessment

A correctly-designed, evidence-verified test of `RenderTargetUsage` discard/preserve semantics, cross-checked
directly against the FNA reference implementation (including the specific `DiscardColor` constant and the
unconditional-clear-on-every-bind behavior). The one finding (F1) is a minor comment-precision issue with no effect
on the test's actual correctness.
