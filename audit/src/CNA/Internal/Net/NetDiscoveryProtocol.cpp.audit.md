# Audit: src/CNA/Internal/Net/NetDiscoveryProtocol.cpp

## Metadata
- Source file: `src/CNA/Internal/Net/NetDiscoveryProtocol.cpp`
- Audit status: AUDITED (full read, 163 lines)
- Subsystem: `cna-internal-core` shard (Net)
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements the Query/Announce encode/decode, including `NetworkSessionProperties`' sparse
`(index, value)`-pair wire representation.

## Executive Verdict
Healthy -- genuinely careful, explicit adversarial-input hardening, independently verified correct.

## Checklist Results

### Adversarial-input hardening: correctly implemented, not merely asserted
`ReadProperties()` (lines 60-99) is exactly the kind of defensive parsing this whole discovery layer needs,
since these datagrams arrive over unauthenticated broadcast UDP from any LAN device: a negative property
index is explicitly rejected (lines 75-78) before it could otherwise reach
`NetworkSessionProperties::operator[]` with a huge, wrapped `std::size_t` (a real out-of-bounds-write
vector the comment traces precisely, not hand-waved); an oversized positive index (up to `kMaxPropertyIndex
= 256`, comfortably above any real game's actual property count) is rejected before a `presentCount`-driven
pre-extend loop could otherwise `Add()` up to ~2 billion times from a single crafted packet (a genuine
hang/OOM DoS vector, also correctly reasoned through in the comment). `ValidateProtocolVersion()` correctly
rejects a mismatched version up front rather than parsing the remainder of a possibly-incompatible payload
as if it were the current format.

### `presentCount`'s own DoS exposure: correctly reasoned as bounded elsewhere
`presentCount` itself (an attacker-controlled `int32_t`) is not independently capped, but the loop it
drives calls `reader.ReadInt32()` twice per iteration -- since the underlying `PacketReader` throws on
buffer underflow (matching .NET's `BinaryReader` contract, verified as this file's own stated assumption),
an oversized claimed `presentCount` against a genuinely short packet terminates via exception at the first
short read, bounding the loop by the packet's *actual* size rather than its claimed count. This assumption
depends on `PacketReader`'s own bounds-enforcing behavior, audited separately under the XNA Net API area
(Task #4) -- flagged here as a cross-file dependency worth confirming there, not a gap in this file itself.

## Detailed Findings
None in this file.

## Cross-File Observations
This file's hardening (explicit negative/oversized-index rejection, explicit version check) is a strong,
positive contrast to `AudioTagParser.cpp` (same overall audit, different subsystem) which uses the more
common but width-dependent `pos + len > bound` idiom instead of this kind of explicit-domain validation.

## Missing or Weak Tests
Not independently located in this pass; a fuzz-style test feeding negative/oversized property indices
directly to `ReadProperties()` would independently confirm the reasoning documented in the source comments.

## Positive Findings
Rigorous, correctly-reasoned defense against a genuinely adversarial input surface (unauthenticated
broadcast UDP) -- explicit rejection of negative and oversized indices with the exact failure mode each
would otherwise cause spelled out in the comments, not just patched reactively.

## Final Assessment
No issues found.
