# CNA Gamer Services deployment

The new CNA service has its own accounts/protocol/assets and is not Xbox LIVE compatible. The implementation is in progress: see [living plan](../plans/plan_gamer_services_server.md). Symbol coverage is not behavioral completeness.

Configure outside ported gameplay code. Precedence is complete `CNA::GamerServices::setConfigurationOverride` (deployment/test hosts only), environment fields, title manifest, user settings. User settings default to `$XDG_CONFIG_HOME/cna/gamer-services.json` or `$HOME/.config/cna/gamer-services.json`. Title manifest defaults to `cna-title.json` in launch CWD or explicit `CNA_GAMER_SERVICES_MANIFEST`. No credential fields are accepted. Endpoint is full `https://host:port/cna/v1`; `gameId` must be administrator-provisioned and stable. Optional `caBundle` configures trust, never bypasses TLS checks.

Environment: `CNA_GAMER_SERVICES_ENDPOINT`, `CNA_GAME_ID`, `CNA_GAMER_SERVICES_CA_BUNDLE`, `CNA_GAMER_SERVICES_INSECURE_LOOPBACK=1` (explicit development only). Insecure HTTP is restricted to numeric `127.0.0.1` or `[::1]`, proxies and redirects disabled. No insecure Internet/LAN default. No endpoint means no service accounts, never fabricated Stub Gamers after backend migration. Credentials are user data, never title manifest values.

```json
{"endpoint":"https://service.example/cna/v1","gameId":"my-title"}
```

Server canonical protocol and administration commands live in sibling `cna-gamer-services-server/README.md` and `protocol/v1.md`. Desktop client depends on libcurl with TLS support and nlohmann/json. Browser/other secure platform transport integration remains unverified.

Measured service features include four local authenticated players, standard Guide sign-in, profiles/
lookup, achievements (metadata/awards), mutual friends and rich presence. Standard ShowFriends,
ShowGamerCard and ShowFriendRequest use CNA system overlays. LocalWithLeaderboards submits final
rows at EndGame or explicit early leave. PlayerMatch/Ranked/invites/relay and standard Avatar migration
remain unfinished.
Rich presence is sent during Dispatcher.Update; friend online state currently reflects authenticated
activity within 90 seconds; Update schedules authenticated heartbeat every 30 seconds. Do not treat this as a production
service release or a claim of measured Xbox event/validation parity across every method.

Achievement/profile picture streams now read immutable SHA-256 resources from the service. The
per-user cache defaults to `$XDG_CACHE_HOME/cna/gamer-services/assets` or
`$HOME/.cache/cna/gamer-services/assets`; CI can override `CNA_GAMER_SERVICES_CACHE_DIR`.
Only content hashes form filenames; data is verified on download and on cache read, corrupt entries
are replaced atomically, and each API call returns a separate read-only stream at position zero.
Cache writes stop at 256 MiB (eviction is not implemented); a full/unwritable cache still allows
retrieval. PNG assets are bounded to 512x512/512 KiB; larger version-2 GLB resources have a 16-MiB
transport ceiling. Hashes do not make malformed image/model content valid. No live avatar catalog
or standard-avatar rendering is implied by this initial generic resource mechanism.


Online leaderboard reads use server-provisioned title/key/game-mode definitions, sort direction and
scalar columns. Offset, gamer-centered and gamer-restricted reads page remotely rather than loading
an entire board or dropping gamers signed in on another machine. Begin/End completions use the
Dispatcher update boundary; native C callbacks return completed reads. Online writer setters now
retain transient data while Playing and flush after final write events at host EndGame;
explicit early leave uses IsLeaving=true. Ranked arbitration, Stream columns and TrueSkill are unfinished. Admin seed commands
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
multi-local membership, property filtering and leased host revisions. Standard online Find now consumes this directory; public create/join/lifecycle integration
and public Internet deployment acceptance remain unfinished. Membership in this directory alone
cannot establish Internet connectivity or provide Ranked arbitration.

NetworkSessionProperties follows the fixed XNA eight-slot shape: Count=8, nullable signed integers
at indices 0..7. Add/Insert/Remove/RemoveAt/Clear throw NotSupportedException; use the ordinary indexer
for configuration. IsReadOnly reports false as the managed reference does. Live-session writes
require the current host and reject disposed owners; advertised snapshots reject writes. C API
symbols remain available and map structural refusal to CNA_RESULT_NOT_SUPPORTED. CNA range helpers
expose only const iterators so writes cannot bypass ownership checks. Rebuild all consumers with
sharp-runtime's GS-007b proxy layout (16->24 bytes on the measured 64-bit ABI).

The invitation control service now persists recipient-bound pending/accepted invitations and
atomically joins up to four authenticated local users through an explicitly accepted invite,
using available private then public slots. Client Guide/InviteAccepted/public invited joins remain pending; the private WSS relay is
implemented and measured separately below. Invitations are CNA service IDs, not Xbox LIVE tokens.

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
and server failure are tested. Public online NetworkSession/invites, reconnect and isolated Internet
acceptance remain unfinished.
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
GPL slirp4netns. No host network/package/desktop changes. Public online async/roster/lifecycle,
reconnect, invitations and standard avatars remain unfinished.

GS-007e1 adds a private authenticated roster gate: service ordinals map to 1..31 wire IDs,
only the actual host account receives the host flag, machine hello claims must exactly match
its 1..4 local accounts, and welcome identities/properties/represented groups are checked before
gamer mutation. The real native probe now exchanges the existing Net ClientHello/ServerWelcome
and AppData codec through ENet, rejects a cross-machine account spoof and verifies application
sender/target IDs. Controls are preflighted for bounded counts/strings/UTF-8/canonical booleans/
IDs/eight properties/exact length before decode/allocation. These helpers are still private;
public PlayerMatch/Ranked create/join and Net Update roster/lease handling remain unfinished.
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
Create/join/invited lifecycle remains unfinished; listings carry private service identity rather
than a purported direct Internet endpoint. SystemLink transport is unchanged. Legacy offline
immediate actions report CompletedSynchronously=true; online actions false. SystemLink's existing
Join preparation still happens in End and awaits the separate APM/lifecycle audit.

Service profile/lookup/achievement/leaderboard Begin results also retain their backend until the
caller releases the result. Queued work carries owned logical values and borrows its executor;
it does not retain its own backend queue. EndRead creates public reader/gamer objects on the
caller thread. A service reader keeps its original title/backend for subsequent paging; Dispose
releases that context while existing gamer snapshots remain owned until the reader is destroyed.
Guide owns pending social operations internally. These operations and result release belong on
the dispatcher owner thread. Broader measured Xbox APM/error-order parity remains incomplete.
