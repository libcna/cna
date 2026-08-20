# Audit: examples/sdlrenderer_clearoptions_audit_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_clearoptions_audit_test.cpp` (132 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 716: `GraphicsDevice::Clear` all `ClearOptions`
  combinations on `SDL_Renderer`.
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_clearoptions_audit` /
  `SDL_Renderer_ClearOptions_Audit`, `cmake/Tests/SdlRendererTests.cmake` lines 325-327).
- XNA/FNA relevance: direct — `GraphicsDevice.Clear(ClearOptions, Color, float, int)`.
- FNA reference: `Graphics/GraphicsDevice.cs` lines 801-841 (`Clear(ClearOptions, Vector4, float, int)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`Clear(ClearOptions, ...)`,
  lines 284-365), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`ClearDepth`/`ClearColorAndDepth`/
  `ClearStencil`/etc., lines 785-793, all `ThrowNo3D`), `include/CNA/Internal/Backends/SdlRenderer/
  SdlGraphicsBackend.hpp` (`SupportsDepthStencil()`/`HasRealDepthBuffer()`, both hardcoded `false`).
- Cross-referenced planning doc (read as context per `AUDIT_SCOPE.md`'s explicit instruction to corroborate/refute
  tracked defects, not as an audited file itself): `plans/plan_graphics.md`, Task 1113 (open, ⬜) — already documents
  the exact defect this report independently re-derives below.

## Purpose

Systematically exercises `GraphicsDevice::Clear(ClearOptions, Color, float, int)` for the 7 non-empty
`ClearOptions` combinations (of the 8 total for a 3-bit flags enum: `Target=1`, `DepthBuffer=2`, `Stencil=4`) on
`SDL_Renderer`, checking which combinations throw (no depth/stencil buffer exists on this 2D-only backend) versus
which clear the color target correctly versus which are silent no-ops. `ClearThrows` (lines 50-61) wraps a single
`Clear()` call in a try/catch; `SampleCenter` (lines 63-70) reads back the viewport-center pixel via
`GetBackBufferData`.

## Executive Verdict

**Significant correctness risk** — not in the production code, but in this test file itself. This test's own
header comment and 4 of its 10 checks assert that `ClearOptions::DepthBuffer` alone,
`Target|DepthBuffer`, `DepthBuffer|Stencil`, and `Target|DepthBuffer|Stencil` all **throw** on `SDL_Renderer`.
This audit traced `GraphicsDevice::Clear(ClearOptions, ...)`'s current implementation and found this is **no
longer true**: a later commit (`90f5db2c`, "`Clear(const Color&) no longer crashes on SDL_RENDERER`", 2026-07-13
11:17, i.e. *after* this test file's own authoring on 2026-07-08 and *after* the Task 871 stencil fix on
2026-07-10) added masking logic that strips `DepthBuffer`/`Stencil` out of `options` *before* the backend is ever
consulted, whenever the active target has no real depth/stencil buffer — which is unconditionally true for
`SDL_Renderer` (`SupportsDepthStencil()` hardcoded `false`). As a direct consequence, none of these 4 combinations
reach `SdlGraphicsBackend::ClearDepth`/`ClearColorAndDepth`/etc. (whose `ThrowNo3D` throws are the entire basis
for this test's 4 "must throw" assertions) — they are masked down to a `Target`-only or empty request beforehand,
so **no throw occurs**, and these 4 checks would fail if the test were actually run today. This is independently,
concretely corroborated by this project's own `plans/plan_graphics.md` (Task 1113, still open/⬜ as of this audit),
which documents this exact test failing for this exact reason. **Separately and more importantly**, this audit
traced FNA's own `GraphicsDevice.Clear(ClearOptions, ...)` (`GraphicsDevice.cs` lines 811-841) and found FNA
performs the **identical** masking (`if (dsFormat == DepthFormat.None) { options &= ClearOptions.Target; }`) —
meaning the July 13 CNA fix that broke this test's assumptions is actually the **XNA/FNA-correct** behavior, and
this test's own "must throw" expectation was never real-XNA-compliant to begin with, even at the moment it was
authored.

## Checklist Results

### API / XNA / FNA parity

FNA's `GraphicsDevice.Clear(ClearOptions, Vector4, float, int)` (`GraphicsDevice.cs` lines 811-841) determines the
active depth-stencil format (`dsFormat`, from the bound render target or the backbuffer) and, critically:
```
if (dsFormat == DepthFormat.None) { options &= ClearOptions.Target; }
else if (dsFormat != DepthFormat.Depth24Stencil8) { options &= ~ClearOptions.Stencil; }
```
— i.e., FNA **never throws** for a `ClearOptions` combination the active target cannot honor; it silently masks
the request down to what the hardware/backend can actually do. `SDL_Renderer` never has a real depth-stencil
buffer (`SupportsDepthStencil()` returns `false` unconditionally, `SdlGraphicsBackend.hpp` line 145), so
`dsFormat == DepthFormat.None` is always the operative branch for this backend — real XNA/FNA behavior for
`dev.Clear(ClearOptions::DepthBuffer, ...)` on a backend with no depth buffer is therefore a **silent, graceful
no-op** (masked to empty `Target`-less options), not an exception. CNA's current `GraphicsDevice::Clear`
(`GraphicsDevice.cpp` lines 291-323) independently reimplements this exact masking logic (`hasRealDepthBuffer`
check + `options &= ClearOptions::Target`), confirmed to match FNA's algorithm precisely (own comment at lines
299-307 explicitly cites "Matches FNA's own GraphicsDevice.Clear(ClearOptions, ...)"). This test's own 4
"must-throw" assertions therefore assert **non-XNA-compliant** behavior, not merely stale-relative-to-a-refactor
behavior.

### Behavioral correctness

Traced each of this test's 7 non-empty-combination checks against the current `GraphicsDevice::Clear`
implementation:

- **`Target` alone** (lines 86-90): `hasClearFlag(options, DepthBuffer)` is false so the depth-range check is
  skipped; masking is a no-op since `Target` bit alone survives `options &= ClearOptions::Target` regardless;
  `clearTarget=true, clearDepth=false, clearStencil=false` → `backend_->Clear(r,g,b,a)` — a real color clear, no
  throw. **This check passes** as written.
- **`DepthBuffer` alone** (line 93): masking reduces `options` to `0` (only `Target` bit survives the mask, and
  `DepthBuffer` isn't it) → `clearTarget=clearDepth=clearStencil=false` → **no backend call at all, no throw** —
  contradicts this test's own `check(ClearThrows(dev, ClearOptions::DepthBuffer), ...)` expectation (line 93,
  expects `true`/throw). **This check would FAIL today.**
- **`Target|DepthBuffer`** (line 94): masking reduces `options` to `Target` only → `backend_->Clear(...)` (a
  normal color clear), **no throw** — contradicts the test's expectation. **This check would FAIL today.**
- **`DepthBuffer|Stencil`** (line 95): masking reduces `options` to `0` → no-op, **no throw** — contradicts.
  **This check would FAIL today.**
- **`Target|DepthBuffer|Stencil`** (line 96): masking reduces `options` to `Target` only → normal color clear,
  **no throw** — contradicts. **This check would FAIL today.**
- **`Stencil` alone** (lines 101-104): masking reduces `options` to `0` → no branch taken → genuine no-op,
  **matches** this test's own expectation (`!ClearThrows`, a no-op) — but for a **different underlying reason**
  than the test's own header comment states. The comment (lines 6-14) attributes this to "Task 871's
  already-tracked, cross-backend Stencil gap" (i.e., the `stencil` parameter being ignored inside the dispatch
  logic) — but Task 871 (commit `006f483a`, 2026-07-10) already **fixed** that gap (`hasClearFlag(options,
  ClearOptions::Stencil)` is genuinely checked at `GraphicsDevice.cpp` line 335). The real reason `Stencil` alone
  is still a no-op today is the *later* (July 13) masking logic stripping the `Stencil` bit before dispatch ever
  sees it — this test's own explanation for why this specific check passes is now stale, even though the check's
  pass/fail outcome itself is coincidentally still correct.
- **`Target|Stencil`** (lines 107-110): masking is a no-op here (only `Stencil`, not `DepthBuffer`, would be
  stripped by a hypothetical partial mask, but the actual mask is all-or-nothing down to `Target` — since
  `hasRealDepthBuffer` is false, `options &= ClearOptions::Target` strips `Stencil` too, leaving `Target` alone)
  → `backend_->Clear(...)` — the color target is cleared, matching this test's own expectation, though again for
  a reason (blanket masking, not selective per-Stencil dispatch) that the file's header comment does not
  anticipate.

**Net: 4 of this test's 10 `check()` calls (the `ClearOptions::DepthBuffer`-involving ones) assert behavior the
current production code does not exhibit; this test would very likely report `FAIL` on at least 4 lines and exit
with code 1 if actually run against the current codebase.** This was not independently re-run in this pass (no
build directory existed in this sandbox and a full CNA rebuild was judged too costly for one file in an 8-file
batch), but the conclusion rests on two independent, mutually-reinforcing lines of evidence: (1) direct source
tracing of `GraphicsDevice::Clear`'s current masking logic against this test's literal assertions, both quoted
above; and (2) this project's own `plans/plan_graphics.md` Task 1113 entry, which states verbatim that it found
`SDL_Renderer_ClearOptions_Audit` failing for exactly this reason via a live `ctest` run, confirmed via `git
stash` to reproduce against the unmodified baseline (not a flake).

### Logic

The test's own git-log commit message (`6f7e4cf0`, "verify(Task 716)") states "Discriminating power verified:
sabotaging the clearTarget flag computation to always report true... All 10 checks pass" and "Full regression...
11 failed -- exactly the true baseline" — this was accurate **at the time of that commit** (2026-07-08, before
the July 13 masking fix existed). The discriminating-power self-test performed then is not wrong per se; it is
simply now testing a code path whose control flow has since changed underneath it.

### Robustness

`PresentationMode::NativeBackBuffer` correctly set (line 121), consistent with this batch's Task 915 rationale —
unaffected by the above finding.

### Testing

This file **is** a test, and per the above analysis is currently a **broken/stale test**: several of its
assertions test for behavior that both (a) the current implementation does not produce, and (b) real XNA/FNA
itself does not produce either (FNA masks to a graceful no-op, matching CNA's current, not CNA's 2026-07-08,
behavior). The correct remediation (not performed by this audit-only pass) is to update this test's own
expectations to match real XNA/FNA masking semantics (no throw; `DepthBuffer`/`Stencil`-only combinations quietly
degrade to whatever the `Target` bit alone would do), not to reinstate throwing in `GraphicsDevice::Clear` — doing
the latter would be a regression *away* from FNA parity.

## Detailed Findings

### F1 — 4 of this test's 10 assertions expect `GraphicsDevice::Clear` to throw for `ClearOptions` combinations
that a later, FNA-parity-motivated fix (commit `90f5db2c`) made into silent no-ops/color-only clears; the test
would fail today, and its own expectation was never real-XNA-compliant to begin with

- Severity: HIGH
- Confidence: HIGH (independently traced through current `GraphicsDevice.cpp` source, cross-checked against
  FNA's own `GraphicsDevice.cs` masking algorithm, and corroborated by this project's own tracked, still-open
  `plans/plan_graphics.md` Task 1113 entry describing a live `ctest` failure for this exact test)
- Category: correctness-of-test / stale-assumption / FNA-parity
- Location/symbol: `check(ClearThrows(dev, ClearOptions::DepthBuffer), ...)` (line 93),
  `check(ClearThrows(dev, ClearOptions::Target | ClearOptions::DepthBuffer), ...)` (line 94),
  `check(ClearThrows(dev, ClearOptions::DepthBuffer | ClearOptions::Stencil), ...)` (line 95),
  `check(ClearThrows(dev, ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil), ...)`
  (line 96); header comment lines 6-14.
- Evidence: `GraphicsDevice::Clear(ClearOptions, const Color&, float, int)` (`GraphicsDevice.cpp` lines 284-365)
  computes `hasRealDepthBuffer` (false for `SDL_Renderer`, since `SdlGraphicsBackend::SupportsDepthStencil()`
  returns `false` unconditionally) and, when false, executes `options &= ClearOptions::Target;` (line 322) —
  *before* any of `clearTarget`/`clearDepth`/`clearStencil` are computed (lines 330-335) or any backend method is
  called (lines 337-364). This strips `DepthBuffer`/`Stencil` out of every one of the 4 combinations this test
  expects to throw, so none of them ever reach `SdlGraphicsBackend::ClearDepth`/`ClearColorAndDepth`/
  `ClearDepthAndStencil`/`ClearColorDepthAndStencil` (whose unconditional `ThrowNo3D` calls are this test's entire
  basis for expecting a throw). FNA's own `GraphicsDevice.Clear` (`GraphicsDevice.cs` lines 826-829) performs the
  textually near-identical masking (`if (dsFormat == DepthFormat.None) { options &= ClearOptions.Target; }`),
  confirming CNA's July 13 fix is the *correct* XNA-parity behavior. `plans/plan_graphics.md`'s own Task 1113 entry
  (still `⬜` open) independently confirms via a real `ctest` run that `SDL_Renderer_ClearOptions_Audit` fails
  today for exactly this reason, and that the failure reproduces against a `git stash`-restored baseline (i.e.,
  not a flake or an artifact of unrelated local changes).
- Why it matters: a CTest-registered regression test that reports `FAIL`/exit-code-1 whenever run is either
  silently ignored (defeating its purpose as a regression guard) or actively misleads anyone who runs it into
  thinking `GraphicsDevice::Clear`'s current, FNA-correct masking behavior is a regression to be "fixed" back
  toward throwing — which would be a real, backwards step away from XNA/FNA parity. The test's own header
  comment's account of *why* the `Stencil`-alone and `Target|Stencil` checks pass ("Task 871's already-tracked,
  cross-backend Stencil gap") is also now inaccurate, since Task 871 already fixed the stencil-dispatch gap it
  describes; those two checks now pass for an entirely different reason (blanket depth/stencil masking added
  later), which happens to produce the same observable outcome but is a materially different code path than the
  comment describes.
- FNA/XNA comparison: FNA's `GraphicsDevice.Clear(ClearOptions, ...)` (`GraphicsDevice.cs` lines 811-841)
  confirmed to perform equivalent format-based masking, never throwing for an unsupported combination — the
  authoritative answer to the open question `plans/plan_graphics.md` Task 1113 itself poses ("is
  `SDL_Renderer_ClearOptions_Audit`'s own expectation simply wrong... or does `GraphicsDevice::Clear()`'s masking
  itself need to [change]?") is: **the test's expectation is the one that needs to change; the masking is
  correct.**
- Related files: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (masking logic, lines 291-323),
  `include/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp` (`SupportsDepthStencil()`, line 145),
  `plans/plan_graphics.md` (Task 1113, Task 871).
- Suggested future action (not implemented by this audit-only pass): update this test's 4 `DepthBuffer`-involving
  checks to assert the FNA-correct outcome (no throw; the color target still clears when `Target` is also
  requested, otherwise a genuine no-op) instead of a throw, and correct the header comment's now-inaccurate
  account of why the `Stencil`/`Target|Stencil` checks pass. Close `plans/plan_graphics.md` Task 1113 accordingly once
  done.

## Cross-File Observations

- `plans/plan_graphics.md` (a root-level, `EXEMPT` planning-tracking doc per `AUDIT_SCOPE.md`, not itself
  audit-reported) already contains an accurate, detailed account of this exact defect (Task 1113) — this audit's
  independent source-level re-derivation corroborates that entry rather than discovering something new, and
  additionally resolves the open question that entry itself poses by checking the FNA reference directly (which
  Task 1113's own text does not appear to have done).
- This is the same class of issue the audit brief specifically asked to watch for ("actively check header
  comments' claims... against the ACTUAL current production code and git log") — the header comment's Task 871
  citation is technically accurate as a historical fact (Task 871 was real and is correctly described) but is
  presented as still explaining *current* behavior for the `Stencil`-alone check, which it no longer fully does
  once the July 13 masking fix is accounted for.
- Cross-reference for a future fix: `dx9-spike`/`plans/plan_graphics.md`'s own note that `DX3`'s
  `examples/dx3_no3d_test.cpp` (Check D) already designs around this exact masking behavior correctly, by testing
  the backend's `ClearColorAndDepth`/etc. methods directly rather than through the public `Clear()` API — a
  workable template for how this file's own checks could be restructured if direct-backend-method testing is
  preferred over updating the public-API-level expectations.
- The same stale claim has already propagated into `docs/sdl-renderer-2d-completeness.md` §8 ("`Clear` — all 8
  `ClearOptions` combinations" row: "any combination including `DepthBuffer` throws (no depth buffer exists at
  all — expected)") — that file is a separate `AUDIT`-eligible `docs/**/*.md` file outside this batch's scope, but
  is flagged here since its own accuracy on this exact point depends on the same now-superseded assumption this
  report identifies, and it should be corrected in step with whatever fix this test receives.

## Missing or Weak Tests

Given F1, the more accurate framing is not "missing" tests but "the existing tests assert the wrong outcome" for
4 of 10 checks — see Suggested future action above.

## Positive Findings

- The 3 checks unaffected by the masking-logic change (`Target` alone, `Stencil` alone, `Target|Stencil`) remain
  behaviorally accurate for the current codebase, even though the `Stencil`-alone case's own comment now
  describes the wrong mechanism.
- The test's underlying intent — systematically exercising every `ClearOptions` combination on a backend with no
  real depth/stencil buffer — is a sound, valuable testing strategy; the defect found here is a staleness problem
  from an intervening fix, not a flaw in the test's original design or reasoning at the time it was written.

## Final Assessment

This test file's central claim (4 specific `ClearOptions` combinations throw on `SDL_Renderer`) is stale: a later,
independently-motivated fix for a real reported crash (`90f5db2c`) changed `GraphicsDevice::Clear`'s masking
behavior to match FNA's own graceful-degradation semantics, which this test predates and does not reflect. This
project's own `plans/plan_graphics.md` already tracks the resulting test failure as an open item (Task 1113); this audit
additionally resolves that item's own open question by confirming against the FNA source that the current,
non-throwing masking behavior is the XNA-correct one, and the test itself is what needs updating.
