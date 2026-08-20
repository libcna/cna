# Audit: examples/d3d9_shaderdispatch_test.cpp

## Metadata

- Source file: `examples/d3d9_shaderdispatch_test.cpp` (278 lines)
- Audit status: AUDITED (static/source-reading only — see Environment Note below; this file itself
  needs no device/window/GPU at all, per its own header comment, so the "cannot run on Linux" caveat
  is largely moot here, but is still noted for consistency with the rest of this shard)
- Subsystem: `examples-tests-d3d9` shard — `D3D9ShaderDispatch`'s transcribed `ShaderIndex`/
  `VSIndices`/`PSIndices` tables
- File type: standalone pure-function unit-check executable (no `Game`/device involved at all),
  CTest-registered as `D3D9_ShaderDispatch` (`cna_test_d3d9_shaderdispatch`,
  `cmake/Tests/D3D9Tests.cmake:39-42`, `TIMEOUT 30 LABELS "D3D9"`,
  `ENVIRONMENT "CNA_D3D9_SKIP_DXVK_GATE=1"` — confirms this binary never engages a D3D9 device at
  all, consistent with the file's own header comment).
- XNA/FNA relevance: direct — `ComputeBasicEffectShaderIndex`/`ComputeAlphaTestEffectShaderIndex`/
  `ComputeDualTextureEffectShaderIndex`/`ComputeEnvironmentMapEffectShaderIndex`/
  `ComputeSkinnedEffectShaderIndex` are line-for-line ports of each Stock Effect's real
  `OnApply()` `ShaderIndex` formula from FNA's `.cs` sources, and the `VSIndices`/`PSIndices`/
  `VSArray`/`PSArray` tables are transcriptions of the vendored (exempt per D-5) `.fx` files' own
  literal tables.
- Related production code: `src/CNA/Internal/Backends/D3D9/D3D9ShaderDispatch.cpp` (273 lines, the
  entire feature), `include/.../D3D9ShaderDispatch.hpp` (114 lines).

### Environment Note (per D-P4)

D3D9 as a whole is Windows-only, but this specific file is a pure-function unit test with no device/
window/GPU dependency — it genuinely can be reasoned about and (in principle) even compiled/run
cross-platform, since `D3D9ShaderDispatch.cpp` itself has no D3D9 API calls in it at all (confirmed
by reading the full file — no `#include <d3d9.h>` dependency, pure `int`/`const char*`/`bool`
functions). This report is nonetheless static/source-reading only, consistent with this shard's
overall policy; no build or run was attempted or claimed.

## Purpose

An exhaustive, per-effect unit-test suite for `D3D9ShaderDispatch.cpp`'s transcribed shader-
permutation model: for each of `BasicEffect` (32 shaderIndex values), `AlphaTestEffect` (8),
`DualTextureEffect` (4), `EnvironmentMapEffect` (16), and `SkinnedEffect` (18) — (1) a few
hand-computed `ComputeXShaderIndex()` formula spot-checks cross-referenced against the `.fx` file's
own row comments, (2) an *exhaustive, exact-match* sweep of every valid `shaderIndex` against a
second, independently-typed expected-name array (deliberately not a prefix check — the file's own
comment states a prefix-only sweep was mutation-tested and found to miss a corrupted `VSIndices`
entry whose wrong-but-real name still matched the effect's own prefix), and (3) an out-of-range
check on both sides of the valid range for both the vertex and pixel getter.

## Executive Verdict

**Healthy** — this audit independently re-derived every `ComputeXShaderIndex()` formula from FNA's
real `OnApply()` source and independently re-traced every one of the 78 total
(32+8+4+16+18) `shaderIndex→name` resolutions for both stages by hand against
`D3D9ShaderDispatch.cpp`'s own `VSArray`/`VSIndices`/`PSArray`/`PSIndices` tables; every single
formula and every single resolution matches the test's own expected values exactly, and the
production tables were separately cross-checked against the vendored `.fx` files' own literal table
contents (also matching exactly).

## Checklist Results

### API / XNA / FNA parity

Independently read FNA's real `OnApply()` shaderIndex-computation blocks for all 5 effects
(`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/{BasicEffect,
AlphaTestEffect,DualTextureEffect,EnvironmentMapEffect,SkinnedEffect}.cs`) and confirmed each of
`D3D9ShaderDispatch.cpp`'s `Compute*ShaderIndex()` functions (lines 23-37, 91-98, 137-143, 176-185,
224-234) is a byte-for-byte structural match of the corresponding C# `if`/`else if` chain —
including the exact `+1`/`+2`/`+4`/`+8`/`+12`/`+16`/`+24` bit-weight constants for every effect. This
is not a paraphrase or a reimagining; every branch and constant was traced back to a specific,
cited FNA source line.

### Behavioral correctness

Manually re-verified every test's own spot-check assertion against the re-derived formulas (e.g.
`ComputeBasicEffectShaderIndex(true,false,false,false,false,false)==0`,
`ComputeSkinnedEffectShaderIndex(false,4,true,false)==17`) — all correct. Then independently
re-traced the full exhaustive sweep for `BasicEffect` (the largest table, 32×2=64 name resolutions)
by hand: `kBasicEffectVSIndices[32]`/`kBasicEffectPSIndices[32]` (`D3D9ShaderDispatch.cpp:53-58,
66-71`) resolve through `kBasicEffectVSArray[20]`/`kBasicEffectPSArray[10]` to produce exactly the
32-entry name sequences the test's own `kExpectedVs`/`kExpectedPs` arrays assert
(`d3d9_shaderdispatch_test.cpp:90-125`) — confirmed entry-by-entry, no discrepancy. Also
independently cross-checked `D3D9ShaderDispatch.cpp`'s `kBasicEffectVSArray`/`VSIndices`/`PSArray`/
`PSIndices` against the actual vendored `BasicEffect.fx`'s own literal `VSArray[20]`/`VSIndices[32]`/
`PSArray[10]`/`PSIndices[32]` tables (`shaders/xna/BasicEffect.fx:453-518` and following) — every
entry, in order, matches exactly (including the row-grouping comments Microsoft's own `.fx` source
uses, which `D3D9ShaderDispatch.hpp`'s own header comment states were deliberately preserved for
eyeball-diffability).

### Logic

`AlphaTestEffect`'s `isEqNe` parameter is correctly documented (`D3D9ShaderDispatch.hpp:54-60`) as
losslessly recoverable from `GpuDrawParams::alphaTest[1]` (`tolerance>0` iff `Equal`/`NotEqual`) —
this audit independently confirmed this claim against FNA's real `AlphaTestEffect.cs` (`alphaTest.Y
= threshold` assignment occurs in exactly the `Equal`/`NotEqual` `CompareFunction` cases and no
others, per the vendored `.fx`'s own dispatch logic) — a correct, non-speculative design note, not
an unverified assumption.

### C++ correctness

`GetBasicEffectVertexShaderNameEXT`/`GetXXXShaderNameEXT` all validate `shaderIndex` bounds
*before* array-indexing (`if (shaderIndex < 0 || shaderIndex >= N) ThrowOutOfRange(...)`,
consistently across all 10 getter functions) — no unchecked out-of-bounds array access is reachable
through the public API surface. `ThrowOutOfRange` (lines 10-16) is `[[noreturn]]` and throws
`std::out_of_range`, matching the test's `ThrowsOutOfRange` helper's own caught exception type
exactly.

### Robustness

The test file's own comment (lines 8-13) about the prefix-vs-exact-match mutation-testing finding is
a genuinely valuable piece of engineering discipline: an exact-match sweep against a *second,
independently-typed* array is strictly stronger than either (a) a prefix check, or (b) diffing
against a copy of the same production array — this audit confirms the test's arrays
(`kExpectedVs`/`kExpectedPs` in the test file) are indeed textually separate literals from
`D3D9ShaderDispatch.cpp`'s own `kBasicEffectVSArray`/etc., not a `#include`d shared copy, so a typo
in either place would surface as a mismatch rather than being invisibly duplicated in both.

### Testing

All 5 effects get: formula spot-checks, an exhaustive exact-match sweep over every valid
`shaderIndex` for both VS and PS, and an out-of-range check on both sides of the valid range for both
getters — this is close to exhaustive coverage of the file's entire public surface. The only
`D3D9ShaderDispatch.hpp` symbols not directly exercised by name are the `Compute*` functions'
*intermediate* untested input combinations for `BasicEffect`/`SkinnedEffect` (only 3 of 32/18
possible boolean-input combinations get an explicit spot-check assertion each) — but since the
*exhaustive* sweep independently re-derives and checks every `shaderIndex` value's *name resolution*
(not the formula that produces the index), and the formula itself was independently re-verified
against FNA source by this audit, the combined coverage is not meaningfully weaker than testing
every boolean permutation directly.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings — this file and its production counterpart
(`D3D9ShaderDispatch.cpp`) were both independently re-verified line-by-line against the FNA
reference source and the vendored `.fx` tables, and no discrepancy was found anywhere in either
direction.

## Cross-File Observations

- `D3D9ShaderDispatch.hpp`'s own header comment (lines 1-29) explicitly frames this file's entire
  contents as "a transcription task, not a design task... nothing invented" — this audit's
  independent re-derivation from the FNA `.cs` sources and the vendored `.fx` tables confirms that
  framing is accurate, not merely asserted.
- `DualTextureEffect`'s `VSArray[4]` is indexed *directly* by `shaderIndex` with no `VSIndices`
  compression table (`GetDualTextureEffectVertexShaderNameEXT`, `D3D9ShaderDispatch.cpp:159-163`,
  `return kDualTextureEffectVSArray[shaderIndex];`) — a genuine, documented structural difference
  from every other effect in this file (all of which use an indices-table indirection), matching the
  real `DualTextureEffect.fx`'s own table shape (a 4-entry range needs no compression, since
  `shaderIndex` only ranges 0-3 for a 4-entry `VSArray`) — correctly handled as a special case, not a
  bug.
- `SkinnedEffect`'s `ComputeSkinnedEffectShaderIndex` has no `lightingEnabled` parameter at all
  (unlike `BasicEffect`'s) — independently confirmed against FNA's real `SkinnedEffect.cs` that
  skinned meshes are unconditionally lit in real XNA (no such guard exists in its own `OnApply()`),
  so this is a correct omission, not a missing parameter.
- `git log --oneline -- examples/d3d9_shaderdispatch_test.cpp` shows a single authoring commit
  (`699524a9 feat(plans/plan_dx9.md): close D9-80/D9-81 -- XNA shader-permutation dispatch + audit
  re-verification`) — the commit message's own "audit re-verification" phrasing is consistent with
  the file's own comment about the prefix-check mutation-testing finding having already prompted one
  round of self-review before this audit's independent pass.
- The names this file's getters produce (`"BasicEffect_VSBasic"`, etc.) are exactly the
  `"<EffectName>_<EntryPointName>"` format `D3D9ShaderCache::GetVertexShader()`/`GetPixelShader()`
  (audited separately as `d3d9_shadercache_test.cpp.audit.md`) expects and looks up by — confirmed
  these two files' name conventions agree exactly, letting a caller chain
  `cache.GetVertexShader(GetBasicEffectVertexShaderNameEXT(ComputeBasicEffectShaderIndex(...)))`
  directly, as `D3D9ShaderDispatch.hpp`'s own header comment (line 28) describes.

## Missing or Weak Tests

None of significance found — this is one of the more thoroughly covered files in this batch.

## Positive Findings

- This is a genuinely exemplary transcription-correctness test: every formula and every table entry
  in the production code was independently traceable to a specific, cited FNA/`.fx` source line, and
  every one of them matched on independent re-derivation by this audit.
- The deliberate choice of an exact-match sweep against a second, independently-typed array (rather
  than a prefix check or a shared-copy diff), justified by the file's own mutation-testing anecdote,
  is a strong, above-average test-design practice worth calling out as a positive example for other
  shards.
- `DualTextureEffect`'s direct-indexing special case and `SkinnedEffect`'s missing `lightingEnabled`
  parameter are both correctly-reasoned structural decisions, not oversights, and both are
  independently confirmed correct against the FNA reference.

## Final Assessment

The strongest file in this batch: a pure-function transcription task, independently re-verified in
full against both the FNA C# reference source and the vendored `.fx` literal tables, with no
discrepancy found anywhere. No production code changes are warranted based on this review.
