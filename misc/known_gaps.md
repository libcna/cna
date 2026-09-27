# Known gaps

Capabilities XNA has and CNA does not, **measured** against a specific XNA behaviour and recorded
with the case that found them. Distinct from [`known_bugs.md`](known_bugs.md): nothing here is
wrong, it is absent — or present as a deliberate divergence — and closing it is a decision rather
than a repair.

A row leaves this file when the capability lands, or when the owner records that CNA will not have
it. Entry numbers are stable evidence references, so removing a closed entry leaves a number gap.

Most of these surface through the `cna-samples` campaign, because porting a sample is what puts
weight on a corner of the API nothing else reaches. The sample that found a gap is named, but a gap
outlives that sample's own fate: several were found through rows that were then cancelled for
unrelated reasons, and the gap is not cancelled with them.

---

## 1. Native `SurfaceFormat::Alpha8` render-target storage is unavailable

**Found:** 2026-09-09 through SAMPLE-087 (AvatarShadows), whose Xbox 360-only row was later
cancelled. **Corrected:** 2026-09-27 against CNA `next 629554a95`. The original audit treated an
unsupported Alpha8 preference as a constructor failure. `SOFTWARE-216` (`70fe41618`) restored
XNA's preferred-format fallback on a different development line; it was absent from the old audit's
CNA HEAD `35268971c` and is present in current `next`. Cancelling the sample did not close the
remaining exact-format capability gap.

`GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Alpha8)` still returns
**false**: EasyGL has no Alpha8 render-target attachment mapping. A normal `RenderTarget2D`
constructor requesting Alpha8 now calls `SelectRenderTargetFormatEXT`, selects `Color` and allocates
that supported target. Its `Format` consequently reports `Color`. The focused capability test
asserts the false exact-format answer, while
`GraphicsCapabilityFloatRenderTargetTest.AnUnsupportedPreferredFormatIsSubstituted` pins the
shared preferred-format fallback with Dxt1. The Alpha8 conclusion here follows the same current
selection code and EasyGL mapping; it has not been separately tested on a real GL context in this
re-analysis.

SAMPLE-087 writes Avatar silhouettes into a full-screen target and has `GroundEffect.fx` sample
its alpha channel to darken the ground by 50%. A `Color` target retains that alpha channel, so
Alpha8 preference alone no longer blocks construction or the shader's alpha-mask algorithm. It
does lose the source's intended **one byte per pixel** storage choice: `Color` uses four bytes per
pixel. The exact Xbox allocation was not captured, so whether that hardware honored the Alpha8
preference or also fell back remains unverified. A faithful native Alpha8 attachment could retain
the memory lesson on CNA, but the visible result still cannot be qualified without the genuine
Avatar body and a console reference capture.

**Closing the remaining gap** means supporting a true alpha-only render target where the renderer
can preserve XNA's alpha output and alpha sampling semantics, with native and browser pixel tests.
A GL red-only attachment by itself writes/samples red, not alpha, so GL_R8 alone is insufficient.
The existing XNA-style preferred-format fallback remains the behavior when exact Alpha8 storage
is unavailable; no sample-side substitution is needed.

---

## 4. There is no online matchmaking or invitation service

**Found:** 2026-09-09, through SAMPLE-096 (Invites).
**Partly closed the same day:** the divergence half — see below.

CNA implements real transport for exactly one `NetworkSessionType`. `ENetBackend::
RealNetworkingEnabled` (`modules/net/src/Internal/ENetBackend.cpp`) is

```cpp
return sessionType == NetworkSessionType::SystemLink;
```

`Local` and `LocalWithLeaderboards` are offline session types in XNA too, so a single machine
genuinely is the whole session and nothing is missing about them. `PlayerMatch` and `Ranked` are
different: they are Xbox LIVE matchmaking types, where the service finds the peers. There is no such
service here, and none is planned. The same absence covers invitations — nothing can raise
`NetworkSession::InviteAccepted`, because nothing delivers an invitation.

### What was closed, and what remains

The gap first recorded here was not the absence but the **silence about it**:

| | `NetworkSession.Create(PlayerMatch, 4, 16)`, one local profile signed in, offline |
|---|---|
| Real XNA 4.0 | throws; the message names the signed-in-gamer and LIVE-profile requirement |
| CNA before 2026-09-09 | succeeded, returning a session of type `PlayerMatch` with no port, no discovery and no peer that could ever arrive |
| CNA now | throws `GamerServicesNotAvailableException`, naming the missing service |

The XNA half is a capture, not a reading: SAMPLE-096's unchanged `Invites.exe` under the local
XNA 4.0 install on an isolated offline Xvfb signs in `Player1`, reaches the A/B menu, and answers A
with that error —
`/rv/tmp/samples/SAMPLE-096-InvitesSample_4_0/evidence/original-windows-reach/02-after-local-sign-in.png`
and `03-offline-player-match-create.png`.

`Create`, `Find` and `JoinInvited` (with their `Begin*` forms) now refuse; `EndJoinInvited` refuses
every result, because no `Begin` can produce one and completing a foreign result as an invited join
was a way around the refusal. Through the C ABI all of these answer `CNA_RESULT_NOT_SUPPORTED`,
whose own documented meaning is already "the platform has no gamer services at all".

**`Join(AvailableNetworkSession*)` deliberately still accepts a `PlayerMatch` entry.** Its input can
no longer come from `Find`, so such an entry can only be constructed directly by a binding, and
`NetworkSessionTest.JoinDoesNotActivateTransportForSyntheticPlayerMatchSession` exists to keep that
path from waiting on a handshake that will never arrive (a real SAMPLE-091 bug). Refusing there
would delete that regression guard without closing anything a game can reach.

**Closing the rest** means the service itself: identity, friends/presence, matchmaking, invitation
delivery and address handoff, for native and browser. That is the scope `SAMPLES-DEC-004` and
`SAMPLES-DEC-006` put to the owner, and it is why SAMPLE-096 is a non-port rather than a blocked
port.
