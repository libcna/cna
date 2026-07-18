# Audit: examples/bgfx_occlusionquery_dispose_active_test.cpp

## Metadata

- Source file: `examples/bgfx_occlusionquery_dispose_active_test.cpp` (116 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `OcclusionQuery.Dispose()` while active (`Begin()`
  called, no matching `End()`) safety test (Task 816)
- File type: standalone `Game`-subclass executable, CTest-registered.
- XNA/FNA relevance: direct — `OcclusionQuery.Dispose()`/`IsDisposed`, `IDisposable` contract.
- FNA reference: `src/Graphics/OcclusionQuery.cs` (`protected override void Dispose(bool
  disposing)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/OcclusionQuery.cpp`,
  `include/Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp`,
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`BgfxOcclusionQueryBackend::~BgfxOcclusionQueryBackend()`, lines 367-374).

## Purpose

Verifies that explicitly calling the XNA `.Dispose()` method on an **active** `OcclusionQuery`
(after `Begin()`, before any matching `End()`) is safe on Bgfx: no crash, and no corrupted shared
backend state (`BgfxGraphicsBackend::activeOcclusionQuery_`) that would affect a subsequently
created, unrelated query. Repeats the active-dispose cycle 10× (to catch handle-leak/corruption
that might only manifest after several cycles), then constructs one fresh `OcclusionQuery` and
confirms it still works normally.

## Executive Verdict

**Needs attention** — the underlying safety property the test asserts (no crash, no corrupted
shared state) is genuinely and correctly verified. However, the file's own header comment
asserts this design is "an already-established, deliberate, **FNA-matching** design" — this audit
independently read FNA's actual `OcclusionQuery.cs` and found this claim is **factually
incorrect**: real FNA's `Dispose(bool disposing)` **is** overridden and **does** immediately
release (queue for release) the native query handle, unlike CNA's `OcclusionQuery`, which has no
`Dispose(bool)` override at all and leaves the real backend fully alive and functional until C++
object destruction (see F1). Separately, the test's own acceptance criterion ("safe OR throws
correctly," deliberately not choosing between the two) means it does not hold the implementation
accountable to this project's own stated `IDisposable` convention of checking `isDisposed_` and
throwing after disposal (see F2).

## Checklist Results

### API / XNA / FNA parity
`query->Begin()`, `query->Dispose()`, `query->getIsDisposedProperty()`, `query->End()` (lines
68-77) match FNA's `OcclusionQuery` surface in name/shape. The **parity claim about the
Dispose-while-active design**, however, does not hold — see F1.

### Behavioral correctness
Confirmed `BgfxOcclusionQueryBackend`'s destructor (`BgfxGraphicsBackend.cpp` lines 367-374)
correctly clears `owner_->activeOcclusionQuery_` if it still points at this handle at destruction
time, preventing a dangling handle reference in the owning `BgfxGraphicsBackend` after the C++
object is actually destroyed — this is the real mechanism protecting the "fresh query afterward
still works" assertion (lines 83-86), and it was independently confirmed to do so correctly.
`OcclusionQuery.hpp` has **no** override of `Dispose(bool)` at all (confirmed via grep — no
`Dispose` symbol present in the header), so `query->Dispose()` (line 70) only flips
`isDisposed_` via the inherited `GraphicsResource`/base `IDisposable` path, leaving `backend_`
(and its live `bgfx::OcclusionQueryHandle`) fully alive and functional — exactly as the file's
comment describes for **CNA's own** behavior. That description of CNA's actual behavior is
accurate; only the claim that it *matches FNA* is not (F1).

### Logic
The 10× repetition loop (lines 66-78) is a sound technique for catching cumulative
handle-leak/corruption that a single active-dispose cycle might not expose — consistent with this
project's established "Task 449" convention (per the file's own citation) for "no natural
incorrect-vs-correct branch" safety confirmations.

### C++ correctness
Each loop iteration constructs a fresh `std::make_unique<OcclusionQuery>(dev)` (line 68) — no
reuse of a disposed object's memory, no double-free risk; `query->End()`/`getIsCompleteProperty()`/
`getPixelCountProperty()` calls after `Dispose()` (lines 75-77) are each individually
`try{}catch(...){}`-wrapped, so a throw from any one of them (if the implementation ever changed
to throw-after-dispose) would not abort the loop or be mischaracterized as an unhandled crash.

### Robustness
`check(!threw, ...)` (line 93) asserts the **whole** 10-cycle-plus-fresh-query sequence didn't
throw an *unexpected* exception (note: `threw` is only set by the outer `catch(...)` at lines
88-91, which wraps the *entire* `for` loop and the fresh-query check — the individual
inner `try{}catch(...){}` blocks around `End()`/`getIsCompleteProperty()`/`getPixelCountProperty()`
already swallow any exception those specific calls might throw, so `threw` can only become `true`
from `Begin()`, `Dispose()`, `getIsDisposedProperty()`, `make_unique<OcclusionQuery>`, or the
fresh-query block itself throwing unexpectedly) — a reasonable design, though it means a future
regression that made `End()` throw after `Dispose()` would be silently swallowed by the inner
`catch(...)` and never surface as a test failure at all (see F2 for why this matters more than it
might first appear).

### Testing
Three checks: no-crash (10× cycle), `IsDisposed` becomes true, fresh query afterward still works.
Reasonable coverage of the *safety* property; see F2 for the coverage this design choice
foregoes.

## Detailed Findings

### F1 — Header comment's claim that Dispose()-while-active being "safe" (not releasing the backend) is "FNA-matching" is factually incorrect

- Severity: MEDIUM
- Confidence: HIGH (read FNA's actual `OcclusionQuery.cs` source directly)
- Category: stale-or-incorrect-comment / FNA-parity-mischaracterization
- Location/symbol: file header comment, lines 8-21, specifically: *"an already-established,
  deliberate, FNA-matching design (GraphicsResource.Dispose(bool) isn't overridden by
  OcclusionQuery)"*
- Evidence: `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/OcclusionQuery.cs` contains:
  ```csharp
  protected override void Dispose(bool disposing)
  {
      if (!IsDisposed)
      {
          IntPtr toDispose = Interlocked.Exchange(ref query, IntPtr.Zero);
          if (toDispose != IntPtr.Zero)
          {
              FNA3D.FNA3D_AddDisposeQuery(GraphicsDevice.GLDevice, toDispose);
          }
      }
      base.Dispose(disposing);
  }
  ```
  This **is** an override of `Dispose(bool disposing)`, and it **does** immediately zero the
  native `query` handle and queue it for release via `FNA3D_AddDisposeQuery` — the opposite of
  "isn't overridden." Real FNA's `Begin()`/`End()` after `Dispose()` would operate on a zeroed
  `IntPtr.Zero` handle passed into `FNA3D_QueryBegin`/`FNA3D_QueryEnd`, which is a materially
  different (and likely much less safe) situation than CNA's actual behavior of leaving the real
  `bgfx::OcclusionQueryHandle` fully valid and functional post-`Dispose()`.
- Why it matters: a maintainer reading this comment would reasonably conclude CNA's
  leave-the-backend-alive-until-C++-destruction design was deliberately chosen *to match FNA*,
  when in fact it is a **deviation** from FNA's real, more eagerly-releasing behavior. This
  matters specifically because CLAUDE.md's own IDisposable section documents this project's
  actual intended convention ("Always check `isDisposed_` before acting; throw
  `std::runtime_error` if used after disposal") — which is closer in spirit to FNA's eager release
  than to CNA's current `OcclusionQuery` behavior. The comment's false parity claim makes an
  actual, project-convention-relevant design gap look like a already-settled, FNA-sanctioned
  non-issue.
- FNA/XNA comparison: FNA's `OcclusionQuery.Dispose(bool)` eagerly releases/queues the native
  handle; CNA's `OcclusionQuery` (no `Dispose(bool)` override) does not release anything at
  `Dispose()` time at all, only at C++ destruction — a genuine, unremarked divergence.
- Related files: `include/Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/OcclusionQuery.cpp` (both outside this batch's direct
  scope, but load-bearing for this finding).
- Suggested future action (not implemented by this audit): correct the comment to state plainly
  that this is a **known CNA simplification/divergence from FNA**, not an FNA-matching design —
  and separately evaluate (as a production-code task, not a test-file fix) whether
  `OcclusionQuery` should gain a `Dispose(bool)` override per CLAUDE.md's own IDisposable
  convention.

### F2 — The test's "safe OR throws" acceptance criterion means it cannot detect a regression to either extreme, and does not enforce this project's own documented IDisposable convention

- Severity: LOW
- Confidence: HIGH (read the test's own assertions directly; only 3 checks exist, none pin down
  which of the two behaviors is required)
- Category: test-coverage / weak-assertion
- Location/symbol: `check(!threw, "Dispose()-ing an active OcclusionQuery (repeated 10x) does not
  crash/throw unexpectedly")` (line 93); inner `try { query->End(); } catch (...) {}` etc. (lines
  75-77)
- Evidence: the file's own header (lines 14-21) explicitly frames both "stays safe" and "throws
  correctly" as acceptable outcomes, and the inner per-call `try/catch(...)` swallows any
  exception those specific post-dispose calls might raise, rather than recording which branch
  actually happened. CLAUDE.md's IDisposable section states the project convention is to "throw
  `std::runtime_error` if used after disposal" — but this test would pass identically whether
  `OcclusionQuery` is fixed to follow that convention, left as-is (silently proceeding against a
  live backend), or regressed to some third state (e.g. throwing `std::bad_alloc` from an
  unrelated internal error) as long as nothing escapes the outer `catch(...)`.
- Why it matters: this test cannot regress-detect a change in *which* of the two accepted
  behaviors is active, nor can it catch the specific project-convention violation CLAUDE.md
  documents (missing `isDisposed_` guard). A future contributor "fixing" `OcclusionQuery` to
  match CLAUDE.md's stated convention (throw after disposal) would see this exact test continue
  to pass with zero indication that behavior changed — which is defensible for a "just don't
  crash" smoke test, but is a real, identifiable weak spot, not a false all-clear invented by this
  audit.
- FNA/XNA comparison: N/A — this is a test-design/project-convention-enforcement gap, not an
  XNA/FNA behavior mismatch (FNA's `Begin()`/`End()` also don't guard against post-dispose use in
  its own source).
- Related files: none beyond `OcclusionQuery.hpp`/`.cpp` (production code, out of this batch's
  scope).
- Suggested future action (not implemented by this audit): once a definitive decision is made
  about whether `OcclusionQuery` should throw after `Dispose()` (per CLAUDE.md's convention) or
  remain permissive, tighten this test's three checks to assert that specific, chosen behavior
  rather than accepting either.

## Cross-File Observations

- The `BgfxOcclusionQueryBackend` destructor's `activeOcclusionQuery_`-clearing guard (verified
  correct in the Checklist section above) is the actual mechanism keeping this test's "fresh query
  still works" assertion true — worth noting for anyone reasoning about whether this test would
  catch a regression to that specific destructor logic: it would (removing that guard would leave
  a dangling handle comparison the next time a *real* query with a coincidentally-reused numeric
  `idx` became active, though this specific test's 10x-then-fresh-query sequence may not
  deterministically trigger index reuse in every bgfx build/version).
- This file and `bgfx_occlusion_query_test.cpp` (same batch) share the project's Task 449
  repetition-based "no natural incorrect-vs-correct branch" testing convention — consistent
  application, not a one-off.

## Missing or Weak Tests

See F2 — the test does not distinguish which of "silently proceeds" vs. "throws" is the current
or intended behavior, so a future change to either extreme would pass unnoticed by this file.

## Positive Findings

- The repeated 10× active-dispose cycle plus a final fresh, unrelated query is a genuinely
  reasonable technique for catching handle-leak/shared-state corruption that a single-shot test
  might miss, and this audit independently confirmed the actual destructor-side guard
  (`BgfxOcclusionQueryBackend`'s `activeOcclusionQuery_` clearing) that makes the "fresh query
  still works" assertion meaningful rather than vacuous.
- Test correctly distinguishes "Dispose() flips IsDisposed" (a state assertion) from "backend
  stays functional" (a behavior assertion) as two separate, individually-labeled checks.

## Final Assessment

The safety property this test actually verifies is real and correctly checked. However, its own
header comment's central justification — that leaving the backend alive after `Dispose()` is an
"FNA-matching" design — is independently confirmed false against the real FNA source (F1,
MEDIUM), and the test's permissive "either behavior is fine" acceptance criterion (F2, LOW) means
it does not hold the implementation accountable to this project's own documented `IDisposable`
convention. Worth a follow-up to correct the comment and, separately, to make a deliberate
decision (and corresponding tightened test) about post-Dispose behavior for `OcclusionQuery`.
