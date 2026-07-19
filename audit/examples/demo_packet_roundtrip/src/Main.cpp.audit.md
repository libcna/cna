# Audit: examples/demo_packet_roundtrip/src/Main.cpp

## Metadata
- Source file: `examples/demo_packet_roundtrip/src/Main.cpp` (207 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_packet_roundtrip` shard
- File type: standalone console demo executable (Task 15.2)
- XNA/FNA relevance: exercises `Microsoft::Xna::Framework::Net::PacketWriter`/`PacketReader`'s full
  `Write`/`Read` pair set (`float`, `double`, `Vector2/3/4`, `Quaternion`, `Matrix`, `Color`)
- Related production code: `PacketReader.hpp`/`.cpp`, `PacketWriter.hpp`/`.cpp` (already audited
  this session as part of the `xna-net` shard)

## Purpose
Writes a random value of every XNA-type `PacketWriter::Write`/`PacketReader::Read` pair into one
`PacketWriter`, drains it to raw bytes the same way `LocalNetworkGamer::SendData`/`ReceiveData`
would for a real network hop, reloads them into a fresh `PacketReader`, and confirms each value
round-trips.

## Executive Verdict
Correct, and directly corroborates this session's own `xna-net` shard finding about the `Color`
read/write asymmetry (`PacketWriter::Write(Color)` writes 4 bytes; `PacketReader::ReadColor()`
reads 4 floats). This demo goes further than that audit did: it explicitly demonstrates the
asymmetry is not just a documentation note but has a real, verified runtime consequence — reading
a `Write(Color)`-written buffer back via `ReadColor()` throws (buffer underrun: 4 bytes written, 16
needed), rather than silently returning garbage.

## Checklist Results
- Correctly avoids C++'s unspecified-argument-evaluation-order pitfall: the Color-matched-pairing
  block (lines 158-165) reads all four `ReadByte()` calls into named locals *before* passing them
  to the `Color` constructor, with an inline comment explicitly explaining why (constructing
  `Color(reader.ReadByte(), reader.ReadByte(), ...)` directly would consume the stream in an
  unspecified, observed-reversed order). This is a genuine, non-obvious correctness point most
  authors would miss.
- The deliberately-mismatched `Color`/`ReadColor()` pairing (lines 177-201) correctly treats either
  outcome (a throw, or a coincidentally-succeeding read) as a "QUIRK" pass rather than a "FAIL" —
  appropriately modeling that this is a documented, accepted upstream asymmetry, not a regression
  to catch.
- Every other type pair (float, double, Vector2/3/4, Quaternion, Matrix) uses direct value equality
  after a round trip through real byte serialization — appropriate given `PacketWriter`/
  `PacketReader` write/read exact IEEE-754 bit patterns with no lossy transformation in between.

## Detailed Findings
None.

## Cross-File Observations
This demo's own comment (lines 170-176) independently cites the same real, well-known XNA quirk
already documented in `audit/include/Microsoft/Xna/Framework/Net/PacketReader.hpp.audit.md` and
`PacketWriter.hpp.audit.md` from the parallel `xna-net` audit this session, including a claimed
verbatim FNA source comment ("`// FIXME: Only using floats because of the overloads...? -flibit`")
— unverifiable against the local FNA reference tree (confirmed empty for this namespace) but
consistent with, and additional corroborating evidence for, the same conclusion reached
independently from the production source alone.

## Missing or Weak Tests
This is itself a demo/smoke-test with its own pass/fail accounting (`gPass`/`gTotal`, non-zero
exit code on any unexpected failure) — effectively already a regression test, assuming it's wired
into CI (not verified in this pass).

## Positive Findings
A genuinely well-constructed, self-verifying round-trip demonstration that correctly anticipates
and defends against a real C++ evaluation-order pitfall, and correctly frames a known upstream
quirk as expected behavior rather than papering over it.

## Final Assessment
No findings.
