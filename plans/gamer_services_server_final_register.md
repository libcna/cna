# Final throw / no-op evidence register (GS-011)

> **HISTORICAL CHECKPOINT.** This document describes CNA GamerServices/Net as of 2026-09-29 to
> 2026-09-30 (f5df1c824..943a34617, GS-011 through the GSX-Q2 cleanup). It is not current
> capability documentation: several statements below (for example "`FriendGamer.HasVoice` | false"
> and "`NetworkGamer.IsGuest` of a remote SystemLink gamer | false") no longer hold. For the
> current implementation and every remaining limitation see
> [`docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md).

The GS-001 inventory (`gamer_services_server_initial_inventory.md`) listed every candidate stub in
GamerServices and Net at baseline b2fd47a45. This register records what remains after the project:
each refusal, constant or never-raised member still in `modules/gamer-services` and `modules/net`,
why it is there, and the evidence. A behaviour listed here is deliberate; anything else that refuses
or does nothing is a defect. The explained, current list of every limitation (including the ones
that are policies, platform limits or unverified behaviour rather than refusals in the code) is
[`docs/gamer-services-known-limitations.md`](../docs/gamer-services-known-limitations.md).

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
| `FriendGamer.HasVoice` | false | The service does not know a friend's audio hardware; in a session `NetworkGamer.HasVoice` reports it (GSX-E1). |
| `Guide.ShowMarketplace` | the Guide's Game content page after XNA's privilege checks: the title's licence, with a Test Purchase while `SimulateTrialMode` is set, and the avatar catalogs installed | CNA has no store to sell from (GSX-E1). `IsTrialMode` itself behaves as XNA's (below). |
| `GamerProfile.GamerZone`/`Reputation` of local profiles and unreviewed accounts | `Unknown` / 0 | XNA's unset values; nothing is invented. An account's chosen zone and review-based stars are reported (GSP-M1). |
| PlayerMatch/Ranked search results' `QualityOfService` | `IsAvailable` false | A service listing has no path to its host before a join. SystemLink results measure the round trip and both bandwidths: downstream from the host's probe train (GSP-L6), upstream from the searcher's train timed by the host's responder thread (GSX-E). |
| `GamerServicesDispatcher.InstallingTitleUpdate` | never raised (`GamerServicesComponent` still exits on it, as XNA's does) | CNA installs no title updates; a title that stops accepting old versions refuses them with `GameUpdateRequiredException` instead (GSX-E4). |
| `WriteTrueSkill` | raised; no skill is computed | Skill boards are ordinary arbitrated boards (GS-006e). |
| `LeaderboardKey.BestScoreRecent`/`BestTimeRecent` | no time window: every row is kept, as for the lifetime keys | The XNA documentation says only "best recent scores/times"; neither it nor the IL gives a window, and the service would have to invent one. |
| Guests in PlayerMatch/Ranked sessions | refused (not authorized for CNA online sessions) | A guest has no service credential for the service to authenticate; guests play Local, LocalWithLeaderboards and SystemLink sessions (GSP-L5). |
| `NetworkGamer.IsGuest` of a remote SystemLink gamer | false | The SystemLink roster does not carry it; a local gamer reports its profile's (GSX-Q2). |
| Browser multiplayer | limitations page | Owner scope: native plus a browser limitations page. |

## Service limits a game can meet

Measured and set by the 2026-09-29 production audit (GSP-O; the server README has the benchmark
and every finding). They surface through the normal XNA exceptions for an unavailable service.

| Limit | Behaviour | Why |
|---|---|---|
| Recorded request IDs per account, title and day | 20,000, then `RATE_LIMITED` | Replay protection for mutations; reads, heartbeats, leases and presence record none, so one account cannot spend a title's budget. |
| Password sign-in and refresh per address | 10 a minute | Bounds scrypt work and guessing. |
| Live sign-ins per account | 32; the 33rd signs the oldest out | Clients without credential storage sign in on every launch (GSP-O7). |
| Relay machines per server; control connections | 1024; 256, 32 per address, 600 new ones a minute per address | One process, one SQLite writer (GSP-O5, GSX-E5). |
| Server processes per database | one; a second refuses to start (`DATABASE_IN_USE`) | One SQLite writer and in-memory relay and event hubs (GSX-E5). |
| Event channels | 8 per account, 4096 per server | Push hints only; clients still poll as the fallback (GSX-E6). |

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
system Guide and End waits (GS-005d/f/g/h/i). Since the Xbox-fidelity pass
(`plan_gamer_services_xbox_fidelity.md`): `Guide.IsTrialMode` latched at each Update from
`SimulateTrialMode` and `AllowOnlineSessions` false in trial (GSX-E1); parties --
`SignedInGamer.PartySize`, `Guide.ShowParty`/`ShowPartySessions`, `SendPartyInvites` -- and joining a
friend's game (GSX-E2); SystemLink upstream bandwidth (GSX-E3); title versions and
`GameUpdateRequiredException` (GSX-E4); network voice -- `HasVoice`, `IsTalking`,
`IsMutedByLocalUser`, `EnableSendVoice` (GSX-E1 voice); the console's social notifications and push
hints (GSX-E6).

## Evidence

Final truth cleanup, 2026-09-30 (`plan_gamer_services_xbox_fidelity.md` GSX-Q2): CNA
`feature/gamer-services-server`, server `feature/gamer-services-server`, cna-samples
`feature/gamer-services-samples`; evidence `/rv/tmp/xbox-fidelity/cleanup/`.

- Server corpus with every CNA harness: **28/28**, no skips.
- CNA, one process per test: CnaGamerServicesTests **604 + 1 known skip**, CnaNetTests **518/518**,
  CnaRuntimeTests **192 + 2 environment skips**; CnaTests in one process 9,024 pass with the 10
  standing HEADLESS graphics/content failures; `CApi_*` **107/110** (the three standing
  environment smokes) with the coverage, limitations and release-gate records current and the ABI
  baseline current (3,202 exports); protocol drift clean.
- Samples (Release/OPENGLES3, private Xvfb, `*-20260930-cleanup`): SAMPLE-087 AvatarShadows draws 16
  catalog v3 avatars with shadows; SAMPLE-096 Invites and SAMPLE-075 (SystemLink and online) pass;
  the achievements/leaderboards program passes.
- Demos: 18 GamerServices and Net demos exit 0 on the private display.
- After merging committed `next` (0f7166cd8, e6d562454, then 4a31b3b7d) into the branch, the suites, C API
  gates (112/115: `next` added five C API tests, which pass), demos, samples and server tests again
  with the same results.

The Xbox-fidelity pass's own validation (GSX-Q1) and the earlier passes' are in Git history.
