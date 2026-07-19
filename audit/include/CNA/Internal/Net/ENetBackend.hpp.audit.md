# Audit: include/CNA/Internal/Net/ENetBackend.hpp

## Metadata
- Source file: `include/CNA/Internal/Net/ENetBackend.hpp`
- Audit status: AUDITED (full read, 255 lines)
- Subsystem: `cna-internal-core` shard (Net)
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA static facade wiring `NetworkSession` to the real ENet transport
- Main related tests: not independently located in this pass (referenced by name: `ENetBackendTests.cpp`,
  `ENetDiscoveryServiceTest`, `TwoProcessLoopbackTest.cpp`)
- Prior dedicated audit history: this exact subsystem has already been through multiple documented
  remediation rounds tracked outside this repo-wide audit (`audit_net.md`, referenced directly in this
  file's own comments as "2026-07-18" fixes, "third round"); this pass is an independent re-review layered
  on top of that history, not a first look.

## Purpose
Declares the process-wide static facade between `NetworkSession` and the real ENet transport: hosting,
connecting, per-session transport-state teardown/pumping, AppData relay, host migration, and several
test-only observability/determinism hooks (clock override, seeded RNG, drop counters).

## Executive Verdict
Needs attention -- one HIGH-severity finding (documented in the paired `.cpp`, since it's a property of the
dispatch logic there, not this header): the host accepts `ServerWelcome`/`GamerJoinBroadcast`/
`GamerLeaveBroadcast`/`StateChangeBroadcast` messages from **any** connected peer with no verification that
the sender is authorized to send that message type, letting any connected client forge host-only broadcast
messages. Everything else in this header -- and the exhaustively-documented single-threaded contract,
test-determinism hooks, and API surface -- is sound.

## Checklist Results

### Documentation quality: matches `VideoDecoder.hpp`'s density of prior-fix context
`HasAudio()`-style historical context is mirrored here for `GetDroppedAppDataCount()` (lines 145-167,
explicitly walking through three separate rounds of fixing what counts as a "drop") and `SendAppData()`
(lines 113-143, explaining the pre-handshake queueing added specifically because immediate `SendData` calls
right after `Join()`/`ConnectToHost()` are confirmed-reachable before the wire-id handshake completes).

### Single-threaded contract: explicit, reasoned, matches XNA's own model
The class comment (lines 36-44) is explicit that there is no internal synchronization anywhere in this
module, by design, matching real XNA's own single-threaded `Game`/`Update()` loop expectation -- a
documented constraint, not an oversight, correctly cross-referenced from `ENetDiscoveryService`'s own
class comment.

## Detailed Findings
See `ENetBackend.cpp`'s report for the HIGH-severity finding (this header only declares the affected
methods; the actual gap is in the dispatch implementation).

## Cross-File Observations
`RealNetworkingEnabled()` correctly scopes real ENet-backed networking to `SystemLink` only, matching the
class comment's XNA-fidelity reasoning (`Local`/`LocalWithLeaderboards` are single-machine by XNA design;
`PlayerMatch`/`Ranked` have no real backing server in this project).

## Missing or Weak Tests
Not independently located in this pass; given the finding below, a test where a malicious/modified client
peer sends a forged `GamerLeaveBroadcast`/`ServerWelcome`/`GamerJoinBroadcast`/`StateChangeBroadcast`
directly to a legitimate host (bypassing the intended client role) would be the most direct way to confirm
and then guard against the gap.

## Positive Findings
Extremely thorough tracking of this subsystem's own prior-fix history -- multiple distinct audit rounds
are cited by exact task ID and date directly in the source, a strong practice for institutional memory on a
subsystem this security/correctness-sensitive.

## Final Assessment
One HIGH-severity finding (documented fully in the paired `.cpp`): no sender-role verification on
host-authoritative broadcast message types, letting any connected client forge them.
