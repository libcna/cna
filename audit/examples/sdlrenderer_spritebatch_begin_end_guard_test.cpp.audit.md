# Audit: examples/sdlrenderer_spritebatch_begin_end_guard_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritebatch_begin_end_guard_test.cpp` (130 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteBatch` Begin/End sequencing-guard test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_spritebatch_begin_end_guard …)` /
  `cna_register_backend_test(NAME SDL_Renderer_SpriteBatch_BeginEndGuard …)`,
  `cmake/Tests/SdlRendererTests.cmake:103-105`. Header traces to Task 677; confirmed via `git log`
  (`49bb2c26 test(Task 677): verify Begin/End sequencing guards on SDL_Renderer`).
- XNA/FNA relevance: `SpriteBatch.Begin()`/`End()`/`Draw()` sequencing-exception contract.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`Begin`/`End`/`Draw` guard
  checks, lines 88-135, 222+), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlSpriteBatchBackend::Begin`/`End`, lines 65-81 — already audited in
  `audit/src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp.audit.md`).
- Also relevant: root `known_bugs.md` — "Multiple SpriteBatch Begin/End in one frame discards all but the last"
  (Vulkan-confirmed, "others unknown"). This file's check 5 is the only place in this 8-file batch that exercises
  more than one Begin/End pair within a single frame, so it is the natural place to look for corroboration or
  refutation of that known defect on this backend — see F1.

## Purpose

A pure sequencing/exception-contract test, explicitly scoped by its own header comment (lines 1-18) to confirm
that `SpriteBatch`'s `begun`-flag guards (already unit-tested against a mock backend in
`SpriteBatchTests.cpp`, Tasks 161-166) still hold when a **real** `GraphicsDevice`/`SdlGraphicsBackend` is
wired in underneath — i.e. that no real SDL call fires (or throws for an unrelated reason) before the
sequencing check itself fires. Five checks: (1) `End()` before `Begin()` throws; (2) `Draw()` before `Begin()`
throws; (3) a second `Begin()` without an intervening `End()` throws; (4) a normal `Begin()`/`Draw()`/`End()`
does not throw; (5) `Begin()`/`End()`/`Begin()`/`End()` (two back-to-back sessions in one frame) does not throw.

## Executive Verdict

**Mostly healthy** — the five checks correctly and specifically verify what the file claims (sequencing
exceptions, not rendering correctness), and each check's assertion was independently traced against
`SpriteBatch::Begin()`/`End()`/`Draw()`'s actual guard order. One gap: check 5 is the only test in this batch
that opens two Begin/End sessions in a single frame, which is exactly the shape of the known,
`known_bugs.md`-documented "second batch overwrites the first" defect (confirmed on Vulkan) — but check 5 only
asserts "does not throw," never reads back a pixel from the *first* session's sprite, so it cannot detect that
class of bug even if this backend had it (see F1). Source-level reading of `SdlSpriteBatchBackend` suggests this
backend is structurally unlikely to have that defect (each `Draw()` call issues an immediate, unqueued
`SDL_RenderTexture`, and `flushBatch()` runs fully at `End()` before `Present()` — nothing defers a sprite past
its own session's `End()`), but that is inference from code reading, not something this test (or any other file
in this batch) actually measures.

## Checklist Results

### API / XNA / FNA parity
FNA's `SpriteBatch.Begin()` throws `InvalidOperationException("Begin cannot be called again until End has
been successfully called.")` on re-entrant `Begin()`, and `End()`/`Draw()` similarly guard on `beginCalled`.
CNA's `SpriteBatch.cpp` throws `std::runtime_error` with equivalent messages ("Begin has been called before
calling End.", "End was called, but Begin has not yet been called.", "SpriteBatch::Draw called before
Begin().") — matching FNA's guarded-sequence *contract* (throw on misuse), modulo the C++ exception type
substitution this project uses project-wide. All three of this test's throwing checks (1-3) map onto exactly
these three guards.

### Behavioral correctness
Traced each check against `SpriteBatch.cpp`:
- Check 1 (`End()` before `Begin()`, line 71): `SpriteBatch::End()` line 135 checks `if (!begun) throw ...` —
  correct, `begun` starts `false`.
- Check 2 (`Draw()` before `Begin()`, line 76): every `Draw` overload opens with `if (!begun) throw ...` (e.g.
  line 224) — correct.
- Check 3 (second `Begin()` without `End()`, lines 81-85): `SpriteBatch::Begin(...)` line 90 checks `if (begun)
  throw ...` before doing anything else (before touching `graphicsDevice_` or `backend_`) — correct, and this is
  precisely the scenario the file's header claims to confirm ("no real backend call happens... before the
  sequencing check fires"); independently confirmed true by reading the guard's position at the very top of the
  7-argument `Begin()` overload.
- Check 4 (clean `Begin()`/`Draw()`/`End()`, lines 88-97): exercises the real
  `SdlSpriteBatchBackend::Begin()`/`Draw()`/`End()` path with a real 1×1 white texture — a genuine sanity check
  that the real backend engages without throwing for an unrelated reason.
- Check 5 (`Begin()`/`End()`/`Begin()`/`End()`, lines 100-109): confirmed no guard in `SpriteBatch::Begin()`/
  `End()` rejects a *second, sequential* (not nested) Begin/End pair — `begun` is correctly reset to `false` at
  the end of `End()` (line 133), so a following `Begin()` sees `begun == false` and proceeds normally. This is
  the correct FNA-parity behavior (FNA explicitly supports multiple Begin/End pairs per frame); the test
  correctly asserts "does not throw" for this legal usage.

### Logic
No branching beyond the try/catch idiom repeated five times; `result_` aggregation (`check()`, lines 44-48) is a
simple OR-into-1, matching the shard's established idiom.

### Memory/resource lifetime
`sb_`/`tex_` are `unique_ptr`-owned, constructed once in `Initialize()`. Step 3's cleanup (`sb_->End();` at line
86) is necessary and present — without it, the still-open batch from step 3's first `Begin()` would make step
4's `Begin()` throw for the wrong reason (leftover `begun == true`), which would have produced a confusing
cascading false-positive/false-negative rather than a clean per-check result. Correctly handled.

### C++ correctness
No unsafe casts; straightforward `try`/`catch (const std::exception&)` around each call, consistent with the
`std::runtime_error` types actually thrown by `SpriteBatch.cpp`.

### Performance
N/A — single-frame test with five trivial operations.

### Thread safety
N/A.

### Architecture
Correctly scoped to avoid duplicating the mock-backend guard tests (`SpriteBatchTests.cpp`) — the header
comment's own stated non-goal ("this task instead confirms the guards still hold end-to-end... not that no
pixel-level assertion is needed") is accurate and consistent with what the file actually does.

### Maintainability
130 lines, clear and minimal; the `check()` helper (lines 44-48) keeps all five assertions uniformly formatted.

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
Each throwing check uses a generic `catch (const std::exception&)`, which is broad enough to catch whatever
concrete exception type `SpriteBatch.cpp` throws (currently `std::runtime_error`) without being coupled to that
specific type — reasonable for a black-box "did it throw" check.

### Testing
See Executive Verdict and F1: this file exercises the full guard state machine correctly for the "does it
throw" question, but is the only file in the batch touching multi-session-per-frame behavior and does not pair
that with any pixel verification.

### Cross-file consistency
Guard logic and this test's expectations are fully consistent with `SpriteBatch.cpp`'s actual implementation;
no discrepancy found between the file's claims and the current source.

## Detailed Findings

### F1 — Check 5 (`Begin()`/`End()`/`Begin()`/`End()`) verifies only "does not throw," not that the first session's sprite actually rendered — leaves `known_bugs.md`'s documented multi-Begin/End discard defect unconfirmed/unrefuted on this backend

- Severity: MEDIUM
- Confidence: MEDIUM (the test-coverage gap itself is certain; whether this backend is actually affected by the
  known defect is not settled by this file, though source-level reasoning below suggests it likely is not)
- Category: test-coverage
- Location/symbol: check 5, lines 99-109 (`okNoThrow` assertion only, no `GetBackBufferData` call)
- Evidence: `known_bugs.md` documents, as a confirmed live defect ("Backend: Vulkan (confirmed), others
  unknown"): "If `SpriteBatch::Begin()`/`End()` is called more than once within a single `Draw()` frame, only
  the draws from the last Begin/End pair are visible." Check 5 is structurally the exact repro shape (two
  Begin/End pairs, one frame) but only asserts `okNoThrow`, never draws a distinguishable sprite in the first
  session and reads it back after the second session's `End()`. A backend exhibiting the known bug would still
  pass this test (no exception is thrown; pixels are merely wrong), so this check cannot detect it.
- Why it matters: `AUDIT_SCOPE.md`'s documentation-treatment section explicitly calls out this exact defect as
  one that "should be corroborated or refuted by the corresponding subsystem's audit rather than ignored." This
  file is the one place in the `examples-tests-sdlrenderer` batch reviewed here that could have settled the
  question for the SDL_Renderer backend specifically, and does not.
- Independent source-level assessment (not proof, but relevant context): `SdlSpriteBatchBackend::Draw()`
  (`SdlGraphicsBackend.cpp` lines 124-141+) issues a synchronous, unqueued `SDL_RenderTexture()` call per
  `Draw()`; `SpriteBatch::flushBatch()` (`SpriteBatch.cpp`) runs entirely inside `End()`, before `backend_->
  End()` returns, and `spriteQueue_.clear()` happens at the *start* of the next `Begin()` (line 108), after the
  previous session's flush already completed. No code path defers a sprite drawn in one Begin/End session past
  that session's own `End()`. This makes it unlikely the SDL_Renderer backend shares Vulkan's defect, but this
  is inferred from reading two files, not measured by any test in this project as far as this batch shows.
- FNA/XNA comparison: N/A — this is a CNA/backend-implementation defect category (FNA's own managed
  `SpriteBatch` has no equivalent multi-session bug; the referenced defect is CNA/backend-specific).
- Related files: `known_bugs.md` (root, exempt from per-file audit but the source of this cross-check),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`, `src/Microsoft/Xna/Framework/Graphics/
  SpriteBatch.cpp`.
- Suggested future action (not implemented by this audit): extend check 5 (or add a sibling check) to draw a
  distinguishable sprite in the first Begin/End session, a different-colored sprite in the second, and read
  back both regions after both sessions close — turning this from an exception-only check into one that would
  actually catch a regression of the `known_bugs.md` defect on this specific backend.

## Cross-File Observations

- This file's scoping decision (guards only, defer semantics to `SpriteBatchTests.cpp`) is explicit and
  reasonable on its own terms; F1 is not a criticism of that scoping choice but of the fact that, given
  `known_bugs.md`'s specific multi-session claim, no file identified in this batch actually performs the
  pixel-level multi-session check that would close the loop.
- Consistent `unique_ptr` ownership and single-frame `done_`-guarded `Draw()` idiom shared with every other file
  in this batch.

## Missing or Weak Tests

See F1 — a pixel-verified two-session-per-frame check is the concrete gap.

## Positive Findings

- All three throwing checks (1-3) were independently traced to the exact guard statement in `SpriteBatch.cpp`
  responsible for each, and all three are correctly positioned (guard first, no side effects before the throw).
- Check 3's own cleanup (`sb_->End()` at line 86) correctly prevents a would-be false result cascading into
  check 4 — a detail easy to get wrong that this file gets right.
- The file's header comment is transparent and accurate about its own scope (guards only, not rendering
  semantics), matching this audit's own independent reading of what the code actually does.

## Final Assessment

A correct, well-scoped sequencing-guard test whose five checks all verify what they claim. The one gap (F1) is
a missed opportunity, not a false claim: the file never states it verifies the multi-Begin/End rendering
defect, but given that defect's prominence in this project's own `known_bugs.md` and the audit's explicit
instruction to corroborate/refute it, the absence of any pixel-level multi-session check in this batch is worth
flagging as a real, actionable coverage gap rather than passing over silently.
