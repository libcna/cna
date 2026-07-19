# Audit: include/Microsoft/Xna/Framework/GamerServices/GameUpdateRequiredException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/GameUpdateRequiredException.hpp`
- Audit status: AUDITED (full read, 48 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
The exception thrown when the game requires an update before it can connect (e.g. to a
matchmaking/leaderboard service).

## Executive Verdict
Correct. Derives directly from `System::Exception` with the standard four-constructor set,
matching the same shape as every other exception type in this shard.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Identical structural shape to `GamerServicesNotAvailableException`/`NetworkException` in this
shard.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, complete constructor set.

## Final Assessment
No findings.
