# Audit: src/Microsoft/Xna/Framework/Net/AvailableNetworkSession.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Net/AvailableNetworkSession.cpp`
- Audit status: AUDITED (full read, 80 lines)
- Subsystem: `xna-net` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace
- Main related tests: not independently located in this pass

## Purpose
Implements the private constructor, `CreateInternal`, every getter, and `operator==`/`operator!=`.

## Executive Verdict
Correct. Every constructor parameter maps to its matching member 1:1; `operator==` compares
exactly the fields its header doc comment claims (`currentGamerCount_`, `hostGamertag_`,
`openPrivateGamerSlots_`, `openPublicGamerSlots_`, `hostAddress_`, `hostPort_`, `sessionType_`) and
correctly excludes `sessionProperties_`/`qualityOfService_`.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`std::move` correctly used for the non-trivially-copyable `properties`/`qos` constructor
parameters in both the constructor and `CreateInternal`.

## Final Assessment
No findings.
