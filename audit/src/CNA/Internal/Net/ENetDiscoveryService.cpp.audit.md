# Audit: src/CNA/Internal/Net/ENetDiscoveryService.cpp

## Metadata
- Source file: `src/CNA/Internal/Net/ENetDiscoveryService.cpp`
- Audit status: AUDITED (full read, 384 lines)
- Subsystem: `cna-internal-core` shard (Net)
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements the raw-UDP discovery socket: `EnsureSocket()` (bind with `SO_REUSEADDR`/`SO_BROADCAST`/
non-blocking), `ReplyToQuery()`, `HandleReceived()` (malformed-datagram-tolerant dispatch), `PollOnce()`/
`Poll()`, and `FindSessions()`'s synchronous bounded search window.

## Executive Verdict
Healthy overall -- exceptionally well-reasoned socket/OS-behavior documentation, correctly implemented
malformed-input tolerance and RAII cleanup. One LOW-severity informational finding: the protocol's inherent
request/reply size asymmetry gives it a UDP reflection/amplification character common to LAN discovery
protocols in general, which is unaddressed anywhere in this file's otherwise-exhaustive per-line commentary.

## Checklist Results

### Malformed/hostile-datagram tolerance: correct, matches the file's own stated threat model
`HandleReceived()` (lines 172-246) explicitly notes this datagram arrives over unauthenticated broadcast UDP
from any LAN device (or a spoofed source) and wraps the whole dispatch in a `try`/`catch` that drops the
offending datagram rather than letting a malformed-packet exception propagate into the caller's game loop --
correctly verified: `PeekTag`/`DecodeQuery`/`DecodeAnnounce` all can throw on malformed input (see
`NetDiscoveryProtocol.cpp`'s own hardening), and this is the layer that actually catches it.

### `CurrentResultsGuard`: correct RAII reset even on exception
Explicitly reasoned and independently re-verified: `currentResults_` (a raw pointer into `FindSessions()`'s
own stack-local `results` vector) is reset to null by the guard's destructor on every exit path, including
an exception unwinding past the normal end-of-function reset -- without this, a later `Poll()` call could
write through a dangling pointer into an already-destroyed stack frame.

### Socket setup rationale: unusually well-substantiated
The `SO_REUSEADDR` comment (lines 60-88) doesn't just assert the flag is needed -- it cites a specific,
directly-observed test failure (`TwoProcessLoopbackTest.cpp`, "a plain, non-REUSEADDR blocking socket failed
to bind against a REUSEADDR socket that already held the port") as the actual evidence, and separately
reasons through why arbitrary same-port datagram delivery is a correctly-handled expected case (via the
connect-port dedup logic) rather than a latent bug.

### LOW/informational: inherent reflection/amplification asymmetry, undiscussed elsewhere in the file
A `DiscoveryQueryMessage` is 3 bytes on the wire (tag+version+session-type-filter); a
`DiscoveryAnnounceMessage` reply is substantially larger (tag+version+port+4 int32 fields+a variable-length
gamertag string+session properties) -- at minimum several times the query's size, more with a longer
gamertag or populated properties. `ReplyToQuery()` (lines 128-170) sends this reply to `queryingAddress`
exactly as read off the incoming datagram's source address, with no verification that the query's claimed
source wasn't spoofed. This gives the protocol the same reflection/amplification shape as other well-known
UDP discovery protocols (SSDP/UPnP, mDNS, NTP) that have historically been abused for DDoS amplification: any
device on the LAN (or able to reach the discovery port and spoof a UDP source address) can cause a
registered host to send an amplified reply to an arbitrary third party. This is standard, expected behavior
for a LAN-scoped discovery protocol design (not a defect specific to this implementation), and the practical
blast radius is limited by SystemLink being LAN-only by design (not forwarded across the public internet in
typical deployments) -- flagged here only because, in contrast to every other subtlety in this file (which
gets explicit, reasoned commentary), this one is not discussed anywhere, and a WAN-facing deployment (e.g. a
user forwarding the discovery UDP port) would make it a real, exploitable amplification vector.

## Detailed Findings

1. **[LOW, informational]** Discovery protocol's request/reply size asymmetry gives it a UDP reflection/
   amplification character (see above); undocumented in this file's otherwise-thorough commentary. File:
   `ReplyToQuery()`, lines 128-170.

## Cross-File Observations
Relies on `NetDiscoveryProtocol`'s own explicit hardening (negative/oversized index rejection, version
check) for the actual decode safety; this file's own contribution is the outer malformed-datagram tolerance
and dangling-pointer-safe result collection.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exceptionally well-substantiated systems-programming documentation -- socket-option rationale backed by a
specific cited test observation rather than a generic assertion; correct RAII-based dangling-pointer
prevention across exception paths.

## Final Assessment
One LOW-severity informational finding (inherent UDP-discovery reflection/amplification characteristic,
standard for this class of protocol, worth an explicit note given the port is user-forwardable in principle).
