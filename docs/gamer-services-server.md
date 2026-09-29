# CNA Gamer Services deployment

The new CNA service has its own accounts/protocol/assets and is not Xbox LIVE compatible. The implementation is in progress: see [living plan](../plans/plan_gamer_services_server.md). Symbol coverage is not behavioral completeness.

Configure outside ported gameplay code. Precedence is complete `CNA::GamerServices::setConfigurationOverride` (deployment/test hosts only), environment fields, title manifest, user settings. User settings default to `$XDG_CONFIG_HOME/cna/gamer-services.json` or `$HOME/.config/cna/gamer-services.json`. Title manifest defaults to `cna-title.json` in launch CWD or explicit `CNA_GAMER_SERVICES_MANIFEST`. No credential fields are accepted. Endpoint is full `https://host:port/cna/v1`; `gameId` must be administrator-provisioned and stable. Optional `caBundle` configures trust, never bypasses TLS checks.

Environment: `CNA_GAMER_SERVICES_ENDPOINT`, `CNA_GAME_ID`, `CNA_GAMER_SERVICES_CA_BUNDLE`, `CNA_GAMER_SERVICES_INSECURE_LOOPBACK=1` (explicit development only). Insecure HTTP is restricted to numeric `127.0.0.1` or `[::1]`, proxies and redirects disabled. No insecure Internet/LAN default. No endpoint means no service accounts, never fabricated Stub Gamers after backend migration. Credentials are user data, never title manifest values.

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
Profiles are stored in `CNA_GAMER_SERVICES_PROFILES_DIR/profiles.json`, else
`$XDG_DATA_HOME/cna/gamer-services/profiles.json` or `$HOME/.local/share/cna/gamer-services/profiles.json`
(`%LOCALAPPDATA%\CNA\gamer-services` on Windows); writes are atomic and serialized between
processes, and a store that cannot be read is never overwritten. Profiles whose `"autoSignIn"` is
`true` in that file, or the comma-separated names in `CNA_GAMER_SERVICES_AUTO_SIGN_IN` (up to four,
created when missing; for unattended runs and CI), are signed in when gamer services start and
appear, raising `SignedIn`, at the first `Dispatcher.Update`, as XNA reports profiles already signed
in at startup. Local profiles are never sent to a service. With an endpoint configured, sign-in uses
accounts only; with `ShowSignIn(panes, onlineOnly: true)` a player may instead type Guest to play as a
guest of the first signed-in account ("Alice (1)", `IsGuest`): nothing is sent to the service, the
guest earns no achievements, cannot be in a PlayerMatch/Ranked session (the service admits only
authenticated accounts), and signs out with its account (GSP-L5). An entry may also carry `"gameDefaults"`, the profile's `GameDefaults`:
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
Without one, offline achievements are the earned keys alone, with no text or score. Profiles report
`GamerZone.Unknown` and a reputation of 0 on every path: CNA keeps neither.

**System Guide.** In a game that draws (a `GamerServicesComponent` with a graphics device service),
the Home key, as in Games for Windows LIVE, or a controller's Guide button opens the Guide for that
player without any call from the game: sign in when nobody is signed in there; Friends, Invite to
game, Messages and Sign out for an account; Sign out for a local profile. This is how a player
sends an invitation from a game such as the XNA Invites sample, which never calls
`Guide.ShowGameInvite` itself. Guide message boxes answer to the keyboard (arrows/Tab move, Enter or
Space chooses, Escape cancels) and to controllers (D-pad or left stick, A, B/Back) as well as the
mouse. `CNA_GAMER_SERVICES_GUIDE_BUTTON=0` disables the Home/Guide-button shortcut for a game that
needs the Home key.

Server canonical protocol and administration commands live in sibling `cna-gamer-services-server/README.md` and `protocol/v1.md`. Desktop client depends on libcurl with TLS support and nlohmann/json. Browser/other secure platform transport integration remains unverified.

**Current state.** With a service: four local authenticated players, standard Guide sign-in,
profiles and lookup, achievements (metadata, awards, pictures), mutual friends with joinable and
invitation state, rich presence, messages, player reviews, leaderboards (LocalWithLeaderboards,
PlayerMatch and Ranked with majority arbitration), standard avatars, and PlayerMatch/Ranked
NetworkSession Create/Find/Join/JoinInvited with Guide invitations, lobby readiness, machine removal,
traffic and round-trip statistics, over the authenticated relay (native only; browsers use the
limitations page). Online sessions honor `AllowHostMigration`: when the host leaves or its process
dies, the service hands the session to the machine with the lowest remaining gamer ID and every
machine raises `HostChanged` (see "Host migration" below). `AddLocalGamer` adds a signed-in account
gamer to this machine's group through the service; it joins, with `GamerJoined` on every machine,
at a later Update, as XNA's kernel reports it. Not implemented: voice, TrueSkill
computation, and party and marketplace services. Friends see the online status (online, away,
busy) a player chooses in the Guide (`FriendGamer.IsAway`/`IsBusy`). Rich presence is sent during
Dispatcher.Update; friend online state reflects authenticated activity within 90 seconds; Update
schedules an authenticated heartbeat every 30 seconds. `docs/xna-4-api-coverage.md` §8–9 lists the
state per feature. This is not a production service release or a claim of measured Xbox
event/validation parity across every method.

The sections below record the service's construction slice by slice; where one says something
remained unfinished, a later slice (and the summary above) supersedes it.

Achievement/profile picture streams now read immutable SHA-256 resources from the service. The
per-user cache defaults to `$XDG_CACHE_HOME/cna/gamer-services/assets` or
`$HOME/.cache/cna/gamer-services/assets`; CI can override `CNA_GAMER_SERVICES_CACHE_DIR`.
Only content hashes form filenames; data is verified on download and on every cache read (a damaged
entry is a miss and is deleted), entries are written to a temporary and renamed into place (an
interrupted write leaves no entry; temporaries older than ten minutes are deleted by later writes),
and each API call returns a separate read-only stream at position zero. The cache keeps at most
256 MiB, evicting the least recently used entries (a read counts as a use); several processes may
share it. A full or unwritable cache still allows retrieval. PNG assets are bounded to 512x512/512 KiB; larger version-2 GLB resources have a 16-MiB
transport ceiling. Hashes do not make malformed image/model content valid.

Avatars (`avatars` capability, server schema 12): the service stores one CNA-encoded description
per account and imports CNA's avatar catalogs (`cna-gamer-services-admin <db> avatar-catalog
<CNA>/modules/gamer-services/assets/avatars/v1`). `AvatarDescription.BeginGetFromGamer` reads a
gamer's description; the renderer downloads only the files of a newer catalog that the library
does not embed, through the same verified hash cache. See `docs/avatars.md`.


Online leaderboard reads use server-provisioned title/key/game-mode definitions, sort direction and
scalar columns. Offset, gamer-centered and gamer-restricted reads page remotely rather than loading
an entire board or dropping gamers signed in on another machine. Begin/End completions use the
Dispatcher update boundary; native C callbacks return completed reads. Online writer setters now
retain transient data while Playing and flush after final write events at host EndGame;
explicit early leave uses IsLeaving=true. Ranked rows are arbitrated by strict majority of the reporting
machines (GS-006e); a board may define `stream` columns (XNA `GetValueStream`, at most 256 bytes: a writer's entry
hands out a writable stream that commits with the entry, a read a read-only one; GSP-L4; offline
local boards do not keep them) and CNA computes no TrueSkill. Admin seed commands
are development fixtures, not a gameplay write mechanism. Page limits and unsupported Stream/recent
window/TrueSkill behavior are documented in the server protocol.


Refresh authority is user-level data. On POSIX, the private credential directory defaults to
`$XDG_STATE_HOME/cna/gamer-services/credentials` or `$HOME/.local/state/cna/gamer-services/credentials`.
`CNA_GAMER_SERVICES_CREDENTIALS_DIR` chooses a CI/deployment directory; `0` disables persistence.
Only rotating refresh credentials and estimated expiry are stored, bound to endpoint, title and
local slot. Passwords, access tokens and gameplay metadata are absent. Owner-only 0700 directories,
0600 atomic files, no-follow opens, hard-link/size checks and a process lease prevent unsafe reads
and concurrent use of the same refresh family. These are protected plaintext files, not an OS
keychain. Windows/browser credentials remain ephemeral until a secure platform provider exists.

Dispatcher.Update restores up to four real service identities, refreshes access before expiry and
maintains heartbeat. Transport loss retains the last authenticated identity and uses bounded
maintenance backoff; confirmed revocation signs it out at the Update boundary. Explicit sign-out
clears that slot's persistent authority. Rotation uses server remaining lifetimes to tolerate clock
offset. A lost successful refresh response or interrupted persistence may require fresh Guide
sign-in because replaying an old refresh credential revokes its family. Reconnect and restart tests
are service-control evidence; they do not prove Internet game-data transport or every Xbox event rule.


The server now implements a persistent control-only PlayerMatch/Ranked directory with authenticated
multi-local membership, property filtering and leased host revisions. Standard online Create, Find,
Join and JoinInvited use this directory (GS-007/008); public Internet deployment acceptance is not
claimed. Membership in this directory alone cannot establish Internet connectivity.

NetworkSessionProperties follows the fixed XNA eight-slot shape: Count=8, nullable signed integers
at indices 0..7. Add/Insert/Remove/RemoveAt/Clear throw NotSupportedException; use the ordinary indexer
for configuration. IsReadOnly reports false as the managed reference does. Live-session writes
require the current host and reject disposed owners; advertised snapshots reject writes. C API
symbols remain available and map structural refusal to CNA_RESULT_NOT_SUPPORTED. CNA range helpers
expose only const iterators so writes cannot bypass ownership checks. Rebuild all consumers with
sharp-runtime's GS-007b proxy layout (16->24 bytes on the measured 64-bit ABI).

The invitation control service now persists recipient-bound pending/accepted invitations and
atomically joins up to four authenticated local users through an explicitly accepted invite,
using available private then public slots. The Guide's invitation prompts, InviteAccepted and
JoinInvited use it (GS-007e3); the private WSS relay is implemented and measured separately below. Invitations are CNA service IDs, not Xbox LIVE tokens.

CNA's private backend now provides typed directory create/find/join/get/touch/update/leave and
recipient send/list/get/accept/dismiss invitation operations with strict response validation.
Public XNA classes receive logical snapshots, not JSON, credentials or persistence objects.
An explicitly selected deterministic fake covers leases, host revisions, fixed-property filtering,
private slot allocation, consent, replay and quotas. It never substitutes for the real online backend.
Multi-local requests collect current credentials under the transport lock and can refresh expired
secondary players without signing out a valid machine owner. The real two-CNA-process TLS probe
uses internal control APIs for this validation; it is not a standard-API gameplay acceptance sample
and it proves neither relay nor Internet realtime connectivity.

The private backend can now request one-use, short-lived relay authority for its owning machine
and exact authenticated local group. Server grants bind all members' revocable login families;
ordinary access refresh preserves authority, while revocation/leave/host expiry invalidates it.
Tickets are ephemeral and never part of gameplay source, URLs, logs or persistent client files.
The secure WSS endpoint and private CNA ENet bridge are implemented; ticket issuance alone
does not enable public PlayerMatch/Ranked sessions.

The sibling server now provides a separate authenticated WSS datagram relay at `/cna/relay/v1`.
Its canonical header/handshake golden vectors are synchronized with CNA. One-use authority binds
all local users, title, session and machine; outgoing frames carry server-injected source identity.
Application queues reserve bounded space before executor posting and revalidate revocable grants.
Standalone verified-WSS forwarding tests are server transport evidence. The private CNA libcurl
bridge now carries genuine ENet datagrams through stable per-machine loopback UDP routes; two CNA
processes/four local accounts exchange reliable, unreliable and 32KiB fragmented application data
for both categories/titles. CA/hostname refusals, local UDP source/length guards, secondary revocation
and server failure are tested. Public online NetworkSession and invitations followed (GS-007/008),
with relay recovery inside a bounded window; public Internet deployment acceptance is not claimed.
Control HTTPS, directory membership and realtime datagrams are separate responsibilities; service
membership alone does not make direct ENet reachable across NAT. No new XNA or C transport API.

Private client relay requires libcurl >=7.86 built with TLS and ws/wss protocols; actual runtime
capabilities are checked before authentication. It derives the WSS authority/path from existing
validated deployment configuration, verifies certificate/hostname/TLS >=1.2 and disables redirects
and proxies. Explicit insecure numeric-loopback settings alone permit ws. One owned worker handles
nonblocking partial frames/sends, rolling outgoing limits, bounded 64-frame queues and fair UDP
drain; wrong local source ports/oversize/queue saturation drop datagrams. Authoritative remote
machine routes alone accept incoming packets. Ready/failure snapshots are consumed by the owner;
no XNA events or object mutations occur on the worker. No native C ABI addition. Windows/macOS/
browser provider behavior is not yet verified; SystemLink continues using its direct ENet path.

Build `cna_service_relay_client_harness` and set `CNA_SERVICE_RELAY_CLIENT_HARNESS` to that absolute
binary path when running the sibling server's `service_cna_relay` CTest. The test explicitly skips
without its harness; an unconfigured skip is not accepted as cross-repository validation.
This private transport probe is not a standard-API acceptance sample or public online lifecycle proof.

GS-008d1 now verifies these real CNA/ENet peers in separate Linux user/network namespaces, each
with its own external slirp4netns outbound NAT, identical private 10.0.2.100 address and no inbound
port mappings. The test checks namespace inodes, interface addresses and default routes; HTTPS/
WSS uses the gateway's matching certificate IP SAN. Reliable fragmentation, unreliable data,
UDP source/size guards, multi-local revocation and server loss pass for both title/category pairs.
This is shared-host NAT-isolation evidence, not public Internet deployment or standard online
NetworkSession acceptance. See sibling server README's optional `service_cna_relay_nat` setup.
Test helpers are external optional prerequisites; production CNA/server neither link nor require
GPL slirp4netns. No host network/package/desktop changes. Public online sessions, invitations and
standard avatars followed in later slices.

GS-007e1 adds a private authenticated roster gate: service ordinals map to 1..31 wire IDs,
only the actual host account receives the host flag, machine hello claims must exactly match
its 1..4 local accounts, and welcome identities/properties/represented groups are checked before
gamer mutation. The real native probe now exchanges the existing Net ClientHello/ServerWelcome
and AppData codec through ENet, rejects a cross-machine account spoof and verifies application
sender/target IDs. Controls are preflighted for bounded counts/strings/UTF-8/canonical booleans/
IDs/eight properties/exact length before decode/allocation. Public PlayerMatch/Ranked create/join
and Net Update roster/lease handling were built on these private helpers later (GS-007).
Caller-owned APM metadata and public service Find are implemented as described below.


Standard PlayerMatch/Ranked Find/BeginFind/EndFind now uses the authenticated directory, including
nullable filters, local capacity, category and title isolation. BeginFind is pending until
Dispatcher.Update publishes completion, wait signal and callback on the update thread. End waits
by pumping and can consume a result once. The raw C++ Begin result is caller-owned (use
`std::unique_ptr<System::IAsyncResult>`); its metadata remains valid after End. Synchronous and C API
adapters release metadata themselves. The existing C API `_async` functions remain blocking
Begin/End adapters with a completion callback; they do not expose an asynchronous-result handle.
Net Begin/End/result release and Dispatcher.Update currently belong on the owning update thread.
Dropping a pending search suppresses its callback and releases the Net busy slot; background
read-only work can still finish. Search refuses more than 256 matches with LIMIT_EXCEEDED.
Create/Join/JoinInvited followed (GS-007); listings carry private service identity rather than a
purported direct Internet endpoint. SystemLink transport is unchanged. Legacy offline
immediate actions report CompletedSynchronously=true; online actions false. SystemLink's existing
Join preparation still happens in End and awaits the separate APM/lifecycle audit.

Service profile/lookup/achievement/leaderboard Begin results also retain their backend until the
caller releases the result. Queued work carries owned logical values and borrows its executor;
it does not retain its own backend queue. EndRead creates public reader/gamer objects on the
caller thread. A service reader keeps its original title/backend for subsequent paging; Dispose
releases that context while existing gamer snapshots remain owned until the reader is destroyed.
Guide owns pending social operations internally. These operations and result release belong on
the dispatcher owner thread. Broader measured Xbox APM/error-order parity remains incomplete.

The private relay client now checks ENet datagrams before UDP injection, including command/payload
lengths and reassembly fragment bounds. Its separate native loopback host caps logical packets
at 1 MiB, waiting data at 4 MiB per peer and fragment bitmaps at 2,048 slots (including minimum
ENet MTU support). These are CNA resource policies, not measured Xbox packet-size compatibility.
The server continues forwarding opaque datagrams; the control and CNR v1 protocols are unchanged.
Malformed remote fragments are refused in both native and separate-NAT E2E while valid ENet
exchange succeeds. Direct SystemLink retains its existing host and transport behavior.

Authenticated relay connections use the endpoint, title and trust configuration captured by their
originating online backend. Later environment or programmatic changes cannot redirect that
backend's credentials/tickets. Fake and unconfigured backends cannot silently acquire a network
authority. This is an internal deployment boundary; XNA gameplay and the C API gain no transport
configuration methods.

Private dispatcher subscriptions now drive preparation/lease observations after the backend event
batch, including nested End waits. Cancellation suppresses copied callback snapshots, self-entry
is skipped and other pending operations can progress. Callback errors do not starve the batch.
Automatic presence retains its dirty revision when the service queue is full and retries after
queued work drains. Public online sessions and their XNA lifecycle events are built on this
boundary. Pending service and Net End calls also drain their retained origin after backend replacement;
superseded identity events have no authority to replace current signed-in gamers.

Service-bound game packet admission validates the authenticated relay source, complete machine
hello/welcome/broadcast groups, directory-owned state/properties and established realtime groups.
Application sender IDs must belong to the source machine on the host; client deliveries arrive
from the host for known remote senders and their own local targets. Length/tag/options/channel,
1 MiB logical-packet ceiling and membership checks precede the single payload copy. Leave broadcasts from the authenticated host must name complete admitted remote groups; arbitrary
unknown/partial leave or end claims cannot replace authority reconciliation. Genuine native and isolated-NAT probes
reject both a forged existing sender ID and an unknown target, while fragmented and unreliable
application exchange still passes. The service host remains trusted to relay game payloads; this
is membership validation, not malicious-host anti-cheat. Public Create/Join, lifecycle conversion
and relay recovery followed; online host migration followed later (GSP-K1). SystemLink keeps its existing codec and transport behavior.

The private owned `ServiceENetSession` now consumes prepared membership and TLS/WSS/native ENet
resources. Hosts become ready after transport preparation; joining clients become ready only after
an exact authenticated welcome. Stable service ordinals produce all local/remote IDs, with one
primary host. Complete connected groups produce owned join/leave observations; directory removal
revokes delivery before later packets. Directory revisions trigger bounded welcome recovery.
The owner pump combines lease renewal, source admission, host forwarding between clients,
local-target data and a single safe failure. Destruction closes transport and releases membership.
Pending/local deliveries are bounded to 128 data messages and 4 MiB; global unacknowledged outgoing
data has the same limits, with a separate 64-message control allowance. This preserves room for
control even under game-data pressure. Existing SystemLink transport is unchanged.

Thirteen deterministic engine cases include three-machine forwarding, both local player slots,
handshake timeout, authority removal and acknowledged-packet ownership limits. Real-server CTests
`service_cna_owned_enet` and `service_cna_owned_enet_nat` use two separate processes, four accounts,
both categories/titles, owned preparation, verified welcome, fragmented/unreliable delivery,
secondary-account revocation, group departure and server failure. Use the same harness and NAT
helper environment as the raw relay tests. They explicitly skip if the required harness/helpers
are absent; a skip is not multiplayer evidence. The raw relay cases separately retain malicious
fragments, source/size and forged incoming ID tests. These are private-engine acceptance; the
public NetworkSession tests and samples (SAMPLE-096, SAMPLE-075) cover the standard API, and no public
Internet deployment is claimed.

A private `OnlineSessionOperation` now joins these stages under a weak dispatcher progress
subscription. Begin-style construction stays pending; completion is published once on the owner
only after prepared host resources or the exact client welcome. End-style consumption transfers
the engine and bounded initial roster/data observations. Retained superseded origins progress
without publishing their identity events. Unconsumed resources continue pumping/renewing; loss
before consumption becomes a deferred error. Callback exceptions preserve the result, while
nested progress, take/destruction inside a callback and abandoned joins are covered. Thirteen
new coordinator cases pass; the real owned probes now use this coordinator through Dispatcher.Update.
This is the private asynchronous lifetime boundary the public NetworkSession adapter uses.

## Host migration (GSP-K1)

`NetworkSession.AllowHostMigration` works for PlayerMatch and Ranked sessions as XNA describes it:
only the host sets it, and every machine reads the host's value. The service decides: when the host
machine leaves (`Dispose`/leave) or stops (its relay connection closes and it does not reconnect
within 20 seconds, the relay-recovery grace), the machine holding the lowest remaining gamer ID
becomes the host and that machine's first gamer the host gamer. On every machine the old host's
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
