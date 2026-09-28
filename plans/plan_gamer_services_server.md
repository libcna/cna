# CNA Gamer Services server implementation — living plan

Mission authorized 2026-09-28. Xbox 360 XNA behavior is the target; Windows/FNA service and avatar stubs are **not** behavioral evidence for Xbox 360. CNA protocol, accounts and original avatar assets have no Xbox LIVE compatibility. This plan supersedes plan_net.md's refusal of PlayerMatch/Ranked/invites and its standard-avatar/EXT split. Prohibition on proprietary/third-party avatar assets remains.

## Repository boundary and initial audit

| Repository/worktree | Branch / HEAD at start | Tracking | Initial status / ownership |
|---|---|---|---|
| canonical CNA `/rv/data/development/github.com/libcna/cna` | next / b2fd47a45757c32326cbbb5c2b39afffdb7392c5 | origin/next | clean at inspection; samples agent; never edit |
| existing CNA `/rv/data/development/github.com/libcna/cnawork` | gamerservicese / same HEAD | none | clean at inspection; leave untouched |
| **this task** `/rv/data/development/github.com/libcna/cnawork/cna-gamer-services` | feature/gamer-services-server / same HEAD | none | newly created from committed next |
| sharp-runtime `/rv/data/development/github.com/libcna/sharp-runtime` | next / fc033a0e (full revision below at checkpoints) | origin/next | clean; audited; no changes presently needed |
| server `/rv/data/development/github.com/libcna/cna-gamer-services-server` | unborn main → feature/gamer-services-server | none | existing empty checkout; origin git@github-libcna:libcna/cna-gamer-services-server; do not init/clone another |

`git worktree list --porcelain` additionally listed prunable detached `/tmp/cna-bible-base` (1bb2145d) and `/tmp/cna-bible-target` (d6e9ff05); do not prune another task's entries. Canonical git common directory is `/rv/data/development/github.com/libcna/cna/.git`.
Read current repository AGENTS.md and CLAUDE.md (including sharp-runtime's current nonhistorical policies), CHECKLIST.md as applicable. User's no-push/dedicated-branch scope takes priority over sharp-runtime historical next/push instructions. Builds use persistent directories, shared ccache `/rv/cnaccache`, `CCACHE_BASEDIR=/rv`; maximum two jobs when compiling sharp-runtime. All window/GPU runs use private runner, never owner's display.

## Audit findings and evidence

* Dispatcher initializes four hardcoded Stub Gamers and Update is empty. Reinitialize frees objects still potentially referenced. Replace with backend events at Update boundary; retain retired gamers until shutdown so event/collection pointers do not dangle.
* Gamer lookup and partner-token family throw NotSupported; profile retrieval fabricates a default snapshot and ignores End result ownership. Existing action wait handle starts signaled while CompletedSynchronously is always false. SignedInGamer friends are empty/false; privileges default to all allowed; presence transport hook does nothing.
* Achievements persist earned keys/ticks in LocalGamerServicesStore JSON without server catalog metadata; GetPicture throws. LeaderboardWriter persists setter changes immediately and readers load entire local files. Online behavior must use server definitions and EndGame write windows. Keep old store only in explicitly selected offline fixture path while migrating.
* Guide keyboard/message overlays are real, update-driven, reusable. ShowFriends, both ShowGameInvite overloads, ShowGamerCard, Marketplace, Messages, Party, PartySessions, PlayerReview, SignIn are empty. Review each against local XML; commerce/review are not automatically impossible.
* NetworkSession has ENet SystemLink, discovery, host migration, latency/loss tests. PlayerMatch/Ranked Create/Find and invited Begin/End refuse. NetworkMachine::RemoveFromSession and PropertyDictionary::CopyTo require renewed Xbox evidence; old plan's inference from FNA alone does not establish Xbox behavior.
* AvatarDescription accepts 1021 bytes, validity only tests first byte, height zero/female defaults, random/fetched data invalid. AvatarAnimation has 71 zero matrices, zero duration; real clip name only EXT. Renderer permanently Unavailable, standard Draw validates and does nothing. EXT path already draws SkinnedModelEXT with useful lighting, tint and disposal behavior. Preserve it until standard Draw is verified.
* Generator is original procedural Blender/mesh-craft pipeline; converted rig currently 20 joints (19 plus neutral), not public 71. Shared body/wardrobe plus hash-addressed immutable GLB catalog is desired. Pipeline README still instructs MakeHuman/Mixamo downloads: obsolete and prohibited; remove those instructions during migration. No proprietary assets used.
* C API gamer/profile/achievement/leaderboard/net/Guide/avatar wrappers currently expose same backend limitations and avatar EXT entry points; align them with standard behavior as features land. Do not add protocol structures to ABI.
* `docs/xna-4-runtime-member-coverage.json` is symbol/type coverage derived from reference XML hashes, not behavioral completeness. `docs/xna-4-api-coverage.md`, `docs/avatar-real-rendering-ext.md`, plan_net.md, misc/known_gaps.md inspected; old claims remain true for current runtime but superseded as **target** decisions.
* sharp-runtime HttpClient.cpp:114 rejects non-http; ClientWebSocket.cpp:146 rejects wss before socket creation. Net.Security is interface-only (no TLS stream). Crypto/random modules exist, but no service logic belongs there. Use mature libcurl HTTPS client with peer/hostname verification; Beast/OpenSSL TLS server. No custom crypto or insecure public authentication. Installed audit: OpenSSL 3.5.7, libcurl 8.14.1, SQLite 3.46.1, Boost.Beast/nlohmann headers and ccache.
* Local evidence: `/rv/data/development/github.com/libcna/xna4-spec`, `/rv/data/development/github.com/libcna/xna4-decomp/dlls`. Dispatcher XML: Initialize locates graphics services; Update raises service events each frame; duplicate Initialize documented invalid. Guide XML: paneCount 1,2,4; onlineOnly permits guest of signed-in online profile. NetworkSession XML: Write* events immediately before lobby transition/when gamer leaves gameplay; no other service calls inside handlers. Leaderboard arbitration must be read carefully before claiming Ranked parity. Available managed IL is Windows build, so avatar no-op IL does **not** settle Xbox semantics. Native console traces are not yet available.

See `gamer_services_server_initial_inventory.md` for every candidate throw/default/EXT/no-op location. Candidates are not all defects: parsing defaults, collection false results and ENet guards are legitimate until individual review.

## Architecture and protocol decisions

Server owns canonical `protocol/v1.md`, golden JSON vectors and wire schema. CNA vendors only versioned protocol artifacts with a byte-for-byte drift check; never duplicate undocumented enums. Control uses bounded UTF-8 JSON envelopes over HTTPS POST `/cna/v1`; negotiation advertises only implemented capabilities. IDs correlate requests; version, title, operation, authentication and payload validated before mutation. SQLite prepared statements + schema version/migrations; password hashing via OpenSSL scrypt with random salts, constant-time comparison; random revocable access credentials stored hashed, title-scoped sessions. No credential logging/command-line passwords. Refresh rotation/user credential store still requires dedicated implementation and tests.

Endpoint precedence: CNA-only programmatic override > environment (CI) > explicit per-title manifest > user configuration. No Internet insecure default; absence means no configured online accounts, never fabricate profiles. Title ID is mandatory for service mode; manifests contain no password/token. Explicit insecure development mode initially restricted to numeric loopback, with redirects/proxies disabled to prevent insecure escape.

Private IGamerServicesBackend accepts logical operations, not XNA objects. One bounded I/O executor queues work; responses/events are applied during Dispatcher.Update. Fake backend is opt-in deterministic test fixture, never implicit online fallback. Up to four slots have separate authenticated identities. Async results retain operation/owner/exception/state, exactly-once callback and End validation; callback delivery at Update with CompletedSynchronously=false for queued work. Cached immediate operations explicitly synchronous. Sync APIs wait with bounded transport timeouts while pumping; no detached arbitrary per-call threads.

Realtime remains ENet; central control session directory is separate. Internet plan is relay-first with authenticated per-session tickets and bounded datagram forwarding, then direct path optimization. Server-assigned IP/port alone is insufficient. No Internet connectivity claim until relay tests through isolated network namespaces/firewall restrictions prove it.

Avatars: CNA-owned versioned envelope within exact 1021-byte public buffer; fixed header/version/body/height/appearance/asset catalog ID, integrity validation and reserved bytes. Shared original GLB resources hash-addressed; 71-slot public transforms always remain exact. Standard renderer resolves graphics device from dispatcher service provider, lazily loads/cache resources, exposes Loading/Ready/Unavailable, renders standard bones/expressions. Prefer generated 71 rig; temporary mapping must remain private and tested.

## Checklist / migration sequence

- [x] GS-001: repository safety, initial inventory and living architecture plan.
- [ ] GS-002: canonical protocol parser/spec/golden corpus and drift checks; server independent CMake project, persistent schema/migrations, administration and hardening unit tests.
- [ ] GS-003: endpoint/title configuration and TLS client transport; fake/backend abstraction; remove fabricated profiles; deterministic event pump and async ownership tests.
- [ ] GS-004: real server auth/revocation/restart, separate local slots, Guide sign-in overlay, user credential/refresh persistence, identity/profile/privilege/lookup tests.
- [ ] GS-005: friends/presence/pictures/social Guide; service achievements metadata/award/pictures/cache across users/titles/restarts.
- [ ] GS-006: session-scoped leaderboard writes, EndGame/leave events/flush, Ranked arbitration and reads/paging/social filters.
- [ ] GS-007: PlayerMatch/Ranked directory, properties/create/find/join, invited joins/InviteAccepted and coherent failure/event order; preserve full SystemLink corpus.
- [ ] GS-008: authenticated relay fallback with untrusted packet limits; prove connectivity across NAT-like isolation, membership revocation, failure/reconnect.
- [ ] GS-009: description encoding/random/service retrieval/cache; original 71-slot rig/presets/expressions; standard renderer Draw/ready/loading/disposal, pixel regression migration; only then retire redundant avatar EXT.
- [ ] GS-010: C ABI synchronization, standard-API-only demos, representative original XNA compatibility samples.
- [ ] GS-011: two separate CNA processes/server restart/E2E entire corpus, protocol fuzz/property tests, final throw/no-op evidence register, truthful docs and clean checkpoints.

Every task may be split into thematic subcommits; mark parent complete only after all criteria. After stable milestones inspect committed next, merge relevant commits (existing history uses merges), rerun regressions; never copy samples worktree files. Update AUDIT/NEXT with measured results, not completeness claims.

## Validation and acceptance matrix

Deterministic backend: four slots, slot replacement/signout/event visibility order; callback exactly once/reentrancy; foreign/wrong-owner/repeated End; completion errors; fake explicitly selected. Protocol: limits/depth/UTF-8/unknown version/unknown op/duplicate IDs/truncation/title authorization. Real E2E: two clients, two users/titles, bad passwords, TLS untrusted CA/hostname, reconnect/revoke, user/server restart; friends/presence; achievement catalog/duplicate/invalid/image; leaderboard EndGame flush/read/paging; PlayerMatch/Ranked filtering/invites/ENet/relay/disconnect/failure. Avatar: 1021 byte roundtrip/malformed/version; every preset/71 bones/bind/expression; cache hit/miss/corrupt/missing; standard Draw pixels/disposal. SystemLink complete unchanged regression suite. GPU tests only tools/platform/run_gpu_tests_private.sh.

Sample candidates: SAMPLE-096 Invites (known gap 4); SAMPLE-087 AvatarShadows (cancelled Xbox-only row, logic useful as new compatibility acceptance case); inspect local sample corpus for GamerServices identity and achievement/leaderboard samples. No suitable original sample → minimal XNA-shaped sample with reason recorded; no CNA service/Avatar EXT gameplay calls.

## Checkpoints / reproducible commands

Initial source set: CNA b2fd47a45757c32326cbbb5c2b39afffdb7392c5; sharp-runtime fc033a0e; server unborn. No integration tests run yet. Existing builds in other worktrees must not be repointed; new task build uses dedicated `cmake-build-debug` and shared dependency source/install roots. Server build `build/` in server checkout. Commands and exact commits/results appended below when verified.

## Genuine blockers / uncertainties

No blocking repository modifications found. Server initially empty, no established build conventions beyond CNA style/license. Console-only validation order/event/Ranked semantics are not fully measured; do not label Windows IL evidence as Xbox confirmation. Full Internet/relay, Guide credential flow, asset cache and standard rendering are unfinished, not blocked by absence of Xbox LIVE. This mission is not complete at an initial working demo.

### 2026-09-28 foundation checkpoint (GS-002a, GS-003a/004a in validation)

Server commit `4ae538a`: independent C++23 CMake product, canonical JSON parser/header/golden vectors, TLS >=1.2 Beast/OpenSSL listener (two I/O threads, 128 connection cap, deadlines), SQLite schema v1 + transactional migration, admin title/user/achievement provisioning and inspect/revoke/reset-earned tools. Password verifier OpenSSL scrypt and random hashed title-scoped expiring tokens. Profile aggregates, directed friend subscriptions/presence, persistent achievement catalog/award state. No refresh/asset/push/matchmaking/relay/leaderboard capability advertised. Directed subscriptions are a protocol foundation, not completed XNA mutual friendships.

Validation server `build/`, two jobs, GCC14 -Wall -Wextra -Wpedantic -Werror: clean build; `service_unit` **101 assertions passed**; `service_tls_e2e` passed: trusted TLS, untrusted CA, wrong hostname, two separate Python client processes, cross-title credential refusal, real server process restart with persisted award/token, logout/revocation, insecure public bind refusal. `ctest --test-dir build --output-on-failure`: **2/2 passed**. These are service E2E tests, not yet CNA gameplay acceptance samples or Internet/relay proof.

CNA dedicated `cmake-build-debug/` initialized because no task-owned build existed. HEADLESS renderer/platform, NULL audio, SDL OFF, video OFF, Draco OFF (uninitialized optional submodule, unrelated to services), C API initially OFF; shared ccache and max two compilation jobs. Existing pinned GoogleTest **source** reused by copying without its .git, because a symlink at a gitlink breaks `git status`; no dependency cloned/downloaded and no shared build repointed. Initial sandboxed build could not write shared ccache; authorized escalated build succeeds. No approval rejection.

Latest committed `next` checked at this checkpoint: still `b2fd47a45`, no integration merge necessary. sharp-runtime remains unchanged at `fc033a0e8541a81498c4a496f56a0f59475c6e34`. GS-002 parent remains open for full protocol push/assets/hardening coverage; GS-004 parent open for Guide/refresh/reconnect/lifetime semantics. Next immediate work: validate CNA backend/async tests and actual two-CNA-client TLS E2E; then Guide overlay, assets and subsequent checklist phases.

### GS-003a endpoint/protocol checkpoint

Implemented complete CNA-only override plus environment/title/user configuration precedence,
strict deployment-only keys, mandatory stable title ID, verified HTTPS and explicit numeric-loopback
HTTP opt-in. Five focused configuration tests pass. Canonical v1 parser/header/golden vectors
are vendored byte-for-byte from server 4ae538a; `tools/net/check_service_protocol.py` checks drift.
libcurl/nlohmann are private desktop control dependencies; browser transport remains unfinished.
No credentials belong in title JSON. Configuration is separate from XNA public APIs.

Server real-CNA E2E extension committed as `eb51d1f`; sharp-runtime unchanged. Backend/Guide
integration is being committed separately after full validation, so this checkpoint alone does
not claim service behavior. GS-003 parent remains open until that checkpoint.

### GS-003b/004a authenticated XNA service checkpoint

Implemented bounded single-worker TLS backend, explicit deterministic fake, four authenticated
slots, zero fabricated accounts at Initialize, duplicate initialization validation, stable signed-in
collection, sign-in/out publication before events, profile/lookup, online-session privilege, friend
read ownership and persistent service achievement metadata/idempotent awards. Begin/End validates
operation and owner, publishes completion/callback once at Update, begins with unsignaled wait
handle and CompletedSynchronously=false, defers exceptions and rejects repeated End. Nested End
inside an event progresses only completion events; identity events are deferred to outer Update.
Retired signed-in objects are retained until shutdown (unbounded repeated sign-in retention needs
future lifetime review). Revoked-token response queues sign-out; no heartbeat/refresh yet.

Standard Guide.ShowSignIn reuses CNA keyboard/password masking and failure message overlays,
sequentially handles up to four occupied/unoccupied player slots, rejects overlapping Guide,
and supports username/password cancellation. Game has a generic internal system-overlay service
called after application Draw and before EndDraw, verified by a runtime order test. Original CNA
bitmap system font moved from example helper to private graphics infrastructure; old helper delegates.
No gameplay server/transport fields added to Microsoft::Xna APIs. Guest/offline sign-in, persistent
refresh credentials and full Guide social/commerce flows remain unfinished.

Validation (HEADLESS graphics/platform, NULL audio, DISPLAY unset and WAYLAND_DISPLAY empty):
* CnaGamerServicesTests: 387 run, 386 pass, 1 existing screensaver skip.
* CnaNetTests: 316/316 pass in confirmation run. First full run had host-migration roster timeout;
  isolated retry passed, full confirmation passed. No ENet/SystemLink implementation changed.
* CnaRuntimeTests: 187 run, 185 pass, 2 existing headless capability skips, including overlay order pass.
* cna_service_client_harness fake: 47 checks pass (four slots, Guide masking/cancel, reentrant profile,
  ownership/repeated-End/deferred failure, metadata/award, event ordering/lifetime).
* Server unit: 101 assertions; real TLS E2E plus actual CNA processes: 2/2 ctest pass, 5.72 seconds.
  CNA scenarios: bad password/CA/hostname, two users/titles/client processes, idempotent achievement,
  client/server restart, lookup/profile, callbacks, signout. This does NOT prove Internet game transport.
* Earlier sandbox runs failed local-store writes/ENet sockets; permitted HEADLESS runs resolve those.

Reproduce CNA configure/build with in-repo cmake-build-debug (HEADLESS, NULL audio, no SDL/video/
Draco/C API/examples, tests ON; sharp-runtime root sibling). Build targets CnaGamerServicesTests,
CnaNetTests, CnaRuntimeTests, cna_service_client_harness and cna_net_* harnesses; shared cache, -j2.
Run binaries with desktop display unset. Server: `CNA_SERVICE_CLIENT_HARNESS=<absolute CNA binary>
ctest --test-dir build --output-on-failure` adds CNA coverage to TLS test (otherwise Python clients).
Server test auto-provisions temporary SQLite titles/users/catalog and ephemeral trusted test certificate,
then removes them. No password/token in argv or logs. All live server/socket tests need network-enabled
execution; GPU runs, when added, must use private runner.

Next unfinished GS-004 items: refresh/user credential persistence, authentication-loss/reconnect and
remaining profile/privilege metadata. Independent GS-005 work proceeds on presence/social/assets.
Full service push, friends mutual/privacy model, pictures/cache, leaderboard/online sessions/relay,
standard avatars, standard samples, C API and final audit remain incomplete. No new stubs or sharp-runtime
changes. Latest next remains b2fd47a45; test server commit eb51d1f, runtime fc033a0e.

### GS-005a social/presence checkpoint

Server `fb02f71cc93824288fa6d475366bf3b284a12f6c` persists mutual request/accept/removal using
existing account-global directed edges; existing one-direction development rows become requests.
Added capability `friend-requests`; client validates negotiation and refuses missing capability.
Pending friends do not see private online/presence state. Limits bound both incoming/outgoing edges.
Lists expose accepted/incoming/outgoing flags; title presence isolated, server restarts preserve graph.
Full account privacy/block/voice/invite state and social push remain unfinished.

Standard ShowFriendRequest confirms/queues requests; ShowFriends pages snapshots and finds a gamertag;
ShowGamerCard retrieves profile and permits accept/remove/cancel/request. Reuses automatic system
message/keyboard overlay; online network operations complete at Update. Actor/profile validation
implemented from local XML; exact Xbox exception order remains unmeasured. Friend snapshots own
objects without the old moved-factory owner-pointer issue. IsFriend requires mutual acceptance.

Presence setters mark dirty and publish via bounded executor at Dispatcher.Update; coalesced revisions
avoid losing newer changes while a previous request is pending. Value substitution is applied to rich
presence text. Fixed a pre-existing mismatch: the old alphabetical string table was indexed with the
nonalphabetical reference enum. Enum order verified against local reference assembly and descriptions
against XML. Full 60-mode text mapping now follows enum order. Generic custom string EXT retained.
Heartbeat, backoff and authenticated refresh remain unfinished (online last-seen expires after 90s).

Validation: deterministic harness now 58 checks, including request/accept/remove, pending flags,
Guide navigation/error, owned snapshot disposal and Level 12 publication only at Update. GamerServices
387 run: 386 pass/1 existing skip. Server CTest 2/2 pass in 8.14s including actual concurrent CNA
clients, mutual friendship, rich presence across processes and admin revoke → failed operation plus
SignedOut at Update. No Internet realtime/connectivity claim; no assets or standard-avatar claim yet.
Next GS-005b: hash-addressed immutable picture/asset storage and client cache; achievement/profile
picture APIs, corrupt/missing/cache/restart tests. GS-004 refresh remains independent unfinished work.


### GS-005b immutable assets/pictures and C API checkpoint

Implemented server schema migration 2: immutable SHA-256 assets, title authorization, user picture
association and bounded `assets.read` chunks. Admin imports trusted paths; clients only supply hashes.
PNG dimensions/size and GLB header/version/length are bounded before import. Full GLB/image content
validation belongs to asset loaders, not the import header check. Original procedural PNG fixtures;
no downloaded/proprietary assets. Protocol parser now caps containers before DOM insertion; golden
vectors plus 5,000 deterministic mutations/truncations run in client and server.

Online backend verifies chunk metadata and SHA-256 and caches immutable resources under the user
cache directory (`CNA_GAMER_SERVICES_CACHE_DIR` for tests). Reads rehash; corrupt entries redownload;
unwritable/full cache permits uncached use. Write ceiling 256 MiB; eviction/download deduplication
remain open. Achievement.GetPicture and GamerProfile.GetGamerPicture return caller-owned read-only
streams at position zero. Configured pictures decode through standard Texture2D.FromStream.
No default/sample pictures invented. Missing configured assets fail deterministically.

C ABI 0.32.0 adds achievement/profile picture copy with required-size and buffer-too-small behavior;
fixes leaked temporary streams in existing size routes. Exact export/header baseline: 3,215 exports,
198 structs, 295 scalar typedefs, 1,470 constants, 14 strings and 141 color constants. Two pure C
client processes sign in through Guide, obtain identities/achievements and copy both pictures from
the TLS server. Coverage generator now finds sharp-runtime above nested worktrees or via explicit
CNA_SHARP_RUNTIME_ROOT; fixture regression covers this previously broken include discovery.

Validation recorded below after final gates. Previous known-good commits: CNA
ac56c4f5094b1fde47ace89cb1459337c15d4b18; server fb02f71cc93824288fa6d475366bf3b284a12f6c;
sharp-runtime fc033a0e8541a81498c4a496f56a0f59475c6e34 (unchanged). Parent GS-005 remains open
for complete metadata/social privacy and full Xbox semantics. Next implementation GS-006 server
leaderboard definitions/reads and transient writes plus host EndGame commit. GS-004 refresh and
persistent credentials remain independent unfinished work.

Final GS-005b validation: server 5,122 assertions pass; real TLS CTest 2/2 pass in 10.90s,
including simultaneous C++ and pure C clients, persistence/restart, image hash/cache hits and
corruption recovery. GamerServices 388 run: 387 pass/1 existing screensaver skip. Private runner
C API/coverage/ABI/protocol gates 17/17 pass in 53.93s. Fake backend 58 checks unchanged. Server
checkpoint a2f8493 (full hash recorded with next integration set). CNA built C API shared library,
ABI/Guide/Gamers smokes, real-client and protocol harnesses with two compile jobs. No new stubs,
no sharp-runtime changes. `next` remains b2fd47a45; samples worktree untouched.


### Known-good GS-005b integration set

CNA 32c0002ffdc191a118689a3058c64390a8f882ce; server
a2f849356c5eed23f55256a3c87caf2de45bf5ec; sharp-runtime
fc033a0e8541a81498c4a496f56a0f59475c6e34. Both task repositories clean at this checkpoint.

### GS-006a leaderboard catalog and remote reads (validated subtask)

Schema 3 defines title/key/game-mode sort, aggregation, arbitration and typed-column metadata.
Only trusted administration can seed development entries. Runtime capability `leaderboard-reads`
provides prepared-query offset/centered/restricted reads, remote total and global ordinal rank;
no write capability is advertised. SQLite >=3.38 supplies fixed-SQL JSON set filtering. Limits:
128 definitions/title, page/gamer set <=100, columns <=32 and <=2 KiB, envelope <=64 KiB.
Large column-heavy pages can refuse LIMIT_EXCEEDED; Stream columns, snapshot paging and rotating
recent windows remain open. Stable user IDs resolve tie order; exact Xbox pivot/tie policy unmeasured.

Standard Reader now creates owned remote Gamer objects and queues Begin/End/page operations through
existing backend executor; operation/owner/repeated-End/pending/disposal checks. C++ wrappers release
old leaked async results. Standard online Writer setters are transient rather than local file writes;
EndGame submission is still the next GS-006b item, not claimed implemented. Explicit legacy factory
fixtures remain local. Native C entry handles retain the reader that owns their remote gamers even
after reader handle destruction; remote gamer storage lasts to reader destruction, not Dispose.

Primary behavior evidence recovered from Microsoft Learn, [LeaderboardWriter class](https://learn.microsoft.com/en-us/previous-versions/windows/xna/ff434258(v=xnagamestudio.40)):
writes during Playing are flushed only by host EndGame; Ranked permits arbitrated and nonarbitrated,
other sessions only nonarbitrated. All machines report arbitrated rows for all gamers; nonarbitrated
rows belong to their local machine. Ranked all machines report TrueSkill, otherwise host does.
LocalWithLeaderboards is the single-player write path. Local XML lacks these useful class paragraphs;
the linked Microsoft page supplies them. Full arbitration/TrueSkill remains pending directory support.

GS-006a validation: fake backend 71 checks; GamerServices 388 run, 387 pass/1 existing skip.
Server 5,136 assertions; final genuine TLS C++/pure C clients and persistence suite 2/2 pass in
14.53s. Private C API leaderboard/header/export/ABI/protocol gates 9/9 pass in 1.30s; ABI remains
0.32.0 with unchanged 3,215 exports. Native C read entry remains usable after reader handle release.
Server committed a13e20d. Next committed next still b2fd47a45; shared samples worktree untouched.
Next GS-006b: authenticated LocalWithLeaderboards game epochs, transient writer scope, final write
callback before Lobby and atomic/idempotent server commit. Ranked arbitration follows GS-007.


### GS-006b LocalWithLeaderboards EndGame checkpoint

- [x] Authenticated local gameplay epochs (schema 4), 1..4 independent title-bound accounts;
  owner/member/catalog/arbitration authorization, bounded lifetime/quota, atomic scalar commits.
- [x] Writer scope opens at Playing Update; rating/column setters retain transient drafts and reject
  writes before Playing/after Lobby. Final WriteUnarbitratedLeaderboard callbacks run before
  commit/GameEnded. Networking and backend calls in those callbacks reject. Both signed-in and
  local-network gamer writers participate; conflicting drafts for the same identity reject.
- [x] Persistence/ascending-best/latest policy, malformed columns, nonmembers, wrong host/title,
  invalid-row atomicity, duplicate rows and identical commit retry across server restart tested.
- [x] LocalNetworkGamer publishes the real signed-in gamertag/display name; the previous test
  asserting Stub Gamer was corrected. SystemLink wire identity remains its real account identity.
- [ ] GS-006c: leaving/disposal write events/submission, explicit epoch abort, transport-loss retry
  policy; Stream columns, catalog validation timing and full event restriction audit.
- [ ] Ranked/all-machine arbitration and TrueSkill require GS-007 session directory integration.

CNA fake harness **99 checks pass**, including four local signed-in gamers; Net **316/316 pass**
(including real ENet two-process data/host migration); GamerServices **388 run, 387 pass, 1 existing
HEADLESS screensaver skip**. Server **5,158 assertions pass**; genuine TLS C++ and pure C clients,
Alice/Bob and two titles, EndGame final score/columns and independent clients after server restart:
`ctest --test-dir build --output-on-failure` **2/2 pass, 17.17s**. Native C API Net/leaderboard,
header/export/ABI/protocol gates pass; coverage-scope rule initially failed because GS task labels
missed its CBIND prefix, then corrected without broadening approved symbols. ABI stays 0.32.0,
3,215 exports. Tests/build logs in each persistent build directory (`gameplay-final-*`).

Reproduce with max two build jobs:
```
CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv cmake --build cmake-build-debug --parallel 2 --target CnaNetTests CnaGamerServicesTests cna_service_client_harness cna_c_api_service_client
env -u DISPLAY WAYLAND_DISPLAY= cmake-build-debug/cna_service_client_harness
env -u DISPLAY WAYLAND_DISPLAY= cmake-build-debug/CnaNetTests
env -u DISPLAY WAYLAND_DISPLAY= cmake-build-debug/CnaGamerServicesTests
tools/platform/run_gpu_tests_private.sh cmake-build-debug -R '^(CApi_NetSmoke|CApi_LeaderboardsSmoke|CApi_HeaderAudit|CApi_Exports|CApiAbiBaseline|CApiAbiHeaderBaseline|CApiDeclaredExports|GamerServices_ProtocolDrift|CApiCoverageMatrix|CApiCoverageScopeModel|CApiLimitations)$' --output-on-failure
# In the sibling server repository, after cmake --build build --parallel 2:
CNA_SERVICE_CLIENT_HARNESS=/rv/data/development/github.com/libcna/cnawork/cna-gamer-services/cmake-build-debug/cna_service_client_harness CNA_SERVICE_C_API_HARNESS=/rv/data/development/github.com/libcna/cnawork/cna-gamer-services/cmake-build-debug/cna_c_api_service_client env -u DISPLAY WAYLAND_DISPLAY= ctest --test-dir build --output-on-failure
```

Controlled next inspection: canonical samples worktree/next is now committed **9473f5c89**, identical
to this feature branch's GS-006a parent; no divergent history to merge. No shared working files
copied or edited. sharp-runtime unchanged. GS-006a tested source set: CNA
9473f5c8972027d0a5f3e484dc5e4374bd095669 / server a13e20d4c2fd34111b52aa3f57b9eaf3e7b93a7a /
sharp-runtime fc033a0e8541a81498c4a496f56a0f59475c6e34. GS-006b hashes recorded at the next
checkpoint after these source commits exist.

Known limits: public dictionary mutable any references/CNAEXT iterators can bypass a later setter
scope check if retained; catalog validation is currently deferred to server commit, not measured
Xbox GetLeaderboard timing. Failed EndGame leaves Playing/drafts intact; callbacks run again on
retry (freezing a previously transmitted transaction is still needed). Disposal currently abandons
a 24-hour epoch without abort, so interrupted games count against the 16 epoch quota. No online
Ranked/PlayerMatch, Internet relay, standard-avatar functionality or unblocked original samples
is claimed at this milestone. Next independent implementation: GS-006c leave/abort lifecycle.

User explicitly selected MIT for independently written server code. Server 317b3aae01a6af7373784e11b2f8ddced2363baf
adds LICENCE, ignore rules and dependency notices. No FNA or proprietary implementation is imported.
SPDX synchronization is a separate clean checkpoint after GS-006b, preserving XNA port MS-PL files.


### Known-good GS-006b integration and MIT checkpoint

CNA **684d453bb6f15a037bb0e723ca3ad82b0093280b**, server
**bb73b6674fcb487918c60f10293056eb3dc441c9**, sharp-runtime
**fc033a0e8541a81498c4a496f56a0f59475c6e34** were built/tested together with the exact
GS-006b results above. Server license-only **130d2a3fe650bf85c85cbf348076f6f8d782c578**
synchronizes all original source notices to MIT; CNA copies the same canonical parser/header and
MIT notice into its private protocol directory. XNA port sources retain MS-PL. Protocol drift check
passes after synchronization. No functionality changed in the license checkpoint, so no redundant
full rebuild; the next functional build recompiles the changed source notice naturally.
The original license staging proposal was automatically rejected because it used index plumbing
against then-uncommitted gameplay changes. It did not execute; ordinary explicit-file staging at
clean checkpoints completed licensing without loss of code. Both feature repos clean at this
checkpoint. No server repository created or pushed; the existing product checkout is used.


### GS-006c early-leave and cancellation checkpoint

- [x] Standard explicit Dispose while Playing raises final local write callbacks with IsLeaving=true,
  commits before releasing identity, and disables drafts. Local removal/disconnection does the
  same before SessionEnded. Existing local-session termination policy is preserved.
- [x] Negotiated leaderboard-epoch-abort cancels only an owner's uncommitted epoch; repeated/absent
  abort is safe, committed rows cannot be erased. Quota is released with cascading membership.
- [x] Unpublished games/destructor abandonment queue bounded best-effort cleanup with owned IDs.
  Destructor never invokes game callbacks or propagates service failure; backend joins its executor
  before destruction, and cleanup jobs do not retain the backend into self-destruction.
- [x] Twenty successive unpublished games cancel without quota exhaustion or score mutation;
  four-user final callbacks, explicit Dispose and disconnect final scores tested in the fake backend.
  Real Alice/Bob processes verify final score 900 across server restart; native C remains healthy.

Validation: fake **113 checks pass**; Net **316/316 pass**; GamerServices **387 pass, 1 existing
HEADLESS skip out of 388**. Server **5,189 assertions pass** (including malformed abort ID,
wrong owner, committed refusal, idempotence, restart and quota); real TLS C++/pure C suite **2/2
pass, 19.89s**. Private C API Net/leaderboard/protocol **3/3 pass**; coverage --check current,
ABI/export surface unchanged. Same reproducible commands as GS-006b; logs `leave-*` in persistent
builds. next inspection remains 9473f5c89; no divergent committed changes to integrate.

This implements only LocalWithLeaderboards early leave. Ranked/all-machine submission waits for
GS-007, and ambiguous transport-failure EndGame retries still repeat callbacks (GS-006d audit).
A failed explicit Dispose preserves the live session/drafts; subsequent retry is possible. C++
destruction abandons without inventing scores, and unavailable-service cancellation expires under
the 24-hour/16-epoch bound. Crash cleanup cannot be guaranteed on a failed Internet connection.
Stream columns, TrueSkill and Xbox validation/order measurements stay unfinished. Next independent
work is GS-004b rotating refresh credentials/user-level persistence and maintenance heartbeat,
then GS-007 session directory; no original sample or standard-avatar acceptance is yet claimed.

### Known-good GS-006c set / next authentication slice (GS-004b/004c)

CNA 911eb90740e9a9b55221e676caaf196cb785a9f7; server
2d10c69d69d78f294c91ea3797fee1a9bf5d6cc4; sharp-runtime
fc033a0e8541a81498c4a496f56a0f59475c6e34. Source checkpoints clean before continuing.

Design before implementation: schema 5 adds title-bound refresh families/hashed rotating 256-bit
credentials, absolute 30-day lifetime, max 32 active families per user and <=1,024 rotations/family.
Access tokens remain one hour. Each refresh invalidates the earlier access token and rotates the
refresh credential atomically; reuse revokes that family, not unrelated devices/titles. Logout and
admin revocation cover access plus refresh authority. Rate-limit unauthenticated refresh attempts;
never log/put credentials in argv. Existing schema-4 access tokens remain valid without refresh.
Negotiated heartbeat updates activity without requiring gameplay to repeatedly fetch profiles.
[Token rotation/replay principles](https://www.rfc-editor.org/info/rfc9700/) guide the credential
lifecycle; CNA's protocol is not OAuth or Xbox LIVE. Lost rotation responses may require fresh
Guide authentication; do not silently replay a used refresh credential indefinitely.

Client follow-up GS-004c: user-level refresh records only (no passwords/access tokens/title-manifest
credentials); endpoint/title/slot isolation, strict sizes and atomic private files on POSIX using
owner checks, 0700 directories, 0600 files, O_NOFOLLOW/O_EXCL and fsync. No private-file persistence
claim on platforms without equivalent enforcement; keep ephemeral sign-in there until an OS
credential provider exists. CI can explicitly disable/isolate this storage via CNA environment
configuration. Dispatcher boundaries publish resumed identities; one executor serializes refresh,
heartbeat and ordinary requests with bounded retry/backoff, avoiding per-operation threads.
Credential cache tests must cover insecure permissions, symlinks/corruption, isolation, restart,
revocation, four slots and password absence. Real test harnesses isolate or disable persistence so
no owner's credentials are created or consumed.

### GS-004b server refresh/heartbeat checkpoint

- [x] Schema-5 migration preserves legacy access tokens; new credentials are hashed and title-bound.
- [x] Refresh rotation retires previous access/refresh atomically, keeps absolute lifetime, detects
  reuse and revokes only that family. Logout/admin revocation cover refresh authority; no bearer
  plaintext persisted in the database, logged or passed via administration argv.
- [x] Server heartbeat capability and development expire-access tool, source throttle, family/
  rotation caps, expired/revoked pruning and deterministic malformed/expired/wrong-title errors.
- [x] Unit expiry, replay, logout, another account/title/device, restart and v1 upgrade/future refusal;
  real TLS/server restart rotation, wrong-title refusal, replay revocation and heartbeat tested.
- [x] GS-004c: POSIX private persistence/resume, automatic refresh/heartbeat and real reconnect corpus (platform limits below).

Final server **5,214 assertions pass**, full genuine TLS C++/native C suite **2/2 pass in 21.00s**
(`refresh-final-e2e.log`). Simulated v1 downgrade fixture initially forgot the new session-column
index; fixed the fixture before final pass, not an actual forward-migration failure. Final source
set: CNA 911eb90740e9a9b55221e676caaf196cb785a9f7 / server
93a92901abc86c4884c7329a87e2723fac35fc59 / sharp-runtime
fc033a0e8541a81498c4a496f56a0f59475c6e34. Reproduce with the GS-006b harness variables/commands.
Server source committed/clean; no sharp-runtime changes. Client does not yet consume refresh or
schedule heartbeat, so no completed persistent XNA sign-in/reconnect claim. Continue GS-004c now.


### GS-004c client credential checkpoint

- [x] Private POSIX credential records, endpoint/title/slot namespace, atomic owner-only files,
  bounded validated reads, symlink/hard-link refusal and exclusive process lease. No passwords or
  access tokens persisted; explicit CI disable/override, unsupported platforms stay ephemeral.
- [x] Update-boundary resume of one/four accounts; serialized rotation, proactive renewal, 401
  renewal and heartbeat, bounded maintenance backoff, stale-operation generation guards, no
  duplicated SignedIn events on ordinary renewal/reconnect. Confirmed revocation clears authority.
- [x] Server issuance exposes serverTime so client deadlines use remaining lifetimes despite clock
  offset. Error messages do not echo arbitrary response error strings, which could contain secrets.
- [x] Seven credential-store unit tests (lease, private mode, isolation, unsafe links/permissions,
  corrupt/oversized input, disable); genuine TLS one/four-account persistence across process/server
  restart, explicit slot signout, expired-access refresh, Update-only heartbeat, outage/reconnect.

Affected targets build with max two jobs. Final fake harness **113 checks pass**; GamerServices
**395 run / 394 pass / 1 existing HEADLESS screen-saver skip** (`credentials-gamers-authorized.log`);
Net/SystemLink **316/316 pass** (`credentials-net-authorized.log`); private runner ABI/header/export/
coverage/Net/leaderboard/protocol gates **11/11 pass, 15.78s** (`credentials-c-api.log`). Server unit
**5,214 assertions pass**, complete real TLS C++/native-C corpus **2/2 pass, 60.73s**
(`build/credentials-complete-e2e.log`, TLS test 56.87s). Initial sandbox runs could not create UDP
sockets or write existing offline test storage; authorized runs passed. Earlier expanded fixture
hit the legitimate 10/min authentication cap; isolate test batches through restart instead of
weakening it. Another fixture checked an intentionally expired Alice token; renew that independent
family before its final persistence check. Final corpus includes both corrections.

User configuration/reproduction is in docs/gamer-services-server.md and server README/protocol.
Protected plaintext refresh files are not an OS keychain. Windows/browser secure credential
providers and transport/platform verification remain open. Lost successful rotation replies or
failed persistence can require fresh Guide sign-in; no unsafe perpetual refresh replay. Full push,
privacy/guest semantics and Xbox event/validation measurements remain open. Source set commits are
recorded immediately after checkpoint commits. No sharp-runtime changes. CNA next remains
9473f5c8972027d0a5f3e484dc5e4374bd095669 (ancestor, no new committed history to integrate).

### GS-007a next implementation design (before code)

Implement a transport-independent persistent server session directory first: PlayerMatch/Ranked
logical session type, title-scoped random identity, host ownership, 1..4 authenticated local users,
capacity/private slots, full sparse 32-property array and null-wildcard search. Server validates all
participant credentials and online privileges before an atomic join; never trust user IDs supplied
without authority. Host-controlled Lobby/Playing/Ended state, monotonic revision/CAS updates,
join-in-progress policy, heartbeat TTL, stale-session pruning, bounded listings/membership/creation
quotas and deterministic errors. Leaving/disconnecting releases membership; host leave ends the
session until migration is implemented. Restart preserves directory state only within its live TTL.
Directory operation success alone must not cause public Join success: realtime connectivity must
be established before GamerJoined/usable NetworkSession. GS-008 relay remains required for Internet
reliability; no direct IP/port Internet success claim. Document server-only capability until CNA
integration, invites, relay and XNA lifecycle are actually tested. Extend canonical protocol and
migration tests; no invented Xbox wire compatibility.


Known-good GS-004c set: CNA d57a41f541b7ff3c3b746a915ae522e6c83a3cb8;
server 79c8b265f4c03aa2240b0b8581fc53dd549868fe;
sharp-runtime fc033a0e8541a81498c4a496f56a0f59475c6e34. Checkpoint repositories clean.
