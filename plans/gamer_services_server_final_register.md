# Final throw / no-op evidence register (GS-011)

The GS-001 inventory (`gamer_services_server_initial_inventory.md`) listed every candidate stub in
GamerServices and Net at baseline b2fd47a45. This register records what remains after the project:
each refusal, constant or never-raised member still in `modules/gamer-services` and `modules/net`,
why it is there, and the evidence. A behaviour listed here is deliberate; anything else that refuses
or does nothing is a defect.

Sweep: every `NotSupportedException`/`NotImplementedException` throw, empty body, "never raised",
constant-valued property and CNAEXT in `modules/gamer-services/{src,include}` and
`modules/net/{src,include}`, re-read against the XNA 4.0 IL (`xna4-decomp`) and XML
(`xna4-decomp/dlls`).

## Refusals that are XNA behaviour

| Member | Behaviour | Why |
|---|---|---|
| `Guide.ShowGameInvite(string sessionId)` | `NotSupportedException` | XNA supports this overload only for Windows Phone LIVE titles. |
| `NetworkSession.AllowJoinInProgress = true` on Ranked | `NotSupportedException` | Ranked sessions never admit join-in-progress in XNA; the service enforces the same (`sessions.update`). |
| `NetworkSessionProperties` `Add`/`Insert`/`Remove`/`RemoveAt`/`Clear` | `NotSupportedException` | XNA's collection has a fixed eight slots; writes go through the indexer. |
| Writing an advertised (search result) `NetworkSessionProperties` | `NotSupportedException` | Read-only in XNA. |
| `Guide.EndShowMessageBox`/`EndShowKeyboardInput` a second time | `InvalidOperationException` | `XOverlappedAsyncResult.PrepareForEndFunction` (GS-005i). |

## Refusals and constants that are CNA limits

Each is documented where it is declared (C++ and C) and in `docs/xna-4-api-coverage.md`.

| Member | Behaviour | Why |
|---|---|---|
| `Gamer.GetPartnerToken`/`BeginGetPartnerToken`/`EndGetPartnerToken` | `NotSupportedException` | Partner tokens authenticate to Xbox LIVE partner web services; the CNA service is not LIVE and issues none. XNA for Windows refuses the same way. |
| `Gamer.GetFromGamertag` family without a service | `NotSupportedException` | Offline profiles have no directory to search. With a service it is a real lookup (GS-004). |
| `LocalNetworkGamer.EnableSendVoice`, `FriendGamer.HasVoice`, `SignedInGamer` voice paths | argument checks only; `HasVoice` false | CNA carries no voice. |
| `LocalNetworkGamer.SendPartyInvites`, `Guide.ShowParty`/`ShowPartySessions` | refuses (party of one) / explanatory pane | No party service. |
| `Guide.ShowMarketplace` | explanatory pane after XNA's privilege checks | No store; titles are fully licensed. |
| `Guide.IsTrialMode` | true only while `SimulateTrialMode` is set | No licensing service (GS-011b removed the public setter XNA keeps internal). |
| `GamerProfile.GamerZone`/`Reputation` of local profiles and unreviewed accounts | `Unknown` / 0 | XNA's unset values; nothing is invented. An account's chosen zone and review-based stars are reported (GSP-M1). |
| `QualityOfService.BytesPerSecondUpstream`; service search results' QoS | 0 / `IsAvailable` false | SystemLink measures the round trip and a downstream estimate from the host's probe train (GSP-L6); a host answers discovery at frame boundaries and cannot time arrivals, and a service listing has no path to its host before a join. |
| `GamerServicesDispatcher.InstallingTitleUpdate` | never raised | CNA installs no title updates. |
| `WriteTrueSkill` | raised; no skill is computed | Skill boards are ordinary arbitrated boards (GS-006e). |
| `LeaderboardKey.BestScoreRecent`/`BestTimeRecent` | no time window: every row is kept, as for the lifetime keys | The XNA documentation says only "best recent scores/times"; neither it nor the IL gives a window, and the service would have to invent one. |
| Guests in PlayerMatch/Ranked sessions | refused (not authorized for CNA online sessions) | A guest has no service credential for the service to authenticate; guests play Local, LocalWithLeaderboards and SystemLink sessions (GSP-L5). |
| Browser multiplayer | limitations page | Owner scope: native plus a browser limitations page. |

## Service limits a game can meet

Measured and set by the 2026-09-29 production audit (GSP-O; the server README has the benchmark
and every finding). They surface through the normal XNA exceptions for an unavailable service.

| Limit | Behaviour | Why |
|---|---|---|
| Recorded request IDs per account, title and day | 20,000, then `RATE_LIMITED` | Replay protection for mutations; reads, heartbeats, leases and presence record none, so one account cannot spend a title's budget. |
| Password sign-in and refresh per address | 10 a minute | Bounds scrypt work and guessing. |
| Live sign-ins per account | 32; the 33rd signs the oldest out | Clients without credential storage sign in on every launch (GSP-O7). |
| Relay machines per server; control connections | 1024; 256, 32 per address | One process, one SQLite writer (GSP-O5). |

## Host-language refusals

| Member | Behaviour | Why |
|---|---|---|
| `PropertyDictionary` equality of an `std::any` of a type it does not define | `NotSupportedException` | C++ has no boxed equality for arbitrary types; every XNA value type compares. |
| `LeaderboardWriter` column of an unsupported C++ type | `NotSupportedException` | Only the XNA leaderboard column types have a service representation. |

## Resolved since the GS-001 inventory

Everything else the inventory named now behaves as XNA does, with a service or offline. The major
items, with the plan entry that settled each: the dispatcher's fabricated gamers (GS-003/004);
profiles, lookup and pictures (GS-004/005b); friends, presence, messages, reviews and the social
Guide panes (GS-005); achievements with service catalog and offline catalog (GS-005/004p);
leaderboards with EndGame/leave windows and Ranked arbitration (GS-006); PlayerMatch/Ranked
sessions, invitations, readiness, machine removal and statistics (GS-007/008, 007i-q);
`NetworkMachine.RemoveFromSession` (GS-007j); `PropertyDictionary.CopyTo` (GS-004o); the standard
avatar API and renderer, with the Avatar EXT retired (GS-009); the Guide's message box, keyboard,
system Guide and End waits (GS-005d/f/g/h/i).

## Evidence

Final validation of the avatar fidelity and completion pass, 2026-09-29, CNA branch
`feature/gamer-services-server` (the GSP commits; `plans/plan_gamer_services_avatar_polish.md`)
with the server at 4ba7f95 and cna-samples `feature/gamer-services-samples` 378483b:

- Server corpus with every CNA harness (client, C API, directory, relay, session, avatar) and the
  slirp4netns NAT helper: **28/28, no skips, 106 s** -- adds host migration and host crash (NAT),
  AddLocalGamer (NAT) and the benchmark smoke to TLS control and restart, directory, arbitration,
  avatars and catalog items on demand, social, invitations, WSS relay, owned ENet, sessions,
  invites and session restart, natively and across separate NAT namespaces. Protocol drift clean.
- Server capacity (`tests/service_benchmark.py`, Release, 64 players): steady 1,602 req/s (was 73),
  sign-in storm 1,193 req/s with ordinary requests at p50 49 ms, one address holding 160 idle
  connections causes no failure, descriptor exhaustion survived (server README).
- CNA, one process per test: CnaGamerServicesTests **551 + 1 known skip** (552),
  CnaNetTests **504/504**, CnaRuntimeTests **192 + 2 environment skips**; `CApi_*` **95/98** (the
  three standing environment smokes: audio, audio-unavailable, content; `CApi_InstalledConsumer`
  passes) and the twelve `CApi*` gate tests pass (82a3a094c brought the coverage inventory up to
  the gamer declarations this pass changed; five new `GetTypeName` overrides are planned C routes);
  C ABI baseline `--check` current (3,202 exports); platform boundary gates pass except
  the `SDL_TOUCH_MOUSEID` classification inherited from `next`.
- Samples (Release/OPENGLES3, private Xvfb, evidence directories `*-20260929-polish`): SAMPLE-096
  Invites pass; SAMPLE-075 NetworkStateManagement SystemLink and LIVE pass; the
  achievements/leaderboards program passes; SAMPLE-087 AvatarShadows draws 16 catalog v2 avatars
  with shadows.
- Avatars: the real renderer (OPENGL33, private display) renders the 255-job catalog v2 review with
  0 failures (`/rv/tmp/avatar-polish/review-final/`).
- Demos (cmake-build-opengl33, private display): the SystemLink pairs net_avatar_sync,
  net_client_server_arena, simulated_network_conditions, gamer_roster_hud, session_browser and
  qos_probe, and session_lifecycle_events, exit 0; the GamerServices demos (achievement showcase,
  sign-in/presence, profile/privileges, friends, leaderboard viewer, Guide console, dispatcher
  watchdog) exit 0 after GSP-P1 fixed their stub-era sign-in.
