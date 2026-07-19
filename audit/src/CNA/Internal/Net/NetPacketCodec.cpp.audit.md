# Audit: src/CNA/Internal/Net/NetPacketCodec.cpp

## Metadata
- Source file: `src/CNA/Internal/Net/NetPacketCodec.cpp`
- Audit status: AUDITED (full read, 286 lines)
- Subsystem: `cna-internal-core` shard (Net)
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements encode/decode for all 6 connected-channel message types, `EncodeCount()`'s explicit 255-entry
wire-format guard, and `SendDataOptionsToEnetFlags()`.

## Executive Verdict
Healthy in isolation -- correctly bounds-checked decode logic, with a good defensive `EncodeCount()` guard
against silent wraparound. This file's messages are the ones affected by the HIGH-severity finding
documented in `ENetBackend.cpp` (this file only encodes/decodes; the dispatch-without-authorization gap is
in the consumer).

## Checklist Results

### `EncodeCount()`: a genuinely useful, explicitly-reasoned defensive guard
Correctly rejects (rather than silently truncating) a collection whose size exceeds the wire format's
single-byte count capacity (lines 38-47) -- the comment explicitly traces the failure mode this prevents
(a naive `static_cast<bytecs>(size)` wrapping 256 to 0 while the full untruncated collection still gets
serialized right after, desynchronizing the decoder) even though it's currently unreachable via any real
join/leave flow given `NetworkSession::MaxSupportedGamers == 31`. Guarding explicitly rather than relying on
that invariant forever is the right call.

### Decode bounds safety: correctly delegated
Every decode function relies on `PacketReader`'s own bounds enforcement (matching .NET `BinaryReader`'s
throw-on-underflow contract) rather than re-implementing length checks -- consistent, and correctly noted
as a cross-file dependency in this shard's other Net reports.

### `DecodeAppData()`'s payload sizing: correct
`remaining = getLengthProperty() - getPositionProperty()` is clamped to `>= 0` before use (line 260) --
correctly defensive even though a negative value shouldn't arise from normal reads.

### `SendDataOptionsToEnetFlags()`: safe default for an out-of-range enum value
The `switch` covers every defined `SendDataOptions` value explicitly; the post-switch fallback return
(`ENET_PACKET_FLAG_RELIABLE`) correctly handles an invalid enum value that could arise from
`static_cast<SendDataOptions>(reader.ReadByte())` on a malformed/crafted `AppData` message's `Options` byte
(see `DecodeAppData`, line 257) -- defaults to the *safest* delivery guarantee rather than undefined
behavior.

## Detailed Findings
None new in this file -- see `ENetBackend.cpp`'s report for the HIGH-severity finding about how the codec's
`ServerWelcome`/`GamerJoinBroadcast`/`GamerLeaveBroadcast`/`StateChangeBroadcast` messages are dispatched
without verifying the sending peer is authorized to send them.

## Cross-File Observations
See `ENetBackend.cpp`'s report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`EncodeCount()`'s explicit, reasoned guard against a currently-unreachable-but-real wraparound bug is a
good example of defending an invariant explicitly rather than trusting it to hold forever by convention.

## Final Assessment
No issues in this file's own encode/decode logic; see `ENetBackend.cpp` for the shard's substantive finding
concerning how these messages are consumed.
