# GamerServices, Net, Guide and Avatar: known limitations

This is the authoritative list of what CNA's GamerServices, Net, Guide and Avatar implementation
does **not** do, does only in part, does differently from XNA on Xbox 360 by design, or has not been
able to verify. It describes the current code (verified 2026-09-30 against CNA
`feature/gamer-services-server`, which then equalled `next` plus this cleanup, and
cna-gamer-services-server `feature/gamer-services-server`). When code changes one of these
entries, change it here in the same commit.

CNA provides a **CNA-owned, XNA/Xbox-like** GamerServices implementation: CNA's own protocol,
accounts, service, relay and original avatar assets, with backend policies of its own where XNA does
not say how something is decided. It is **not Xbox LIVE compatible** and does not claim to be.

Other documents: [`gamer-services-server.md`](gamer-services-server.md) (configuration and
deployment), [`avatars.md`](avatars.md) (the avatar system),
[`browser-network-readiness.md`](browser-network-readiness.md) (the browser boundary), the server's
`README.md` and `protocol/v1.md` (service operations, limits and capacity). The final register
([`plans/gamer_services_server_final_register.md`](../plans/gamer_services_server_final_register.md))
lists each refusal in the code with its evidence; the three GamerServices plans are history.

**Status values:** *intentionally unsupported* (CNA will not do it, for the reason given) ·
*partially implemented* (the working part and the missing part are stated) · *platform-limited*
(works on some targets only) · *not independently validated* (implemented, not measured in the way
named) · *implementation differs by design* (XNA shows the outcome, CNA decided how) ·
*CNA-equivalent only* (a CNA function stands where Xbox had its own service). The summary table
abbreviates them as Unsupported, Partial, CNA policy (XNA shows the outcome, CNA decides it),
Differs by design, CNA-equivalent only, Platform limitation, Unverified, Deployment limitation and
Host-language difference.

## Not limitations: implemented and tested

Listed so that nobody reads an older document as saying they are missing. Achievements (service
and offline catalogs), leaderboards (LocalWithLeaderboards, PlayerMatch, Ranked, Stream columns),
PlayerMatch and Ranked sessions, SystemLink, the authenticated TLS/WSS relay, invitations and
`InviteAccepted`/`JoinInvited`, joining a friend's game, friends, rich presence, away/busy,
`GameDefaults`, `GamerZone`, review-based `Reputation`, messages and player reviews, online host
migration, online `AddLocalGamer`, parties (`PartySize`, `ShowParty`, `ShowPartySessions`,
`SendPartyInvites`), the service event channel (push hints), the console's social notifications,
NetworkSession voice, SystemLink QoS including upstream bandwidth, trial mode and the test purchase,
title-version gating, the console-style Guide with its sign-in picker, gamer cards and avatar
editor, the Guide owning input while visible, the standard `AvatarRenderer`/`AvatarAnimation`/
`AvatarDescription` with XNA's 71-bone skeleton and `AvatarDescription.Changed`, locally installed
avatar catalogs and versioned catalog-pack updates.

## Summary

| # | Area | Status | XNA-port impact | Intentional? | Future feasible? |
|---|---|---|---|---|---|
| 1 | Xbox LIVE protocol, accounts, credentials | Unsupported | None through the XNA API | Yes | No (outside CNA's identity) |
| 2 | `Gamer.GetPartnerToken` family | Unsupported | Blocks only titles using partner web services | Yes | No |
| 3 | Marketplace, store, payments | CNA-equivalent only | Minor: no real purchase | Yes | Possible as a CNA store; not planned |
| 4 | TrueSkill computation | Unsupported | None observable through the API | Yes | Only with a documented CNA algorithm |
| 5 | Title-update installation | CNA-equivalent only | None | Yes | Not needed |
| 6 | Microsoft avatar assets | Unsupported | None functionally | Yes | No |
| 7 | `...Recent` leaderboard window | Partial | Minor | Yes (no evidence for a window) | Yes, if evidence appears |
| 8 | Reputation formula | CNA policy | None | Yes | n/a |
| 9 | Host election | CNA policy | None | Yes | n/a |
| 10 | Ranked arbitration rule | CNA policy | None | Yes | n/a |
| 11 | Party model | CNA policy | None | Yes | n/a |
| 12 | Online status and presence timing | CNA policy | None | Yes | n/a |
| 13 | Invitation lifetime and identity | CNA policy | None | Yes | n/a |
| 14 | Voice routing: one microphone per machine, mute list | CNA policy | Minor | Yes | Yes (per-gamer devices) |
| 15 | Guide look, sounds, input ownership | CNA-equivalent only | None | Yes | n/a |
| 16 | Guests | Partial | Specialized titles only | Partly | Online guests: needs service design |
| 17 | QoS of service search results; one round-trip sample | Partial | Minor | Yes (no path before a join) | Yes, after a direct/relay probe |
| 18 | `FriendGamer.HasVoice`: CNA's definition of "voice capability" | CNA policy | None | Yes | n/a |
| 19 | Push scope | Partial | None | Yes | Yes (more topics) |
| 20 | Privileges and privacy: operator policy, not console parental controls | CNA policy | Minor | Yes | n/a |
| 21 | Profile editing, account self-service | Partial | None | Partly | Yes |
| 22 | Leaderboard Stream columns, unmeasured Xbox rules | Partial | Minor | Yes | Partly |
| 23 | No service configured (offline profiles) | Platform limitation | Minor | Yes | n/a |
| 24 | Credential persistence off POSIX | Platform limitation | None (sign-in every launch) | No | Yes |
| 25 | Relay-only online transport over TCP | Differs by design | Minor latency | Yes | Yes (direct path) |
| 26 | Browser (WebAssembly) multiplayer and services | Platform limitation | Blocks online/LAN play in browsers | Yes (owner scope) | Yes, with new adapters |
| 27 | Voice availability by build and device | Platform limitation | Minor | Yes | Partly |
| 28 | Non-Linux clients and server hosts | Unverified | Unknown | No | Yes |
| 29 | Real microphone and speakers | Unverified | Unknown | No | Yes, with hardware |
| 30 | Guide system sounds heard | Unverified | None | No | Yes, with hardware |
| 31 | Public Internet deployment | Unverified | None for a LAN/NAT test; unknown in production | No | Yes |
| 32 | Console-only rules read from Windows IL | Unverified | Unknown, believed minor | No | Only with console traces |
| 33 | `AvatarRenderer` on renderers other than OpenGL 3.3/Vulkan | Unverified | Unknown | No | Yes |
| 34 | Single server process, SQLite writer, in-memory hubs | Deployment limitation | None | Yes | Yes, with coordination work |
| 35 | SQLite as the only database | Deployment limitation | None | Yes | Yes |
| 36 | No cluster or horizontal scale | Deployment limitation | None | Yes | Yes, substantial |
| 37 | Distributed hostile traffic | Deployment limitation | None | Yes | Operator's firewall/proxy |
| 38 | Operational limits a game can meet | Deployment limitation | Rare, surfaces as XNA exceptions | Yes | Tunable |
| 39 | Avatar baked shading and occlusion | Partial (visual quality) | None | Yes | Only inside the renderer |
| 40 | Avatar visual fidelity of catalog v3 | Differs by design | None | Yes | New catalog versions |
| 41 | Avatar catalog and format edge cases | Partial | Minor | Yes | Partly |
| 42 | `PropertyDictionary` equality of arbitrary `std::any` | Host-language difference | Minor | Yes | No |
| 43 | Leaderboard columns of non-XNA C++ types | Host-language difference | None | Yes | No |
| 44 | Value-type `AvatarDescription` and shared `Changed` | Host-language difference | None | Yes | No |

---

## Xbox infrastructure CNA does not reproduce

These are outside CNA's identity even where the XNA-facing API has a CNA equivalent elsewhere.

### 1. Xbox LIVE protocol, accounts and credentials

- **Status:** intentionally unsupported.
- **XNA / Xbox:** XNA games reached Xbox LIVE (or Games for Windows - LIVE) through the XNA API;
  the wire protocol, accounts and service binaries were Microsoft's.
- **CNA now:** its own HTTPS/JSON control protocol (`protocol/v1.md`), WSS relay
  (`protocol/relay-v1.md`) and event channel, its own accounts and server. Nothing reads or produces
  Xbox LIVE data or credentials.
- **Reason:** proprietary infrastructure; CNA reimplements the programming model, not the service.
- **Observable impact:** none through the XNA API; a player uses a CNA account, not an Xbox one.
- **Porting impact:** none.
- **Future work:** not intended.

### 2. `Gamer.GetPartnerToken`, `BeginGetPartnerToken`, `EndGetPartnerToken`

- **Status:** intentionally unsupported.
- **XNA / Xbox:** a token for Xbox LIVE partner web services, for registered partners. The Windows
  assembly's IL throws `NotSupportedException` ("only available for Xbox LIVE Registered
  Developers"); the API served Windows Phone titles.
- **CNA now:** all three throw `NotSupportedException`, with or without a service.
- **Reason:** such a token is an Xbox LIVE credential; CNA issues none and will not imitate one.
- **Observable impact:** the exception, as on XNA for Windows.
- **Porting impact:** blocks only specialized titles that call a partner web service; none of the
  verified samples does.
- **Future work:** not intended. A title needing its own web service authenticates it separately.

### 3. Marketplace, store and payments

- **Status:** CNA-equivalent only.
- **XNA / Xbox:** `Guide.ShowMarketplace(PlayerIndex)` opened the Xbox Marketplace; `IsTrialMode`
  turned false after a purchase; `SimulateTrialMode` tested trial code.
- **CNA now:** `ShowMarketplace` runs XNA's checks, then opens the Guide's **Game content** page:
  the title's licence, the installed avatar catalogs and the catalog-update setting. `IsTrialMode`
  starts true and is latched from `SimulateTrialMode` at every `GamerServicesDispatcher.Update`, as
  XNA does; while a title simulates trial mode the page offers XNA's "Test Purchase", which ends the
  simulation. `AllowOnlineSessions` is false in trial, as in XNA. There is no catalogue of content,
  no payment and no entitlement service.
- **Reason:** CNA has no store and does not imitate the Xbox Marketplace.
- **Observable impact:** a CNA title is fully licensed unless it simulates trial mode; nothing can be
  bought.
- **Porting impact:** minor. Trial-mode logic works through `SimulateTrialMode`; a title that sells
  downloadable content needs its own store.
- **Future work:** a CNA store is conceivable; nothing is planned.

### 4. TrueSkill computation

- **Status:** intentionally unsupported.
- **XNA / Xbox:** Ranked sessions raise `NetworkSession.WriteTrueSkill`, "the final chance to write
  TrueSkill data"; Xbox LIVE computed skill from what was written. XNA exposes no API that reads a
  computed skill by name (`LeaderboardKey` has only the four score/time keys), and no document ties
  skill to matchmaking.
- **CNA now:** Ranked sessions, `WriteTrueSkill`, `WriteArbitratedLeaderboard` and arbitration are
  real (see 10). A board written from `WriteTrueSkill` is an ordinary arbitrated board holding the
  values the machines agree on; no skill is computed and matchmaking does not use one.
- **Reason:** the Xbox LIVE algorithm and its parameters are Microsoft's; another rating algorithm
  presented under that name would be misleading.
- **Observable impact:** a skill board holds what the game wrote rather than a service-computed
  rating.
- **Porting impact:** none observed; specialized Ranked titles that expect a service-computed value
  would see their own values instead.
- **Future work:** only as an explicitly named CNA rating policy, never as "TrueSkill".

### 5. Title-update installation

- **Status:** CNA-equivalent only.
- **XNA / Xbox:** `GamerServicesDispatcher.InstallingTitleUpdate` "notifies the game when a Games for
  Windows - LIVE title update is being installed" (on the console the dashboard installed updates
  before launch); `GameUpdateRequiredException` when LIVE no longer accepted a title version.
- **CNA now:** an operator may set the oldest game version a title accepts
  (`title-minimum-version`); an older client (configuration `titleVersion` / `CNA_GAME_VERSION`) is
  refused at sign-in, where the Guide says the game must be updated, and a starting network session
  throws `GameUpdateRequiredException`. `InstallingTitleUpdate` is never raised because CNA installs
  no updates; `GamerServicesComponent` still exits the game on it, as XNA's does.
- **Reason:** distributing game builds is the title's business, not the service's.
- **Observable impact:** games see the same exception for a refused version; no installer runs.
- **Porting impact:** none.
- **Future work:** not needed.

### 6. Microsoft avatar assets

- **Status:** intentionally unsupported.
- **XNA / Xbox:** the console drew avatars from Microsoft's own assets, installed with system updates.
- **CNA now:** every mesh, texture and animation is original CNA work
  (`tools/avatar_builder/`), in CNA's own 1,021-byte description encoding; see 40 for how v3 looks.
- **Reason:** proprietary assets; the project forbids them.
- **Observable impact:** avatars look like CNA avatars, not Xbox 360 avatars; the XNA API behaves
  the same.
- **Porting impact:** none functionally.
- **Future work:** not intended.

## Behaviour XNA shows but CNA decides (backend policy)

XNA exposes the outcome of each of these, not how it is decided. CNA's rule is its own; it is not a
claim about how Xbox LIVE decided.

### 7. `LeaderboardKey.BestScoreRecent` and `BestTimeRecent`

- **Status:** partially implemented.
- **XNA / Xbox:** "best recent scores/times for this player and his or her Xbox LIVE friends". No
  IL, XML or other document found names the window.
- **CNA now:** the Recent keys are ordinary boards with no time window: every row is kept, as for
  the lifetime keys. Reads, paging, friend and gamer filters behave as for any board.
- **Reason:** inventing a 7- or 30-day window would present a guess as XNA behaviour.
- **Observable impact:** an old score stays on a Recent board.
- **Porting impact:** minor; a game that relies on old scores expiring shows them longer.
- **Future work:** add a window if evidence of Xbox's appears.

### 8. `GamerProfile.Reputation`

- **Status:** implementation differs by design.
- **XNA / Xbox:** "a number of stars ranging 0 to 5" (a `float`); Xbox LIVE's formula is not
  public.
- **CNA now:** source data are real CNA player reviews (`reviews.submit`: prefer, avoid, or clear,
  from the Guide's Player Review). The numerical policy is CNA's: stars = 5 x prefer / (prefer +
  avoid), rounded to the nearest quarter star (server: `round(20 x prefer / total) / 4`). A member
  nobody has reviewed, and every local offline profile, reports 0, XNA's unset value.
- **Reason:** XNA exposes stars, not a formula.
- **Observable impact:** the value's type, range and meaning are XNA's; its arithmetic is CNA's.
- **Porting impact:** none.
- **Future work:** none needed.

### 9. Host election in online host migration

- **Status:** implementation differs by design (the migration itself is implemented).
- **XNA / Xbox:** with `AllowHostMigration` set, the host migrates, `HostChanged` is raised, the
  session survives with its roster, properties and gamer IDs. XNA does not say which machine is
  chosen.
- **CNA now:** XNA-observable behaviour is as above for SystemLink and for PlayerMatch/Ranked. The
  CNA backend host-election policy for online sessions: the service makes the machine holding the
  lowest remaining gamer ordinal the host, and that machine's owner the host gamer. A departed host
  is noticed at once when it leaves; a crashed one when its relay connection has been closed for 20
  seconds; a client waits up to 30 seconds for the service's decision, then ends with
  `HostEndedSession`. The old host's gamers leave (`GamerLeft`) before `HostChanged`.
- **Reason:** the choice is not observable in XNA; CNA needs a deterministic rule.
- **Observable impact:** which machine becomes host may differ from what a console would pick.
- **Porting impact:** none.
- **Future work:** none needed.

### 10. Ranked arbitration

- **Status:** implementation differs by design.
- **XNA / Xbox:** arbitrated statistics are written by every machine for every gamer; Xbox LIVE
  reconciled them.
- **CNA now:** a row is written only when a strict majority of the machines that reported it sent
  an identical row; a round resolves when every machine reported, 60 seconds after it ended, or a
  day after it opened. Ranked sessions refuse join-in-progress and new joins during play.
- **Reason:** XNA states the reporting rule, not the reconciliation.
- **Observable impact:** disagreeing reports are discarded rather than resolved by Xbox rules.
- **Porting impact:** none.
- **Future work:** none needed.

### 11. Parties

- **Status:** implementation differs by design.
- **XNA / Xbox:** games see `SignedInGamer.PartySize`, `Guide.ShowParty`, `ShowPartySessions` and
  `LocalNetworkGamer.SendPartyInvites`; the party service was the console's.
- **CNA now:** one account-level party per account (it stays together across titles), at most eight
  members counting outstanding invitations, invitations valid for an hour, leadership passing to the
  longest-standing member; only mutual friends can be invited. `PartySize` follows the service at
  gamer-services updates (0 without a party). There is no party voice chat outside a NetworkSession.
- **Reason:** XNA shows the party, not its rules.
- **Observable impact:** none beyond the rules above.
- **Porting impact:** none.
- **Future work:** party chat would need a separate voice path.

### 12. Online status and presence timing

- **Status:** implementation differs by design.
- **XNA / Xbox:** `FriendGamer.IsOnline`, `IsAway`, `IsBusy`, `IsPlaying`, `Presence`.
- **CNA now:** away and busy are exactly what the player chose in the Guide (never inferred from
  inactivity); a friend is online while their client showed authenticated activity in the last 90
  seconds (a heartbeat every 30); rich presence is sent during `GamerServicesDispatcher.Update`.
- **Reason:** CNA has no reliable idle signal; the console's timing is not documented.
- **Observable impact:** a friend who quits without signing out shows online for up to 90 seconds.
- **Porting impact:** none.
- **Future work:** none needed.

### 13. Invitations

- **Status:** implementation differs by design.
- **XNA / Xbox:** `Guide.ShowGameInvite`, the Guide's invitation, `NetworkSession.InviteAccepted`,
  `JoinInvited`.
- **CNA now:** CNA invitation IDs (not Xbox tokens), valid for 15 minutes and only while the session
  lives; receiving is never accepting. An `InviteAccepted` nobody was subscribed to is delivered once
  to the first subscriber, and only while its invitation is still there to join: an invitation used,
  cleared, revoked by its session ending, or declined never produces a later `InviteAccepted`
  (`OnlineInvitationTest`, `GuideUiTest.AnInvitationClosedWithoutAnAnswerStaysPendingAndIsAnsweredOnce`).
- **Reason:** CNA's own service.
- **Observable impact:** none beyond the lifetime.
- **Porting impact:** none; SAMPLE-096 (Invites) passes.
- **Future work:** none needed.

### 14. Voice routing policy

- **Status:** implementation differs by design (voice itself is implemented).
- **XNA / Xbox:** voice was routed automatically between the gamers of a session; each Xbox 360
  controller could carry its own headset; `NetworkGamer.HasVoice`, `IsTalking`,
  `IsMutedByLocalUser`, `LocalNetworkGamer.EnableSendVoice`; `GamerPrivileges.AllowCommunication`.
- **CNA now:** voice is NetworkSession realtime traffic, not a GamerServices service: microphone
  capture, 16 kHz mono in 20 ms frames, speech detection, Opus VOIP at 16 kbit/s, sent unreliably
  (ENet channel 1) to one gamer per remote machine allowed to hear it, relayed by the host like game
  data, over SystemLink or the authenticated online relay; each talker decoded and played through
  `DynamicSoundEffectInstance` (lost frames concealed, late ones dropped). Policy: **one microphone
  per machine**, owned by Player One's gamer (else the first local gamer), so other local gamers on
  that machine report `HasVoice` false; the Guide gamer card's Mute stops both directions for that
  local profile for the life of the process; a player the account has blocked is muted for as long
  as the block lasts (stored by the service, 20); `AllowCommunication` is honoured (Blocked: no
  voice; FriendsOnly: friends only). Local sessions carry no voice (one machine).
- **Reason:** a desktop has one audio input device in the common case.
- **Observable impact:** a second local player on one PC cannot talk; a Mute (unlike a Block) is
  forgotten at exit.
- **Porting impact:** minor.
- **Future work:** per-gamer devices and stored mutes are feasible.

### 15. The Guide's look, sounds and input ownership

- **Status:** CNA-equivalent only.
- **XNA / Xbox:** the console drew its own system UI (the Guide) and took the controller while it
  was up.
- **CNA now:** CNA's own visual language, typeface, icons and synthesized system sounds; every
  standard `Guide.Show*` opens its page after XNA's checks; the Home key or a pad's Guide button
  opens and closes the Guide. While the Guide is visible the game reads a neutral keyboard,
  connected pads with no buttons pressed and sticks and triggers centred, and no mouse buttons (the
  pointer and wheel still move); input held when the Guide closes stays hidden from the game until
  released (`SystemInputTest`, `GuideInputTest`, `SystemGuideTest.TheGuideButtonClosesTheGuideItOpened`).
  `CNA_GAMER_SERVICES_GUIDE_BUTTON=0` frees the Home key for a game that needs it.
- **Reason:** Xbox's UI is Microsoft's; the input rule reproduces what games observed.
- **Observable impact:** the Guide looks like CNA's.
- **Porting impact:** none.
- **Future work:** none needed.

## Partial implementations

### 16. Guests

- **Status:** partially implemented.
- **XNA / Xbox:** with `ShowSignIn(paneCount, onlineOnly: true)` further players may sign in as
  guests of a signed-in LIVE profile; `SignedInGamer.IsGuest` and `NetworkGamer.IsGuest` report it;
  LocalWithLeaderboards "allows guests ... to join".
- **CNA now:** a guest signs in as "Alice (1)" of the lowest-numbered signed-in account, without a
  password or a service call, and signs out with it. By session type:

  | Session type | Guest may create, join or be added | Notes |
  |---|---|---|
  | Local | yes | through an explicit gamer list or `AddLocalGamer`; the implicit selection (no list) skips guests |
  | LocalWithLeaderboards | yes, as Local | the game starts and ends normally; the accounts' rows reach the service's boards, while a guest's writes stay in this computer's local store (a guest has no service identity; `OnlineLeaderboardTest.AGuestInALocalWithLeaderboardsGameLeavesTheAccountsRowsToTheService`) |
  | SystemLink | yes, as Local | `NetworkGamer.IsGuest` is true on every machine: the roster carries a guest flag, in the join, the welcome and `AddLocalGamer` (`TwoProcessLoopbackTest.GuestsAreGuestsOnEveryMachineAcrossRealProcesses`); a peer built before 2026-09-30 reads other machines' guests as non-guests, and its own guests reach newer peers as non-guests |
  | PlayerMatch, Ranked | no | `AllowOnlineSessions` is false; creating, joining or adding a guest refuses (`GamerServicesNotAvailableException`, `GamerPrivilegeException` for `AddLocalGamer`) |

  A guest earns no achievements (`GamerPrivilegeException`). Without a service there are no
  guests: offline profiles are full local profiles.
- **Reason:** the service authenticates every participant of an online session and every
  leaderboard writer; a guest has no credential of its own.
- **Observable impact:** online guests are refused.
- **Porting impact:** specialized titles only (online split-screen with guests).
- **Future work:** online guests need a service-side guest identity bound to its host account.

### 17. `QualityOfService`

- **Status:** partially implemented.
- **XNA / Xbox:** `AvailableNetworkSession.QualityOfService`: round trip (average and minimum),
  estimated bandwidth up and down, `IsAvailable`.
- **CNA now:** SystemLink search results measure the round trip and both bandwidths (downstream
  from the host's probe train, upstream from the searcher's train timed by the host's responder).
  The round trip is one discovery exchange, so average and minimum are the same value. PlayerMatch
  and Ranked search results report `IsAvailable` false: before a join there is no path to the
  host, whose realtime traffic exists only through the relay after joining. Joined sessions measure
  `NetworkGamer.RoundtripTime` and `NetworkSession.BytesPerSecondSent`/`Received` on every
  transport.
- **Reason:** a service listing has no network path to measure.
- **Observable impact:** online lobbies cannot sort by connection quality.
- **Porting impact:** minor.
- **Future work:** a pre-join probe through the relay is feasible.

### 18. `FriendGamer.HasVoice`

- **Status:** implementation differs by design (the value is real; its definition is CNA's).
- **XNA / Xbox:** "whether this friend currently has voice capability". The IL reads one bit of
  the friend state the console supplied (`FriendState.FriendHasVoice`); what set it is not in the
  managed code or the documentation. `NetworkGamer.HasVoice` is documented as "has a voice
  headset".
- **CNA now:** true while the friend is online and the friend's client reported, with its latest
  heartbeat (every 30 seconds), that it can talk: voice built and switched on, a recording device
  present, and the account's communication privilege not Blocked. Offline friends, clients without
  voice and services without capability `friend-voice` report false. Only that one boolean leaves
  the friend's machine; no device detail. A client that stops without signing out stops counting
  when it stops counting as online (90 seconds).
- **Reason:** "currently has voice capability" names a live state of the friend's machine; this is
  the closest CNA can observe, and it is observed, not guessed.
- **Observable impact:** a friend with a headset shows as able to talk within 30 seconds of it
  changing; whether a console counted a muted or unplugged headset the same way is unknown.
- **Porting impact:** none.
- **Future work:** none needed.

### 19. Service push scope

- **Status:** partially implemented (push is implemented; its scope is bounded on purpose).
- **XNA / Xbox:** not observable; the console held a live connection.
- **CNA now:** a signed-in client keeps one authenticated WebSocket per account to `/cna/v1/events`
  (capability `events`; access token in the first message, checked again every minute). The server
  sends small hints, never data: `invitations` (an invitation sent to you), `messages` (a message
  to you), `friends` (a friend request, acceptance or removal involving you), `party` (a party
  invitation, join or departure). A hint only makes the matching watcher read now; the authoritative
  state always comes from the ordinary requests. Hints for one channel are coalesced while a write
  is in flight; there is no sequence number, and none is needed because nothing is applied from a
  hint. The client reconnects with back-off (4 to 60 seconds) and with its renewed token. Polling
  stays the fallback and the recovery path: invitations and parties every 5 seconds, messages and
  friends every 15, the heartbeat every 30, avatars of signed-in players every 10. Friends coming
  online, presence text, the session directory, leaderboards, achievements and avatar changes are
  read, not pushed. `CNA_GAMER_SERVICES_EVENTS=0` turns the channel off.
- **Reason:** hints keep one source of truth and make a lost hint cost only one poll interval.
- **Observable impact:** invitations, messages, friend requests and party changes arrive within
  about a second; "is now online" within 15 seconds.
- **Porting impact:** none.
- **Future work:** further topics are easy to add.

### 20. Privileges, privacy and blocking

- **Status:** implementation differs by design (CNA account policy).
- **XNA / Xbox:** `GamerPrivileges` (`AllowCommunication`, `AllowProfileViewing` and
  `AllowUserCreatedContent` as Everyone/FriendsOnly/Blocked; `AllowTradeContent`,
  `AllowPurchaseContent`, `AllowPremiumContent`, `AllowOnlineSessions` as booleans) came from the
  account's membership and the console's parental controls. The XNA IL checks one of them itself
  (`ShowMarketplace`, purchase); every other refusal was the console's result code
  `ProfileNotPrivileged`, surfacing as `GamerPrivilegeException`. XNA has no block-list API.
- **CNA now:**
  - The operator sets each account's privileges (`cna-gamer-services-admin <db> privilege ...`,
    the stand-in for parental controls); the player learns them at sign-in.
  - The service enforces communication (both ends) on messages, game and party invitations, join
    requests and friend requests, and profile viewing on reading another member's profile; voice
    follows `AllowCommunication` on the client.
  - `Guide.ShowComposeMessage` and `ShowGameInvite` throw `GamerPrivilegeException` when
    communication is Blocked, and `ShowGamerCard` of another gamer does when profile viewing is.
    That Xbox refused exactly these calls is inferred from the IL's result-code mapping and the
    Guide documentation's "check this privilege first" remarks; it was not observed on a console.
  - A member may block another from the gamer card (Block/Unblock): both ways, it ends the
    friendship, withdraws pending invitations, refuses messages, invitations, friend requests,
    profile reads and join requests, hides either one's sessions from the other's search, and mutes
    voice between them.
  - `AllowUserCreatedContent`, `AllowTradeContent` and `AllowPremiumContent` are reported but
    restrict nothing: CNA has no service for them. A game may read them, as XNA intended.
  - Local profiles and guests keep every privilege but online sessions (and purchase for local
    profiles).
  - `PrivacyTests` (service), `GuidePrivilegeTest`, `GuideUiTest.TheGamerCardBlocksAndUnblocksAMember`.
- **Reason:** CNA has no console parental controls; an operator policy is the honest equivalent.
- **Observable impact:** privileges and blocks behave as the operator and players set them; the
  console's exact rules (for example, whether a friends-only child could send a friend request)
  are CNA's.
- **Porting impact:** minor; privilege-checking code sees real values.
- **Future work:** self-service privacy settings for adults, if wanted.

### 21. Profile editing and account self-service

- **Status:** partially implemented.
- **XNA / Xbox:** players edited their gamer picture, motto and region on the console and created
  their own accounts.
- **CNA now:** players choose their avatar, gamer zone and online status in the Guide, and edit
  `GameDefaults` in their tools. The gamer picture is set by the operator
  (`cna-gamer-services-admin <db> picture`); motto and region have no setter (accounts report an
  empty motto and region "US"). Accounts are created by the operator; there is no self-registration
  or password change over the wire.
- **Reason:** account management was scoped out of the service; the operator provisions players.
- **Observable impact:** empty mottos.
- **Porting impact:** none.
- **Future work:** feasible.

### 22. Leaderboard columns and unmeasured Xbox rules

- **Status:** partially implemented.
- **XNA / Xbox:** `PropertyDictionary.GetValueStream` for Stream columns (size limit not
  documented); Xbox validated writes and chose centring rules not documented in XNA.
- **CNA now:** Stream columns work online, up to 256 bytes (CNA's bound); offline local boards (no
  service) do not keep Stream columns. Columns are at most 2 KiB per row; pages up to 100 rows;
  ranks are global ordinals with ties broken by user ID; centring starts half a page above the
  pivot. These are CNA rules; Xbox's validation and pivot rules were never measured.
- **Reason:** no evidence for the Xbox values.
- **Observable impact:** larger Stream payloads are refused.
- **Porting impact:** minor.
- **Future work:** raise the bound if a title needs it.

### 23. Without a service (offline profiles)

- **Status:** platform limitation (by configuration).
- **XNA / Xbox:** an Xbox 360 without Xbox LIVE had local profiles.
- **CNA now:** without an endpoint, gamers are local offline profiles: Local, LocalWithLeaderboards
  and SystemLink sessions, local achievements (with an optional title catalog) and local
  leaderboards; `IsSignedInToLive` false; no guests; `Gamer.GetFromGamertag` and its Begin/End
  throw `NotSupportedException` (no directory to search); service Guide pages and PlayerMatch/Ranked
  refuse, as on a console without LIVE.
- **Reason:** there is nothing to ask.
- **Observable impact:** as on an offline console.
- **Porting impact:** minor.
- **Future work:** none needed.

### 24. Credential persistence off POSIX

- **Status:** platform limitation.
- **XNA / Xbox:** the console remembered a signed-in profile.
- **CNA now:** on POSIX, rotating refresh credentials are kept in an owner-only directory
  (`CNA_GAMER_SERVICES_CREDENTIALS_DIR`, `0` disables). Windows and browser clients keep none and
  sign in on every launch; past 32 live sign-ins the service signs the account's oldest one out.
- **Reason:** no secure credential store is implemented for those platforms.
- **Observable impact:** a Windows player types the password at each launch.
- **Porting impact:** none.
- **Future work:** feasible with the platform's credential store.

### 25. Online transport

- **Status:** implementation differs by design.
- **XNA / Xbox:** peer-to-peer UDP with NAT traversal through Xbox LIVE.
- **CNA now:** every PlayerMatch/Ranked datagram (game data and voice) is an ENet datagram carried
  through the service's authenticated WSS relay; there is no direct peer path and no NAT
  traversal. SystemLink uses direct ENet/UDP on the LAN.
- **Reason:** a relay works behind any NAT and keeps the service's authority over membership.
- **Observable impact:** TCP head-of-line latency and jitter under loss; relay bandwidth on the
  server.
- **Porting impact:** minor for action games; none for turn-based ones.
- **Future work:** a direct path as an optimization is feasible.

## Platform limitations

### 26. Browser (WebAssembly) multiplayer and services

- **Status:** platform limitation, out of scope by the owner's decision (2026-09-28, confirmed in
  the Xbox-fidelity pass, GSX-E7).
- **XNA / Xbox:** no browser target.
- **CNA now:** browser builds render and run games; without a service they use local profiles.
  A configured service is refused (`BROWSER_SERVICE_TRANSPORT_UNAVAILABLE`), so accounts,
  PlayerMatch, Ranked, invitations and parties are unavailable; the relay refuses
  (`BROWSER_RELAY_TRANSPORT_UNAVAILABLE`); SystemLink discovery finds nothing; voice and the event
  channel are not compiled for Emscripten.
- **Reason:** browsers have no UDP and need an asynchronous transport; the owner accepted native
  multiplayer plus a limitations page.
- **Observable impact:** online and LAN play are unavailable in browsers.
- **Porting impact:** blocks multiplayer in browser builds only.
- **Future work:** a browser control transport (fetch, CORS/origin rules on the server), an ENet
  datagram adapter over `emscripten/websocket.h` and yielding End waits; see
  [`browser-network-readiness.md`](browser-network-readiness.md).

### 27. Voice availability by build, platform and device

- **Status:** platform limitation.
- **XNA / Xbox:** voice needed a headset; without one `HasVoice` was false.
- **CNA now:** `CNA_ENABLE_VOICE` (`AUTO` default: voice when pkg-config finds libopus >= 1.3;
  `ON` fails configuration without it; `OFF` never). libopus is not vendored: Windows builds carry
  voice only when one is provided, Emscripten never. At run time `CNA_VOICE=0` turns it off. Without
  voice (compiled out or turned off) every `HasVoice`/`IsTalking` is false, `EnableSendVoice` still
  checks its arguments, and the session works as before; a host without voice still relays other
  machines' voice frames. With no capture device the machine's gamer reports `HasVoice` false and
  still hears others; with no playback device received speech is dropped silently; a remote gamer
  whose machine has no voice reports `HasVoice` false. Voice never fails a session.
- **Reason:** Opus is a system dependency, not vendored; audio devices vary per machine.
- **Observable impact:** a build or machine without voice behaves as an Xbox without a headset.
- **Porting impact:** minor.
- **Future work:** vendoring Opus for Windows and a browser path are feasible.

### 28. Non-Linux clients and server hosts

- **Status:** not independently validated.
- **CNA now:** the online client paths (HTTPS, relay, event channel, credential store) and the
  server were tested on Linux. CNA builds for Windows, but its GamerServices online paths were not
  exercised there; Windows and macOS server builds are unvalidated.
- **Reason:** the test hosts are Linux.
- **Observable impact:** none known; defects there would be undiscovered.
- **Porting impact:** unknown.
- **Future work:** run the server's CNA harness corpus on those hosts.

## Not independently validated

### 29. Real microphone and speakers

- **Status:** not independently validated.
- **CNA now:** voice is implemented and tested in software: codec, routing, host relaying,
  concealment, muting, `EnableSendVoice`, two real processes both ways, SystemLink and online.
  Real microphone and speaker end-to-end audio was **not physically heard** on the test machine
  (no audio devices); capture and playback use the platform recording provider and the SoundEffect
  path the rest of CNA uses.
- **Reason:** the test machine has no microphone or speakers.
- **Observable impact:** unknown until heard: audio quality, latency and echo are unmeasured.
- **Porting impact:** unknown.
- **Future work:** a listening test on a machine with a headset.

### 30. Guide system sounds

- **Status:** not independently validated.
- **CNA now:** the Guide's synthesized sounds play through `SoundEffect` and are exercised by tests
  (silently without a device; `CNA_GAMER_SERVICES_SOUNDS=0` turns them off); they were never heard on
  the test machine.
- **Reason:** no audio output on the test machine.
- **Observable impact:** none on function; how they sound is unverified.
- **Porting impact:** none.
- **Future work:** listen once on a machine with speakers.

### 31. Public Internet deployment

- **Status:** not independently validated.
- **CNA now:** the architecture supports a remote TLS/WSS service (control, relay and events on one
  port); tests run two CNA processes in separate network namespaces behind outbound-only NAT with
  identical private addresses (game data can travel only through the relay), with server restart,
  host migration and a killed host; the server README has a public deployment checklist. No
  deployment between two genuinely remote networks has been qualified: public Internet latency,
  loss and failover are unmeasured. The service is unqualified there, not unsupported.
- **Reason:** no second, genuinely remote network was available.
- **Observable impact:** none in the tests; production latency and failure behaviour are unknown.
- **Porting impact:** none for porting; relevant to whoever operates a public service.
- **Future work:** a two-network qualification following the README's deployment checklist.

### 32. Console-only rules read from the Windows assemblies

- **Status:** not independently validated.
- **CNA now:** event order, exception order, APM behaviour and Ranked rules follow the XNA 4.0 IL
  and XML documentation; the IL available is the Windows build, and Xbox 360 behaviour was never
  traced on a console. Where the Windows assembly is a stub (avatars, leaderboards), CNA implements
  the Xbox behaviour the documentation describes (see "Windows stubs" below).
- **Reason:** no console traces exist for this project.
- **Observable impact:** unknown, believed minor: the rules games rely on are in the documentation.
- **Porting impact:** unknown.
- **Future work:** only with console traces.

### 33. `AvatarRenderer` on other renderers

- **Status:** not independently validated.
- **CNA now:** drawn and compared on OpenGL 3.3 (EasyGL) and Vulkan (the catalog reviews and
  `EasyGL_AvatarRenderer_Standard`, `Vulkan_AvatarRenderer_Standard`); HEADLESS runs the API tests.
  Other renderers use the same `SkinnedEffect` path but were not measured with avatars.
- **Reason:** those renderers were not built in the avatar passes.
- **Observable impact:** unknown; a renderer-specific skinning defect would show there first.
- **Porting impact:** unknown.
- **Future work:** run the avatar review (`cna_avatar_review`) on each renderer.

## Deployment and operations (not XNA compatibility)

### 34. One server process, one SQLite writer, in-memory hubs

- **Status:** deployment limitation.
- **CNA now:** one process owns the database (a second on the same database refuses to start with
  `DATABASE_IN_USE`, through an advisory lock on `<database>.lock`; the admin tool takes none), one
  SQLite writer, and in-memory state for the relay hub (live relay channels), the event hub (live
  event channels) and the per-account request budget.
- **Reason:** simple, measured (about 1,300 requests a second on the reference machine), enough
  for the intended deployments.
- **Observable impact:** none for games.
- **Porting impact:** none.
- **Future work:** see 36.

### 35. SQLite is the only database

- **Status:** deployment limitation.
- **CNA now:** SQLite in WAL mode; no PostgreSQL or MySQL backend exists. This is not a
  compatibility gap.
- **Reason:** one process needs no database server.
- **Porting impact:** none.
- **Future work:** another backend is feasible, but alone it does not give scale (36).

### 36. No cluster

- **Status:** deployment limitation.
- **CNA now:** no multi-node operation. Two copies against one SQLite database are refused (34),
  and would be wrong even with a shared database server: relay routing, event delivery and live
  session ownership live in one process's memory. Scaling out needs a shared durable database,
  distributed session ownership, event publish/subscribe between nodes, relay routing between nodes
  and lease/failover rules, not only a different database.
- **Reason:** not needed for the intended deployments.
- **Porting impact:** none.
- **Future work:** substantial; all five parts above.

### 37. Distributed hostile traffic

- **Status:** deployment limitation.
- **CNA now:** per-address limits (32 control connections, 600 new connections and 10 sign-ins a
  minute per address), 256 control connections and 1,024 relays per server, bounded queues and rate
  limits per relay channel. A flood from many addresses can still fill the connection table: a
  firewall, L4 proxy or DDoS filter in front is required. CNA does not claim DDoS protection.
- **Reason:** distributed floods are filtered upstream of any single service process.
- **Porting impact:** none.
- **Future work:** the operator's firewall or proxy.

### 38. Operational limits a game can meet

- **Status:** deployment limitation.
- **CNA now:** surfaced through the normal XNA exceptions for an unavailable service: 20,000
  recorded requests per account, title and day; 32 live sign-ins per account (the oldest signed
  out); 200 messages an hour; 64 pending invitations per recipient and 32 sends an hour; 8 event
  channels per account and 4,096 per server; 1,024 live sessions per title. A new TLS certificate
  needs a restart (clients and relays reconnect). Full list: the server README.
- **Reason:** bounds that keep one process healthy under abuse.
- **Observable impact:** a game meeting a limit sees the service as unavailable for that call.
- **Porting impact:** none for ordinary games.
- **Future work:** tunable in the server.

## Avatars

The model is the console's: the service owns each account's `AvatarDescription` and its revision;
the CNA runtime owns the standard, immutable catalogs (meshes, textures, animations); normal
rendering draws the description with the locally installed exact catalog and never streams
per-user models. A server catalog pack is only the update path for a catalog a client lacks. See
[`avatars.md`](avatars.md).

### 39. Baked shading and occlusion

- **Status:** partially implemented (visual quality, not API completeness).
- **XNA / Xbox:** the console's avatar renderer took `LightDirection`, `LightColor` and
  `AmbientLightColor` and drew with its own internal shading.
- **CNA now:** exactly those three inputs plus a per-material highlight; textures carry some
  painted shading, but there is no baked ambient occlusion, no contact shadowing between garments
  and body and no self-shadowing. This is not the XNA AvatarShadows sample's shadow, which the game
  draws itself and which works (SAMPLE-087).
- **Reason:** occlusion would have to be generated per catalog item and fed through the renderer
  families; XNA's `AvatarRenderer` has no input for it and CNA adds no public property for it.
- **Observable impact:** avatars look flatter in crevices than the console's.
- **Porting impact:** none.
- **Future work:** baked occlusion in a future catalog version, used internally by the renderer.

### 40. Visual fidelity of catalog v3

- **Status:** implementation differs by design (art direction).
- **CNA now:** original assets in the Xbox 360-era style (large head, short limbs, fitted clothes,
  decal faces). Known deficiencies: no baked occlusion (39); garments are fitted surfaces with
  hems, cuffs and pockets but no modelled folds; a preset authored for one body lets a planted foot
  drift 1-3 cm on the other; in the deepest crouch (FemaleIdleFixShoe) a few millimetres of trouser
  front can show above a shirt hem.
- **Porting impact:** none; functional compatibility does not depend on likeness.
- **Future work:** a later catalog version (v3 is released and does not change).

### 41. Catalog and description-format edge cases

- **Status:** partially implemented.
- **CNA now:** description formats 1 and 2 exist (format 2 adds facial hair and face shape); no
  newer format. Catalogs v1 and v2 are frozen and pinned by hash tests, v3 is the current catalog,
  also released and pinned; a description is always drawn from exactly the catalog version it
  names, and same-named files of different versions are told apart by size and SHA-256. Edges:
  - a description in an unknown future format is not a CNA description: height 0, female body type,
    and a renderer `Unavailable` (XNA's "results are undefined" case);
  - a catalog neither compiled in nor installed, with no service or with updates off
    (`CNA_AVATAR_CATALOG_UPDATES=0`) or larger than `maxAvatarCatalogBytes` (64 MiB by default): a
    service description comes as the service's marked projection onto a catalog the client has; a
    description exchanged directly between games draws as the deterministic default avatar (the
    newest body with the description's body type, height, build and colours, every item its slot
    default); ids are never read against another version;
  - a pack that fails to download or validate is discarded and not asked for again for a minute;
    an interrupted one leaves only a staging directory that a retry verifies and reuses;
  - installed packs are kept, never evicted or uninstalled; the server's file route allows each
    account 1 GiB of downloads per title and hour;
  - CNA builds older than format 2 read a format 2 description exchanged directly as invalid; the
    service gives them a format 1 copy.
- **Reason:** exact-version resolution never reads ids against another catalog, and a client
  cannot draw a format it does not know.
- **Observable impact:** a player without the catalog sees a projected or default avatar.
- **Porting impact:** minor.
- **Future work:** a newer catalog can arrive as a pack; a newer description format needs a client
  update.

## Host-language differences

These follow from C++, not from the service.

### 42. `PropertyDictionary` equality of an arbitrary `std::any`

- **Status:** host-language difference.
- **XNA / Xbox:** .NET compares any boxed values.
- **CNA now:** every XNA value type compares; a value of another C++ type in an `std::any` throws
  `NotSupportedException` when compared.
- **Reason:** C++ has no boxed equality for arbitrary types.
- **Observable impact / porting impact:** minor; only code storing its own C++ types in a
  dictionary meets it.
- **Future work:** none.

### 43. Leaderboard columns of non-XNA C++ types

- **Status:** host-language difference.
- **XNA / Xbox:** columns hold the XNA leaderboard column types.
- **CNA now:** `LeaderboardWriter` columns accept those types; another C++ type throws
  `NotSupportedException`.
- **Reason:** only the XNA column types have a service representation.
- **Observable impact / porting impact:** none for XNA-shaped code.
- **Future work:** none.

### 44. `AvatarDescription` is a value; `Changed` is shared

- **Status:** host-language difference.
- **XNA / Xbox:** `AvatarDescription` is a class; its `Changed` belongs to the one object.
- **CNA now:** in C++ it is a value, so every copy of one description shares one `Changed` event
  (subscribing through any copy subscribes to it). Begin results are caller-owned
  (`std::unique_ptr<System::IAsyncResult>`).
- **Reason:** C++ value semantics.
- **Observable impact / porting impact:** none; a port subscribes as in XNA.
- **Future work:** none.

## Also not limitations

**XNA's own refusals, kept:** `Guide.ShowGameInvite(string)` (Windows Phone only),
`AllowJoinInProgress = true` on Ranked, `NetworkSessionProperties` `Add`/`Insert`/`Remove`/
`RemoveAt`/`Clear`, writing an advertised `NetworkSessionProperties`, a second
`EndShowMessageBox`/`EndShowKeyboardInput` — as in XNA (final register).

**Windows stubs versus Xbox behaviour:** on Windows, XNA's `AvatarRenderer` drew nothing,
`AvatarDescription.BeginGetFromGamer` returned no avatar and `AvatarAnimation` was empty; on the
Xbox 360 they worked. CNA deliberately implements the Xbox behaviour, as it does for leaderboards,
parties and the other console-only services.

## Impact on porting XNA games and samples

**Normally irrelevant to ordinary XNA games:** Xbox LIVE compatibility (1), title-update
installation (5), Microsoft avatar assets (6), the CNA backend policies (8-13, 15, 18, 20), push scope (19),
profile self-service (21), credential persistence (24), the server's single process, SQLite,
cluster and flood limits (34-37), public Internet qualification (31), avatar shading and likeness
(39, 40), host-language differences (42-44).

**Only affects specialized titles or samples:** partner tokens (2), a real store (3), a
service-computed skill (4), expiring Recent scores (7), online guests (16),
online QoS before joining (17), large Stream
columns (22), several local talkers on one PC (14), avatar catalog edge cases (41).

**Platform-specific:** browser multiplayer (26), voice builds and devices (27), non-Linux
validation (28), physical audio (29, 30), avatars on unmeasured renderers (33), relay latency (25).

**Could block faithful porting:** only browser multiplayer (26), for a title whose browser build
must play online or on a LAN, and a title built around a Microsoft service CNA does not reproduce
(partner web services, the Xbox Marketplace, TrueSkill-based matchmaking). No verified XNA sample is
blocked.

**Samples.** GamerServices/Net/Avatar functionality is no longer a general blocker for ordinary
XNA 4 sample ports. The currently verified corpus includes AvatarShadows (SAMPLE-087), Invites
(SAMPLE-096), NetworkStateManagement over SystemLink and online PlayerMatch (SAMPLE-075), and the
achievements/leaderboards compatibility program (a CNA-written program on the XNA API), run on
2026-09-30 (`/rv/tmp/samples/*/evidence/*-20260930-xbox-fidelity`). This does not mean that every
historical XNA sample has been individually ported and executed. Samples depending specifically on
one of the limitations above may still require adaptation or may not be faithfully reproducible.
