# Audit: src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (534 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/SpriteBatch.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements `SpriteBatch`'s full `Begin`/`End`/`Draw`/`DrawString` behavior: queuing, sort-mode
handling, and per-glyph text layout for `DrawString`.

## Executive Verdict
The bulk of this file — `Begin`'s state defaulting (correctly matching FNA's real null-means-preset
semantics for sampler/depth-stencil state, per the file's own "Task 803 finding" comment, already
fixed), `flushBatch`'s sort-mode dispatch, and the `Draw` overload family's rectangle/scale math —
is correct and carefully cross-checked against FNA. However, `DrawString` contains **two distinct,
confirmed HIGH-severity defects**, one of which is newly identified in this pass (the axis-table
bounds issue) and one of which duplicates a defect already found in the sibling `SpriteFont.cpp`.

## Checklist Results
- `Begin(...)` (lines 81-124): correctly defaults `depthStencilState` to `DepthStencilState::None`
  and `samplerState` to `SamplerState::LinearClamp` when null, always re-applying rather than
  inheriting a stale value from a previous `Begin()` — the file's own inline comment cites this as
  a confirmed prior fix ("Task 803 finding"), correctly implemented.
- `pushSprite()` (lines 145-175): the `ObjectDisposedException::ThrowIf` guard on a disposed
  `Texture2D` reaching the batch queue is a genuine, disclosed hardening over FNA's own unguarded
  equivalent (FNA's managed runtime fails more gracefully than a raw C++ null-reference dereference
  would) — correctly reasoned and implemented.
- `flushBatch()` (lines 185-216): `BackToFront`/`FrontToBack`/`Texture` sort predicates correctly
  match `SpriteSortMode`'s documented semantics (verified against that enum's own audit report).
- Every `Draw`/`DrawString` overload correctly throws `std::runtime_error` if called before
  `Begin()` — though see the cross-cutting note below re: exception type.

## Detailed Findings

### HIGH — `DrawString`'s default-character fallback can dereference an invalid map iterator (duplicates `SpriteFont::MeasureString`'s defect)
```cpp
auto it = spriteFont.characterIndexMap_.find(c);
if (it == spriteFont.characterIndexMap_.end())
{
    if (!spriteFont.defaultCharacter_.has_value())
        throw std::invalid_argument(
            "Text contains characters that cannot be resolved by this SpriteFont.");
    it = spriteFont.characterIndexMap_.find(spriteFont.defaultCharacter_.value());
}
const int index = it->second;
```
(lines 457-465). Identical shape and identical root cause to the finding in
`src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp.audit.md`: if the second `find()` (for
`defaultCharacter_`) also fails, `it` is `characterIndexMap_.end()` and `it->second` is undefined
behavior. See that report for the full FNA-comparison and fix-shape discussion — this is the same
defect reachable through a second call site (`DrawString` uses its `friend`-granted direct access
to `spriteFont.characterIndexMap_`/`defaultCharacter_` rather than calling `MeasureString`, so
fixing one call site would not fix the other).

### HIGH — `DrawString`'s axis-direction lookup tables are sized for 3 entries, but `SpriteEffects` is a real, composable `[Flags]` enum with a valid 4th (combined) value — out-of-bounds array read
```cpp
static constexpr float axisDirX[3]        = {-1.0f, 1.0f, -1.0f};
static constexpr float axisDirY[3]        = {-1.0f, -1.0f, 1.0f};
static constexpr float axisIsMirroredX[3] = { 0.0f, 1.0f,  0.0f};
static constexpr float axisIsMirroredY[3] = { 0.0f, 0.0f,  1.0f};
const int effIdx = static_cast<int>(effects);
```
(lines 427-431, then indexed at lines 437-438, 481-482, 485-486). Real XNA's `SpriteEffects` is
`[Flags]`-attributed and explicitly documented as combinable
(`SpriteEffects.FlipHorizontally | SpriteEffects.FlipVertically` is valid, real XNA usage) — FNA's
own `SpriteBatch.cs` (lines 41-68) declares all four of these same lookup tables with **4 entries
each** (indices 0-3), explicitly to handle the combined value (`effects & (SpriteEffects) 0x03`,
confirmed via direct read of the FNA reference source).

CNA's `SpriteEffects` enum (audited separately, `include/Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp.audit.md`)
is missing the `operator|` overload that would let a caller construct the combined value
idiomatically — but this does **not** prevent it: a caller can (and, confirmed via grep, already
does elsewhere in this exact codebase — `examples/sdlgpu_2d_test.cpp:126`) construct the combined
value via `static_cast<SpriteEffects>(static_cast<int>(FlipHorizontally) |
static_cast<int>(FlipVertically))`. If a `SpriteEffects` value of `3` (both flags) reaches this
function via `DrawString`'s own `effects` parameter, `effIdx = 3` indexes past the end of all four
3-element arrays declared here — a real, out-of-bounds stack read, undefined behavior.

**Failure scenario**: `spriteBatch.DrawString(font, "text", pos, color, rotation, origin, scale,
static_cast<SpriteEffects>(3), depth)` — a real, valid, XNA-documented call shape — reads
uninitialized/adjacent stack memory for `axisDirX[3]`/`axisDirY[3]`/`axisIsMirroredX[3]`/
`axisIsMirroredY[3]`, producing garbage text layout at best and a crash (if compiler/ASan flags
the OOB read) at worst.

**Suggested fix** (report-only; no source changes made per this audit's scope): extend all four
`constexpr` arrays to 4 entries matching FNA's real values (index 3 = both flags: `axisDirX[3]=1.0f`,
`axisDirY[3]=1.0f`, `axisIsMirroredX[3]=1.0f`, `axisIsMirroredY[3]=1.0f`, read directly from FNA's
own table declarations), and mask `effIdx` the same way FNA does (`effects & 0x03`) as defense in
depth against any future `SpriteEffects` value outside the 2-bit range.

## Cross-File Observations
- Every `Draw`/`DrawString` overload's "called before Begin()" guard throws raw `std::runtime_error`
  rather than this project's own `System::InvalidOperationException` — consistent with the
  project-wide recurring exception-type pattern flagged repeatedly elsewhere in this audit (not
  re-raised as a separate finding here since it's already a tracked cross-cutting pattern, but
  worth noting `SpriteBatch` — among the most heavily-used types in the entire framework — shares
  it at 10+ call sites).
- `Draw()`'s plain overloads (not `DrawString`) forward `effects` opaquely to the backend's own
  `Draw()` without any array indexing in this file — the axis-table bug is specific to
  `DrawString`'s glyph-positioning math, not the plain sprite-draw path. Whether individual
  backends' own `Draw()` implementations have a similar fixed-size lookup for combined
  `SpriteEffects` values was not checked in this pass (out of this shard's scope; worth flagging
  for backend-specific audits).

## Missing or Weak Tests
`tests/Microsoft/Xna/Framework/Graphics/SpriteBatchTests.cpp`'s
`SpriteEffectsTest.FlipHorizontallyAndFlipVerticallyAreDifferent` only checks the two individual
values are distinct from each other — no test exercises `DrawString` with a combined-flags value,
which is exactly the scenario needed to catch the array-bounds finding above.

## Positive Findings
The `Begin()` state-defaulting logic (Task 803) and the disposed-texture guard in `pushSprite()`
are both genuine, correctly-reasoned hardening fixes over a literal byte-for-byte FNA port.

## Final Assessment
Two HIGH findings in `DrawString`: (1) a duplicated unchecked-iterator-dereference shared with
`SpriteFont::MeasureString`; (2) an out-of-bounds array read reachable via a real, valid,
XNA-documented combined `SpriteEffects` value that this codebase already demonstrates constructing
elsewhere via manual cast.
