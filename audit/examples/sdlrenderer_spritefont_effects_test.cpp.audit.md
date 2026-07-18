# Audit: examples/sdlrenderer_spritefont_effects_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_spritefont_effects_test.cpp` (194 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `SpriteBatch::DrawString` + `SpriteEffects` regression test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_spritefont_effects` /
  `SDL_Renderer_SpriteFont_Effects`, `cmake/Tests/SdlRendererTests.cmake:193-195`), introduced in the *same*
  commit as its own fix: `0accd452`/`efa02475` ("fix(Task 694): SpriteBatch::DrawString honors SpriteEffects flip
  correctly").
- XNA/FNA relevance: direct — `SpriteEffects.FlipHorizontally`/`FlipVertically` applied to `SpriteBatch.
  DrawString`, plus rotation/origin/scale interaction (pre-existing, unrelated to the Task 694 fix).
- FNA reference: `Graphics/SpriteBatch.cs` `DrawString`'s `axisDirectionX/Y`/`axisIsMirroredX/Y` lookup tables
  (lines 40-68) and their use (lines 742-752, 815+).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (`DrawString`, lines 401-506).

## Purpose

Two independent checks. **Test 1** (lines 131-150) is the actual regression test for the Task 694 fix: drawing
`"AB"` (white/green, 8x8 each) with `SpriteEffects::FlipHorizontally` must not just flip each glyph's own texture
sampling in place — it must also reorder the glyph *sequence*, so `'B'` renders first (leftmost) and `'A'` second
(rightmost), the exact mirror of the unflipped layout. **Test 2** (lines 152-172) is an unrelated, pre-existing
rotation/origin/scale placement check (single glyph, `origin=(4,4)`, `scale=(2,2)`), explicitly noted by the file's
own header as "not touched by the fix" — included as a non-regression guard alongside the new fix-specific check.

## Executive Verdict

**Needs attention** — both of this file's own checks are correct and independently re-derived to match production
code exactly, and the fix they guard is real (confirmed via `git show` of the introducing commit). However, this
audit found a genuine, unflagged gap one level up the stack: `DrawString`'s axis-direction tables in
`SpriteBatch.cpp` are sized for only 3 `SpriteEffects` values (`None`/`FlipHorizontally`/`FlipVertically`), while
FNA's own equivalent tables are sized for 4 (including the combined `FlipHorizontally|FlipVertically` case) — this
file (and every other file in this codebase, as far as this audit found) has zero coverage of that combination,
and the underlying array-indexed lookup would be an out-of-bounds read if a caller ever produced that value.

## Checklist Results

### API / XNA / FNA parity

`DrawString`'s `axisDirX[3]`/`axisDirY[3]`/`axisIsMirroredX[3]`/`axisIsMirroredY[3]` tables (`SpriteBatch.cpp:
427-430`) exactly match the first 3 entries of FNA's own `axisDirectionX`/`axisDirectionY`/`axisIsMirroredX`/
`axisIsMirroredY` static arrays (`SpriteBatch.cs:41-68`), confirmed by direct comparison: FNA's arrays are
`{-1,1,-1,1}`/`{-1,-1,1,1}`/`{0,1,0,1}`/`{0,0,1,1}` (4 entries, indices 0-3 = None/FlipH/FlipV/Both); CNA's
arrays are `{-1,1,-1}`/`{-1,-1,1}`/`{0,1,0}`/`{0,0,1}` (3 entries, indices 0-2 only). **CNA's `SpriteEffects` enum
itself (`SpriteEffects.hpp`) only declares 3 values (`None=0`, `FlipHorizontally=1`, `FlipVertically=2`) and has
no `operator|` overload anywhere in the codebase** (confirmed by grep across `include/`/`src/`) — so ordinary CNA
calling code cannot even *construct* `SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically` the way a
C# caller can combine XNA's real `[Flags] enum SpriteEffects` values; producing the combined value 3 requires an
explicit `static_cast<SpriteEffects>(3)`, which is not idiomatic C++ call-site code. This substantially narrows
real-world exposure, but it means: (a) `DrawString` cannot correctly render simultaneously-both-flipped text at
all (there is no way to request it through the public API as designed), and (b) `effIdx = static_cast<int>(
effects)` (`SpriteBatch.cpp:431`) combined with a 3-element `constexpr` array is a live out-of-bounds read
(`axisDirX[3]`, undefined behavior) for anyone who *does* force the value via cast — worse than FNA's own
behavior, which handles it correctly. See F1.

### Behavioral correctness

Independently re-derived **Test 1** (`FlipHorizontally`, `origin=Vector2::Zero`, `position=(4,4)`, two-glyph font
where `croppingAB_` is identical `(0,0,8,8)` for both glyphs but `glyphData_` differs: `'A'=(0,0,8,8)` white,
`'B'=(8,0,8,8)` green): `MeasureString("AB") = (16,8)` (confirmed via `SpriteFont::MeasureString`, matching FNA's
identical algorithm line-for-line). `baseOffset = (0,0) - (16*1, 8*0) = (-16,0)`. For `'A'` (`curOffset.X=0`
after the first-in-line `abs(0)` step): `offsetX = -16 + (0+0)*1 = -16`; `+= cGlyph.Width(8)*1 = -8`; `localX =
8`; `dest.X = round(4+8) = 12` → `'A'` occupies `x∈[12,20)`. For `'B'` (`curOffset.X=8` after `'A'`'s
`cKern.Y+cKern.Z`): `offsetX = -16 + (8+0)*1 = -8`; `+= cGlyph.Width(8)*1 = 0`; `localX = 0`; `dest.X = round(4+0)
= 4` → `'B'` occupies `x∈[4,12)`. This audit's re-derivation **matches the file's own check assertions exactly**:
`sample(8,8)` (inside `[4,12)`, `'B'`'s region) → green; `sample(16,8)` (inside `[12,20)`, `'A'`'s region) →
white — i.e. `'B'` now renders left of `'A'`, the correct mirrored result. **Test 2**'s
`dest=(2,2,16,16)` claim was independently re-derived and confirmed in the sibling
`sdlrenderer_spritefont_single_glyph_test.cpp`-style walkthrough (the `effects=None` formula path, unaffected by
Task 694) and matches exactly.

### Logic

The `check()` helper (lines 82-86) records failures into `result_` without stopping the run — consistent with
sibling files in this batch.

### C++ correctness

See F1: `effIdx = static_cast<int>(effects)` (`SpriteBatch.cpp:431`) has no range validation before indexing the
3-element `constexpr` arrays; this is architecturally the SpriteBatch.cpp file's own concern (out of this file's
direct scope, since this test never passes an out-of-range value), but is directly relevant here because this is
the one file in the whole audited codebase whose entire purpose is `SpriteEffects`-with-`DrawString` coverage, and
it does not (and, given the enum's current definition, effectively *cannot* through the public API) exercise the
combined case.

### Memory/resource lifetime

Two independent font/atlas pairs (`atlasAB_`/`fontAB_`, `atlasA_`/`fontA_`) constructed once in `Initialize()`,
kept alive through both test blocks in a single `Draw()` call — no lifetime concerns.

### Performance / Thread safety

N/A — single-frame diagnostic executable.

### Architecture

Good separation: Test 1 isolates the *new* fix, Test 2 explicitly documents (in its own header comment) that it
is an unrelated pre-existing-and-unaffected code path included as a non-regression guard — a clear, honest
distinction that made this audit's re-verification straightforward.

### Robustness

Both tests use solid-colour 8x8 glyphs specifically so a colour mismatch unambiguously indicates a placement bug
rather than a partial/anti-aliasing artifact — appropriate for a pixel-exact regression test.

### Testing

This file is itself a test. See F1 for the one real gap this audit surfaced (not previously flagged anywhere in
this file's own comments, unlike the self-disclosed gaps seen in some sibling EasyGL tests during this audit).

## Detailed Findings

### F1 — `SpriteBatch::DrawString`'s per-effect lookup tables are sized for 3 `SpriteEffects` values, not FNA's 4; combining both flips is unsupported and, if ever forced via cast, is an out-of-bounds array read

- Severity: MEDIUM
- Confidence: HIGH (read both the CNA array declarations/sizes and FNA's `SpriteBatch.cs` reference tables
  directly, byte-for-byte; confirmed no `operator|` exists for `SpriteEffects` anywhere in the tree via grep)
- Category: correctness / API-completeness / latent-UB
- Location/symbol: `SpriteBatch.cpp:427-431` (`axisDirX[3]`, `axisDirY[3]`, `axisIsMirroredX[3]`,
  `axisIsMirroredY[3]`, `const int effIdx = static_cast<int>(effects);`); `include/Microsoft/Xna/Framework/
  Graphics/SpriteEffects.hpp` (enum has only 3 values, no combined constant, no bitwise operators)
- Evidence: FNA's `SpriteBatch.cs` (lines 41-68) declares all four tables with 4 entries each — index 3 is the
  `FlipHorizontally | FlipVertically` combination, a legal value of XNA's `[Flags] enum SpriteEffects` that any
  real XNA game can construct and pass to `DrawString`. CNA's equivalent tables (`SpriteBatch.cpp:427-430`) are
  `constexpr float ...[3]`, i.e. only cover indices 0-2. `effIdx` is derived by a raw `static_cast<int>(effects)`
  with no bounds check, then used to index all four arrays at lines 481-482, 485-486 — if `effIdx == 3` were ever
  reached, this is an out-of-bounds read on a `constexpr` array (undefined behavior, not a caught/thrown error).
- Why it matters: functionally, CNA's `DrawString` cannot correctly render "flip both axes" text at all — there is
  no `SpriteEffects` value representing it (the enum has no `Both`/combined member, and no `operator|` exists to
  construct one from the two named values), so this is a real, silent XNA/FNA-parity gap for a legal XNA
  `[Flags]` combination, not merely an untested edge case. Separately, *if* any code anywhere (a future
  `operator|` addition, a raw enum-value cast from deserialized/scripted data, etc.) ever produces the integer
  value 3, the current array-indexed implementation would read past the end of a 3-element array rather than
  throwing or clamping — undefined behavior, worse than simply "not supported."
- FNA/XNA comparison: XNA's real `SpriteEffects` is `[Flags]`; FNA's own `DrawString` explicitly masks
  `effects &= (SpriteEffects) 0x03` before indexing its 4-entry tables specifically to tolerate any stray high
  bits, and separately actually implements the combined case's math correctly (both axes mirrored, both `axisDir`
  and `axisIsMirrored` values doubly applied per the 4th table row). CNA implements neither the masking safety net
  nor the 4th-row math.
- Related files: `SpriteBatch.cpp` (the actual defect location — this file only *exposes* it by being the one
  dedicated `SpriteEffects`+`DrawString` test in the codebase and not covering this case), `include/Microsoft/
  Xna/Framework/Graphics/SpriteEffects.hpp` (missing `operator|`/combined value), the regular (non-text)
  `SdlSpriteBatchBackend::Draw` path (`SdlGraphicsBackend.cpp:245-249`) — which, by contrast, correctly handles
  the combined case via independent bitwise tests (`(int)effects & (int)SpriteEffects::FlipHorizontally` etc.,
  not table-indexed), demonstrating the *sprite* draw path does not have this gap, only the *text* layout path.
- Suggested future action (not implemented by this audit): either (a) add `operator|`/a `Both` value to
  `SpriteEffects` and extend `DrawString`'s tables to 4 entries with FNA's exact 4th-row math plus an `& 0x03`
  mask for defensive safety, matching the sprite-draw path's existing bitwise-test approach instead of array
  indexing, or (b) if combined flips are deliberately out of scope for CNA's `SpriteEffects`, document that
  decision explicitly in `SpriteEffects.hpp` and consider a defensive bounds check/`assert` in `DrawString` so an
  out-of-range `effIdx` fails loudly rather than reading past the array.

## Cross-File Observations

- The regular (non-text) sprite draw path in the same SDL_Renderer backend (`SdlGraphicsBackend.cpp:245-249`)
  correctly supports the combined-flip case via bitwise tests producing `SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL`
  — meaning F1 is specific to the shared, backend-agnostic `SpriteBatch::DrawString` text-layout code, not a
  backend limitation; it would reproduce identically on every other backend, since `DrawString` lives in shared
  `SpriteBatch.cpp`.
- This file's own introducing commit (`0accd452`) is explicitly, honestly scoped to fixing the None/FlipH/FlipV
  cases; nothing in that commit's message or this file's header comment claims the combined case was considered,
  so F1 is a newly-surfaced gap by this audit, not a previously-known-and-accepted limitation.

## Missing or Weak Tests

See F1 — no test anywhere in the codebase (as far as this batch's 8-file scope shows) exercises
`SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically` with `DrawString`.

## Positive Findings

- Both of this file's own checks were independently re-derived by hand against the actual, current
  `SpriteBatch::DrawString` formula and match exactly — the Task 694 fix this file guards is real, verified
  correct, and the file's own explanatory header comment (particularly its algebraic derivation of CNA's sign
  convention relative to FNA's) is accurate.
- Good test hygiene: clearly separates the new fix-specific check (Test 1) from an unrelated pre-existing
  behavior guard (Test 2), each documented independently.

## Final Assessment

The file's own two checks are correct and were independently confirmed against production code. The "Needs
attention" verdict is driven by F1, a real gap discovered by tracing one level up into the shared `DrawString`
implementation this file exercises: the combined-flip `SpriteEffects` case is unsupported by the public API
(no way to construct it) and would be genuine undefined behavior if ever forced via a raw cast — worth a
follow-up in `SpriteBatch.cpp`/`SpriteEffects.hpp`, not in this test file itself.
