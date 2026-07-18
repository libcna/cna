# Audit: examples/sprite_font_test.cpp

## Metadata

- Source file: `examples/sprite_font_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard by manifest assignment; registration is EasyGL-only
  (`cmake/Tests/EasyGLTests.cmake:200-203`, `EasyGL_SpriteFont_Properties`), but the file itself is
  genuinely backend-agnostic: it does no drawing/pixel-readback, only `SpriteFont::MeasureString`
  and property get/set logic against a real `GraphicsDevice`-backed `Texture2D` atlas.
- XNA/FNA relevance: direct — `SpriteFont.MeasureString`, `LineSpacing`, `Spacing`,
  `DefaultCharacter`, `Characters`.
- FNA reference: `Graphics/SpriteFont.cs` (`MeasureString(string)`, lines 127-218).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp`
  (`MeasureString`, lines 71-137), `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp`
  (`DrawString`'s identical character-resolution logic, lines ~445-465).

## Purpose

Constructs two minimal `SpriteFont` instances (a throwaway 3-character one, then a 4-character one
actually used for assertions) and tests `MeasureString` against 5 scenarios (empty string, single
char, two chars, an embedded newline, and an unresolvable character falling back to
`DefaultCharacter`), plus `LineSpacing`/`Spacing`/`DefaultCharacter`/`Characters` get/set behavior.

## Executive Verdict

**Needs attention** — every `MeasureString` numeric assertion in this file was independently
re-derived by hand against both FNA's real C# algorithm and CNA's own implementation, and all are
correct; `SpriteFont::MeasureString` is confirmed to be a faithful line-for-line port of FNA's
algorithm. However, this audit's line-by-line trace of the same production code this file
exercises (`SpriteFont.cpp`'s character-resolution logic) found a **genuine, reachable
undefined-behavior defect** — an unchecked `unordered_map::end()` dereference when
`DefaultCharacter` is set to a character not present in the font's own character list — that this
test's own `check()` sequence sets up the exact vulnerable precondition for (line 147,
`f.setDefaultCharacterProperty(std::optional<charcs>(u'*'))`, where `'*'` is not one of the four
characters `f` was constructed with) and then stops one call short of triggering it.

## Checklist Results

### API / XNA / FNA parity
`SpriteFont(atlas, bounds, cropping, chars, lineSpacing, spacing, kerning, defaultCharacter)`
constructor signature (lines 65-66, 82-84) matches CNA's `SpriteFont.cpp:12-19`.
`MeasureString(const String&)` (called throughout) matches FNA's `MeasureString(string)` signature
in shape (FNA additionally has a `StringBuilder` overload, mirrored by
`SpriteFont::MeasureString(const System::Text::StringBuilder&)` at `SpriteFont.cpp:139-142`, not
exercised by this file but not required to be — see Missing Tests).

### Behavioral correctness — independently re-derived every numeric assertion
Read `SpriteFont::MeasureString` (`SpriteFont.cpp:71-137`) and FNA's `SpriteFont.cs:127-218` side
by side; confirmed CNA's implementation is a faithful port (same `firstInLine`/`curLineWidth`/
`finalLineHeight` state machine, same `Math.Abs(cKern.X)` for the first character in a line vs.
`Spacing + cKern.X` otherwise, same `\r`-skip/`\n`-reset handling). Re-derived by hand:
- `MeasureString("")` → `(0,0)` (line 89): matches `SpriteFont.cpp:73-76`'s explicit
  empty-string-returns-`Vector2::Zero` early exit, matching FNA's identical `text.Length==0` guard.
- `MeasureString("A")` → `(12,24)` (lines 97-99): kerning `A=(0,10,2)` →
  `curLineWidth=|0|+10+2=12`; cropping height 20 < `lineSpacing=24`, so `finalLineHeight` stays 24.
  Confirmed correct against the algorithm.
- `MeasureString("AB")` → `(22,24)` (lines 108-110): `A` contributes 12; `B` (not first)
  contributes `spacing(0)+kernX(1)+kernY(8)+kernZ(1)=10` → `12+10=22`; heights (20,18) both < 24.
  Confirmed correct.
- `MeasureString("A\nB")` → `(12,48)` (lines 119-121): line 1 = 12 (height 24 via `lineSpacing`,
  since croppingA=20<24); `\n` resets and adds 24 to `result.Y`; line 2 `B` first-in-line:
  `|1|+8+1=10`; final `result.X=max(12,10)=12`, `result.Y=24+24=48`. Confirmed correct.
- `MeasureString("Z")` → `(8,24)` (lines 127-129): `'Z'` unresolved → falls back to `'?'`
  kerning `(0,8,0)` → `curLineWidth=|0|+8+0=8`; cropping height for `'?'` is 16 < 24. Confirmed
  correct.

All five derivations independently confirm the test's own comments (lines 86-130), which are
themselves accurate — this is a genuinely well-verified, not merely self-consistent, test for its
stated scope.

### Logic — the defect this audit found by tracing the character-resolution fallback path
`SpriteFont::MeasureString`'s unresolved-character handling (`SpriteFont.cpp:101-111`):
```cpp
auto it = characterIndexMap_.find(c);
if (it == characterIndexMap_.end())
{
    if (!defaultCharacter_.has_value())
    {
        throw std::invalid_argument(
            "Text contains characters that cannot be resolved by this SpriteFont.");
    }
    it = characterIndexMap_.find(defaultCharacter_.value());
}
const int index = it->second;
```
If `defaultCharacter_.has_value()` is `true` but `defaultCharacter_.value()` is **not itself** a
key in `characterIndexMap_`, the second `find()` also returns `end()`, and `it->second` is then
dereferenced on an `end()` iterator with **no check** — undefined behavior (in practice, a crash or
a garbage `index` used to index `kerning_`/`croppingData_`, itself further UB via out-of-bounds
`std::vector` access). The identical pattern, with the identical missing check, exists in
`SpriteBatch::DrawString` (`SpriteBatch.cpp:457-465`), so this defect is not confined to
`MeasureString` — it affects the actual glyph-rendering path too.

FNA's equivalent (`SpriteFont.cs:180`, `index = characterIndexMap[DefaultCharacter.Value];`) uses
C#'s `Dictionary<TKey,TValue>` indexer, which throws a clean, catchable `KeyNotFoundException` if
the key is absent — FNA has **defined, exception-based** behavior for this exact scenario; CNA has
undefined behavior. This is a genuine FNA-parity gap in the underlying `SpriteFont`/`SpriteBatch`
production code (`CLAUDE.md`'s own "Exception behavior where practical" guidance), surfaced by
tracing this test file's own setup.

`setDefaultCharacterProperty(std::optional<charcs>)` (`SpriteFont.hpp:68`, `SpriteFont.cpp:46-49`)
performs **no validation at all** that the assigned character is actually present in the font's own
`characterMap_` — it is a fully public setter, directly reachable from any game code, with no
documented precondition in either the FNA reference or the CNA header comment warning callers
against this.

### Testing — this file sets up the exact vulnerable precondition and stops one call short
Lines 145-148:
```cpp
f.setDefaultCharacterProperty(std::nullopt);
check(!f.getDefaultCharacterProperty().has_value(), "DefaultCharacter nullopt");
f.setDefaultCharacterProperty(std::optional<charcs>(u'*'));
check(f.getDefaultCharacterProperty().value() == u'*', "DefaultCharacter set '*'");
```
`f` was constructed (line 82) with `chars2 = {u' ', u'?', u'A', u'B'}` — `'*'` is **not** one of
them. Setting `DefaultCharacter` to `'*'` here creates exactly the precondition this audit traced
as vulnerable above. The test only checks the getter round-trip (line 148) and never follows up
with a `MeasureString()` call containing an unresolvable character (e.g. `f.MeasureString("Z")`,
already used earlier in the file at line 127 for a *different* purpose, while `DefaultCharacter`
was still `'?'` — a valid character at that point in the file's execution order). Had the test
called `f.MeasureString("Z")` (or any other unresolved character) after line 147, it would have
hit exactly the `it->second`-on-`end()` UB traced above.

## Detailed Findings

### F1 — SpriteFont/SpriteBatch dereference an `unordered_map::end()` iterator with no check when `DefaultCharacter` is set to a character absent from the font's own character list, an undefined-behavior crash risk reachable through fully public API
- Severity: HIGH
- Confidence: HIGH (traced the exact code path in both `SpriteFont.cpp:101-111` and
  `SpriteBatch.cpp:457-465`; confirmed FNA's equivalent throws a clean, catchable exception instead;
  confirmed this test file's own line 147 creates the exact vulnerable precondition)
- Category: correctness / undefined-behavior / FNA-parity
- Location: `src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp:101-111` (`MeasureString`);
  `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp:457-465` (`DrawString`); test precondition
  at `examples/sprite_font_test.cpp:147`
- Evidence: see the Logic section above for the full code excerpt and side-by-side FNA comparison.
- Why it matters: any game (or any test in this codebase) that calls
  `spriteFont.setDefaultCharacterProperty(someChar)` where `someChar` was never part of that font's
  atlas — a plausible content-authoring mistake, not a contrived adversarial input — followed by
  measuring or drawing any string containing a character outside the font's own set, hits undefined
  behavior instead of FNA's clean `KeyNotFoundException`. This is reachable purely through public
  API misuse with no adversarial intent required, and affects both `MeasureString` (silent
  corruption/crash) and `DrawString` (the actual rendering path, same defect).
- FNA/XNA comparison: FNA's `Dictionary<TKey,TValue>` indexer throws `KeyNotFoundException` for
  this exact scenario (`SpriteFont.cs:180`/`279` in both `MeasureString` overloads, and the
  equivalent in `SpriteBatch.DrawString`'s character resolution) — CNA has no equivalent guard.
- Suggested follow-up (not implemented by this audit — this is an AUDIT-ONLY task): add a check
  after the fallback `find()` in both `SpriteFont::MeasureString` and `SpriteBatch::DrawString`
  that throws a clear exception (e.g. `std::invalid_argument` or `std::out_of_range`, matching this
  project's established convention) when the fallback lookup itself also misses, rather than
  falling through to an unchecked `it->second`. This test file would then be one line away
  (`f.MeasureString("Z")` immediately after line 147) from directly proving the fix.

## Cross-File Observations

- The identical unchecked-fallback pattern exists in `SpriteBatch::DrawString`
  (`SpriteBatch.cpp:457-465`), confirmed by direct reading — this is a shared, two-call-site defect
  in the production `SpriteFont`/`SpriteBatch` pairing, not isolated to the `MeasureString` method
  this test file directly exercises.
- This finding was discovered specifically because this audit traced *why* the test's own setup
  sequence (lines 145-148) looked like it was building toward an edge-case check that never
  actually arrives — a useful example of a test's own structure hinting at an untested, and in this
  case genuinely broken, code path.

## Missing or Weak Tests

- See F1 — the single most valuable missing test in this file is a `MeasureString()` (or
  `DrawString`, out of scope for this file) call with an unresolvable character *while*
  `DefaultCharacter` is set to a character also absent from the font's map — exactly the scenario
  this file's own line 147 sets up and does not follow through on.
- `MeasureString(const System::Text::StringBuilder&)` overload (`SpriteFont.cpp:139-142`) has no
  test in this file (only the `String`/`const char*`-literal overload is exercised) — a minor gap
  per `CLAUDE.md`'s "every method overload must be covered by at least one test case," though the
  `StringBuilder` overload is a one-line forward to the tested overload (`return
  MeasureString(text.ToString());`), so the risk of an undetected regression there is low.
- No test for `SpriteFont(...)`'s constructor with mismatched-length vectors (e.g.
  `characters.size() != kerningData.size()`) — the constructor performs no such validation
  (`SpriteFont.cpp:12-34`), which could itself be a separate latent out-of-bounds risk, though this
  audit did not trace it as thoroughly as the `DefaultCharacter` fallback case above since no test
  in this file approaches that precondition.

## Positive Findings

- Every `MeasureString` numeric expectation in this file was independently re-derived and confirmed
  correct against both FNA's real C# algorithm and CNA's own faithful line-for-line port — genuine,
  non-boilerplate verification, not just internal self-consistency.
- Good discriminating design for the fallback-to-`DefaultCharacter` scenario in the *valid* case
  (`'Z'` falling back to `'?'`, which **is** present in the map) — this part is correctly proven.
- `LineSpacing`/`Spacing`/`DefaultCharacter`/`Characters` getter/setter round-trips are all simple,
  correct, and proportionate to their triviality.

## Final Assessment

A well-verified test for its stated happy-path scope, whose own setup sequence exposed — via this
audit's trace of the production code it exercises — a genuine, reachable undefined-behavior defect
in `SpriteFont::MeasureString` and `SpriteBatch::DrawString` when `DefaultCharacter` is set to a
character outside the font's own character list. The test itself sets up the vulnerable
precondition (line 147) and stops one call short of triggering it — the single highest-value
follow-up test for this file is exactly the call it doesn't make. A minor, unrelated dead-code
observation: the file constructs a throwaway 3-character `SpriteFont font` (lines 65-66) that is
never referenced again after construction (all assertions use the separately-constructed
4-character `f`) — harmless but worth removing for clarity.
