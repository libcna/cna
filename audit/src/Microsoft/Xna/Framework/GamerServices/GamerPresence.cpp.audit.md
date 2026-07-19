# Audit: src/Microsoft/Xna/Framework/GamerServices/GamerPresence.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/GamerServices/GamerPresence.cpp`
- Audit status: AUDITED (full read, 118 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Implements `GamerPresence`'s constructor, `PresenceMode`/`PresenceValue` properties, and the
60-entry `presenceModeStrings_` display-string table `setPresenceModeProperty` indexes into.

## Executive Verdict
One confirmed MEDIUM-severity defect: `presenceModeStrings_` (lines 6-67) is alphabetically sorted
by display string, **not** ordered to match `GamerPresenceMode`'s declared enum ordinals —
`setPresenceModeProperty(value)` indexes it with `static_cast<std::size_t>(value)`, so the string
assigned for nearly every mode is wrong. Currently zero externally observable effect (see Detailed
Findings for why), but a real, systematic data-correctness bug.

## Checklist Results
- Constructor/`CreateInternal`/`getPresenceModeProperty`/`getPresenceValueProperty`: correct.
- `setPresenceModeProperty`/`setPresenceValueProperty`: correct control flow (only calls
  `SetPresenceModeStringEXT` when the resulting string/value actually changes), but see the finding
  below for the underlying table it reads from.

## Detailed Findings

### MEDIUM — `presenceModeStrings_` is sorted alphabetically, not indexed to match
`GamerPresenceMode`'s enum ordinals
`setPresenceModeProperty()` (lines 85-98) does:
```cpp
auto idx = static_cast<std::size_t>(value);
if (idx < presenceModeStrings_.size())
{
    const std::string& s = presenceModeStrings_[idx];
    ...
}
```
— i.e. it assumes `presenceModeStrings_[N]` is the correct display string for
`GamerPresenceMode` ordinal `N`. Directly comparing the 60-entry array (lines 6-67) against the
60-value enum declared in `GamerPresenceMode.hpp` (already audited by this fork) shows the array is
alphabetically sorted by display string (case-insensitive, with punctuation such as hyphens given
low sort weight — e.g. `"Configuring Settings"` sorts before `"Co-Op: Level {0}"`, a
culture-aware-string-comparison signature, not a hand-authored-to-match-the-enum table), while the
enum's declared order groups values by category (game modes, then difficulty, then status) and does
not remotely follow alphabetical order. Cross-referencing every position confirms the mapping is
almost entirely wrong, e.g.:
- `GamerPresenceMode::None` (ordinal 0) → `"Arcade Mode"` (should logically be empty/no presence).
- `GamerPresenceMode::SinglePlayer` (ordinal 1) → `"At Menu"` (should be `"Single Player"`, which is
  actually stored at index 42).
- `GamerPresenceMode::Winning` (ordinal 28) → `""` (the array's one empty entry, which lands there
  purely because alphabetically nothing sorts between `"Nearly Finished"` and `"On a Roll"` — not
  because `Winning` is meant to have no display string; the real `"Winning"` string is stored at
  index 58).
- `GamerPresenceMode::CornflowerBlue` (ordinal 59, the well-known XNA "cheat"/easter-egg presence
  value) → `"Won the Game"` (the real `"Cornflower Blue"` string is stored at index 8).
Only one entry coincidentally lines up: `GamerPresenceMode::ExplorationMode` (ordinal 15) →
`"Exploration Mode"` (index 15) — confirmed via direct enumeration, not a broader pattern.

**Currently zero observable effect**: `GamerPresence.hpp` exposes no public getter for the
underlying `presence_` string at all — only `PresenceMode` (the enum) and `PresenceValue` (the
int). The only consumer of the (wrong) resolved string is `SetPresenceModeStringEXT(presence_)`,
itself a permanent no-op (`void GamerPresence::SetPresenceModeStringEXT(const std::string&
/*mode*/) {}`, documented as "has no effect on non-Xbox platforms"). So today, nothing anywhere
reads or acts on the scrambled string.

**Why this still matters**: the moment either (a) `SetPresenceModeStringEXT` gains a real
platform-specific implementation (e.g. forwarding to a Steam/Discord rich-presence integration, a
plausible and likely future extension point given its very naming as an "EXT" hook), or (b) any
future getter exposes `presence_` directly, every `GamerPresenceMode` value except `ExplorationMode`
would immediately display the wrong text — a silent, hard-to-notice bug (each individual string is
a plausible-looking presence description, just for the wrong mode) rather than a crash.

**Suggested fix** (report-only; no source changes made per this audit's scope): reorder
`presenceModeStrings_` to match `GamerPresenceMode`'s declared enum ordinals exactly (i.e., index 0
should be whatever real XNA's `None` display string is, index 1 `SinglePlayer`'s, etc.) — likely an
`std::unordered_map<GamerPresenceMode, std::string>` or a `switch` would also eliminate the
silent-indexing risk entirely, though a correctly-ordered `std::array` preserves the existing O(1)
lookup shape with a minimal diff.

## Cross-File Observations
See the paired `.hpp` report — the header's contract (no public string getter) is exactly why this
defect is currently dormant.

## Missing or Weak Tests
A test asserting the resolved display string for each `GamerPresenceMode` value (via a test-only
accessor or by capturing `SetPresenceModeStringEXT`'s argument through a subclass/mock) would have
caught this immediately; not found in this pass.

## Positive Findings
Change-detection logic in both setters (only calling `SetPresenceModeStringEXT` when the value
actually changes) is correct and efficient.

## Final Assessment
One MEDIUM finding: `presenceModeStrings_`'s alphabetical ordering does not match
`GamerPresenceMode`'s enum ordinals, scrambling the resolved presence string for 59 of 60 values —
currently dormant (no live consumer) but a real, systematic latent defect.
