# Audit: examples/easygl_effect_current_technique_test.cpp

## Metadata

- Source file: `examples/easygl_effect_current_technique_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (Task 185), `examples-tests-easygl` shard
- File type: C++ integration-test executable (`Game` subclass, `main()`), CPU-only
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Effect.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/EffectTechnique.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.cpp`,
  `src/Microsoft/Xna/Framework/Graphics/EffectPass.cpp`,
  `include/Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp`
- XNA/FNA relevance: `Effect.CurrentTechnique` (get/set), `Effect.Techniques` (indexed and
  name-keyed collection), `EffectTechnique.Passes`, and `EffectPass.Apply()` are all real XNA 4.0
  API. FNA's `EffectPass.Apply()` throws `InvalidOperationException` when applying a pass that does
  not belong to the effect's current technique — CNA mirrors this (see Behavioral correctness).
- Main related tests: this is the only test in this shard directly exercising
  `CurrentTechnique`/`Techniques`/`Passes`/`EffectPass::Apply()` plumbing.

## Purpose

Verifies four things about `Effect`'s technique/pass machinery, using `AlphaTestEffect` as a
concrete stock-effect instance: (1) `getCurrentTechniqueProperty()` is non-null immediately after
construction and points at `Techniques[0]`; (2) `Techniques` is both indexable and name-keyed
(`Techniques["Default"]`); (3) `Techniques[0].Passes` has at least one pass; (4)
`setCurrentTechniqueProperty()` round-trips correctly and `Passes[0].Apply()` completes without
throwing once the current technique is set to the pass's own owning technique.

## Executive Verdict

**Healthy** — every assertion in this file corresponds to real, traced production logic (not
stubbed-out getters), and the file catches a genuine XNA behavioral contract
(`EffectPass::Apply()`'s technique-ownership check) rather than merely confirming the code doesn't
crash.

## Checklist Results

### API / XNA / FNA parity
`getCurrentTechniqueProperty()`/`setCurrentTechniqueProperty()` naming follows this project's
documented C# `get`/`set` property convention (`CLAUDE.md`). `Techniques["Default"]` (name-keyed
lookup) and `Techniques[0]` (indexed lookup) both correspond to XNA's `EffectTechniqueCollection`
indexer overloads (`this[int]`, `this[string]`).

### Behavioral correctness
Traced against `Effect::Effect(GraphicsDevice&)` (`Effect.cpp` lines 10-16):
```cpp
techniques_.Add(EffectTechnique(this, "Default"));
currentTechnique_ = &techniques_[0];
```
— confirms `getCurrentTechniqueProperty() == &Techniques[0]` (test lines 42-46) holds by
construction, and the technique is genuinely named `"Default"` (test line 51-52 relies on this,
not a placeholder). `EffectTechniqueCollection::operator[](const std::string&)`
(`EffectTechniqueCollection.cpp` lines 12-16) is a real linear search by name (`if
(e->getNameProperty() == name) return e.get();`), not a stub that always returns non-null — the
test's assertion `getTechniquesProperty()["Default"] != nullptr` (line 51) is a meaningful check
that would fail if the technique were misnamed or the lookup were broken.

`Techniques[0].getPassesProperty()` traces to `EffectTechnique::EffectTechnique(Effect* owner,
std::string name)` (`EffectTechnique.cpp` lines 14-18): `passes_.Add(EffectPass(owner, "P0",
id_))` — confirms exactly one pass exists, named `"P0"`, tagged with the technique's own
`id_` (a `NextId()`-assigned `std::uint64_t`, `EffectTechnique.hpp` line 91). This `techniqueId_`
tagging is exactly what `EffectPass::Apply()` (`EffectPass.cpp` lines 19-30) checks:
```cpp
const EffectTechnique* current = owner_->getCurrentTechniqueProperty();
if (current == nullptr || current->getIdInternal() != techniqueId_)
    throw System::InvalidOperationException("Applied a pass not in the current technique!");
owner_->Apply();
```
The test explicitly sets `fx.setCurrentTechniqueProperty(t0)` (line 61) where `t0 =
&fx.getTechniquesProperty()[0]` — the same technique `passes[0]` belongs to — before calling
`passes[0].Apply()` (line 69), so the ID match holds and `Apply()` correctly does not throw. This
is a genuine, non-trivial check: had the test called `Apply()` on a pass belonging to a
*different* technique than the current one, it would correctly throw
`InvalidOperationException`, and the test's own `try`/`catch(...)` (lines 67-72) would have caught
that and reported `applyOk = false` — the assertion is discriminating, not vacuous.

### Logic
`EffectTechniqueCollection` stores elements as `std::vector<std::unique_ptr<EffectTechnique>>`
specifically so pointers captured before a later `Add()` (like `Effect`'s own
`currentTechnique_`) remain valid after vector reallocation (documented in
`EffectTechniqueCollection.hpp`'s class comment) — since no `Add()` call is made after
`Effect`'s constructor in this test, this pointer-stability guarantee isn't itself stressed here,
but the design is sound and the test's pointer-identity checks (`def == &fx.getTechniquesProperty()[0]`,
line 45) rely on operator`[]`'s stable dereferencing through the `unique_ptr` indirection, which is
correctly implemented (`operator[](int) { return *elements_.at(index); }`).

### Memory/resource lifetime
No ownership concerns — `fx` is a stack `AlphaTestEffect`, `def`/`t0` are non-owning observing
pointers into `fx`'s own technique collection, valid for `fx`'s lifetime (the whole of
`Initialize()`).

### C++ correctness
No issues. `EffectTechnique*`/`EffectPassCollection&` returned by reference/pointer correctly
alias into `fx`'s internal storage; no dangling risk within this test's scope.

### Performance
N/A — single-shot `Initialize()`-only test.

### Thread safety
N/A.

### Architecture
Correctly limited to the public XNA-facing API.

### Maintainability
Small (96 lines), clearly commented, proportionate to what it tests.

### Portability
N/A.

### Robustness
The `try`/`catch(...)` around `Apply()` (lines 67-72) is a reasonable pattern for a "must not
throw" assertion, though it swallows the actual exception type/message — acceptable here since the
test only needs to know whether *any* exception occurred, and a FAIL printout plus nonzero exit
code is still produced by `check()`.

### Testing
This file is itself a test — see Missing or Weak Tests for the untested negative case.

### Cross-file consistency
Consistent across `Effect.cpp`/`EffectTechnique.cpp`/`EffectTechniqueCollection.cpp`/`EffectPass.cpp`
— the test's assumptions about default technique naming (`"Default"`), default pass naming
(`"P0"`), and the `techniqueId_` ownership-check mechanism all match the actual implementation
exactly.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — The negative case of `EffectPass::Apply()`'s ownership check is never exercised

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `EffectPass::Apply()` (`EffectPass.cpp` lines 19-30), specifically the
  `throw System::InvalidOperationException("Applied a pass not in the current technique!")` branch
- Evidence: this test only ever calls `Apply()` on a pass whose owning technique *is* the current
  technique (test lines 60-69) — it never sets `CurrentTechnique` to something else (or leaves a
  second technique uncurrent) and then calls `Apply()` on a pass from the non-current one, so the
  `throw` branch itself is never actually taken/observed by any test in this shard.
- Why it matters: since `AlphaTestEffect`/every stock effect here only ever has one technique
  ("Default"), constructing a genuine negative case would require either a second technique
  (not something stock effects expose) or a hand-rolled multi-technique test double — a real gap,
  but a proportionate one given the current single-technique-per-effect reality of this codebase.
- FNA/XNA comparison: FNA's own `EffectPass.Apply()` performs an analogous currency check
  internally (via its native effect object); CNA's `InvalidOperationException` message and
  trigger condition are a reasonable, XNA-consistent translation.
- Suggested future action (not implemented by this audit): if/when a multi-technique `Effect`
  test double exists elsewhere in the suite, add a dedicated negative test for this throw path;
  otherwise this is acceptable residual risk given no stock effect currently has >1 technique.

## Cross-File Observations

None beyond what's traced above.

## Missing or Weak Tests

- The `InvalidOperationException` throw path in `EffectPass::Apply()` (F1) has no positive
  ("did throw") test anywhere found in this shard search.
- `Techniques.getCountProperty()` is only asserted `>= 1` (line 49), not `== 1` — a minor
  looseness given every stock effect currently has exactly one technique, but this means the test
  would silently pass even if a future regression accidentally added spurious duplicate
  techniques.

## Positive Findings

- Every assertion in this file maps to real, non-trivial production logic traced end-to-end
  (constructor default wiring → name-keyed lookup → pass-to-technique ID tagging → apply-time
  ownership check) — an exemplary "test that actually tests something" for this shard's
  anti-boilerplate standard.
- Correctly exercises `EffectPass::Apply()`'s real ownership-check code path (a positive case),
  which many superficial "just call Apply() and check it runs" tests elsewhere could have gotten
  away with omitting entirely (i.e. skipping the explicit `setCurrentTechniqueProperty` call would
  still often work by construction-time default, but this test does it deliberately and explicitly
  verifies the round-trip).

## Final Assessment

A small but genuinely substantive test: it traces cleanly through `Effect`, `EffectTechnique`,
`EffectTechniqueCollection`, and `EffectPass`, and its assertions would catch real regressions in
default-technique wiring, name-keyed lookup, or the pass-ownership check. Its only gap is the
untested negative (throwing) branch of `EffectPass::Apply()`, which is a proportionate, low-risk
omission given the current one-technique-per-effect architecture.
