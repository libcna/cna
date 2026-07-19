# Audit: src/Microsoft/Xna/Framework/Graphics/SamplerStateCollection.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/SamplerStateCollection.cpp` (30 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `Graphics/SamplerStateCollection.cs` (fully diffed)
- Main related tests: not independently located in this pass

## Purpose
Implements the constructor (initializing all 16 slots to `SamplerState::LinearWrap`, matching FNA) and both `operator[]` overloads.

## Executive Verdict
Correct bounds-checking behavior, but uses the wrong exception type per this project's own established convention (see Detailed Findings). Constructor default (`LinearWrap` for every slot) matches FNA's `SamplerStateCollection` constructor exactly.

## Checklist Results
- Bounds check (`index < 0 || index >= MaxSamplers`) present and correct on both the const and non-const `operator[]` overloads.
- Exception-type convention: **violated** — see below.

## Detailed Findings

### LOW-MEDIUM — `operator[]` (both overloads) throws raw `std::out_of_range` instead of this project's own `System::ArgumentOutOfRangeException`
Lines 17 and 26: `throw std::out_of_range("Sampler index out of range.");` in both the mutable and const `operator[]`. This project's established convention (confirmed repeatedly across every shard audited this session — `xna-net`, `xna-gamerservices`) is to use the project's own sharp-runtime exception types (`System::ArgumentOutOfRangeException`, `ArgumentException`, `ObjectDisposedException`, `NotSupportedException`) at API boundaries rather than raw `std::` exceptions, so that a game catching `System::Exception`-derived types (the idiomatic XNA/C# pattern this project's own exception hierarchy is built to support) can actually catch an out-of-range sampler index. A raw `std::out_of_range` bypasses that hierarchy entirely.
- Severity: LOW-MEDIUM (consistent with how this same pattern has been rated elsewhere in this audit — a real API-consistency/catchability gap, not a crash or data-corruption risk)
- Location: `SamplerStateCollection.cpp:17,26`

## Cross-File Observations
See the paired `.hpp` report for the more significant (LOW-MEDIUM) missing-dirty-tracking finding relative to FNA's real `SamplerStateCollection`.

## Missing or Weak Tests
A test asserting the correct exception *type* (not just that *some* exception is thrown) for an out-of-range index would have caught this; not independently located in this pass.

## Positive Findings
Bounds-checking logic itself is correct on both overloads.

## Final Assessment
One LOW-MEDIUM finding: wrong exception type (`std::out_of_range` instead of `System::ArgumentOutOfRangeException`) on both `operator[]` overloads.
