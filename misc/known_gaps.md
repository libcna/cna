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

## 4. Online sessions and invitations are native-only

**Found:** 2026-09-09, through SAMPLE-096 (Invites).
**Partly closed the same day:** the divergence half — see below.
**Native closed:** 2026-09-28/29 by the `plans/plan_gamer_services_server.md` mission.

`PlayerMatch` and `Ranked` are service matchmaking types, where the service finds the peers.
CNA now has one: an independent CNA account service (TLS control, a session directory, invitation
delivery and an authenticated relay; not Xbox LIVE and not compatible with it). With it configured,
`Create`/`Find`/`Join`, `InviteAccepted`/`JoinInvited`, session properties, lobby readiness and the
Ranked leaderboard lifecycle work between separate processes (GS-007/GS-008). SAMPLE-096 (Invites)
and SAMPLE-075 (NetworkStateManagement, LIVE route) are ported and accepted against it on cna-samples
branch `feature/gamer-services-samples`; SAMPLE-100 (NetworkPrediction) was requalified against it.

What remains:

- **The browser.** An Emscripten build has no account transport or relay (the backend refuses with
  "CNA browser account transport is not implemented"), so there `PlayerMatch`/`Ranked` refuse as
  they did natively before. The owner's accepted scope is native plus a browser limitations page
  (`docs/browser-network-readiness.md`), not browser multiplayer.
Online host migration and `AddLocalGamer` in online sessions, listed here until 2026-09-29, now work
(`plans/plan_gamer_services_avatar_polish.md` GSP-K1/K2), as do away/busy, gamer zone, reputation,
parties and network voice (`plans/plan_gamer_services_xbox_fidelity.md`). Every remaining
GamerServices/Net/Avatar limitation -- partner tokens, TrueSkill computation, the Recent leaderboard
window, the store, online guests, service-listing QoS and the browser among them -- is listed in
`docs/gamer-services-known-limitations.md`.

### What was closed first

The gap first recorded here was not the absence but the **silence about it**:

| | `NetworkSession.Create(PlayerMatch, 4, 16)`, one local profile signed in, no service configured |
|---|---|
| Real XNA 4.0 | throws; the message names the signed-in-gamer and LIVE-profile requirement |
| CNA before 2026-09-09 | succeeded, returning a session of type `PlayerMatch` with no port, no discovery and no peer that could ever arrive |
| CNA now | throws `GamerServicesNotAvailableException`, naming the missing service |

The XNA half is a capture, not a reading: SAMPLE-096's unchanged `Invites.exe` under the local
XNA 4.0 install on an isolated offline Xvfb signs in `Player1`, reaches the A/B menu, and answers A
with that error —
`/rv/tmp/samples/SAMPLE-096-InvitesSample_4_0/evidence/original-windows-reach/02-after-local-sign-in.png`
and `03-offline-player-match-create.png`. Without a configured service CNA still answers the same
way, and through the C ABI these answer `CNA_RESULT_NOT_SUPPORTED`.

**`Join(AvailableNetworkSession*)` deliberately still accepts a synthetic `PlayerMatch` entry**
constructed directly by a binding, and
`NetworkSessionTest.JoinDoesNotActivateTransportForSyntheticPlayerMatchSession` exists to keep that
path from waiting on a handshake that will never arrive (a real SAMPLE-091 bug).
