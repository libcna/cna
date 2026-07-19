# Audit: tests/Microsoft/Xna/Framework/Net/LocalNetworkGamerTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Net/LocalNetworkGamerTests.cpp` (150 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-net` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `LocalNetworkGamer.hpp`/`.cpp`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises `LocalNetworkGamer`'s full `SendData`/`ReceiveData` overload set (byte-array and
`PacketWriter`/`PacketReader` variants, with/without offset+count/recipient) via a `Local`-type
session fixture.

## Executive Verdict
Thorough, with real regression tests for two confirmed prior UB fixes (Task 2.8/2.9 offset/count
bounds checks) that construct scenarios specifically shaped to trigger the original defect.

## Checklist Results
- `ReceiveDataWithOffsetThrowsInsteadOfWritingPastBufferEnd`'s own comment precisely explains the
  original bug (`len` computed via `min(packet.size(), data.size())`, ignoring `offset`, so
  `offset + len > data.size()` silently wrote past the buffer) and constructs an exact scenario
  (`offset=5`, buffer size 10, packet size 8 → `len=8`, `5+8=13 > 10`) that would trigger it —
  genuinely targeted, not just a generic bounds test.
- `SendDataThrowsWhenOffsetPlusCountExceedsBuffer`/`SendDataToRecipientThrowsWhenOffsetPlusCountExceedsBuffer`
  similarly target the Task 2.9 fix directly.
- `SendDataThenReceiveDataRoundtrip`'s own comment correctly explains why `IsDataAvailable` stays
  false for a `Local`-type session (the real `PacketSend` handling is gated behind
  `ENetBackend::RealNetworkingEnabled()`, false for `Local`) and points to a separate test suite
  (`ENetBackendTest`) for the real `SystemLink` path — an accurate, non-misleading test name given
  what it actually proves.
- `ReceiveDataIntoPacketReaderReturnsZero`'s test name and body are consistent with the confirmed
  production finding that this overload always returns 0 (a preserved FNA quirk) — though this
  specific test only exercises the "no packet queued" early-return path, not the "packet queued but
  still returns 0 due to the unset `len`" path documented in the production audit. See Missing or
  Weak Tests.

## Detailed Findings
None.

## Cross-File Observations
Confirms, via `~LocalGamerFixture() { session->Dispose(); }`, the same `Dispose()`-without-`delete`
pattern already noted across multiple example demos this session — here it's arguably correct
usage though, since the fixture never transfers ownership of `session` anywhere else and the test
binary's process lifetime makes the leak moot; still, a `unique_ptr`-based fixture would be
slightly more idiomatic.

## Missing or Weak Tests
`ReceiveDataIntoPacketReaderReturnsZero`'s name implies coverage of the "always returns 0" quirk,
but only exercises the trivial empty-queue path (which correctly returns 0 for an unrelated reason —
no packet was ever available). A test that enqueues a real packet first, then confirms the return
value is *still* 0 despite data genuinely having been written to the `PacketReader`, would be a
stronger, more direct proof of the documented quirk (the header's own doc comment: "FNA declares a
length variable that is never updated before being returned").

## Positive Findings
The two offset/count boundary tests are excellent, precisely-targeted regression tests for real,
previously-confirmed UB fixes.

## Final Assessment
No MEDIUM+ findings; one minor test-coverage improvement opportunity noted above (LOW).
