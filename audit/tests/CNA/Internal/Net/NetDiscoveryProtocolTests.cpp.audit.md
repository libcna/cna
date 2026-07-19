# Audit: tests/CNA/Internal/Net/NetDiscoveryProtocolTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Net/NetDiscoveryProtocolTests.cpp` (170 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Net::NetDiscoveryProtocol` (backs
  `Microsoft::Xna::Framework::Net::NetworkSession::Find`'s `SystemLink` LAN discovery wire protocol;
  CNA-internal, no direct FNA equivalent — real XNA LAN discovery is platform-specific)
- Main related tests: complements `ENetDiscoveryServiceTests.cpp`'s system-level malformed-packet
  survival test (cross-referenced explicitly in this file's own comments)

## Purpose
Tests the discovery wire protocol's Query/Announce message encode/decode round-trips (including
sparse optional properties), and — most significantly — a set of security-hardening tests against
crafted/adversarial packets, since LAN discovery is unauthenticated broadcast UDP.

## Executive Verdict
Excellent, security-conscious test file. `DecodeAnnounceRejectsNegativePropertyIndex` documents and
tests a REAL, CONFIRMED-FIXED undefined-behavior vulnerability: a crafted packet with a negative
property index used to reach an unsigned-cast-of-negative out-of-bounds vector access — genuine UB
from untrusted network input, now cleanly rejected with a catchable exception.

## Checklist Results
- `DecodeAnnounceRejectsNegativePropertyIndex` (Task 1.1) and `DecodeAnnounceRejectsHugePropertyIndex`
  (Task 1.2) both correctly use `BuildAnnounceWithRawPropertyIndex`, a helper that deliberately
  constructs a wire-format message `DiscoveryAnnounceMessage::Encode()` itself could never produce
  — explicitly modeling a crafted/corrupted packet from an untrusted source (this file's own
  comment correctly notes LAN discovery is unauthenticated broadcast UDP, so this is a realistic
  threat model, not a hypothetical one).
- `DecodeAnnounceRejectsHugePropertyIndex`'s own comment is a sharp piece of reasoning: an unbounded
  positive index near `INT32_MAX` would make a pre-extend loop call `Add()` ~2 billion times — a
  real hang/OOM vulnerability from a single crafted packet, entirely decoupled from the actual
  buffer size. The test correctly notes its OWN implicit safety net (the suite's overall timeout
  would itself catch a regression-to-hang), while still asserting the direct, specific expected
  outcome (a prompt, clean exception) rather than relying solely on the implicit timeout.
- `DecodeQueryRejectsMismatchedProtocolVersion`/`DecodeAnnounceRejectsMismatchedProtocolVersion`
  (Task 1.6) document and test a real, previously-dormant bug: `ProtocolVersion` was written and
  read back but NEVER actually compared — purely decorative. The tests correctly simulate a
  faithfully-encoded message from a genuinely different protocol version (not a malformed payload),
  which is the correct threat model for this specific defect (a version mismatch from an
  old/new build, not an attacker).
- `DecodeQueryThrowsOnTruncatedBuffer`/`DecodeAnnounceThrowsOnTruncatedBuffer` (Task 5.14) correctly
  assert the specific exception type (`System::IO::EndOfStreamException`, the project's own type,
  not a raw `std::` exception) for a buffer truncated down to just its tag byte — and the file's own
  comment correctly distinguishes this codec-unit-level direct assertion from
  `ENetDiscoveryServiceTests.cpp`'s system-level survival test, avoiding redundant/overlapping
  coverage between the two files.
- `AnnounceRoundtripWithSparseProperties` deliberately tests a sparse pattern (values at indices 0
  and 3, `nullopt` at 1 and 2) rather than only a fully-populated or fully-empty properties list —
  correctly exercising the "with gaps" case that a simpler all-or-nothing test would miss.
- `PeekTagOnEmptyBufferThrows` correctly tests the degenerate empty-buffer case with the correct
  standard exception type (`std::out_of_range`) for what is genuinely a simple bounds check, not a
  parse of structured wire data (a reasonable type choice distinct from the `EndOfStreamException`
  used for the structured-decode truncation cases).

## Detailed Findings
None.

## Cross-File Observations
The truncated-buffer tests here are explicitly and correctly scoped as the codec-unit-level
complement to `ENetDiscoveryServiceTests.cpp`'s system-level malformed-packet-survival test — a
clean, non-duplicated division of responsibility between the two files (this file: does the codec
itself throw cleanly on truncation; the sibling file: does the surrounding system survive that
exception without crashing or wedging).

## Missing or Weak Tests
None identified — the adversarial-input coverage here is unusually complete for a wire-protocol
codec.

## Positive Findings
The negative/huge property-index tests are genuinely strong security-regression tests for real,
previously-exploitable undefined-behavior and hang/OOM vulnerabilities in code that processes
untrusted, unauthenticated broadcast UDP input — exactly the kind of input surface that most
benefits from this level of adversarial test rigor.

## Final Assessment
No findings.
