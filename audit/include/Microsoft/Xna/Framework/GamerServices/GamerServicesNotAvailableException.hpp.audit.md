# Audit: include/Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp`
- Audit status: AUDITED (full read, 48 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
The exception thrown when a GamerServices API is called on a platform that does not support it.

## Executive Verdict
Correct. Derives directly from `System::Exception` (this type has no FNA-documented custom base),
with the standard four-constructor set (default, message, message+innerException, protected
serialization).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Same constructor-set shape as `GameUpdateRequiredException`/`NetworkException` in this shard, and
`NetworkSessionJoinException` (already audited, `xna-net` shard) — a consistent exception-authoring
convention across this codebase.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, complete constructor set.

## Final Assessment
No findings.
