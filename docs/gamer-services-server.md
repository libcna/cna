# CNA Gamer Services deployment

The CNA service has its own accounts, protocol and assets and is not Xbox LIVE compatible: CNA
provides a CNA-owned, XNA/Xbox-like GamerServices implementation with backend policies of its own.
Everything it does not do, does only in part or decides differently from Xbox is listed in
[`gamer-services-known-limitations.md`](gamer-services-known-limitations.md); the plans of the
server (GS-*), avatar-polish (GSP-*), Xbox-fidelity (GSX-*) and final-hardening (GSH-*) passes are
in Git history.

Configure outside ported gameplay code. A complete
`CNA::GamerServices::setConfigurationOverride` bypasses file/environment resolution; it is for
deployment/test hosts. Otherwise CNA reads user settings first, then the title manifest, then
nonempty environment fields. User settings default to `$XDG_CONFIG_HOME/cna/gamer-services.json`
or `$HOME/.config/cna/gamer-services.json`; the manifest is `cna-title.json` in launch CWD or the
explicit `CNA_GAMER_SERVICES_MANIFEST` path. An explicitly named missing manifest fails.
Unknown JSON fields and malformed present files fail rather than being rescued by a later
override. Neither file accepts credentials. Empty environment strings do not clear file values.

The endpoint is the full `https://host:port/cna/v1`; `gameId` must be provisioned and stable.
`caBundle` configures trust without disabling chain/hostname checks. `titleVersion`
(`CNA_GAME_VERSION`, one to four dot-separated numbers such as `1.2.0`) is sent when the service
advertises `title-version`. An administrator can set `title-minimum-version`; an older or
unstated version can then be refused at sign-in and session creation with the update-required
path. CNA installs no title updates itself and does not raise `InstallingTitleUpdate`; the
component retains its handler for that event.

Environment: `CNA_GAMER_SERVICES_ENDPOINT`, `CNA_GAME_ID`, `CNA_GAME_VERSION`, `CNA_GAMER_SERVICES_CA_BUNDLE`, `CNA_GAMER_SERVICES_INSECURE_LOOPBACK=1` (explicit development only). Insecure HTTP is restricted to numeric `127.0.0.1` or `[::1]`, proxies and redirects disabled. No insecure Internet/LAN default. No endpoint means no service accounts, never fabricated Stub Gamers after backend migration. Credentials are user data, never title manifest values.

```json
{"endpoint":"https://service.example/cna/v1","gameId":"my-title"}
```

**Local offline profiles.** Without an endpoint, gamers are local offline profiles, as on an
Xbox 360 without Xbox LIVE: `Guide.ShowSignIn(paneCount, false)` asks each pane for a profile name,
offering the stored profiles and creating a new one on first use (1 to 15 ASCII letters, digits and
single spaces, starting with a letter; names are unique ignoring case). A local gamer has
`IsSignedInToLive` false, no online-session or purchase privilege, and its own avatar (a random CNA
description stored with the profile, returned by `AvatarDescription.BeginGetFromGamer`). It plays
Local and SystemLink sessions, and its achievements and leaderboards use the local store; service
Guide panes and PlayerMatch/Ranked refuse it. `ShowSignIn(paneCount, true)` requires the service.
Offline leaderboard Rating assignments persist immediately. A LocalWithLeaderboards game's
EndGame transition also saves subsequent column changes before GameEnded, including changes
through retained streams. Unchanged cached rows are not written over another writer's updates.
Profiles are stored in `CNA_GAMER_SERVICES_PROFILES_DIR/profiles.json`, else
`$XDG_DATA_HOME/cna/gamer-services/profiles.json` or `$HOME/.local/share/cna/gamer-services/profiles.json`
(`%LOCALAPPDATA%\CNA\gamer-services\profiles.json` is the fallback after XDG/HOME, including on
Windows). Cooperating processes serialize writes and replace whole files; an unreadable store is
not overwritten. This is not an fsync/power-loss durability guarantee. Profiles whose `"autoSignIn"` is
`true` in that file, or the comma-separated names in `CNA_GAMER_SERVICES_AUTO_SIGN_IN` (up to four,
created when missing; for unattended runs and CI), are signed in when gamer services start and
appear, raising `SignedIn`, at the first `Dispatcher.Update`, as XNA reports profiles already signed
in at startup. Local profiles are never sent to a service. With an endpoint configured, sign-in uses
accounts only; with `ShowSignIn(panes, onlineOnly: true)` a player may instead type Guest to play as a
guest of the first signed-in account ("Alice (1)", `IsGuest`): nothing is sent to the service, the
guest earns no achievements, cannot be in a PlayerMatch/Ranked session (the service admits only
authenticated accounts), and signs out with its account (GSP-L5); the full guest matrix is below.
An entry may also carry `"gameDefaults"`, the profile's `GameDefaults`:
`gameDifficulty` (`Easy`/`Normal`/`Hard`), `controllerSensitivity` (`Low`/`Medium`/`High`),
`racingCameraAngle` (`Back`/`Front`/`Inside`), `primaryColor`/`secondaryColor` (`#rrggbb`) and the
booleans `autoAim`, `autoCenter`, `moveWithRightThumbStick`, `invertYAxis`, `manualTransmission`,
`accelerateWithButtons`, `brakeWithButtons`. A missing or unreadable field keeps XNA's unset value,
and the object is kept as written when the store is rewritten. An account keeps the same object on
its service (`profile.gameDefaults` / `profile.setGameDefaults`, capability `game-defaults`; the
service accepts only these keys and values), set by the player's tools or by administration
(`cna-gamer-services-admin <db> game-defaults <user>` with the object on stdin); CNA reads it at
sign-in into `SignedInGamer.GameDefaults` (GSP-L1).

**Offline achievement catalog.** A title can ship `GamerServices/Achievements.json` in its title
directory: a JSON array of the objects the service administration tool takes (`key`, `name`,
`description`, `howToEarn`, `score` 0..1000, `display`), with `picture` a title-relative PNG path in
place of a content hash. Offline, `GetAchievements` then lists every defined achievement, earned or
not, with its text, score and picture; `AwardAchievement` refuses keys the catalog does not define
(`GamerServicesNotAvailableException`, as the service does) and keeps an achievement's first earned
date; a local profile's `GamerScore`/`TotalAchievements` total what the catalog says was earned. A
malformed catalog is refused with an `InvalidOperationException` naming the file and the problem.
Without one, offline achievements are the earned keys alone, with no text or score. An account's
profile reports the `GamerZone` its member chose in the system Guide (Gamer zone) and a `Reputation`
of 0-5 stars. Its source data are real CNA player reviews (`prefer` or `avoid`); its numerical
policy is CNA's: 5 x prefer / (prefer + avoid), rounded to the nearest quarter star. XNA exposes the
stars, not how Xbox LIVE computed them, and CNA does not claim to reproduce that. A member nobody has
reviewed reports reputation 0; its independently chosen gamer zone is retained. Local profiles
report `GamerZone.Unknown` and reputation 0. No review-derived rating is invented (GSP-M1).

**System Guide.** In a game that draws (a `GamerServicesComponent` with a graphics device service),
the Home key, as in Games for Windows LIVE, or a controller's Guide button opens the Guide for that
player without any call from the game, and closes the Guide it opened: the sign-in picker when
nobody is signed in there, otherwise the Guide's pages (see "The Guide" below). This is how a player
sends an invitation from a game such as the XNA Invites sample, which never calls
`Guide.ShowGameInvite` itself. While the Guide is visible it owns the keyboard, the pads' buttons,
sticks and triggers and the mouse buttons; the game reads them released, and input held when the
Guide closes stays hidden from the game until it is released. `CNA_GAMER_SERVICES_GUIDE_BUTTON=0`
disables the Home/Guide-button shortcut for a game that needs the Home key.

Canonical service protocols and administration commands live in the companion
`cna-gamer-services-server/README.md`, `protocol/v1.md` and `protocol/relay-v1.md`; the README
also retains dated capacity/audit records. Database migration numbers are internal server history,
not CNA client configuration or a compatibility selector: clients negotiate capabilities on v1.
The native client needs libcurl with TLS and nlohmann/json; relay/events additionally need
WebSockets as described below. A browser build has no service transport (see
[browser readiness](browser-network-readiness.md)); Linux is the qualified online-client host.

**Current state.** With a service: four local authenticated players, standard Guide sign-in,
profiles and lookup, achievements (metadata, awards, pictures), mutual friends with joinable and
invitation state, rich presence, messages, player reviews, leaderboards (LocalWithLeaderboards,
PlayerMatch and Ranked with majority arbitration), standard avatars, and PlayerMatch/Ranked
NetworkSession Create/Find/Join/JoinInvited with Guide invitations, lobby readiness, machine removal,
traffic and round-trip statistics, over the authenticated relay (native only; browsers use the
limitations page). Online sessions honor `AllowHostMigration`: when the host leaves or its process
dies, the service hands the session to the machine with the lowest remaining gamer ID and every
machine raises `HostChanged` (see "Host migration" below; which machine is chosen is CNA backend
policy). `AddLocalGamer` adds a signed-in account
gamer to this machine's group through the service; it joins, with `GamerJoined` on every machine,
at a later Update, as XNA's kernel reports it. Parties: an account-level party of up to eight
friends (`SignedInGamer.PartySize` follows it at gamer-services updates, 0 without a party; the
Guide's Party page invites friends, answers party invitations and leaves; `ShowPartySessions` lists
party members' joinable games, or shows Friends without a party, as XNA does;
`LocalNetworkGamer.SendPartyInvites` invites the rest of the party to the current online session).
A friend's or party member's joinable game can be joined from their gamer card or the party pages:
the game hears `InviteAccepted` as for an invitation. Voice is routed automatically in SystemLink
and online sessions, as XNA routes it: the machine's microphone belongs to Player One's gamer (else
its first local gamer), speech is detected, encoded with Opus at 16 kHz in 20 ms frames and sent
unreliably to each remote machine allowed to hear it (the host relays, like game data); received
speech plays through the SoundEffect path. `NetworkGamer.HasVoice`, `IsTalking` and
`IsMutedByLocalUser` follow it, `LocalNetworkGamer.EnableSendVoice` switches one direction, and the
Guide gamer card's Mute stops both. Service communication/block snapshots reach existing gamers
on a successful `policy-refresh` heartbeat and Dispatcher.Update; voice then rechecks permissions
on its five-second interval. Service loss retains the last known policy, and queued sound is not
retroactively erased. The relay does not inspect or instantly revoke voice packets on privacy
changes. The microphone opens only while someone could hear it;
`CNA_VOICE=0` turns voice off, and a build without libopus carries none (`CNA_ENABLE_VOICE`).
Voice media is not a Gamer Services control-plane feature: the control protocol has no voice
operation, and NetworkSession voice is carried as realtime session traffic through SystemLink or the
authenticated online relay. It is tested in software (codec, routing, relaying, muting, two real
processes); a real microphone and speaker were not physically heard on the test machine.

The console's social notifications appear as Guide toasts: a new message, a friend request, a friend
coming online, and invitations to games and parties. **Push:** a signed-in client keeps one
authenticated WebSocket per account to the service's event channel (`/cna/v1/events`, capability
`events`; the access token goes in the first message and is re-checked every minute). The service
sends small "something changed" hints -- `invitations`, `messages`, `friends`, `party` -- only after
the request that caused them succeeded; the client then schedules a fresh authoritative read
with the ordinary requests as the update pump processes them. Hints carry topics rather than
authoritative entity data or a replay sequence; duplicates can cause extra reads and lost hints
are recovered by a later successful poll. Failed connection attempts back off from 4 to 60 seconds;
an established channel loss initially retries after 2 seconds. Reconnect uses
its renewed token; `CNA_GAMER_SERVICES_EVENTS=0` turns the channel off. Polling remains the fallback
and recovery path: invitations and parties are read every 5 seconds, messages and friends every 15;
a friend coming online, presence, the session directory and leaderboards are only read, never pushed.

Guests (`ShowSignIn(onlineOnly)`, GSP-L5), by session type:

| Session type | A guest may create, join or be added |
|---|---|
| Local | yes, through an explicit gamer list or `AddLocalGamer` (the implicit local-gamer selection skips guests) |
| LocalWithLeaderboards | yes, as Local; a guest's leaderboard writes stay in the local store, not on the service's boards |
| SystemLink | yes, as Local; `NetworkGamer.IsGuest` is true on every machine (the roster carries the guest flag) |
| PlayerMatch, Ranked | no: a guest has no service credential, so `AllowOnlineSessions` is false and the service admits only authenticated accounts |

Friends see the online status (online, away, busy) a player chooses in the Guide
(`FriendGamer.IsAway`/`IsBusy`). Rich presence is sent during Dispatcher.Update; friend online state
reflects authenticated activity within 90 seconds; Update schedules an authenticated heartbeat every
30 seconds. [`gamer-services-known-limitations.md`](gamer-services-known-limitations.md) lists what
is not done, done in part, or decided differently from Xbox. This is not a production
service release: public Internet deployment has not been independently qualified (local and NAT
relay tests exist, and the server README has a deployment checklist), and no measured Xbox
event/validation parity across every method is claimed.

## Pictures, avatars, leaderboards and credentials

Achievement and profile picture streams read immutable SHA-256 resources from the service. The
per-user cache defaults to `$XDG_CACHE_HOME/cna/gamer-services/assets` or
`$HOME/.cache/cna/gamer-services/assets`; CI can override `CNA_GAMER_SERVICES_CACHE_DIR`.
Only content hashes form entry filenames. Reads verify the bytes; hash/read failures are misses
and the damaged entry is removed, while invalid file type/size is a miss without guaranteed
deletion. Entries are staged and renamed into place; an interrupted write can leave a temporary,
which later writes remove after ten minutes. API picture reads return separate read-only streams
at position zero. The default committed-entry capacity is 256 MiB with mtime-based least-recent-use
eviction. Cooperating native writers serialize the complete admission, eviction and publication
transaction through the persistent `.capacity.lock` file; a failed lock refuses the write. Each
successful write bounds hash-named entry bytes to that writer's configured capacity, including
across processes. Temporary files and lock metadata consume separate overhead; readers' mtime
updates still make LRU approximate. The eight-process capacity regression uses a smaller 16-MiB
budget and verifies every surviving entry's hash; lock obstruction/link controls refuse writes.
Whole-entry sharing does not imply power-loss durability.
A full/unwritable cache can still retrieve from the service. PNG assets are bounded to
512x512/512 KiB; larger version-2 GLB resources have a 16-MiB
transport ceiling. Hashes do not make malformed image/model content valid.

Avatars (`avatars` capability): the service stores one CNA-encoded description
per account and imports CNA's avatar catalogs (`cna-gamer-services-admin <db> avatar-catalog
<CNA>/modules/gamer-services/assets/avatars/v3`; catalog v3 is the baseline, and the service
refuses catalogs and descriptions below it). `AvatarDescription.BeginGetFromGamer` reads a
gamer's description (negotiated: the stored avatar, or a projection onto a catalog this client has)
and nothing else; the catalogs of this release draw it locally, and no per-user model is ever
downloaded. A catalog the release lacks is
installed whole as a validated pack through the binary file route and kept outside the asset
cache (`avatar-catalog-packs`, `files`). See `docs/avatars.md`.


Online leaderboard reads use server-provisioned title/key/game-mode definitions, sort direction and
scalar columns. Offset, gamer-centered and gamer-restricted reads page remotely rather than loading
an entire board or dropping gamers signed in on another machine. Begin/End completions use the
Dispatcher update boundary; native C callbacks return completed reads. Online writer setters now
retain transient data while Playing and flush after final write events at host EndGame;
explicit early leave uses IsLeaving=true. Ranked rows are arbitrated by strict majority of the reporting
machines (GS-006e); a board may define `stream` columns (XNA `GetValueStream`, at most 256 bytes: a writer's entry
hands out a writable stream that commits with the entry, a read a read-only one; GSP-L4).
Offline local boards also persist these stream contents with the same CNA-specific 256-byte bound;
reopened writers can grow their restored streams. CNA computes no TrueSkill. Admin seed commands
are development fixtures, not a gameplay write mechanism. Page limits, Stream columns and the
missing recent window and TrueSkill are documented in the server protocol.


Refresh authority is user-level data: only rotating refresh credentials and their estimated
expiry are stored, bound to endpoint, title and local slot; passwords, access tokens and gameplay
metadata never are, and nothing is logged. A process lease (one per endpoint and title) prevents
concurrent use of one refresh family. Where it is kept:

| Host | Store |
|---|---|
| Linux, default location, a desktop keyring answering | The freedesktop Secret Service (GNOME Keyring, KWallet, ...), loaded at run time (libsecret-1.so.0; no build dependency). Older private records can migrate into it. A refused write falls back to the private file below. The initial availability probe has a two-second bound; this is not a deadline on every later synchronous keyring operation |
| Linux without a keyring (headless, no session bus), macOS, other POSIX | Owner-only 0700 directory, 0600 atomic files, no-follow opens, hard-link and size checks: protected plaintext, not encrypted at rest. macOS does not use the Keychain (not integrated) |
| Windows | `%LOCALAPPDATA%\cna\gamer-services\credentials`, each record sealed to the Windows user with DPAPI (the endpoint/title binding as entropy); checked under Wine, never on Windows itself |
| Browser | Nothing is kept |

`$XDG_STATE_HOME/cna/gamer-services/credentials` or `$HOME/.local/state/cna/gamer-services/credentials`
is the POSIX default directory. `CNA_GAMER_SERVICES_CREDENTIALS_DIR` names another directory, which
then always holds private files (CI, tests, deployments), or `0` disables persistence;
`CNA_GAMER_SERVICES_KEYRING=0` keeps Linux on files.

Dispatcher.Update restores up to four real service identities, refreshes access before expiry and
maintains heartbeat. Transport loss retains the last authenticated identity and uses bounded
maintenance backoff; confirmed revocation signs it out at the Update boundary. Explicit sign-out
clears that slot's persistent authority. Rotation uses server remaining lifetimes to tolerate clock
offset. A lost successful refresh response or interrupted persistence may require fresh Guide
sign-in because replaying an old refresh credential revokes its family. Reconnect and restart tests
are service-control evidence; they do not prove Internet game-data transport or every Xbox event rule.

## The Guide

CNA draws its own system UI over the game, in one visual language of its own (not a copy of the
console's): the standard Guide calls (`ShowSignIn`, `ShowFriends`, `ShowGamerCard`, `ShowMessages`,
`ShowComposeMessage`, `ShowPlayers`, `ShowPlayerReview`, `ShowGameInvite`, `ShowParty`,
`ShowPartySessions`, `ShowMarketplace`, `ShowAchievementsEXT`, the message box and the keyboard) open
its pages, and the Home key or a pad's Guide button opens the system Guide itself
(`CNA_GAMER_SERVICES_GUIDE_BUTTON=0` turns that off). The system Guide has the signed-in gamer's
portrait, gamertag, score and status, and a rail of pages -- Home, Friends, Party, Messages,
Achievements, Leaderboards, Recent players, Game content, Settings -- switched with Q/E or the
shoulder buttons. Sign-in picks a profile for a player slot among four; gamer cards show the avatar,
score, zone, reputation, presence and relationship, with the actions that apply (join, invite to
the game or the party, message, review, mute, friend request). Edit avatar opens the full-screen
avatar editor: a live preview with a camera that follows the category, rendered item cards,
named face controls, try-on before keeping, and a save that asks. Notifications appear at
`Guide.NotificationPosition` (bottom centre by default) inside the title-safe area, one at a time
for four seconds each, and never take input. A game that calls `EndShowMessageBox` or
`EndShowKeyboardInput` before the player answers waits, as in XNA: its own Update and Draw stop
while the Guide keeps reading input and drawing over a cleared screen; with no running `Game` to
draw it, the early End throws `InvalidOperationException`. A keyboard, a pad or the mouse drive
every page. Motion is short;
`CNA_GAMER_SERVICES_REDUCED_MOTION=1` removes it. Quiet system sounds, synthesized by CNA, play
when a player drives the Guide; `CNA_GAMER_SERVICES_SOUNDS=0` turns them off.

## Host migration (GSP-K1)

`NetworkSession.AllowHostMigration` works for PlayerMatch and Ranked sessions as XNA describes it:
only the host sets it, and every machine reads the host's value. The service decides: when the host
machine leaves (`Dispose`/leave) or stops (its relay connection closes and it does not reconnect
within 20 seconds, the relay-recovery grace), the machine holding the lowest remaining gamer ID
becomes the host and that machine's first gamer the host gamer. That is CNA backend host-election
policy: XNA shows only that a migration happened and raises `HostChanged`, not how the new host is
picked, and CNA does not claim Xbox LIVE picked the same way. On every machine the old host's
gamers leave (`GamerLeft`), then `HostChanged` is raised with the departed host as `OldHost`;
the new host takes over settings, `StartGame`/`EndGame`, readiness relaying and machine removal.
Gamer IDs, the roster and session properties carry over unchanged. A client whose host vanished
waits up to 30 seconds for the service's decision; if the session is gone, or migration is off, it
ends with `HostEndedSession` as before. Ranked arbitration rounds record members and machines, so
a handover does not disturb them; the old host's missing report resolves by the round's timeout.
Services without the `host-migration` capability never receive the setting, and their sessions end
with their host. Evidence: `OnlineNetworkSessionTest` (a joiner becoming host, a client following
the directory to a new remote host, the unchanged no-migration path), server directory and relay
tests, and `service_cna_session_migration` / `service_cna_session_host_crash` (two processes over
the TLS/WSS relay; the crash case kills the host process in its own NAT namespace).

## Adding local gamers (GSP-K2)

Online `NetworkSession.AddLocalGamer` validates as XNA does (null, disposed, duplicate, a game in
progress without join-in-progress, ended, no open public slot, the session's local-gamer limit --
4 for sessions created, found or invite-joined with a gamer list, as in the reference) and then
asks the service to add the gamer to this machine's group (`sessions.addMembers`, capability
`session-add-members`). The gamer arrives at a later Update: a `LocalNetworkGamer` sharing the
machine, with its directory ID, and `GamerJoined` on every machine (the host announces a grown
group to the others). A gamer without a service account refuses with `GamerPrivilegeException`;
a service refusal (slots taken meanwhile, the account in another session) leaves the gamer out.
Evidence: `OnlineNetworkSessionTest` (host and client adds, a refused add), server directory
tests, and `service_cna_session_add_gamer` (two processes in separate NAT namespaces; the joiner
adds its second gamer, then exchanges data and plays a Ranked round with it).

## Transport and ownership contracts

The staged GS-* construction record is in Git history. The current layers have distinct
responsibilities:

| Layer | Contract |
|---|---|
| HTTPS control | Configuration, capabilities, authenticated accounts, directory membership, invitations and durable service data |
| WSS relay | One-use grants bound to title/session/machine and the exact authenticated local group; opaque ENet datagrams with server-provided source identity |
| Native session engine | ENet reliability/fragmentation, roster admission, host forwarding, lobby/game observations, recovery and resource ownership |
| Public XNA layer | NetworkSession/Gamer/Guide objects and events published on their owning update thread |

Membership in the directory does not establish direct Internet connectivity. Online game and voice
traffic uses the relay; SystemLink retains its direct IPv4 ENet/discovery route. No STUN/ICE or
direct-Internet fallback is implemented.

### Native prerequisites and captured authority

Control transport uses libcurl with TLS and nlohmann/json; a native build without the transport
refuses a configured endpoint. Relay transport additionally needs libcurl >= 7.86 built with
WebSockets and the required ws/wss protocols. A version number alone is insufficient: the client
checks actual runtime capabilities. Browser builds have no service transport.

The relay derives its authority/path from the validated configuration retained by the originating
backend. Changing environment variables or the global override later cannot redirect that
backend's credentials or tickets. TLS verifies chain/hostname and requires TLS >= 1.2; redirects
and proxies are disabled. Only explicit insecure numeric-loopback configuration permits ws/http.
Tickets are ephemeral and are not gameplay configuration, persistent refresh records or URLs.

Control exchanges reuse a serialized curl handle, allowing connection reuse; a permanently open
connection or one handshake for the process lifetime is not guaranteed. With the service's
request-outcomes capability, selected lost-response transport errors can retry once using the same
request ID. Login, refresh and relay-ticket issuance are excluded because their secret-bearing
outcomes are not replayed. This is not a retry using a fresh mutation ID.

### Packet admission and limits

The relay bridge uses stable per-machine loopback UDP routes inside the native client; that local
carrier does not make the external WSS route loopback-only. One worker owns its socket/partial
frame state and publishes observations rather than mutating XNA objects on that worker.
Local UDP source/length checks, bounded queues and ENet datagram validation run before injection.

The session engine validates authenticated source-machine identity, complete local gamer groups,
host-owned properties, known sender/target IDs and control direction before applying observations.
Service ordinals map to wire gamer IDs 1..31. The host still relays game payloads and is trusted:
membership validation is not malicious-host anti-cheat.

Current private relay policies include a 1 MiB logical packet ceiling, 4 MiB ENet waiting data per
peer and at most 2,048 fragment bitmap slots. Engine pending/local data and globally unacknowledged
outgoing data each have 128-message/4 MiB bounds; outgoing control has a separate 64-message,
256 KiB allowance. These are CNA resource policies, not measured Xbox packet-size guarantees.
The public payload includes protocol overhead, so the entire logical ceiling is not caller data.

Destruction closes owned transport/preparation resources and releases service membership.
Short service/relay outages can recover within the engine's 15-second recovery window; host
migration has its separate 30-second wait and the service's relay grace. These are detected-state
deadlines, not a promise that every wall-clock outage shorter than 15 seconds succeeds.

### Begin/End and update ownership

Online Find/Create/Join/JoinInvited begin pending and publish completion/callbacks on the owner
through Dispatcher.Update; End can pump that origin while waiting. Preparation is complete only
after host resources or the joiner's exact authenticated welcome. Abandoning a pending result
suppresses its callback and releases owned/busy state; already queued read-only work can finish.
An unconsumed prepared result continues pumping/renewing and can fail before consumption.

Native raw Net Begin results are caller-owned; use std::unique_ptr<System::IAsyncResult> where
that API returns a raw pointer. End consumes the operation once; result metadata remains alive
until the caller releases it. Synchronous wrappers release their metadata. The C API's existing
_async functions are blocking Begin/End adapters with completion callbacks, not independently
polled asynchronous-result handles. See [the C ABI contract](c-api/README.md).

Service profile/achievement/leaderboard results retain their originating backend; a reader retains
the original title/backend for subsequent paging. Dispose releases that paging context, while
already returned gamer snapshots remain owned by the reader until its destruction.
Result release, public object/event publication and Net updates belong to the owning update
thread. Retained superseded origins can progress without publishing stale identity events.
This does not establish all-method Microsoft XNA APM/error-order parity.

NetworkSessionProperties keeps eight nullable signed-integer slots. Structural collection edits
throw NotSupportedException; host-owned live writes and immutable advertised snapshots have their
own guards. Const range helpers prevent bypassing those writes. No historical object-size number
is a stable public C++ ABI promise; rebuild consumers when changing the dependency/runtime ABI.

## Verification and deployment boundary

Useful source locations are ServiceConfiguration.cpp, GamerServicesBackend.cpp, CredentialStore.cpp
and AssetDiskCache.cpp under modules/gamer-services/src/Internal, and ServiceENetSession,
ServiceGamePacketPolicy, RelayTransport, RelayWebSocket and RelayEnetPolicy under
modules/net/src/Internal. Public adapters and tests live in their respective module trees.

Check vendored parser/header/golden bytes against a local companion checkout:

```sh
python3 tools/net/check_service_protocol.py ../cna-gamer-services-server
```

That checks six files; it does not prove that every client operation, server feature, deployed
binary or database migration is compatible. Configuration, asset-cache, credential, typed-directory,
online-session and update-subscription tests provide focused software evidence.

The companion service registers raw relay, owned-engine and public-session/invite/restart/migration/
add-gamer CTests. Several require absolute CNA harness paths; the stronger NAT cases require real
Linux namespaces and slirp4netns and return 77 when prerequisites are missing. Historical loopback
or isolated shared-host passes are not new public-Internet, physical audio, Windows/macOS or Xbox
qualification. Follow [the manual qualification plan](gamer-services-manual-qualification.md) for
the exact seven namespace gates and 19 remaining lab scenarios, and retain individual pass/skip
outcomes. This documentation review does not deploy a service or qualify a production release.
