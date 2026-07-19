# Audit: src/CNA/Internal/GamerServices/LocalGamerServicesStore.cpp

## Metadata
- Source file: `src/CNA/Internal/GamerServices/LocalGamerServicesStore.cpp`
- Audit status: AUDITED (full read, 418 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: implements local achievement/leaderboard persistence for `Microsoft::Xna::Framework::GamerServices`
- Main related tests: consumed by `LeaderboardWriter.cpp`/`LeaderboardReader.cpp` (confirmed via grep)

## Purpose
Implements JSON-file read/write for achievements and leaderboards, including a torn-write-safe
write-to-temp-then-rename pattern and a `PropertyDictionary`<->JSON column serializer covering all 8 concrete
value types `PropertyDictionary::SetValue()` supports.

## Executive Verdict
Needs attention — 1 confirmed, low-severity latent finding (sanitizer path-traversal weakness, not actively
exploitable at current call sites); otherwise careful, well-documented, correct implementation.

## Checklist Results

### Confirmed: `SanitizeStoreFileNameComponent()` allows a bare "." or ".." through unchanged
The whitelist (`isalnum || '-' || '_' || '.'`) includes the period character with no special-casing for an
all-period result — `SanitizeStoreFileNameComponent("..")` returns `".."` verbatim, a valid directory-traversal
token if ever used as a bare path component. **Confirmed NOT actively exploitable at either real call site**
in this codebase (verified via grep across `LeaderboardWriter.cpp`/`LeaderboardReader.cpp`): `fileKey` always
comes from `MakeLeaderboardFileKeyEXT()`, which unconditionally appends `"_" + gameMode` after sanitizing,
and `gamertag`-derived paths always have `.json` appended immediately after sanitizing in this file's own 2
achievement call sites — so in practice a pure ".." can never reach `fs::path` as its own component. Still a
latent gap relative to the function's own name/contract ("safe to use as a single path component") should a
future caller use it standalone.

### Confirmed correct: torn-write protection
`WriteJsonFile()`'s temp-file-then-rename pattern is correctly implemented, with an explicit fallback to a
direct write if `fs::rename()` fails (e.g. a cross-filesystem temp directory) — the fallback still writes
correctly, just without the atomicity guarantee, rather than silently losing the update.

### Confirmed correct: "never throws on corrupt/missing" contract
`TryReadJsonFile()` catches `JsonParseException` and returns `std::nullopt` rather than propagating, and
every `Load*EXT()` function correctly returns an empty/default result rather than throwing when the file is
missing, corrupt, or has an unexpected JSON shape (checked at every `FindMember`/type-check site) — matches
the header's own documented contract exactly.

### Confirmed correct: PropertyDictionary<->JSON round-trip
`ColumnsToJson()`/`JsonToColumns()` correctly cover all 8 concrete `std::any` types `PropertyDictionary`
supports (int/long long/double/float/string/DateTime/TimeSpan/LeaderboardOutcome), each round-tripped via
`DateTime`/`TimeSpan`'s own tick-count representation (exact, no string-formatting precision loss) rather than
a lossy conversion. `Stream*` values are deliberately and correctly skipped (a live I/O handle has no
meaningful persisted form) rather than erroring.

### C++ correctness / Memory/resource lifetime / Performance / Portability / Maintainability / Robustness
No other issues found.

## Detailed Findings
1 low-severity, non-actively-exploitable finding (sanitizer's own bare "."/".." passthrough).

## Cross-File Observations
Cross-checked both real consumers (`LeaderboardWriter.cpp`/`LeaderboardReader.cpp`) to confirm the
sanitizer weakness above is not currently reachable.

## Missing or Weak Tests
No dedicated test found exercising the sanitizer's own edge cases (empty string, all-period string) in
isolation.

## Positive Findings
Genuinely careful torn-write protection with a sensible fallback; a complete, precise (tick-based, not
string-based) PropertyDictionary<->JSON round-trip; consistent "never throws on corrupt data" contract
enforcement throughout.

## Final Assessment
1 low-severity, latent (not actively exploitable) finding; otherwise correct, careful implementation.
