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
| `GamerProfile.GamerZone`/`Reputation` | `Unknown` / 0 | CNA keeps neither; reviews do not rate gamers (GS-004p). |
| `QualityOfService` bandwidth | 0 | Only the discovery round trip is measured; service search results are unmeasured (`IsAvailable` false). |
| `GamerServicesDispatcher.InstallingTitleUpdate` | never raised | CNA installs no title updates. |
| `WriteTrueSkill` | raised; no skill is computed | Skill boards are ordinary arbitrated boards (GS-006e). |
| Guest sign-in (`ShowSignIn` guests) | not offered | Accounts and local profiles only. |
| Browser multiplayer | limitations page | Owner scope: native plus a browser limitations page. |

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

Final validation, 2026-09-29, CNA branch `feature/gamer-services-server` with the server at the
commit recorded in the plan's known-good set:

- Server corpus with every CNA harness (client, C API, directory, relay, session, avatar) and the
  slirp4netns NAT helper: **23/23, no skips, 340.7 s** — TLS control and restart, directory,
  arbitration, avatars, social, invitations, WSS relay, owned ENet, sessions, invites and session
  restart, each natively and across separate NAT namespaces.
- CNA: CnaNetTests **495/495** (SystemLink regression corpus included: ENet backend, discovery,
  two-process loopback, migration, added gamers, Find limits), CnaGamerServicesTests **498 + 1
  known skip**, CnaRuntimeTests **192/192**; C API `CApi_*` 94/98 plus `CApi_InstalledConsumer` on
  a rebuilt static archive (the four others are the three standing environment smokes);
  ABI baseline `--check` current (0.34.0, 3,202 exports); protocol drift check clean; platform
  boundary gates pass except the pre-existing `SDL_TOUCH_MOUSEID` classification from `next`.
- Samples (cna-samples `feature/gamer-services-samples`, Release/OPENGLES3, private Xvfb, new
  evidence directories dated 20260929-final): SAMPLE-096 Invites pass; SAMPLE-075
  NetworkStateManagement SystemLink and LIVE pass; the achievements/leaderboards program passes;
  SAMPLE-087 AvatarShadows draws 16 standard avatars with shadows.
- SystemLink demos (cmake-build-opengl33, private Xvfb): net_avatar_sync, net_client_server_arena,
  simulated_network_conditions, gamer_roster_hud, session_browser and qos_probe host/join pairs and
  session_lifecycle_events all exit 0; qos_probe without a signed-in gamer refuses with its message.
