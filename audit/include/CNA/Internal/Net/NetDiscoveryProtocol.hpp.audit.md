# Audit: include/CNA/Internal/Net/NetDiscoveryProtocol.hpp

## Metadata
- Source file: `include/CNA/Internal/Net/NetDiscoveryProtocol.hpp`
- Audit status: AUDITED (full read, 74 lines)
- Subsystem: `cna-internal-core` shard (Net)
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA raw-UDP LAN discovery wire format behind `NetworkSession::Find()`
- Main related tests: not independently located in this pass

## Purpose
Declares the Query/Announce message structs and codec for CNA's own raw-UDP LAN discovery protocol (a
separate transport from ENet's connected channels, since discovery must work before any connection exists).

## Executive Verdict
Healthy -- see the paired `.cpp` for verification of this file's genuinely careful adversarial-input
hardening (explicit protocol-version check, negative/oversized property-index rejection).

## Checklist Results
Clearly documents why an explicit tag is needed here (unlike `NetPacketCodec`'s connected-channel messages,
both discovery message types share one raw socket).

## Detailed Findings
None in this header -- see the paired `.cpp`.

## Cross-File Observations
Reuses `NetPacketCodec`'s `PacketWriter`/`PacketReader`-based wire format for consistency even though the
transport differs, a sensible design-reuse choice.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
N/A (see .cpp).

## Final Assessment
No issues found.
