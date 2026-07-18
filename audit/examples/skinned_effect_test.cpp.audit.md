# Audit: examples/skinned_effect_test.cpp

## Metadata

- Source file: `examples/skinned_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard by manifest assignment; registration is EasyGL-only
  (`cmake/Tests/EasyGLTests.cmake:194-197`, `EasyGL_SkinnedEffect_Properties`), but the file itself
  performs **no rendering, no draw calls, and no backend-specific behavior at all** — it only
  constructs a `SkinnedEffect` against a real `GraphicsDevice` and checks C++ property/getter
  behavior. It is genuinely backend-agnostic in the fullest sense (unlike files that are merely
  "no backend-specific API used" but still render pixels).
- XNA/FNA relevance: direct — `SkinnedEffect.WeightsPerVertex`, `SkinnedEffect.MaxBones`,
  `SetBoneTransforms`/`GetBoneTransforms`, `World`/`View`/`Projection`.
- FNA reference: `Graphics/Effect/StockEffects/SkinnedEffect.cs`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`,
  `include/Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp`.

## Purpose

A pure property/API integration test (Task 24) for `SkinnedEffect`: `WeightsPerVertex` get/set
(1/2/4), the `MaxBones` constant, `SetBoneTransforms`/`GetBoneTransforms` round-tripping (1-bone
and 4-bone cases), `World`/`View`/`Projection` defaults and round-trip, `GetTypeName()`, and
`Techniques`/`CurrentTechnique` non-null/non-empty sanity.

## Executive Verdict

**Mostly healthy** — every assertion made is correct and was independently re-verified against
production `SkinnedEffect.cpp` and the FNA reference (`MaxBones=72`, `WeightsPerVertex` default 4,
`GetTypeName()` string all confirmed to match exactly). The gap is what the file does *not* test:
none of `SkinnedEffect`'s several documented exception paths (invalid `WeightsPerVertex`, empty or
oversized `SetBoneTransforms`, out-of-range `GetBoneTransforms(count)`) are exercised at all.

## Checklist Results

### API / XNA / FNA parity
`SkinnedEffect::MaxBones` (checked `== 72` at line 59) — confirmed against
`include/Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp:28` (`static const int MaxBones;`,
defined `= 72` per `SkinnedEffect.cpp:34`'s out-of-class definition) and FNA's own
`SkinnedEffect.cs:21` (`public const int MaxBones = 72;`) — exact match.
`WeightsPerVertex` default-4 claim (line 50) confirmed against
`include/.../SkinnedEffect.hpp:416` (`int weightsPerVertex_ = 4;`) and FNA's
`SkinnedEffect.cs:66` (`int weightsPerVertex = 4;`) — exact match.
`GetTypeName()` expected string (line 97, `"Microsoft.Xna.Framework.Graphics.SkinnedEffect"`)
confirmed against `SkinnedEffect.cpp:520-524` verbatim.

### Behavioral correctness
`SetBoneTransforms`/`GetBoneTransforms` round-trip checks (lines 62-84) independently traced
against `SkinnedEffect.cpp:294-318`: `SetBoneTransforms` forwards to `bonesParam_->SetValue(...)`,
`GetBoneTransforms(count)` reads back via `bonesParam_->GetValueMatrixArray(count)` and then
explicitly restores `m.M44 = 1.0f` on each returned matrix (a documented FNA-mirroring quirk for
bones potentially stored in 4×3 form) — the test's `meq()` helper (lines 31-38) compares all 16
matrix components with a `1e-5f` tolerance, which would in fact catch a regression in that `M44`
restoration logic, since `Matrix::CreateTranslation`'s own `M44` is already `1.0f` and any
corruption would show up as a mismatch. Good, non-trivial coverage of that specific historical
detail even though the test comment doesn't call it out explicitly.

### Logic
`World`/`View`/`Projection` default-identity checks (lines 87-89) and the `World` round-trip
(lines 92-94) are simple pass-through property checks; no logic beyond direct storage is implied
by FNA's own semantics here, so this is proportionate coverage.

### Testing — the primary gap this audit identifies
`SkinnedEffect` has several explicit exception-throwing paths in production code, none of which
this file exercises:
- `setWeightsPerVertexProperty(v)` throws `std::out_of_range` for any `v` not in `{1,2,4}`
  (`SkinnedEffect.cpp:288-289`) — this file only ever sets `1`, `2`, `4` (lines 51-56), the three
  *valid* values; no call with e.g. `3` or `0` to confirm the throw actually fires.
- `SetBoneTransforms(boneTransforms)` throws `std::invalid_argument` for an empty vector
  (`SkinnedEffect.cpp:296-297`) and for `boneTransforms.size() > MaxBones` (line 298-299) — neither
  is exercised; both calls in this file use non-empty, well-under-`MaxBones` vectors (1 and 4
  bones).
- `GetBoneTransforms(count)` throws `std::out_of_range` for `count <= 0` or `count > MaxBones`
  (`SkinnedEffect.cpp:306-307`) — not exercised; both calls use `count` matching what was just set
  (1 and 4).

This directly matches `CLAUDE.md`'s own explicit testing rule ("Out-ref overloads... must be tested
separately," and more generally the project's stated requirement that "every public method...
must have at least one unit test" including exception/boundary behavior per
`AUDIT_CHECKLIST.md` §3/§13) — the *happy-path* behavior of all four of these methods is well
covered, but their exception contracts are entirely unverified by any test this audit has seen in
this batch.

## Detailed Findings

### F1 — SkinnedEffect's four documented exception-throwing conditions have zero test coverage in this file
- Severity: MEDIUM
- Confidence: HIGH (each throw condition and its exact exception type was read directly from
  `SkinnedEffect.cpp`, and each corresponding call site in this test file was confirmed to only ever
  pass valid arguments)
- Category: test-coverage
- Location: `setWeightsPerVertexProperty` (`SkinnedEffect.cpp:288-289`, called only with 1/2/4 at
  test lines 51-56); `SetBoneTransforms` (`SkinnedEffect.cpp:296-299`, called only with non-empty,
  under-limit vectors at test lines 63-64, 72-78); `GetBoneTransforms` (`SkinnedEffect.cpp:306-307`,
  called only with matching valid counts at test lines 65, 79)
- Why it matters: a regression that silently removed any of these four guard checks (e.g. a future
  refactor that changed `throw std::out_of_range(...)` to a silently-clamped value, or vice versa a
  regression that made a currently-valid call incorrectly throw) would not be caught by this file
  or, as far as this audit's scope covers, by `skinned_effect_integration_test.cpp` (same batch,
  which also never calls these with invalid arguments). This is exactly the kind of gap
  `CLAUDE.md`'s "Tests" section calls out by name ("Out-ref overloads... must be tested separately")
  generalized to any documented exception path.
- FNA/XNA comparison: FNA's own `SkinnedEffect.cs` throws `ArgumentNullException`/
  `ArgumentOutOfRangeException` for the equivalent conditions (`WeightsPerVertex` setter,
  `SetBoneTransforms`, `GetBoneTransforms`) — CNA's chosen C++ exception types
  (`std::out_of_range`/`std::invalid_argument`) are a reasonable, already-established CNA
  convention (per `CLAUDE.md`'s "Exception behavior where practical" guidance), not itself a
  finding; only the absence of any test proving these throws actually fire is being reported here.
- Suggested follow-up (not implemented by this audit): add `try`/`catch` (or an
  `EXPECT_THROW`-style helper, if this file is ever migrated to GoogleTest — currently it uses a
  hand-rolled `check()`/`g_failures` pattern rather than gtest) assertions for each of the four
  conditions above.

## Cross-File Observations

- This file's hand-rolled `check()`/`g_failures`/`main()` pattern (lines 20-29, 107-119) is
  identical in shape to `sprite_font_test.cpp` (same batch) and to `skinned_effect_integration_test
  .cpp`'s sibling `result_`/`Exit()` pattern — a consistent, if not GoogleTest-based, harness
  convention across this shard.
- Complements `skinned_effect_integration_test.cpp` (same batch): that file proves the GPU-side
  bone transform is real; this file proves the C++-side property/API surface is complete and
  correct. Together they give reasonable happy-path coverage of `SkinnedEffect`, but neither
  exercises its exception contracts (see F1) or a non-identity-`World` lit scenario (see that
  file's own F1).

## Missing or Weak Tests

See F1 — no coverage anywhere in this batch for `SkinnedEffect`'s `std::out_of_range`/
`std::invalid_argument` throw paths.

## Positive Findings

- Every constant and default value asserted (`MaxBones=72`, `WeightsPerVertex` default `4`,
  `GetTypeName()` string) was independently re-verified against both the FNA reference source and
  current CNA production code, and all match exactly.
- The `meq()` 16-component matrix comparison (rather than e.g. just comparing translation columns)
  gives genuine, non-trivial coverage of `GetBoneTransforms`'s `M44` restoration quirk, a detail
  easy to regress silently.
- Good minimal-but-real coverage breadth: touches every major property surface of the class
  (`WeightsPerVertex`, bones, `World`/`View`/`Projection`, `GetTypeName`, `Techniques`) in one
  compact file.

## Final Assessment

A correct, well-verified property test for `SkinnedEffect`'s happy path, whose main weakness is
that it never exercises any of the class's four documented exception-throwing conditions —
leaving those guard checks with no regression coverage across this entire audit batch.
