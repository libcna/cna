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
multi-local membership, property filtering and leased host revisions. The CNA public online
NetworkSession path and Internet relay are still unfinished. Membership in this directory alone
cannot establish Internet connectivity or provide Ranked arbitration.
