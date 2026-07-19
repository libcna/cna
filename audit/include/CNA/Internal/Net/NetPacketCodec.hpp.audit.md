# Audit: include/CNA/Internal/Net/NetPacketCodec.hpp

## Metadata
- Source file: `include/CNA/Internal/Net/NetPacketCodec.hpp`
- Audit status: AUDITED (full read, 145 lines)
- Subsystem: `cna-internal-core` shard (Net)
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA connected-channel wire protocol behind `NetworkSession`'s SystemLink
  transport
- Main related tests: not independently located in this pass

## Purpose
Declares the connected-channel message types (`ClientHello`/`ServerWelcome`/`GamerJoinBroadcast`/
`GamerLeaveBroadcast`/`StateChangeBroadcast`/`AppData`) and their encode/decode, plus the
`SendDataOptions`->ENet-flags mapping.

## Executive Verdict
Healthy -- see the paired `.cpp` for verified-correct bounds safety and one significant finding that spans
this file's dispatch consumer (`ENetBackend.cpp`), documented there.

## Checklist Results
`SendDataOptionsToEnetFlags()`'s doc comment (lines 121-136) is a good example of documenting a genuine,
reasoned mapping choice (why `Chat` maps to `Reliable` despite FNA never implementing real delivery for it)
rather than leaving the choice unexplained.

## Detailed Findings
None in this header. See `ENetBackend.cpp`'s report for a HIGH-severity finding concerning how the messages
declared here are dispatched without sender-authorization checks.

## Cross-File Observations
Built on the already-existing `PacketWriter`/`PacketReader` (thin `System::IO::BinaryWriter`/`BinaryReader`
wrappers) rather than hand-rolled byte packing -- a good consistency choice, and it means this file's own
decode bounds-safety depends on those classes' bounds enforcement (audited separately under the XNA Net API
area, Task #4).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Clean, minimal wire-format design; single-byte count fields inherently cap any `reserve()`-based allocation
from a malicious count (max 255), a good structural property against a large-allocation DoS via a forged
count field.

## Final Assessment
No issues in this header; see `ENetBackend.cpp`'s report for the substantive shard finding.
