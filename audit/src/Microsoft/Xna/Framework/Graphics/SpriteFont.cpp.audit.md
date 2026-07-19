# Audit: src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/SpriteFont.cpp` (143 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/SpriteFont.cs`
  (`MeasureString(string)`/`MeasureString(StringBuilder)`)
- Main related tests: not independently located in this pass

## Purpose
Implements the constructor (building `characterIndexMap_` from `characterMap_`) and
`MeasureString`'s text-layout measurement logic.

## Executive Verdict
The constructor and the bulk of `MeasureString`'s layout math (kerning accumulation, line-height
tracking, newline handling) are a correct, faithful port of FNA's real `MeasureString(string)`.
However, `MeasureString` contains a real, HIGH-severity defect: the "character not found, fall back
to the default character" path can dereference an invalid iterator (`unordered_map::end()`) instead
of throwing a clean, catchable error — genuine undefined behavior, and NOT what FNA itself does in
the equivalent situation.

## Checklist Results
- Constructor (lines 12-34): builds `characterIndexMap_` via one pass over `characterMap_`,
  correctly matching FNA's real `for (int i = 0; i < characters.Count; i += 1)
  characterIndexMap[characters[i]] = i;` loop.
- `MeasureString(text)` (lines 71-137): newline/carriage-return handling, kerning accumulation
  (`cKern.X`/`Y`/`Z`), and cropping-height-driven line-height tracking all correctly mirror FNA's
  real algorithm structure and field usage.
- `MeasureString(StringBuilder)` (line 139-142) correctly delegates to the `string` overload via
  `ToString()` — a reasonable simplification of FNA's real (separately-duplicated, to avoid
  StringBuilder-to-string garbage) implementation; acceptable since C++ has no equivalent
  GC-pressure concern to avoid.

## Detailed Findings

### HIGH — Fallback-to-default-character path can dereference `characterIndexMap_.end()` (real UB), where FNA's real equivalent safely throws
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
(lines 101-111). If `c` is not in the map AND `defaultCharacter_` has a value AND that default
character is *also* not present in `characterIndexMap_` (e.g. a `SpriteFont` built by a content
reader/application that sets a default character not actually included in the glyph set — no
validation anywhere in this file or the constructor prevents this), the second `find()` also
returns `end()`, and `it->second` dereferences a past-the-end iterator: undefined behavior (a
`std::unordered_map`'s `end()` iterator does not point to a valid node; dereferencing it is a
real memory-safety violation, not a merely-incorrect-but-safe value).

**FNA comparison**: FNA's real equivalent (`SpriteFont.cs` line 180,
`index = characterIndexMap[DefaultCharacter.Value];`) uses C#'s `Dictionary<TKey,TValue>` indexer,
which — for a genuinely missing key — throws a clean, catchable `KeyNotFoundException`. FNA itself
would still surface an error in this edge case, but a *safe, catchable* one, not undefined behavior.
This is therefore a **genuine, confirmed defect relative to FNA's own real, safe behavior** — not a
preserved-as-is upstream quirk.

**Failure scenario**: any `SpriteFont` constructed with `defaultCharacter` set to a value not
present in `characters` (a real, constructible state — nothing validates this invariant anywhere),
then `MeasureString()` called with any character not in `characters` either, triggers the
UB path.

**Suggested fix** (report-only; no source changes made per this audit's scope): after the second
`find()`, check `it != characterIndexMap_.end()` and throw the same
`std::invalid_argument`/an equivalent to `KeyNotFoundException` if it still fails, rather than
assuming success.

## Cross-File Observations
The identical pattern (same unchecked second `find()` + `it->second` dereference) is duplicated in
`SpriteBatch::DrawString` (`SpriteBatch.cpp` lines 457-465, via the `friend`-granted access to
`spriteFont.characterIndexMap_`) — see that file's audit report for the second instance of this
exact defect and a related, more severe finding (`axisDirX`/`axisDirY` array bounds) in the same
function.

## Missing or Weak Tests
No test was located in this pass exercising a `SpriteFont` whose `defaultCharacter` is itself
absent from `characters` — this is exactly the scenario that would need to be tested to catch the
finding above.

## Positive Findings
The core kerning/line-height measurement algorithm is a careful, correct, line-for-line-equivalent
port of FNA's real logic.

## Final Assessment
One HIGH finding: the default-character fallback path can dereference an invalid map iterator,
which is real undefined behavior and diverges from FNA's own safe (exception-throwing) behavior in
the same edge case. Duplicated in `SpriteBatch::DrawString`.
