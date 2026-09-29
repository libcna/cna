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
capacity/private slots, fixed sparse eight-property array and null-wildcard search. Server validates all
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


GS-007 audit correction: existing managed reference NetworkSessionProperties.cs at
`/rv/data/development/github.com/libcna/xna4-decomp/reference/xna4/decompiled/windows/Microsoft.Xna.Framework.Net/Microsoft.Xna.Framework.Net/`
uses eight fixed nullable signed integers, Count=8, out-of-range guards and unsupported structural
Add/Remove/Clear. Current CNA inherits FNA's variable-list approximation, so public correction plus
host/read-only mutation guards is GS-007b (not silently accepted as measured compatibility).
Server uses eight fixed property slots. Microsoft reference XML confirms 2..31 max gamers, host-only
property updates and null-wildcard Find filtering; see also
[Microsoft NetworkSessionProperties](https://learn.microsoft.com/en-us/previous-versions/windows/xna/bb975815(v=xnagamestudio.40)).
No managed/FNA source copied into MIT server; facts guide independent protocol implementation.


### GS-007a directory checkpoint

- [x] Independent schema-6 persistent PlayerMatch/Ranked control directory; secure authentication
  of 1..4 locals, per-title membership, public/private capacity, fixed eight-property filtering,
  host-only CAS updates, Lobby/Playing and join-in-progress policy, bounded queries/quotas/leases.
- [x] Host expiry/leave closes membership; expired remote machine frees all its locals and bumps
  revision. Restart preserves only live leases. No ENet identity/endpoint/ticket claims yet.
- [x] Canonical header/parser/golden request copied byte-for-byte into CNA; explicit unsigned
  overflow rejection before narrowing, regression tests for every slot/type/bound.
- [x] 99 dedicated server assertions: four locals, authorization/title/privilege/isolation, atomic
  rejected joins, sparse matching/capacity/private reservations, playing/host/CAS state, expired
  remote/host/leave/restart, malformed types/fields. 5,215 existing protocol/service assertions pass.
- [x] Real TLS directory workers in separate processes: Alice+Charlie host, Bob+Dana discover,
  mismatch filter, join, grouped leave; both PlayerMatch and Ranked; server restart between host
  creation and remote discovery. This is control evidence, not public XNA networking/relay evidence.

Final server CTest **3/3 pass, 64.51s** (`build/directory-e2e.log`, TLS 58.16s, directory 2.52s).
CNA affected targets build; GamerServices **397 run / 396 pass / 1 existing screen-saver skip**
(`directory-gamers.log`); private native-C ABI/header/coverage/protocol/Net/boards **11/11 pass,
15.46s** (`directory-c-api.log`). Existing SystemLink 316/316 was verified in GS-004c; no transport
code changed here. Initial -Werror indentation and unsigned JSON comparison test failures corrected
before final pass. No proprietary/Xbox/FNA implementation copied into the MIT server.

GS-007 remains incomplete: CNA public Create/Find/Join integration, invitations/InviteAccepted,
realtime relay/direct transport, host migration and Ranked arbitration are still open. Next concrete
item GS-007b corrects fixed property semantics and host guards, followed by directory client/relay.
Generic pre-write interception may be needed in sharp-runtime's ElementReference: current proxies
cannot reject host/read-only writes before mutating. Keep it generic; no GamerServices logic there.
Standing layout approval SA-3 (docs/StandingApprovals.md) covers private storage growth with layout
pins, migration/rebuild note and full gate. Preserve all old constructor signatures/noexcept; any
change will be on a dedicated branch and recorded with exact tests. No sharp-runtime changes yet.


Known-good GS-007a source set: CNA 5c6edd9cb604c8a27a5cdfc01cb6db9ec82967c5 / server
 ae5770e80873f410cac605b0007c7922bdb81e2b / sharp-runtime
fc033a0e8541a81498c4a496f56a0f59475c6e34. Both service repositories clean at checkpoint.

GS-007b design/started: sharp-runtime `/rv/data/development/github.com/libcna/sharp-runtime` on
`feature/gamer-services-collections`, base fc033a0e, initially clean. Add only generic optional
pre-write guard to ElementReference<T>, additive three-argument constructor preserving two-argument
constructor and noexcept. It must guard every assignment/compound/inc/dec, leave value/version
unchanged on refusal and allow reads. Proxy gains one private pointer (64-bit size 16->24, align 8);
SA-3 applies with layout tests, migration/full-consumer-rebuild note and full component gate.
No CNA account/protocol/host policy goes in runtime. CNA property collection will supply the guard.
Use existing runtime build/, max two compile jobs, no pushes (mission instruction supersedes local
standing push rule). Completed runtime source/tests/docs must be committed before CNA integration.

GS-007b runtime completed: generic pre-write guard, six additive tests, legacy constructor/noexcept
preserved and three-pointer layout pin. Full clean build and complete component gate **18,104/18,104,
41 executables, zero skips/failures** using isolated unchanged original SOAP fixture. Two optional
SOAP skips were environmental; five stale XML test strings reproduced on original header and fixed
in separate c5cb8138 prerequisite. Guard commit 6c4a857d (feature/gamer-services-collections); no push.
Runtime has two unchanged module-boundary checker failures (Xml.Serialization/Core.Base dependency,
ServiceModel/Net.Http visibility); neither introduced by this primitive. Details and reproducible
commands in runtime docs/Migration-CNA-GS-007b.md. CNA fixed-property integration now started.


GS-007c design (independent server work while full CNA layout rebuild runs): persistent control
invitations bound to title, live directory session, sender membership and authenticated recipient.
CNA-owned opaque 128-bit IDs are not standalone bearer credentials. Separate send/list/get/accept/
dismiss and sessions.joinInvited operations; receiving is not accepting and acceptance does not
reserve capacity. An accepted invite admits authenticated 1..4 local participants, private slots
first, then public, atomically; replay succeeds only for the same already-joined machine/group.
No fabricated InviteAccepted constants or unsolicited receipt event. Guide/client Update delivery
is a subsequent integration item, not evidence provided by these server operations.

Schema 7, FK cascade on session close, 15-minute invite TTL while host lease remains live, retained
bounded records for one-day anti-abuse accounting. Limits: 64 live incoming per recipient/title,
32 newly-created invitations per sender/title/hour, 16,384 retained records per title. Strict fields,
opaque lowercase hex IDs, authenticated/title-isolated access, duplicate pending send idempotence,
stale/consumed/closed/cross-user rejection. No arbitrary caller paths or query text.
Server supports both directory kinds as CNA control policy; Xbox Ranked invitation/arbitration
rules remain unmeasured and must be checked before exposing public XNA flows. Any active member
may invite an account (no invented friends-only restriction). Logical control remains independent
from ENet data/relay. Canonical protocol/capability/golden copies and restart/two-worker TLS tests
will be updated; neither invitation UI nor Internet game transport is claimed complete.

Concurrent next audit: committed next advanced to 8d56fa2fa (c6d9d49de +8d56fa2fa SAMPLE-100 overlay
focus and LocalNetworkGamer identity fixes). These affect our Guide/Net path. Integrate committed
history at the clean GS-007b checkpoint, preserve the samples agent's plan notes, rerun affected
corpus. No uncommitted sample files incorporated.

Acceptance source inspection expanded in plans/gamer_services_acceptance_sources.md: original
SAMPLE-096 event handler confirms Guide acceptance precedes InviteAccepted and immediate JoinInvited;
SAMPLE-087 standard random-description/30-preset renderer logic is available, without extracting
proprietary Avatar assets. SAMPLE-075 identity flow exists. No achievement/leaderboard source matched
in the currently extracted sample corpus, so a minimal XNA-shaped compatibility sample is needed.
These are queued source evidence, not newly unblocked samples; no samples-agent files modified.

GS-007b implementation detail: five remaining structural NotSupportedException throws in
NetworkSessionProperties (Add/Insert/Remove/RemoveAt/Clear) are intentional fixed-array behavior,
proved by the existing managed reference; they are not service-unavailable stubs. IsReadOnly=false
also matches that reference, including read-only advertised snapshots. Owner check validates
index first, then disposed owner/current host before mutation; transport replacement preserves
the guard. C ABI structural symbols remain and map refusal to NOT_SUPPORTED; fixed copy/enumeration
and advertised-copy tests replace obsolete appending behavior. Docs/c-api/NET, LIMITATIONS and
FEATURE_MATRIX now describe the corrected behavior. Full rebuild currently ongoing; two unrelated
pre-existing content-pipeline test nodiscard warnings are recorded, not changed by this task.


### GS-007b native/runtime checkpoint complete

- [x] Fixed eight nullable property values, reference structural exceptions, index/copy/enumerator
  semantics, meaningful current-host/disposed guards and immutable advertised values.
- [x] Generic indexed proxy pre-write guard completed independently in sharp-runtime; no CNA policy
  there. Runtime full clean gate 18,104/18,104, no skipped/failed tests.
- [x] Bounded SystemLink parsing rejects >8 slots, invalid sparse counts and duplicate indices;
  accepts old <=8-slot frames with null padding. Existing ENet transport retained.
- [x] Public Join now exercised in the actual two-process ENet client harness, including host-only
  write rejection and real payload exchange. Existing migration/disconnect corpus passes.
- [x] C API structural refusal, fixed CopyTo/enumeration and immutable advertised copies synchronized;
  coverage/limitation source mappings corrected and generated reports refreshed.

Validation: focused consumers CnaNetTests, CnaGamerServicesTests, native-C Net/service clients,
service/protocol/two-process/dispatcher harnesses build. Native Net **324/324 pass, 4.325s**; GamerServices
**397 run /396 pass /1 existing screen-saver skip**; fake **113 checks pass**. Private C ABI/header/
coverage/Net/boards **10/10 pass, 12.44s** (protocol drift deferred until in-progress GS-007c canonical
copies are synchronized). Runtime source 6c4a857de129cf29b5d43430bedf24157d594f12.
Logs: cmake-build-debug/properties-{focused-build,net-final,gamers,fake,c-api-final}.log.

Full-all CNA build was intentionally interrupted at ~500 unrelated object targets; focused affected
consumers were then completed. Do not describe the whole all-target configuration as built. The
full-all attempt showed two pre-existing content-pipeline nodiscard warnings; focused Net rebuild
showed 32 pre-existing ignored-nodiscard warnings in exception test cases (no changed production
source warnings). Initial new fixture failure used Create's host flag while expecting Join's client
role; corrected fixture and added genuine public Join coverage. C smoke conversion briefly changed
an unrelated machine-count expectation; reverted after the gate caught it. Generated mapping/report
staleness corrected at the source. Final relevant gates above pass; no new failure remains.

GS-007c server invitation code/unit tests pass, final TLS/admin-reset gate currently in progress.
This does not yet connect Guide/InviteAccepted/public PlayerMatch or Ranked, and does not implement
Internet relay. Next committed-next integration target remains 8d56fa2fa after this native checkpoint.
## SAMPLE-100 native acceptance repairs — 2026-09-28

The current account backend exposed two pre-existing owning-layer defects in the unchanged
NetworkPrediction sample. Game.IsActive remained true while the real Guide sign-in overlay was
visible, so the original IsActive guard called ShowSignIn again and threw GuideAlreadyVisibleException.
The internal IGameOverlay now reports modal visibility; Game's getter combines it with retained
window focus, and the real Guide overlay supplies Guide.IsVisible. Drawing-only overlays remain
nonmodal. This follows the XNA Guide activity contract documented by the framework's author:
[Shawn Hargreaves, trial mode and Guide activity](https://shawnhargreaves.com/blog/trial-mode-in-xna-game-studio-3-0.html).
FNA's desktop Guide is a no-op and therefore supplies no equivalent real overlay.

LocalNetworkGamer also initialized its inherited Gamer identity to “Stub Gamer” despite holding a
real SignedInGamer. It now copies that profile's Gamertag and DisplayName; the existing wire roster
already carried the actual tag. The old local-join event assertion was corrected to expect the
provided profile instead of locking in the placeholder. Internal null-handle test fixtures retain
existing behavior; actual sample accounts are not fabricated.

The Debug CnaTests target builds. All 170 selected GameTest, GuideTest, LocalNetworkGamerTest,
NetworkGamerTest, NetPacketCodecTest and NetworkSessionTest cases pass through the private GPU
runner. New regressions cover modal activity/window-focus restoration and both identity fields.
The initial 169/170 run failed only the obsolete “Stub Gamer” assertion. Fresh Release OPENGLES3
sample processes sign in through genuine Guide username/password input against a separate
verified-TLS service fixture with two persisted accounts, create/find/join and exchange tank state
and all host options; both exit cleanly. Four separated-tank gameplay crops match exactly. No
sample/runtime bypass, public XNA signature change, C ABI addition or sharp-runtime edit was made.
Original Wine reaches the menu but cannot create its GFWL network session; original LAN behavior
is not claimed as measured. The owner's accepted scope is native plus a browser limitations page,
not browser multiplayer completion. Evidence is in sibling cna-samples SAMPLE-100 artifact
`evidence/requal-20260928/`; all GS parent tasks and browser directory/relay/auth gaps stay open.

### GS-007b committed-next integration checkpoint

Merged only committed `next` 8d56fa2fa6cbd40b2b99a4178f0510fce45d1043 into the dedicated
feature branch after c611878d538632d252650fb5d6eb2525c940256c. Kept service user IDs and
real signed-in display names when resolving LocalNetworkGamer; kept both agents' plan evidence.
The null internal fixture still has an empty name; normal service accounts are never fabricated.
No samples-agent uncommitted state copied. New generic modal/nonmodal Game overlay behavior
is retained. Affected Runtime/Net/GamerServices targets build; 325/325 Net tests pass (4.508s),
GamerServices 397 run/396 pass/one existing HEADLESS screensaver skip (1.960s), and 12/12 GameTest
cases pass on private Weston/Xwayland (159ms). Existing ignored-nodiscard test warnings remain.
Logs `cmake-build-debug/next-integration-{build,game,net,gamers}.log`.

Server GS-007c c1dcdda committed separately after 4/4 CTest pass in 65.71s; canonical protocol
synchronization and client typed directory/invitation implementation follow immediately. No new
public online NetworkSession/InviteAccepted/relay/avatar capability is claimed by this merge.

### GS-007c invitation service and canonical protocol checkpoint

- [x] Server schema 7: persistent recipient/title/session-bound invitations with explicit
  accept/dismiss, 900s lifetime, bounded inbox/title/sender quota, and atomic private-first invited
  joins for 1..4 independent local credentials. Duplicate operations preserve identity/timestamps;
  used invitation replay only resumes the same active group, never resurrects a departed group.
  Sender abuse counters survive directory close/recreate and admin reset.
- [x] Administration `inspect-online <title>` and `reset-online <title>`; schema upgrades preserve
  accounts, achievements and boards. Host lease expiration still closes sessions; no host migration.
- [x] Canonical protocol header/parser/golden copies synchronized; drift/property/C ABI gates
  10/10 passed. Affected client/protocol/C API harnesses build. Public C ABI unchanged.
- [ ] GS-007d: typed private client directory/invitation boundary, strict response validation,
  deterministic fake model and real two-CNA-process control tests (including four local credentials).
- [ ] GS-007e/GS-008: authenticated realtime relay and public online NetworkSession integration;
  Guide invite confirmation/InviteAccepted/BeginJoinInvited, lifecycle/event parity and Ranked writes.

Server c1dcdda99401133ccf146530592a0fe81638b248: clean `-Werror` build, 4/4 CTest passed
in 65.71s (unit 5,216 assertions; directory 99; invitation 153; verified-TLS multi-process
restart suite). Both directory kinds exercise ordinary joins and explicit accepted private invites
across restart, wrong recipients/titles, capacity atomicity, paging, quotas and admin reset.
Logs server `build/invitations-{build,unit,tls,final}.log`; CNA
`cmake-build-debug/invitation-protocol-{build,gates}.log`.

Reproduce server: `CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv cmake --build build --parallel 2`,
then set CNA_SERVICE_CLIENT_HARNESS and CNA_SERVICE_C_API_HARNESS to dedicated CNA build binaries
(as existing TLS harness documentation) and `ctest --test-dir build --output-on-failure`.
Opening the DB migrates transactionally; back up production DB before upgrade. Control protocol
remains HTTPS POST /cna/v1 with negotiated session-directory/session-invitations capabilities.
No server push/relay is implemented and no Internet multiplayer/public online XNA success is claimed.

Known-good integration set at this checkpoint (CNA protocol-sync commit immediately following):
CNA parent 744f51a5c388fcfbc18a34bea1fbe12517e99bcb;
sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12;
server c1dcdda99401133ccf146530592a0fe81638b248.

### GS-007d typed private client control checkpoint

- [x] Typed private IServiceSessionDirectory operations: create/find/ordinary and invited join,
  member read/lease renewal/host conditional update/group leave; send/list/get/accept/dismiss.
  DTOs contain logical identities, fixed properties and snapshots; no credentials/JSON/SQL in
  public Microsoft::Xna APIs. Hold owning backend lifetime while using its directory reference.
- [x] Strict bounded response checks: exact fields, numeric types/overflow, fixed eight properties,
  opaque IDs/correlation, host/local group identity, unique user/ordinal/at-most-four-per-machine,
  slot/count consistency, page bounds/filter/category, invitation states/timestamps and inbox IDs.
- [x] Online capability gates; explicit fake with injected clock, host CAS, join-in-progress,
  group membership, private-first accepted invites, retry/leave rules, host/remote leases and quota
  survival across close/recreate. Fake is opt-in and has no disk/network fallback authority.
- [x] Participant credentials are resolved under the transport lock. Owner or secondary expiry
  refreshes independently and rebuilds all participant arguments before retry. Secondary revocation
  signs out only that identity at Dispatcher.Update; a valid owner is retained. The existing
  LocalWithLeaderboards begin operation uses the same repair. No arbitrary per-operation threads.
- [x] Genuine two-CNA-process/four-account TLS control probe, both directory kinds/two game IDs,
  explicit Guide sign-in, real server restart/invite persistence, ordinary then private invited
  join/leave/retry/filtering, expired owner+secondary/secondary-only credentials, revoked secondary
  at Update and multi-local leaderboard scope recovery. Private control probe is NOT an original
  XNA online NetworkSession gameplay sample or Internet-connectivity proof.
- [ ] Next GS-008a: canonical bounded realtime relay framing/tickets; GS-008b secure relay and
  loopback ENet bridge, then public online NetworkSession/Guide invitation integration (GS-007e).

Validation: clean affected native/C API build, zero new warnings. 11/11 new deterministic tests
(6ms); complete GamerServices 408 run/407 pass/one existing HEADLESS screensaver skip (2.270s);
Net 325/325 pass (4.372s), including existing real ENet/SystemLink multi-process tests. Private
fake/protocol/property/C API header/exports/ABI/scope/coverage/Net/boards gates 13/13 pass (14.99s).
Generated C inventory stays 472 public headers/8,130 symbols; new directory header explicitly
excluded as internal (469 excluded headers). ABI 0.32/3,215 exports unchanged.
Server full gate 5/5 pass (75.37s): 5,216 unit, 99 directory, 153 invitation assertions; general TLS
59.24s, two-CNA directory 6.85s (31 join/16 host checks per kind). No server test skipped with
CNA probe variables configured. Server ded9163bc48aed90a74f887c176224728709b245; runtime
6c4a857de129cf29b5d43430bedf24157d594f12. CNA implementation checkpoint is the next GS-007d
commit, based on 594bf03f5. Exact commit set appended before the next implementation milestone.

Commands: build CNA targets CnaGamerServicesTests CnaNetTests cna_service_client_harness
cna_service_directory_client_harness cna_c_api_service_client cna_c_api_net_smoke
cna_c_api_leaderboards_smoke with shared ccache/--parallel 2. Server test env:
CNA_SERVICE_CLIENT_HARNESS=<task CNA build>/cna_service_client_harness;
CNA_SERVICE_C_API_HARNESS=<task CNA build>/cna_c_api_service_client;
CNA_SERVICE_DIRECTORY_CLIENT_HARNESS=<task CNA build>/cna_service_directory_client_harness.
`ctest --test-dir build --output-on-failure`; dedicated control test `-R '^service_cna_directory$'`
skips explicitly with code 77 if its probe is absent. TLS fixture provisions/migrates SQLite and
test CA in owned build temp, removes them and never puts secrets in argv/logs.
Logs: CNA `cmake-build-debug/directory-client-{final-build,unit,gamers,net,gates}.log`;
server `build/cna-directory-{build,e2e,final}.log` and `build/Testing/Temporary/LastTest.log`.

Initial new E2E exposed an incorrect internal heartbeat operation name; corrected to canonical
auth.ping. Response/fake unit tests did not cover that real negotiation path, so real E2E is
required. Additional review isolated secondary-auth failure from owner invalidation; tested by
secondary-only admin revocation. One initial C API inventory gate was stale after adding an
internal header; regenerated its source-derived scope report and final 13/13 gate passed.
Latest committed next remains 8d56fa2fa; no samples-agent uncommitted state touched.

### GS-008 relay implementation sequence (active)

Exact tested GS-007d integration: CNA c273fb92601f0e8c46da91eb95e849a6f55b37ee; server
ded9163bc48aed90a74f887c176224728709b245; sharp-runtime
6c4a857de129cf29b5d43430bedf24157d594f12. All three clean before this next slice;
committed next 8d56fa2fa integrated. No new sharp-runtime primitive is needed for the following
CNA-specific transport routing; generic proxy change remains the only runtime modification.

- [x] GS-008a1: canonical binary datagram codec/spec/golden vectors in server, exact CNA copies,
  bounded zero-copy parse and deterministic malformed/mutation tests in both products.
- [x] GS-008a2: one-use 60s hashed relay tickets, title/session/machine/local-member authentication,
  persistence migration and revocable connection grant validation. No data capability before usable.
- [x] GS-008b: Beast/OpenSSL secure WebSocket relay endpoint, single connection per machine,
  per-connection strand/one writer/bounded queue, frame/rate/timeout limits and grant revocation.
- [x] GS-008c1: private bounded receive assembler and correlated welcome validation; Net 336/336.
- [x] GS-008c2: private libcurl WSS/loopback UDP I/O and genuine two-process ENet probe.
- [x] GS-008c3: connect transport status/roster/lease handling to public Net Update/lifecycle (GS-007e2c3b).
- [x] GS-008c: CNA libcurl WSS connection and loopback UDP routes preserving ENet datagrams,
  protocol/certificate validation and controlled Net updates. SystemLink keeps its direct path.
- [x] GS-008d1: two actual ENet clients in separate rootless outbound NAT namespaces.
- [x] GS-008d2: client reconnect/server-restart and remaining authenticated fault corpus (see GS-008d2 checkpoint).
- [ ] GS-008d: genuine ENet exchange under relay-only isolated routing/firewall conditions,
  forged source/destination/malformed/rate/disconnect/revoke/server failure/reconnect corpus.
- [x] GS-007e1: private authenticated roster/control gate and genuine spoof/refusal relay probes.
- [x] GS-007e2a: exact null/family guards for public Net End operations.
- [x] GS-007e2b: caller-owned metadata/End-once and standard pending service Find.
- [x] GS-007e2c1: private loopback ENet host and relay-only preallocation packet/fragment limits.
- [x] GS-007e2c2a: immutable originating backend authority; fake/unconfigured relay refusal.
- [x] GS-007e2c2: immutable backend deployment authority and owned online preparation/rollback.
- [x] GS-007e2c3: consume prepared transport in standard public create/join and E2E.
- [x] GS-007e2c: owned online create/join preparation, including cleanup on abandonment/failure.
- [x] GS-007e2: public async operation ownership/preparation and online lifecycle wiring.
- [ ] GS-007e: publish standard PlayerMatch/Ranked lifecycle and Guide/invited joins once directory
  plus realtime transport succeeds; do not report membership-only success as multiplayer.

Relay control/membership and game datagrams are separate protocols. WSS uses the existing
HTTPS authority with a distinct /cna/relay/v1 upgrade endpoint; authentication credentials/tickets
are carried only inside verified TLS, never URLs/logs. Datagrams address a service-authorized
machine, not an arbitrary Internet endpoint; relay injects the authenticated source machine.
ENet retains its reliability/fragmentation/handshake semantics; a private loopback UDP bridge
provides per-peer routes. Direct Internet optimization is deferred; no IP/port-only NAT claim.
Reliable WSS/TCP carries ENet unreliable packets too and may incur head-of-line latency; this
is an explicit first relay tradeoff, not Xbox transport compatibility.

Transport audit: installed libcurl 8.14.1/OpenSSL 3.5.7 advertises ws/wss. CNA currently links that
shared libcurl. Its documented WSS path applies normal TLS CA/peer/hostname verification:
[libcurl WebSocket API](https://curl.se/libcurl/c/libcurl-ws.html). Use connect-only recv/send
with bounded frame accumulation/partial send handling, not hand-built WebSocket or cryptography.
Raise optional relay's minimum libcurl API level only when its client lands and test actual ws/wss
runtime capability. Server already depends on Beast/OpenSSL; Beast requires application-owned
queues/strands and permits one read plus one write concurrently:
[Beast stream notes](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/notes.html).
Desktop secure transport is measured here; browser/Windows/console availability remains a
separate platform validation item. Do not send credentials via sharp-runtime plain HTTP/WS.

GS-008a1 checkpoint: canonical original MIT server codec/header/golden corpus and relay-v1.md,
exact CNA copies under private Net implementation, extended drift gate. Header 24 bytes, payload
1..4,096 (compile-time checked against local ENET_PROTOCOL_MAXIMUM_MTU), maximum frame 4,120;
64 frames/263,680-byte queue ceilings defined for the next transport slice. Parser validates
limits/magic/version/type/reserved/nonzero machine before exposing a borrowed datagram span.
No passwords/tickets/objects/Internet endpoint fields in game datagrams. Server owns constants;
client never maintains an independent numeric encoding.

Server standalone codec 10,027 assertions including 10,000 deterministic mutations, clean
-Werror build; service/directory/invite/codec unit gate 4/4 pass (7.52s). CNA new 3/3 golden/MTU/
mutation tests plus complete Net corpus 328/328 pass (4.366s), private protocol drift/properties
2/2 pass (0.23s). CWD-independent test fixture path pinned by CMake. No XNA/C API signature
change. Logs server build/relay-protocol-{build,unit,final}.log and CNA
cmake-build-debug/relay-protocol-{build,unit,net,gates}.log. The first fixture accidentally encoded
a seventeen-byte ID and literal terminal escape; strict codec/JSON gates refused it, corrected
canonical fixture to sixteen bytes and actual newline. Production codec did not require relaxation.

No WebSocket endpoint, ticket or forwarding is implemented/advertised by this slice. Next
GS-008a2 is one-use hashed relay ticket/grant authority, then GS-008b secure forwarding.
Latest next remains 8d56fa2fa. Full TLS/CNA multi-process gate remains the measured GS-007d
75.37s checkpoint, not rerun as evidence of unused standalone framing.

GS-008a1 exact tested set: CNA c90f0e39f45e7823623058a1063b364735a9214e, server
c78de9481301c8566cbedc568fb9ae1cba780644, runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
GS-008a2 now active: short-lived one-use hashed tickets require exact authenticated local group
and machine owner. Established grants bind all members' revocable refresh families so ordinary
access rotation does not break them; no grant survives a revoked/expired family, membership/host
lease expiry or title mismatch. Grant release and one-hour ceiling bound retained authority.
Ticket schema migration/test coverage precedes the secure endpoint; advertise ticket issuance
separately and never advertise working relay before forwarding exists.

### GS-008a2 secure relay authority checkpoint

Implemented schema 8 with hashed one-use 60s tickets, exact complete local-group/machine-owner
checks, title/session isolation, bounded per-machine/title records and transactional redemption.
Server-owned grants bind every member's revocable refresh family and expire within an hour;
normal access rotation preserves authority, revoke/expiry/privilege loss/leave/host expiry refuses
it. Exact disconnected-grant release does not mutate account/directory state. Unconsumed tickets
survive restart until expiry, used bearer credentials cannot be replayed. No WebSocket endpoint
or forwarding exists yet; only relay-tickets is advertised, not working relay.

CNA adds typed ephemeral ticket retrieval and capability/response bounds. No game-facing token
API, C binding or persistent ticket cache. The existing two-CNA control probe validates host and
remote machine authority after multi-local refresh. Explicit fake generates fixture-only authority;
it does not emulate cryptographic grants or connect to the online server.
Canonical relay header now lives beside the existing control header in GamerServices' private
protocol directory; Net privately includes that single canonical artifact. This removes a would-be
GamerServices-to-Net dependency when ticket validation needs relay constants. Codec implementation
stays in Net; no public API or behavioral redesign. Header/parser/golden drift remains enforced.

Validation: server clean -Werror build, authority 60 assertions; full configured CTest 7/7 pass
in 74.10s (unit 5,217; directory 99; invitations 153; relay framing 10,027; authority 60; TLS
57.18s; actual two-CNA processes 6.53s/32 join+17 host checks per kind). Schema v1-to-8 upgrade
and future-schema refusal still pass. CNA GamerServices 409 run/408 pass/one known HEADLESS
screensaver skip (1.838s), Net 328/328 pass (4.376s), private C API/fake/protocol/ABI gates
13/13 pass (11.87s). ABI 0.32/3,215 exports unchanged. Existing Net nodiscard test warnings,
no new production warnings. Two initial new server indentation warnings were fixed before the
clean build; no unresolved new failure.
Logs: server build/relay-auth-{build,unit,final}.log, CNA
cmake-build-debug/relay-auth-{build,gamers,net,gates}.log. Reuse the same three probe env vars
and `ctest --test-dir build --output-on-failure`; authority alone `-R '^service_relay_authorization$'`.
Opening SQLite migrates transactionally; back up before upgrading.

Concurrent milestone inspection: next was externally advanced to c90f0e39f (our committed
GS-008a1 checkpoint), also tracking origin/next. Canonical samples worktree status clean. This
agent did not move next, push, or copy any uncommitted work. It is already an ancestor of this
feature branch, so no merge is necessary. Runtime still 6c4a857de129cf29b5d43430bedf24157d594f12.
The next task is GS-008b secure WSS forwarding, rate/queue/grant failure tests, followed by
CNA ENet bridge and relay-only connectivity proof; all public online sessions/Guide invites/
Ranked lifecycle/standard avatars/sample acceptance/final audit remain unfinished.

GS-008a2 exact tested integration: CNA 1978c862fc82af6e9f8ffa540f6bba7f7c111b02;
server a3af3bf7a13ef5bcc5fd0a3323de4f9d7989a54b; runtime
6c4a857de129cf29b5d43430bedf24157d594f12. Repositories clean before GS-008b except this
new active-plan entry. Next currently c90f0e39f, already integrated by ancestry.

GS-008b implementation contract: separate WSS /cna/relay/v1 endpoint; bounded four-field
version/id/game/ticket encrypted handshake; one connection per machine, server-generated source
ID, session/title-only destination routing. Beast streams use per-connection Asio strands, one
reader/one serialized writer, 64-frame bounded queues, handshake/idle/grant deadlines and rate
limits. Grant revalidation on a periodic timer closes revoked/expired/member-lost connections;
close releases only that grant. Explicit loopback insecure mode is development-only; TLS is
mandatory for public binds. Successful relay registration never publishes XNA state on an I/O
thread. Independent WSS tests precede CNA ENet bridge/public NetworkSession integration.

GS-008b in progress: implemented private server RelayHub plus cross-strand RelayQueue. Reservation
precedes executor posting, includes active write, and permits one pending wake notification; overflow
posts one close request. This prevents an unbounded hidden posted-frame queue. Separate authenticated
streams use a strand per accepted socket, one reader/serialized writer, 5s authentication/validation,
30s idle keepalive, 512 message/control-frame and 1 MiB ingress per second, 96 active relays/128 total
connections. Unknown/self destinations drop, never broadcast. Header constants synchronized; build
and independent verified-WSS security/forwarding tests are in progress, not acceptance evidence yet.

GS-008b tests found and corrected an exact-limit fragmentation edge: Beast async_read needs one
scratch byte to consume a final zero-length continuation after 4,120 accumulated bytes. Private
buffer is 4,121 bytes; read_message_max remains 4,120 and rejects extra payload before growth.
The slow-consumer fixture initially spent its deadline draining an artificially tiny TCP window;
corrected it to verify grant reclamation first, abort that uncooperative fixture socket and test
fresh registration/forwarding. Independent corrected WSS run passed 257 checks in 47.19s. A final
review added unknown-field replacement (not just extra-field) refusal and matching golden vectors;
all binaries are rebuilding before the final full integration gate. Initial live manifest/binary
mismatch produced expected old-code failures; these are recorded, not accepted final results.

GS-008c implementation contract (next, not implemented): private Net transport derives same-authority
WSS URL from validated HTTPS deployment settings; mature libcurl verifies CA/hostname/TLS >=1.2,
disables proxies/redirects, checks actual ws/wss support and bounds partial-frame reassembly before
allocation. One owned worker per machine serves persistent TLS plus loopback UDP routes, not arbitrary
threads wrapping Begin/End. Service tickets stay memory-only. Stable per-remote-machine loopback
ports preserve ENet peer identity; only local ENet endpoint datagrams are forwarded, incoming source
IDs must match service-authorized routes. No XNA mutation on worker; Update consumes readiness/failure
and authoritative directory roster before GamerJoined. Existing SystemLink path remains direct.
CNA parser tests must cover fragmented/chunked/empty-final/control-interleaved messages and strict
welcome correlation/limits; native two-process probe must exercise real ENet before public Create/Join.
Sources consulted for partial sends/receive metadata: https://curl.se/libcurl/c/curl_ws_send.html,
https://curl.se/libcurl/c/curl_ws_recv.html and https://curl.se/libcurl/c/CURLOPT_CONNECT_ONLY.html.

### GS-008b secure server relay checkpoint

Server cba4235cd539e6ed514ccfcf4b3abe48aa6f2efd implements separate authenticated WSS forwarding,
exact hello schema/correlation, version/capability response, one live channel per machine, authorized
source injection and title/session routing; missing/self/foreign destinations drop. Per-socket
strands, one reader/writer, bounded cross-strand reservation including active writes, one pending
wake/overflow notification, 96 relay/128 connection caps, 512 messages/control frames + 1 MiB ingress
per second, 5s authentication/close/periodic grant checks and 30s idle keepalive are active. Secondary
account revocation/lease expiry invalidates the complete multi-local grant within 5s. Disconnect
releases only the exact grant; reconnect needs fresh one-use authority. No URLs/header credentials,
password/token logging, custom crypto or XNA objects over the relay. Admin inspect-online includes
relay ticket/grant counts; reset-online FK cascade includes grants. MIT server/independent BSD-3-Clause
Python websockets 15.0.1 test dependency remain separately licensed. No new runtime primitive.

Final matching-build configured CTest 9/9 pass, no skip, 120.48s: service 5,217 assertions,
directory 99/1.60s, invitations 153/2.70s, protocol 10,027/.02s, authority 60/1.69s,
flow/hello/queue/rate/routing 725/.01s; general TLS + native C++/C probes/restart 56.96s;
two native CNA control processes 6.47s/32 join+17 host checks per category; WSS 259 checks/47.36s.
WSS wire corpus covers both directory categories/titles, two two-user machines, verified trust/
hostname refusals, exact hello/unknown-field replacement/UTF-8 errors, use-once/duplicate machine,
max and fragmented datagrams, source injection, self/unknown/foreign session/title destinations,
repeated reconnect, malformed/type/oversize frames, rate limits, slow-recipient grant reclamation,
host lease loss and secondary revoke. Explicitly no CNA ENet/isolated Internet/public online sample
claim. Canonical hello golden vectors now accompany binary frames and match CNA byte-for-byte.

CNA build + Net 328/328 pass (4.400s; includes the existing real SystemLink/subprocess corpus),
GamerServices 409 run/408 pass/one known HEADLESS screensaver skip (2.343s), private protocol/C API/
ABI/fake gates 13/13 pass (13.44s); final changed-vector codec 3/3 + drift check pass. No new public
XNA/C native exports; ABI 0.32/3,215 exports unchanged. Clean server -Werror build. Logs server
build/relay-wss-{build,final-matching}.log; CNA cmake-build-debug/relay-wss-{build,net,gamers,gates}.log.
Earlier failing diagnostic logs preserve exact-limit fragmentation and unbounded tiny-TCP-window
fixture failures; both fixed, final matching gate has no unresolved failure. A source/vector update
before rebuild caused an intermediate 7/9 old-binary mismatch; final rebuild/gate above supersedes it.

Concurrent inspection: next c90f0e39f remains an ancestor, no merge needed; samples worktree untouched.
Runtime 6c4a857de129cf29b5d43430bedf24157d594f12 unchanged/clean. Tested CNA source checkpoint is
this GS-008b protocol-sync commit; exact resulting code commit set is recorded immediately afterward.
Rootless user+network namespace probe (`unshare --user --map-root-user --net true`) succeeds outside
sandbox; `unshare`, `ip` and `bwrap` are available, slirp4netns is absent. GS-008d can use separate
rootless NAT namespaces with a mature external NAT helper, subject to provisioning; no isolation
claim has yet been measured. First unfinished task is GS-008c private libcurl WSS/loopback UDP
transport and parser/real-ENet tests, followed by isolation/standard online sessions/invites/avatars.

GS-008b exact tested code integration set:
- CNA: 08260011b5b9a25bb8ce4726aa9ad7fff7b046ec (`feature/gamer-services-server`).
- sharp-runtime: 6c4a857de129cf29b5d43430bedf24157d594f12 (`feature/gamer-services-collections`).
- cna-gamer-services-server: cba4235cd539e6ed514ccfcf4b3abe48aa6f2efd (`feature/gamer-services-server`).
All three clean at this checkpoint. This subsequent handoff commit changes documentation only.
GS-008c1 now active: bounded private client message assembler and strict relay welcome validation,
with deterministic fragmentation/chunk/control/boundary/error tests before native WSS/UDP I/O.

### GS-008c1 private client receive checkpoint

Private Net `RelayMessageAssembler` owns completed messages and validates chunk offsets/remaining
lengths, aggregate 4,120-byte ceiling, at most 64 fragments, control-frame limits and ordering before
allocation. Control messages interleave between data frames without replacing the partial message;
empty final fragments are valid. Any refusal clears assembly state and exposes only constant codes.
The 64-fragment limit is a client receive policy, not a claim about server fragment enforcement.
Strict <=1,024-byte welcome parsing reuses the bounded UTF-8/duplicate-key parser, validates exact
root/result schema, request correlation, ticket session/machine, datagram/queue limits and unique
bounded capabilities. Unknown optional capabilities remain permitted. JSON linking is explicit in Net.

Eight new tests cover all boundaries, metadata overflow/order errors, control interleaving, malformed
welcome and 1,000 deterministic random fragment/chunk layouts. Clean two-job build and complete Net
336/336 pass (4.410s), including SystemLink and subprocess regressions; canonical protocol drift
passes. Initial 335/336 run exposed an incorrect test expecting a specific `what()` rather than the
safe `code()` plus generic text; assertion corrected, no parser defect or unresolved failure.
Logs: `cmake-build-debug/relay-client-parser-{build,net-tests}.log`. Server cba4235 and runtime
6c4a857 unchanged/clean. No public XNA/native ABI addition. Parent GS-008c remains incomplete.
Next is GS-008c2 verified libcurl WSS worker and bounded per-machine loopback UDP routes, tested with
actual ENet in separate CNA processes before online NetworkSession is enabled. Concurrent next has
advanced to 31a560af9 (INPUT-EMU-001); inspect/integrate committed history at this milestone.

GS-008c1 integration: merged committed next 31a560af9 (INPUT-EMU-001) after parser commit
a543dc45b. Only conflicts were concurrent top-of-file AUDIT/NEXT notices; retained both reports.
No samples working-tree file was copied. Rebuild and Net regression follow before GS-008c2.

GS-008c2 active implementation: private libcurl CONNECT_ONLY WSS provider and one owned cancellable
worker bridge now compile. Authority derives only from validated deployment URL; certificate,
hostname, TLS >=1.2, redirects/proxy refusal and runtime TLS/ws/wss support are enforced. Net's
libcurl minimum becomes 7.86 (WebSocket APIs); no sharp-runtime business logic or primitive added.
Stable loopback sockets for <=30 authorized remote machines preserve ENet peer identity; only the
known local ENet source endpoint may send. Client queue <=64 frames including partial active send,
rolling 1s send budget reserves eight control messages; fair round-robin bounded UDP drain,
unknown incoming source drop, obsolete unstarted route frames drop, atomic route replacement and
controlled Ready/Failed/Stopped snapshots. Saturation is observable UDP loss, handled by ENet,
rather than an unbounded queue. Five-second hello/partial message/send deadlines; no detached work.
Four new unit cases and an independent MIT server-owned test provision four accounts/two titles,
standard Guide authentication in two CNA processes, verified libcurl WSS and actual ENet channels,
including 32KiB reliable fragmentation, unreliable data, wrong source/oversize local UDP refusal,
secondary revoke and server loss. Public online NetworkSession remains refused until its integration.
Final matching validation/commit follows. Next isolation dependency was unpacked into /tmp only:
Debian slirp4netns 1.2.1-1.1, libslirp 4.8.0-1+deb13u1 (no host package/network changes).
An ephemeral rootless NAT namespace reaches the configured host-side test TCP listener through
10.0.2.2 and has a distinct network namespace inode; this is provisioning evidence, not CNA/ENet
isolated multiplayer. Upstream manual: https://github.com/rootless-containers/slirp4netns/blob/master/slirp4netns.1.md.
The externally executed GPL-2.0-or-later test helper is not linked, copied into or distributed with
MIT server code. Next: two separately NATed real CNA clients, reconnect/failure corpus, standard
online session/invites integration and avatar migration. Parent GS-008c/d/e still open.

GS-008c1 integration follow-up: private C API gate initially 11/13; generated inventory/limitations
were stale after next INPUT-EMU-001's four new device/window CNAEXT declarations and one platform
header. Regenerated summaries (8,134 symbols, Net/GamerServices counts unchanged, no C exports),
with no new input bindings. Final private gate passes 13/13; this is committed separately from
relay implementation. Existing ABI remains 0.32/3,215 exports. No unexplained failure retained.

### GS-008c2 verified native relay checkpoint

Completed private transport implementation with four unit cases; no public XNA/C protocol API.
Final build is clean, two jobs. Net 340/340 (4.480s), GamerServices 409 run/408 pass/one known
HEADLESS Guide screensaver skip (2.184s), private ABI/C API/protocol/fake gates 13/13 (13.29s).
C API inventory correction for committed next is separate commit 856efb4c4. No changed ABI or
export count. Existing SystemLink/subprocess corpus stays passing. Final server 10/10, no skip,
133.34s: service 3.83s (5,217 assertions), general TLS/native C++/C/restart 58.62s, directory
99/2.22s, invitations 153/3.16s, two-CNA control 6.94s, relay codec 10,027/.02s, authority
60/2.17s, flow 725/.01s, WSS 259/48.10s, actual two-CNA ENet/WSS 8.26s.
The final native probe runs four accounts/two local gamers per machine for both categories and
two isolated title IDs. Four application messages each direction/category (16 delivered total),
reliable fragmentation of 32KiB data plus discriminator, reliable small/MTU-size and unreliable
second channel compare exact bytes. Wrong local UDP source and 4,097-byte datagrams are refused;
stable route replacement is checked. Wrong trust/hostname abort before secret hello. Secondary
Dana revoke closes only her machine grant; host remains ready until independent server loss.
Server failure is observed as safe Failed snapshot, and stop joins/reclaims worker/socket lifetime.
The harness uses private directory/ENet infrastructure, not public online NetworkSession;
public acceptance, Internet/NAT isolation and reconnect remain incomplete. No avatar claim.

Native command: build `cna_service_relay_client_harness`, then set
`CNA_SERVICE_RELAY_CLIENT_HARNESS=<absolute binary>` for sibling `service_cna_relay` CTest.
Full server gate also sets existing CLIENT/C_API/DIRECTORY harness variables as earlier documented.
Logs CNA `cmake-build-debug/relay-client-io-{build,net-final,gamers,private-gates-final,native-final}.log`;
server `build/relay-client-io-{build,full-tests}.log`. Initial compilation exposed platform-dependent
ENetBuffer field order; switched to named field assignments (Unix/Windows declarations audited),
then clean compile. Initial private gate 11/13 was stale next-derived inventory, fixed as recorded;
no unresolved failure remains at this checkpoint. Last formatting-only edit changes no behavior.
Server committed 620415aa39c91b6510a05dccb5ed7b2f66f72ce2; runtime unchanged
6c4a857de129cf29b5d43430bedf24157d594f12. Resulting CNA relay commit is pinned in the next
checkpoint. Next unfinished: GS-008d1 run these real peers in separate unprivileged NAT namespaces
without incoming ports; then reconnect/remaining fault corpus and GS-008c3/GS-007e public lifecycle.

### GS-008d1 separate NAT namespace checkpoint

Independent MIT server test mode `--isolated` executes the same native ENet/CNA relay probe in
separate rootless user/network namespaces with external packaged slirp4netns helpers. Namespace
inodes differ from host and each other; child checks only loopback/10.0.2.100 IPv4 interfaces and
10.0.2.2/tap0 default route before exec. No helper API socket/inbound mapping is created; both
identical private addresses cannot identify the other ENet listener. HTTPS/WSS gateway access
uses a verified matching IP SAN. Four accounts/two local users per peer, both categories/titles,
all 16 application deliveries including 32KiB fragmentation/unreliable channel, UDP guards,
secondary revocation/owner isolation and server loss pass. No credentials are buffered by the
namespace setup helper or logged. Child processes, helper exit-FDs and namespaces are reclaimed.
No host routes/firewall/sysctl/packages/desktop modification. Helper in /tmp only, original MIT
orchestration does not incorporate/link GPL helper code. Production transport stays libcurl TLS.

Final matching server clean incremental -Werror build and 11/11 CTest pass, no skip, 137.21s;
normal native relay 7.76s, separate NAT native relay 8.14s, independent WSS 47.18s. Earlier isolated
standalone run also passed; no new failing diagnostic. Prerequisite absence is an explicit skip77,
not evidence, and never silently changes kernel/host policy. Reproduce full earlier four-probe
server command with `CNA_SERVICE_SLIRP4NETNS=/tmp/cna-gamer-services-nat-tools/root/usr/bin/slirp4netns`
and `CNA_SERVICE_SLIRP_LIBRARY_PATH=/tmp/cna-gamer-services-nat-tools/root/usr/lib/x86_64-linux-gnu`.
A system helper can omit library override. Log server build/relay-nat-{build,initial,full-tests}.log.
CNA production code unchanged, Net/GamerServices/private gates remain the GS-008c2 measured set.
Known-good tested code set: CNA 967305dd7b93f992f7c177a7055bd5892ba8523e;
server 5a850401da339ec44b67ed93fde8f019f44c6e9d; runtime
6c4a857de129cf29b5d43430bedf24157d594f12. All on recorded feature branches, clean before this
CNA documentation-only checkpoint. This is shared-host NAT isolation, not public Internet
production/latency/failover proof; parent GS-008d remains open for reconnect and remaining faults.

Next active GS-007e1: private authenticated roster/handshake validation before public online
integration. Existing ENet ClientHello trusts peer gamertags; online mode must bind peer loopback
route to the authenticated service machine and accept only its exact 1..4 accounts. Use service
ordinals for deterministic 1..31 wire IDs, owner-only host flags, validate welcome/roster and
properties before object mutation. Preserve direct SystemLink behavior. Audit also confirms
NetworkSession's nested action is currently completed immediately and End owns/deletes it;
public async refactoring must handle callback reentry, wait signaling, operation ownership and
C API pending-action consumption together. GamerServices ServiceAsyncResult is caller-owned and
cannot simply be substituted without that ownership audit. Runtime BinaryReader.ReadString already
checks declared length against a seekable stream before allocation; no generic runtime fix needed.
Bound service handshake count/string/UTF-8/boolean/exact length before decode; no custom crypto or
XNA objects on service control. Full public Create/Find/Join and invites remain gated until this
roster and controlled async/lifecycle integration are genuinely tested.

### GS-007e1 authenticated private control checkpoint

New private ServiceRoster validates full bounded directory authority (not advertisements), unique
accounts/tags/ordinals, exact capacity/private counts, nonzero machines, host binding, local group
existence and <=4 participants per machine. UTF-8 is checked through the existing strict JSON
encoder only after field byte limits. IDs are ordinal+1 (1..31), independent of arrival order;
only hostId receives IsHost. Exact machine claims preserve the caller's local order. Host welcomes
include complete connected groups from authority; client validates assignments, all represented
remote groups, host account, exact properties and no own/duplicate/spoofed entries before mutation.
Unknown/new roster identities require fresh authority in future Update wiring, never blind trust.

Private control preflight checks <=4KiB before field copies: known control tags, 1..4 hello/assigned
counts, <=31 roster/leave counts, IDs1..31, canonical <=32-byte UTF-8 BinaryWriter names, strict
booleans, all eight properties, known state and exact end-of-packet. AppData is explicitly a separate
path, retaining ENet application semantics; the native test alone bounds its synthetic payload64KiB.
Direct SystemLink decoder/transport remains untouched. No proprietary protocol, new public XNA/C
method, C API mapping or runtime primitive. Runtime seekable ReadString already checks remaining
length; there was no generic allocation defect to fix.

Six new cases include all 31 slots/four locals, UTF-8, malformed authority, spoofed/missing/partial
welcome, every control truncation/trailing byte and 10,000 deterministic decode/encode mutations.
Clean two-job build; focused6/6 (59ms), complete Net346/346 (4.515s), private ABI/C API/protocol/
fake gates13/13 (12.20s). Native relay probes now run actual bounded Net ClientHello/ServerWelcome
and AppData messages, compare service-authorized sender/target IDs and inject one cross-machine
Alice claim before a valid Bob/Dana hello; host refuses it before identity mutation. Normal+separate
NAT CTest2/2 pass, no skip, 15.79s (7.68/8.10s), still 16 exact application deliveries including
32KiB fragmentation/unreliable channel, four accounts/two titles and revoke/server loss. Server
unchanged5a850401da339ec44b67ed93fde8f019f44c6e9d; runtime unchanged6c4a857. Prior full server11/11
is the unchanged server baseline; two affected CNA integration tests were rerun against this new
code. Logs CNA `cmake-build-debug/service-roster-{build,unit,net,private-gates}.log`, server
`build/service-roster-native-tests.log`. No unresolved failure. Next now967305dd7 already includes
our committed GS-008c2 and is an ancestor; samples worktree clean/untouched, no new merge needed.

Next GS-007e2: ownership-safe public online APM/preparation plus Net lifecycle/roster/lease wiring.
Immediate audit defect: existing EndCreate/EndFind/EndJoin accept null when activeAction is null and
can dereference null; they also accept the wrong Begin family if its pointer is current. Add exact
operation-family/null validation without consuming a mismatched action, then build genuine pending
online preparation through the existing bounded executor. Do not merely flip RealNetworkingEnabled;
online peers must use the authenticated roster gate before GamerJoined and preserve SystemLink.
Existing End-owned legacy results vs caller-owned GamerServices results and callback reentry need
an explicit lifetime solution. Ranked leaderboard arbitration, Guide invites, reconnect, all
standard Avatar migration and original acceptance samples remain unfinished.


### GS-007e2a complete: exact public Net End ownership guards

Existing EndCreate/EndFind/EndJoin compared only pointer identity, admitting null when no action
exists and accepting a different Begin family. Add a private operation discriminator and validate
non-null/current/family before any dereference or mutation. Wrong Ends retain the original action
and busy state. Four regression cases cover null/foreign pointers and all six cross-family pairs,
then the correct End succeeds. The local XNA managed shared XOverlappedAsyncResult.cs
PrepareForEndFunction (lines 68..84) explicitly throws ArgumentNullException for null,
ArgumentException for a foreign result type, InvalidOperationException on a second End, and waits
for completion. Null now follows that evidence; family rejection is a CNA ownership guard and
console-native family/error order is not yet measured. The next async slice must preserve result
metadata after End, implement End-once and genuine wait/pump behavior.
No transport gating or SystemLink behavior changes. Validation: incremental native/C API builds
pass; Net 350/350 (4.601s), including all four new cases; private C API/ABI/protocol/fake gates
13/13 (10.49s). Logs: cmake-build-debug/service-end-guards-{build,net,private}.log.
Server 5a850401da339ec44b67ed93fde8f019f44c6e9d and runtime
6c4a857de129cf29b5d43430bedf24157d594f12 unchanged. GS-007e2a complete; parent public
online APM/lifecycle remains unfinished. Next: caller-owned result lifetime and real pending
service searches, then authenticated online transport/session preparation.


### GS-007e2b checkpoint: caller-owned APM metadata and public online Find

After GS-007e2a commit 34abca070, preserve Net results through End (same caller-owned C++ policy as
GamerServices APM). Sync/C API adapters own them with RAII. Registry lookup compares pointer
identities without dereferencing foreign pointers. End-once, busy cleanup on errors/abandonment,
callback reentry/throws and metadata/wait lifetime are tested. Immediate legacy offline results
report CompletedSynchronously=true; queued online searches false. This differs from the managed
shared XOverlapped result's always-false native-operation flag; CNA's completion timing is explicit.
SystemLink Join preparation/handshake still occurs in End and needs its separate lifecycle audit.

PlayerMatch/Ranked BeginFind queues typed directory work on the existing bounded backend executor;
only Dispatcher.Update publishes completion/callback. Capture immutable identity/filter values,
validate authenticated current local identities, page up to 256 listings with duplicate/category/
capacity/filter checks, and carry an opaque private listing snapshot for later real joins. More
than 256 matches fails LIMIT_EXCEEDED rather than silently inventing/truncating sessions.
No packet/config/credential methods added to XNA. Create/Join online remain refused pending actual
secure transport/lifecycle wiring; synthetic online listings no longer bypass membership by
creating disconnected placeholder sessions. Fake and genuine multiprocess validation pending.

GS-007e2b audit follow-up: a queued search must not retain its own backend (self-destruction on
worker/cyclic fake lifetime). The result now retains the executor; queued work uses its raw
pointer while backend shutdown joins work before field destruction. A fake lifetime regression
proves pending result release destroys a replaced backend without a self-retention cycle.
The test-only backend replacement hook is intended for quiescent use; production backend remains
fixed for the dispatcher lifetime. Existing SystemLink threading policy remains the update owner.


GS-007e2b verified results (2026-09-28): Net 363/363 (4.669s), including 13 new cases across pending
service searches and metadata lifetime. Private C API/ABI/protocol/fake gates 13/13 (17.94s),
8134-symbol inventory unchanged except declaration digest; native C ABI exports/baseline unchanged.
Incremental affected targets build. Initial fake-auth test expected a private ServiceOperationError,
but the fake's established signout error is the public GamerServicesNotAvailableException; fixed
that assertion. Initial private gate 11/13 caught the outdated successful synthetic online join
smoke and generated digest; corrected both. First expanded E2E failed because the new search-empty
role was absent from the harness allowlist; corrected that test setup. No unresolved failure.
Full configured server suite 11/11, no skip, 148.70s (public directory 7.78s, relay 9.95s, NAT relay
13.01s). After the final lifetime-only executor retention adjustment, matching public directory
E2E 1/1 (8.94s) and final Net/private gates above passed. Logs: server
build/service-public-find-{integration,final}.log and CNA cmake-build-debug/service-search-
{final-build,net,private}.log. Server commit 0cd1d08984210a120e7b0b7cb238f6f438f4d8af;
runtime 6c4a857de129cf29b5d43430bedf24157d594f12. CNA source in this GS-007e2b commit;
exact resulting code hash is pinned at the next plan checkpoint. next remains 967305dd7, already
an ancestor; samples worktree untouched. No XNA sample is claimed newly unblocked by Find alone.

Immediate audit prerequisite before mutating online preparation: other GamerServices APM closures
still retain their own backend, and leaderboard BeginServiceRead constructs public reader/gamer
objects on the executor. GS-004k will move these to caller-owned executor lifetime and logical
background results with public-object construction during End on the update owner. Then continue
GS-007e2c owned online create/join preparation plus GS-008c3 roster/lease/update wiring.
Public online creation/joins/invites, Ranked arbitration, reconnect, standard avatars and real
acceptance samples remain unfinished. This checkpoint is not project completion.


Known-good GS-007e2b tested code set:
- CNA b7754e45f5259203c9e31fee30b8796cf2e68558.
- sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
- cna-gamer-services-server 0cd1d08984210a120e7b0b7cb238f6f438f4d8af.

### GS-004k completed: service APM executor and object publication lifetime

- [x] GS-004k: retained executor without queued self-cycles; logical worker values; reader/Guide lifetime, 18 new tests and real TLS corpus.

ServiceAsyncResult now retains its selected executor, and queued work receives a borrowed backend
reference rather than capturing a shared backend itself. Audit and convert all seven callers,
including profile/lookup, awards/achievement reads, social changes and leaderboard reads/pages.
Reader work returns only logical page/selector data; EndRead materializes the public reader/gamers
on the caller/update owner. Completed readers retain their original backend/title for later paging.
No GamerServices business or secure transport primitive is added to sharp-runtime. Preserve
existing error/End timeout policy here; broader measured APM/lifecycle work remains GS-007e2.
Tests: pending abandonment/replacement lifetime, all public async families, selected-executor
paging/disposal and actual TLS account/leaderboard C API corpus. Validation results below.

GS-004k ownership detail: scheduler lifetime is separate from a retained reader's logical origin;
work on a different scheduler takes only a temporary weak-origin lease. This preserves old
reader/title paging without a pending cross-backend self-cycle or use after released origin.
Dispose releases a reader's query/backend context while retained gamer snapshots live until the
reader object is released. Guide owns its pending social result so shutdown can release it even
without a final Update; callback transfers that ownership before opening the next pane.

### GS-007e2c preparation audit (not yet implemented)

The existing ENetHostHandle::CreateHost binds ENET_HOST_ANY; public relay sessions must
use a separate loopback-only host, preserving direct SystemLink unchanged. Bundled ENet
protocol.c checks maximumPacketSize before reassembly allocation, but the default host
limits are broad. Its fragment count can independently reach ENET_PROTOCOL_MAXIMUM_FRAGMENT_COUNT,
so a byte ceiling alone does not bound the per-command fragment bitmap. Add relay-only
ENet datagram preflight before UDP injection: bounded command count, exact command/payload
lengths, no unconfigured compression/checksum, bounded logical packet, waiting bytes,
fragment count and count-versus-total-byte checks. The server continues forwarding opaque
game datagrams; this client gate does not make the control protocol an ENet protocol.
Validate normal reliable/unreliable/fragmented traffic in native and separate-NAT tests.

Prepared session resources cannot hold their own queued backend strongly: ready values in
backend completion queues would form another retention cycle. Keep cancellation state
independent of the XNA callback target; rollback failed/abandoned membership on the worker,
and release ENet/relay sockets when preparation is discarded. Bound relay authority to the
backend's original validated configuration, not a newly re-resolved environment or server
supplied URL. A network failure can prevent rollback; document the existing 90-second
membership lease fallback rather than promising remote cleanup after unreachable servers.

GS-004k validation: final sequential incremental build of CnaGamerServicesTests, CnaNetTests,
cna_service_client_harness, cna_c_api_service_client and cna_c_api_net_smoke succeeded, maximum
two jobs. Existing ignored-nodiscard warnings in old GamerServices tests remain; no new compiler
error or warning in this change. Full GamerServices: **427 run / 426 pass / 1 known HEADLESS
screensaver skip, 7.840s**, including **18/18** new lifetime cases. Full Net/SystemLink: **363/363,
5.896s**. Actual TLS server E2E with native/C clients: **1/1, 85.82s** (test 85.79s), including
new callback-owner EndRead, typed leaderboard paging, multiple users/titles and restart persistence.
Private C ABI/protocol/fake gates: **13/13, 85.51s**, through the private display runner. Logs: cmake-build-debug/service-apm-lifetime-{final-build,
gamer,net,private}.log; server build/service-apm-lifetime-tls.log. Earlier overlapping build
attempts were canceled; their initial combined log is not validation evidence. The final build
and tests are from one sequential compiler invocation. Coverage generator remains 472 headers /
8,134 symbols; private reader state does not change native C ABI declarations/exports.

Sharp-runtime unchanged at 6c4a857de129cf29b5d43430bedf24157d594f12; server unchanged at
0cd1d08984210a120e7b0b7cb238f6f438f4d8af. Committed CNA next remains 967305dd7b93f992f7c177a7055bd5892ba8523e,
an ancestor already integrated. No samples-agent worktree modification. Broader service End
null/family/reentrant timeout semantics and abandoned reader-page busy state remain follow-up
audit, not a claim of full Xbox APM parity. Public online create/join/invites and standard
avatars remain unfinished. Next GS-007e2c1 followed by authority-bound owned preparation.

### GS-004k known-good integration set

- CNA 72df8cd6e6f9b63c9181686ba0b776a355948150.
- sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
- cna-gamer-services-server 0cd1d08984210a120e7b0b7cb238f6f438f4d8af.

Exact tests/times and commands are recorded above and in the existing reproduction sections.

### GS-007e2c1 completed: relay ENet allocation and endpoint boundary

Implemented private CreateRelayHost: numeric 127.0.0.1 only, ephemeral port, canonical 31-peer
ceiling, two channels, 1-MiB logical packet and 4-MiB waiting data per peer. Native only; browser
provider throws explicitly. Existing CreateHost/CreateClient and SystemLink remain unchanged.
Private relay ingress now validates uncompressed/checksum-free ENet framing before UDP injection:
32 commands maximum, exact bounded fixed/payload lengths, known flags/types, nonzero fragment
count, count <= logical byte length, 2,048 bitmap slots maximum and safe offset/length arithmetic.
2,048 accommodates the maximum packet at ENet's minimum negotiated 576-byte MTU. Unknown
compression is refused because these hosts do not configure a compressor. No dependency fork,
server/control protocol change, sharp-runtime primitive or native C ABI/XNA addition. These
are CNA resource policies, not measured Xbox packet-size limits.

New tests cover every fragment truncation, malicious bitmap/offset/length values, 10,000 seeded
mutations, minimum MTU, loopback binding/native limits, fragmentation and oversized sends. Real
CNA peers inject a hostile million-slot/one-byte fragment through the actual server; each other
peer confirms rejection before ENet allocation while normal handshake/game traffic succeeds.
Native probe also applies the same host allocation limits. Ready/failure counters remain private.
Final matching MTU-adjusted validation: **370/370 Net tests, 4.744s**, including seven new
cases and 10,000 mutations; actual native plus separate-NAT relay **2/2, 17.80s**
(native 8.60s; NAT 9.20s). Private C ABI/protocol/fake
gates **13/13, 31.16s** before the private fragment-count-only MTU adjustment; shape/exports are
unchanged (472 headers / 8,134 symbols). Final MTU build succeeded without new warnings. Initial
new enum-mixing warnings were fixed with explicit conversions before final validation.

Next: bind relay deployment authority to the exact originating backend and implement owned
preparation/cancellation/rollback; then consume it in public create/join and wire roster/leases
to Net Update. Also found a genuine ranked gap from local NetworkSessionType.xml: Ranked
forbids join-in-progress; current private directory permits enabling it. Record GS-007f1 to
fix server/fake/client validation and persisted legacy rows before claiming ranked lifecycle.

GS-007e2c1 logs: CNA cmake-build-debug/service-relay-enet-{mtu-build,mtu-net,private}.log;
server build/service-relay-enet-mtu-e2e.log. Reproduce build target CnaNetTests,
cna_service_relay_client_harness and cna_c_api_net_smoke with the standard -j2/cache settings;
run Net headless; run server service_cna_relay and service_cna_relay_nat with the harness and
external NAT helper variables recorded in GS-008d1. Native/NAT probes are private transport
acceptance, not yet standard public online NetworkSession samples. Server/runtime unchanged.

### GS-007e2c1 known-good integration set

- CNA cea4218551fc853535040e5a899ad4859fe39278.
- sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
- cna-gamer-services-server 0cd1d08984210a120e7b0b7cb238f6f438f4d8af.

### GS-007e2c2a completed: originating backend deployment authority

Add a CNA-internal configuration copy from the actual originating online backend, not a
new environment/title/override resolution. A ready action/reader may retain an older title
context while configuration/global backend changes; relay ticket and TLS endpoint/trust must
come from that same context. Fake/unconfigured providers explicitly have no relay authority.
The native probe now derives its authenticated relay authority from the signed-in backend;
unauthenticated TLS-refusal probes still use their explicit deployment fixture. Four unit
cases cover replacement, detached copy mutation, later override, fake/offline refusal and
explicit numeric-loopback development choice. No XNA/C ABI or sharp-runtime addition.
Validation: **431 GamerServices run / 430 pass / 1 known HEADLESS skip, 5.056s**, including
**4/4** authority cases; actual native plus separate-NAT relay **2/2, 22.09s** (10.14s/11.95s);
focused private header/export/protocol gates **3/3, 0.92s**; regenerated inventory gate
**1/1, 11.00s**. Build succeeded with two jobs. Inventory remains 472 public headers / 8,134
symbols; one new internal header is excluded (471 total excluded). No native C ABI change.
Logs: CNA service-backend-authority-{build,gamer,private,coverage}.log and server
build/service-backend-authority-relay.log. Owned preparation/cancellation/rollback remains GS-007e2c2.
Latest committed next advanced to 092fc7f919475857fe04df247c9e13b177f0598f at this milestone; inspect
and integrate after this verified clean commit, then rerun relevant regression gates.

### GS-007e2c2a known-good integration set

- CNA 4a4c3acfcd5cf91019bd929b7f1cb07dfaf1fb31.
- sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
- cna-gamer-services-server 0cd1d08984210a120e7b0b7cb238f6f438f4d8af.

### GS-001f completed: committed next integration

- [x] GS-001f: committed SAMPLE-104 integration; clean merge resolution and affected corpus.

Integrate committed next 092fc7f919475857fe04df247c9e13b177f0598f, SAMPLE-104. Changes:
Game defaults argument exception formatting to Framework 4 unless explicitly configured;
two Runtime regressions, compatibility docs and a source-relative Metal test glob. No
uncommitted samples-agent file was inspected/copied or modified. Merge conflicts only in
AUDIT/NEXT introductory notes; retain both samples and GamerServices records. Validate
affected Runtime plus service/Net regressions before committing this merge. Server/runtime
repositories remain unchanged. Then proceed with owned online preparation and GS-007f1
Ranked join-in-progress contract correction (documented, not yet implemented).

GS-001f validation: sequential -j2 build of Runtime/GamerServices/Net/native relay/control/C API
targets succeeded. All three full unit binaries ran via tools/platform/run_gpu_tests_private.sh
--exec (no live desktop): Runtime **192 run / 190 pass / 2 known HEADLESS capability skips,
2.452s**, including both SAMPLE-104 cases; GamerServices **431 run / 430 pass / 1 known
screensaver skip, 2.227s**; Net **370/370, 4.723s**. Focused C API/protocol gates: **5/5, 0.41s**.
Logs: cmake-build-debug/service-next104-{build,runtime,gamer,net,private}.log. Reproduce:
`tools/platform/run_gpu_tests_private.sh --exec <absolute build>/CnaRuntimeTests` (and the
other two unit executables). Do not compare this HEADLESS Runtime count directly to the samples
agent's OPENGLES3 registration; these are separate configured corpora. No new server/runtime
repository changes. Argument-profile selection is credited to the samples agent's committed work,
not a newly unblocked online GamerServices/Net sample in this project.

### GS-001f known-good committed integration set

- CNA 8bfb24a42a58c457d424b6a821c30dfccff7efa2 (contains next 092fc7f91).
- sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
- cna-gamer-services-server 0cd1d08984210a120e7b0b7cb238f6f438f4d8af.

### GS-007f1a completed: Ranked service admission semantics

Evidence: local xna4-spec/Microsoft.Xna.Framework.Net/NetworkSessionType.xml explicitly forbids
Ranked join-in-progress; NetworkSession.xml AllowJoinInProgress declares NotSupportedException.
Available Windows managed NetworkSession.cs lines 200–218 skips unchanged values and rejects
Ranked changes before SendAllowCommand. This establishes a functional service admission rule;
exact console corner-case exception order still lacks a measured probe.

Server schema migration 9 repairs legacy Ranked true flags and advances revision while keeping
members. Reject enabled Ranked create/update atomically, hide gameplay Ranked listings and
refuse new ordinary/invited groups even with legacy true flags. Existing identical membership
replay stays idempotent. Client typed response/roster authority refuses true Ranked snapshots;
client create validates before dispatch and deterministic fake mirrors the rule/atomicity.
Three new service cases and one roster case; extend server directory assertions for ordinary/
invited refusal, untouched invitation/membership, migration and restart. Canonical protocol
doc updated; envelope/CNR v1/header/golden encodings unchanged. Validation below.

- [x] GS-007f1a: server persistence/client/fake Ranked lobby-only admission, tests and integration.
- [ ] GS-007f1b: public AllowJoinInProgress validation/event propagation after public Ranked
  construction is enabled; host/dispose/unchanged-value order needs focused standard API tests.
Public online construction is still refused, so do not invent a public test factory solely to
instantiate an otherwise unreachable Ranked session. Existing public setter/migration guard
gaps and SystemLink flag propagation remain unfinished; no claims of full Ranked API parity.
Owned online preparation/cancellation/rollback remains the next main GS-007e2c2 task.

### GS-007e2c2 implemented design: owned preparation and cancellation

The result owns its originating backend, while queued work/completions own only a separate
mutex-protected state. Work borrows the executor; no state queued by a backend may strongly
own that same backend. Acquire membership, validate full local-group authority, issue a relay
ticket, allocate the bounded loopback host and wait for verified relay readiness before publishing
resources. No XNA objects are constructed on the worker. Cancellation and publication serialize
under the same lock. A running canceled job rolls back its own membership; a canceled ready
result claims the prepared resources, closes sockets and performs bounded best-effort leave
while the result still retains the backend. A consumed lease independently retains the origin
and owns subsequent release. Destruction may wait for an in-flight request or bounded leave;
there are no detached cleanup threads. An unreachable leave relies on the documented 90-second
membership lease; never claim immediate cleanup during outage. Deterministic fake dependencies
are explicit private injections; normal fake/unconfigured mode cannot obtain network authority.
This slice establishes ownership and relay readiness, not the ENet welcome/public join lifecycle.

### GS-007f1a validation and server checkpoint

Incremental CNA/server builds passed. GamerServices 434 run / 433 pass / one known HEADLESS
screensaver skip (6.231s); Net 371/371 (5.083s); private C API/protocol gates 13/13 (85.77s).
Full server configured corpus finally 11/11 (155.14s), no skipped native/NAT scenarios. Real
Ranked host update rejects true without revision mutation; auxiliary verified TLS Bob requests
prove hidden gameplay listings, ordinary/invited refusal and accepted consent preserved. Native
host verifies unchanged two-local roster and issues a fresh lobby invite after legitimate recipient
dismissal. Four-user ordinary/invited directory joins, public Find and server restart still pass.
No public online Create/Join or Ranked arbitration completeness assertion.

Initial fake playing-policy case incorrectly enabled Ranked JIP: retargeted the existing positive
case to PlayerMatch, alongside new negative Ranked cases. Initial native negative probe tried to
read a recipient-only invite as its sender: removed that unauthorized assertion, kept recipient TLS
checks. Full server first run 9/11 (228.72s), two old schema-version assertions expected 8; updated
both to 9 and the future-schema refusal to 10, then reran all 11 successfully. A pre-existing
SystemLink migration race failed once (370/371) before an isolated pass and full 371/371 confirmation;
separate GS-007g1 harness coordination task follows. No production SystemLink code changed.
Logs: cmake-build-debug/service-ranked-policy-{final-build,final-gamer,final-net,final-private}.log,
service-ranked-policy-native-fix-build.log, server build/service-ranked-policy-confirmation-e2e.log
(and earlier failure logs retained). Server commit 6d41f41; sharp-runtime unchanged at
6c4a857de129cf29b5d43430bedf24157d594f12. Latest committed CNA next now equals 8bfb24a42;
no additional unintegrated committed history or uncommitted samples-agent copying.

### GS-007f1a known-good committed integration set

- CNA 1e3cebfb277610f3bf032a29065ccf6dbd4cd8e2.
- sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
- cna-gamer-services-server 6d41f419eca43b57e939903fd25aa416e465df8f.

### GS-007g1 completed: SystemLink migration test coordination

The pre-existing test disposed its host after the host saw three gamers, before delivery of the
full roster to both survivors and installation of their migration handlers. Each survivor now sends
an actual reliable ENet application readiness acknowledgement only after both conditions hold.
The host validates distinct remote senders and exact readiness payload, and disposes after both
acknowledgements. The process test checks ROSTER_READY=2 as well as promotion, real rediscovery,
reconnection and the post-migration application round trip. No sleeps replace the handshake;
watchdog and all original assertions remain. No SystemLink production transport was modified.
Incremental target build passed; isolated three-process migration repeated 30/30 successfully
(service-migration-ready-repeat.log). Full Net working-tree confirmation 385/385, 4.687s includes
14 still-uncommitted GS-007e2c2 preparation cases; those are separately checkpointed next, not part
of this test-coordination change. Logs: service-migration-ready-build.log and
service-owned-preparation-final-net.log under cmake-build-debug.

- [x] GS-007g1: coordinate full-roster readiness before the process harness disconnects its host.

### GS-007e2c2 completed: private owned native preparation

OnlineSessionPreparation owns the originating backend; queued synchronized state never owns that
backend or public gamer/session objects. Acquire typed membership, validate exact local account
set, kind and joined session correlation; issue a bound one-use ticket and wait up to ten seconds
for bounded native ENet/WSS readiness. Owner completion publishes once even if replayed, and
consumption is one-shot including errors. Cancellation wins publication under the same lock.
Active canceled work and failed preparation close resources and best-effort leave; ready abandoned
results leave while origin is still retained. A consumed PreparedOnlineSession independently owns
origin, sockets and membership release, including joined-machine rather than host ownership.
No detached threads and no sharp-runtime business logic/primitives added. Native fake authority is
refused; test configuration/transport providers are explicit private dependency injection only.

Eighteen deterministic cases cover immediate argument/authority guards, owner completion and replay,
End-once, dropped queued/ready/completed results, backend last-owner retention/no cycles, throwing
callbacks, factory/relay failure, ten-second timeout, active worker cancellation race, joined-group
release and authentication loss. The first full run had one test fixture expecting the host to find
its own session; corrected to search as the released remote account, preserving host/group assertions.
Final Net **389/389, 14.675s**; affected targets/C API build pass with no new compiler warning.
Private C API/header/export/protocol gates **5/5, 0.84s**. Public XNA/C ABI shape is unchanged;
no inventory regeneration is necessary for a src-only private header (472/8134 remains the last
measured native API inventory). Native directory E2E **1/1, 10.85s** after final hardening:
two primary CNA processes/four users plus another title's search peer, verified HTTPS/WSS,
failed create and join rollback, real abandoned ready-host cleanup, successful create/ordinary/
invited preparation, recipient consent, expired multi-local credentials, server restart and
public BeginFind/EndFind. No ENet welcome/public Create/Join completeness claim. Independent
actual ENet and isolated-NAT relay cases were included in the preceding server **11/11** set.
Logs: cmake-build-debug/service-owned-preparation-{hardened-build,hardened-net,private}.log;
server build/service-owned-preparation-final-directory.log. Server docs/test acceptance commit
**db5434b788da98be488b8b75697d2f1ab12ac256**; runtime stays **6c4a857de129cf29b5d43430bedf24157d594f12**.

Failure limits: bounded leave can fail when offline/unauthenticated, so interrupted membership
expires via its 90-second lease. A lost create response provides no returned membership ID to
roll back; lease fallback still applies. Destruction can wait for bounded in-flight network work
and cleanup. Accepted invitation consumption is not automatically restored after a later
transport failure; reconnect/invite retry semantics need evidence and GS-007e2c3 integration.
Owner construction/cancellation/take/completion dispatch share one thread; only worker state is
synchronized. Do not wrap this helper in NetworkSessionAction::Queue capturing a strong preparation:
that recreates the queued executor cycle. The action must own preparation separately from its
queued Storage, and attach consumed resources on the owner thread. Join must not publish a
successful public APM completion until ENet welcome, exact service IDs and the authorized host
have also been established. Next main item is GS-007e2c3 plus GS-008c3 lifecycle/roster/lease wiring.

### GS-007e2c2 known-good committed integration set

- CNA 7dbfcf63e0db0524b2f431e6b2af5138f30fcaa8.
- sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
- cna-gamer-services-server db5434b788da98be488b8b75697d2f1ab12ac256.

### GS-008c3a completed: owner-bound directory snapshot/lease pump

Implement a private bounded service-control pump before wiring public session objects. It retains
origin but queued work retains only synchronized logical state; no queued executor ownership cycle
or XNA object/event on the worker. Poll authoritative snapshots at one-second intervals and renew
membership at thirty-second intervals, independent of relay traffic and authentication heartbeat.
Allow only one pending read; validate session/category/requesting machine/exact local group and
monotonic revision. Return owned logical observations only from the caller's update boundary.
Publish a safe failure once, stop scheduling until explicit retry, and suppress abandoned results.
Destructor/cancel marks state inert; the separate prepared lease owns membership/socket release.
GamerServicesDispatcher.Update must continue draining backend completion capacity. Deterministic
fake-clock cases and a native server-restart/renewal probe validate this boundary; realtime status,
public Create/Join/ENet welcome and XNA lifecycle event conversion remain GS-008c3/GS-007e2c3.

- [x] GS-008c3a: private bounded owner-update directory snapshots, lease renewal and safe retry.

GS-008c3a implementation: ServiceSessionPump retains its origin outside queued State. Only one
logical snapshot/failure can be pending; a busy request prevents more work. Worker validates full
roster, same session/category/requesting machine/exact local user group and monotonic revision;
malformed authority becomes INVALID_RESPONSE, unknown exception/error text becomes a safe fixed
failure. update consumes only owned data; no public XNA gamer/session objects or events are
created on the worker. Failure publishes once and scheduling stops until explicit retry with
immediate renewal. cancel/destruction suppresses queued/ready/active output and does not own
membership release (PreparedOnlineSession does). Backend destruction joins in-flight native work;
queued closures borrow, never strongly retain, their own executor.

Nine new deterministic cases: initial authority/owner guards, one pending request/update boundary,
roster/property/gameplay changes, fake-clock 1s reads/30s renewal, authentication failure/retry,
host closure/stopped scheduling, queued/ready cancellation, last-owner/no-cycle and queue saturation.
Final affected builds pass, full Net **398/398, 14.684s**, private C API/header/export/protocol
**5/5, 0.59s**, native TLS/WSS directory acceptance **1/1, 10.85s**. The native host forces actual
pump lease renewal after a real server restart and again after admin-expiring Alice/Charlie access
credentials; remote group has left, two local identities and secondary membership remain correct.
Fake clocks test the interval policy; the native probe does not wait thirty physical seconds.
GamerServicesDispatcher.Update drains backend completion capacity throughout. Logs:
cmake-build-debug/service-snapshot-pump-{final-build,final-net,private}.log and server
build/service-snapshot-pump-final-directory.log. No protocol/schema/public XNA/C ABI/runtime
changes. Realtime status and XNA lifecycle conversion are still the unchecked parent GS-008c3.

Committed next advanced to **92d23c84d12725c97872e3eaf249f789ff7579b6** (SAMPLE-104 browser
partial-service portability) during this slice. Reviewed committed changes: native CURL/TLS path
unchanged; Emscripten explicitly refuses account/relay transport, creates no fake connection,
uses owner-pumped unavailable work and copies host JSON headers without leaking host libc include
roots into the cross build. Browser service functionality is not newly implemented. Integrate only
this committed history after the current pump checkpoint; never copy samples-agent local changes.

### GS-008c3a known-good committed integration set

- CNA a1a811a7296c861697faf57c550309005fb7033f.
- sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12.
- cna-gamer-services-server 0b6f2fc17fe184da65dd54f0568ef6fbdd50b8d9.

### GS-001g completed: integrate committed browser portability

Merging committed next 92d23c84d only into the dedicated feature worktree. Conflicts are confined
to concurrent AUDIT/NEXT status insertions; retain both samples and service evidence. Native
TLS/account/relay implementation is unchanged by the portability guards. The new preparation
resource fixture explicitly requires native loopback ENet: exclude those eighteen native-only
cases under __EMSCRIPTEN__, whose loopback host correctly refuses construction. Existing
browser-only unsupported-transport diagnostics remain the samples agent's five cases. This
agent revalidated native service/runtime/net and matching server E2E; no new browser service
or real original Xbox-host sample acceptance is claimed. No samples worktree changes are copied.

Validation: affected runtime/service/net/native harness/C API targets build successfully. Private
native GamerServices **434 run / 433 pass / one known HEADLESS screensaver skip, 2.241s**;
Net **398/398, 14.737s**; Runtime **192 run / 190 pass / two known HEADLESS skips, 2.447s**.
The matching full server corpus is **11/11, 187.55s**, with all native C/C++ harnesses and the
rootless isolated-NAT relay case configured (no skip substituted for those E2E cases). Logs:
cmake-build-debug/service-next-browser-{build,gamer,net,runtime,private}.log and server
build/service-next-browser-e2e.log. Sharp-runtime remains 6c4a857de129cf29b5d43430bedf24157d594f12;
server remains 0b6f2fc17fe184da65dd54f0568ef6fbdd50b8d9. Current next integrated here is
92d23c84d12725c97872e3eaf249f789ff7579b6. Public online lifecycle is still unfinished.

Private C API/header/export/protocol gates for GS-001g: **5/5, 5.19s**.

### GS-004k2 completed: retained completion authority and presence queue ownership

Follow-up audit found that queued presence captured its owning backend strongly, creating a
self-retention cycle when abandoned before execution. Borrow that executor while its joined
worker/active dispatcher retains lifetime. Both service and Net End waits also need to drain
the retained scheduling authority after global backend replacement; otherwise valid pending
operations stall until timeout. Only logical completions from superseded authority are published,
never its identity events. Dispatch the full batch before rethrowing its first callback error.
Service End claims consumption before callback pumping, matching the already-tested Net guard.
Add real pending-origin, callback, stale-identity, abandonment and presence retry tests; then
validate native service/net/C API and the actual TLS directory acceptance. No wire/schema change.

GS-001g known-good committed set: CNA 0c136c9d5 (merge of committed next
92d23c84d12725c97872e3eaf249f789ff7579b6), runtime
6c4a857de129cf29b5d43430bedf24157d594f12, server
0b6f2fc17fe184da65dd54f0568ef6fbdd50b8d9. Full server 11/11 as above.

GS-004k2: fourteen new GamerServices cases cover five APM families after pending origin replacement,
reentrant End consumption, abandoned queued presence/no executor cycle, stale presence completion,
full callback-batch delivery after an exception and exclusion of active/stale identity pumping.
Two new Net cases cover retained pending search, metadata/callback thread, stale sign-out isolation,
error consumption and busy-state recovery. Real TLS directory clients now also begin public Find,
replace the global backend while pending, and successfully End using the retained origin.
Affected builds pass. Final native GamerServices **448 run / 447 pass / one known HEADLESS skip,
1.980s**, Net **400/400, 14.680s**, private C API/header/export/protocol gates **5/5, 2.69s**;
actual multiple-process TLS directory/restart/expired-credential test **1/1, 10.48s**.
C API inventory regenerated: unchanged **472 headers / 8134 symbols**; private helper excluded.
Logs: cmake-build-debug/service-retained-pump-{build,gamer,net,private}.log and server
build/service-retained-pump-directory.log. No wire/schema/runtime/server code changed. Superseded
backend identity events are intentionally discarded: they no longer have authority over the
active global sign-in list. Pending completions alone remain valid, bound to their original title.
Service End now claims consumption before waiting, including callback exceptions/timeouts;
callers own result metadata until destruction. Public online lifecycle remains GS-007e2c3/GS-008c3.

### GS-008c3b completed: private dispatcher progress subscriptions

The owned preparation/session pump needs owner-thread progress before a public NetworkSession
exists, including End waits inside other callbacks. Use sharp-runtime's existing generic
System::EventHandler token/snapshot primitives; no new runtime primitive or GamerServices policy
belongs in sharp-runtime. A CNA-private RAII subscription weakly references its state from the
registry, suppresses canceled copied snapshots and self-reentry, and permits other operations
at nested Update boundaries. Observe only after the backend's completion/identity batch. Drain
all callbacks before rethrowing the first error; nested identity events remain deferred. Cover
cancellation/self-destruction/new subscriptions/nested End progress/errors/thread ownership.
Then run actual directory preparation and renewal through this hook. This is a prerequisite
boundary, not yet public Create/Join or XNA lifecycle conversion (parent GS-008c3 remains open).

GS-004k2 known-good set: CNA 9f392f62b41399609467e10a51016f67b3f877a9,
sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12, server
0b6f2fc17fe184da65dd54f0568ef6fbdd50b8d9.

GS-008c3b audit also found an actual pump starvation path: when automatic presence is dirty and
the 128-entry service queue is full, scheduling threw before draining any work, so every Update
could repeat the same failure forever. Treat only automatic presence queue-unavailability as a
retryable dirty state; still drain the bounded batch and progress observers. User-requested Begin
queue limits remain errors. Add a saturated queue plus dirty presence test proving all 128 jobs,
progress callbacks and the eventual presence revision are delivered. No protocol limit enlarged.

GS-008c3b validation so far: twelve new behavior cases cover owner/completion ordering,
cancellation and callback lifetime, self-destruction, removal from a copied dispatch snapshot,
next-boundary registration, nested independent operation progress, deferred identities, full
error-batch dispatch and guard restoration, foreign-thread refusal, and dirty presence with a
saturated 128-entry queue. First full run used a stale dispatcher object (compiled four seconds
before the final presence catch edit), reproducing the starvation failure; rebuild the final
object before testing. Final affected build passes; native GamerServices **460 run / 459 pass /
one known HEADLESS skip, 4.534s**, Net **400/400, 15.245s**, private C API/header/export/protocol
**5/5, 0.99s**. Genuine two-process TLS/WSS directory preparation and restart/expired credential
renewal through the subscriptions **1/1, 15.78s** before the final automatic-presence correction.
Full matching server corpus is running with all native clients and isolated-NAT tools configured.
Inventory regenerated: **472 public headers / 8134 symbols**, **472 excluded headers** (one new
CNA-internal subscription header); no public XNA/C ABI expansion. Runtime/server/wire/schema unchanged.
Logs: cmake-build-debug/service-update-subscription-{build,final-build,gamer,final-gamer,net,
final-net,private}.log and server build/service-update-subscription-{directory,e2e}.log.

Final GS-008c3b matching server corpus **11/11, 172.77s**, including actual C/C++ TLS/WSS,
independent users/titles, restart, expired credentials, owned rollback, private update-hook
preparation/renewal, fragmented/unreliable ENet, malicious fragment/claim checks, revocation,
server failure and rootless isolated-NAT clients. No cases skipped. Standard public online
Create/Join, ENet-to-XNA gamer/lifecycle conversion and avatars are still unfinished. No new
original XNA sample unblocked by this prerequisite alone. Next GS-008c3c/GS-007e2c3: authenticated
realtime handshake/data authority and public session ownership/ENet welcome integration.

### GS-008c3c1 completed: service-bound realtime packet admission

Before public session/gamer mutation, add a private policy over authenticated directory snapshots
and relay-supplied machine identity. Control preflight bounds allocation; enforce host/client
direction, exact complete local groups, welcome authority, complete broadcast groups and
server-owned state/properties. Host leave broadcasts require complete known admitted remote groups; removal/end reconcile
authenticated authority rather than trusting arbitrary IDs. Application messages validate tag/header/options/channel/1MiB bound,
handshake-established source and admitted connected groups, exact sender ownership and target
membership before the single owned payload allocation. The host can relay to another admitted
machine; clients accept only host-delivered packets addressed to their local group, with senders
from admitted remote groups. This does not provide malicious-host anti-cheat, migration, lifecycle
or public Create/Join by itself. Preserve the existing SystemLink codec/backend; use the same
codec bytes behind a service-specific private admission boundary. Validate deterministic negative
cases/mutations plus the actual native TLS/WSS/NAT relay harness; never treat directory reservation
alone as a connected gamer.

GS-008c3b known-good committed set: CNA 73011288ea4d5fe2a4c4bf8bd2f61f99c4d2abe3,
sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12, server
0b6f2fc17fe184da65dd54f0568ef6fbdd50b8d9. Full matching server 11/11 as above.

GS-008c3c1 implementation: controls preflight before decoding, enforce authenticated source and
host/client direction, exact hello/welcome groups and complete join/leave groups. Host leave may
remove only complete already-admitted remote groups, never local/host/duplicate/unknown IDs.
State/properties must match the service snapshot; end is reconciled through authority/transport
closure. Game headers/options/channels/1MiB ceiling, establishment, connected admission and source/
target membership are checked before the single owned payload copy. Host relays may target other
admitted groups; clients receive host-relayed remote senders addressed to their own local group.
The host is trusted for forwarding; this does not add malicious-host anti-cheat or Xbox wire
compatibility. No public XNA/C ABI expansion, server game decoder, protocol/schema/runtime change.

Seventeen deterministic cases, including all 31 IDs and **20,000 control/application mutations**.
Initial compile failed only because the new CNA/Internal test used the deeper Microsoft namespace
relative include path; corrected to four parent segments. Final affected builds pass, full native
Net **417/417, 14.805s**, private C API/header/export/protocol **5/5, 0.65s**. Actual native and
separate rootless-NAT relay acceptance **2/2, 16.47s**, with both PlayerMatch/Ranked categories,
two titles/four identities, valid hello/welcome, rejected forged account claims, newly rejected
existing sender and unknown target IDs before delivery, malicious fragments/UDP source/size
checks, legitimate fragmented/unreliable exchange, secondary revocation and server failure.
No skip substituted. Server reporting/docs compile and preserve CNR v1/golden bytes. Logs:
cmake-build-debug/service-packet-policy-{build,final-build,final-tests-build,hardened-build,
net,final-net,private,final-private}.log; server build/service-packet-policy-{relay,final-relay}.log.
Normal public online session creation/join, reconciling later roster revisions/lease/transport
changes into XNA lifecycle, EndGame Ranked epochs and avatar migration remain unfinished.

Before public creation integration, review private-slot semantics: local NetworkSession.xml
exception range permits <=maximumGamers, while the parameter text says strictly less. Server
currently reserves at least one public slot and rejects a host group exceeding public capacity.
Do not change this based on the contradictory XML alone; managed/native evidence or a console
probe is needed for the edge, while ordinary supported configurations can be integrated now.
This uncertainty is not a blocker for all other public lifecycle work.

GS-008c3c1 tested server commit: d153226aefd15428bfccdbdf74bfae97f1f4d8a5;
sharp-runtime remains 6c4a857de129cf29b5d43430bedf24157d594f12.

### GS-008c3c2 complete: owned service ENet session engine

Consume the prepared lease into a private single-owner realtime engine. Combine authenticated
snapshot/lease pumping, stable relay routes, exact service ordinal IDs, host/client welcome,
complete remote group join/leave and source-checked local delivery/host forwarding. Produce owned
logical observations only; the subsequent public APM/session adapter will turn these into XNA
objects/events. Keep SystemLink's mature backend intact. Bound handshake/recovery by steady time,
retry welcome after stale directory metadata with bounded hello cadence, reconcile removed
membership before accepting later packets, retain origin outside queued work and release resources
on abandonment. Test two logical engines/four locals with explicit fake-directory/native-loopback
route providers, then actual multi-process TLS/WSS/NAT. Do not publish public Join completion
before the authorized ENet welcome. Parent GS-007e2c3/GS-008c3 remains unfinished until that adapter.

GS-008c3c1 known-good committed set: CNA b1ea16aeb4fd10d436fc07ce482fbc8be6d483fe,
sharp-runtime 6c4a857de129cf29b5d43430bedf24157d594f12, server
 d153226aefd15428bfccdbdf74bfae97f1f4d8a5. Net 417/417 and actual native/NAT 2/2 as above.

GS-008c3c2 implementation now consumes the lease into the owner-thread engine. Exact host/client
welcome, complete group observations, stable local IDs, source-checked game routing, local delivery,
three-machine host forwarding, revision recovery and single failure are covered. Receive/local
observations have 128-data/4MiB bounds; unacknowledged outgoing packet allocations now retain
separate counters through ENet free callbacks, capped globally at 128-data/4MiB with reserved
64-control/256KiB capacity. ACK/destruction release the counters. Existing SystemLink Send wrapper
and backend are untouched. Host identity/group changes fail deterministically rather than using a
stale upstream pointer. No public XNA/C ABI, runtime primitive, protocol byte or schema change.

Thirteen engine cases pass, including three machines/full groups, both local player slots,
fragmented reliable/unreliable data, 128-local and 4MiB local/outgoing limits, authority removal,
host failure, malformed constructor cleanup, thread ownership and exact no-welcome timeout.
Focused 13/13 in 2.083s; final native Net 430/430 in 17.647s; private native C API/header/export/
canonical protocol gates 5/5 in 1.65s. Real owned TLS/WSS/native plus rootless-NAT preliminary
2/2 in 20.43s (10.38s/10.05s); final matching server corpus **13/13, zero skips, 206.58s**. Exact test times: unit 7.31s, TLS
68.96s, directory 5.17s, invitations 9.01s, native directory 17.80s, relay protocol 0.08s, relay
authorization 6.57s, flow 0.04s, adversarial WSS 46.52s, raw native 11.76s, raw NAT 11.73s,
owned native 9.96s, owned NAT 11.59s.
Raw hostile-packet probes remain separate and retain malicious fragment/UDP source/size and
forged incoming ID proofs. Owned probes explicitly validate preparation pending, client welcome
before readiness, both local account sender/target slots, complete departure after secondary
revocation, failure once and owned cleanup. They are not public online NetworkSession acceptance.

Concurrent committed-state inspection: CNA next was advanced externally to the existing feature
checkpoint b1ea16aeb4fd10d436fc07ce482fbc8be6d483fe; it is already an ancestor here, so no merge
or shared-worktree edits are needed. Sharp-runtime is now clean at committed SAMPLE-104
007280bd1cc789f851f7f454a5041c8ce2479e13 on feature/gamer-services-collections (tracking origin).
Its dictionary enumeration / Framework diagnostics are samples work, not this task. The CNA build
consumes this sibling source directly; ArgumentException/String object timestamps are newer than
current source and the final incremental build is current. Use 007280bd in the next verified set,
rather than repeating the stale 6c4a857 plan runtime entry. No edits/commits/push to runtime here.
Current instruction hashes unchanged: CNA AGENTS b91ae2908fd327a76011e4522a9a9b718dd9814371f680b2d029c2b431e24920,
CLAUDE 6a532dae21d07f21faaade89be4a426a349da01c3e6d8ecbd335396c0596f13e; runtime AGENTS
37d7bca7a91556fa5af614fd59406af72c0187f8f6d31c4fd970ac1a3e3ab179 / CLAUDE
243e4729343347d64827db09ff7e40187b1ed226e01d4b122ee3dc7d7d722512. Server has no such files.
All builds reuse CNA cmake-build-debug and server build, at maximum two compilation jobs.

- [x] GS-008c3c2: owned private ENet/membership engine with thirteen deterministic cases and
  actual secure two-process/NAT acceptance; public parents remain unchecked.

Server acceptance commit tested with this CNA change: 8d50c8079c806485cc433251c356299d0a7fe356;
sharp-runtime 007280bd1cc789f851f7f454a5041c8ce2479e13. CNA engine revision is the thematic
GS-008c3c2 commit containing this section; record its exact hash at the next milestone.
Logs: CNA cmake-build-debug/service-{enet-session-focused,owned-enet-build,
owned-enet-hardened-build,owned-enet-hardened-focused,owned-enet-final-net,
owned-enet-private}.log; server build/service-owned-enet-{configure,e2e,full-e2e}.log.
No newly ported original XNA sample or avatar EXT migration is claimed. Endpoint credentials
remain verified TLS/WSS, never sharp-runtime plaintext HTTP. Browser service support, reconnect,
public online lifecycle, Ranked epochs, social Guide/InviteAccepted and standard avatars remain open.

GS-008c3c2 known-good exact integration set:
- CNA 6a2f6fcf04639e3a796e3b1cd8050718248f7086.
- sharp-runtime 007280bd1cc789f851f7f454a5041c8ce2479e13 (committed samples change; untouched here).
- server 8d50c8079c806485cc433251c356299d0a7fe356.

### GS-007e2c3a complete: owned online Begin/End readiness coordinator

Add a private owner-thread coordinator consuming OnlineSessionPreparation into ServiceENetSession.
A weak dispatcher progress subscription keeps work independent of End polling; worker queues must
not own the coordinator/backend. Capture logical local names/IDs, never public gamer objects in
workers. Complete once only after host resources or exact client welcome; preserve owned initial
roster observations for the subsequent public session adapter. Callback errors must not replace the
operation result; callback take/destruction/reentrancy must be safe. Unconsumed resources renew
while retained, bounded pending data/control observations prevent growth, abandonment cancels and
rolls back. Support retained-origin progress after global backend replacement, deferred logical
errors, at-most-once consume and failure before/after readiness. Verify deterministic tests and the
actual secure owned/native/NAT harness; do not mark public Create/Join complete before the adapter.

GS-007e2c3a implemented: weak owned dispatcher subscription retains logical preparation, origin
and realtime engine independently of worker work. Completion is once on the owner after host
resources/client exact welcome; initial roster and incoming data transfer on one consume. Pending
results continue service/lease progress; staged observations retain at most 128-data/4MiB and 256
total entries. Callback exceptions do not overwrite success or operation failure. Take/destruction
inside callbacks and nested updates are safe; cancellation/abandonment release only owned groups,
suppress notification, and never create an executor ownership cycle. Transport/authority loss
between readiness and End is deferred instead of handing out a dead engine. No public XNA/C ABI
or runtime/wire/schema changes; public Create/Join are still refused until their adapter lands.

Thirteen new coordinator cases cover pending/exact welcome, owner callbacks, early/repeated take,
cancellation before work/after transport readiness/during join, callback take/self-destruction,
nested and throwing callbacks, origin replacement, failure after completion, foreign thread and
160 actual incoming packets while an unconsumed result retains only 128 plus initial roster.
Final native Net **443/443, 17.595s**; private C API/header/export/protocol **5/5, 1.20s**.
Affected incremental builds pass without warnings/errors at two jobs. Initial focused engine+
coordinator 24/24 in 2.166s, followed by two further ownership/bounded-retention cases in the final
full Net run. Actual raw-native/raw-NAT/owned-native/owned-NAT **4/4, zero skips, 41.94s**
(9.82s/10.58s/10.43s/11.09s). The owned launch boundary now explicitly says pending and lets
both processes advance the coordinator before established-session consumption. Other nine server
cases were unchanged and passed in GS-008c3c2's full 13/13; do not describe this later focused
run as a fresh full 13/13. Server acceptance commit d764f4d0f378429f037d74619388518a457897d5;
runtime remains 007280bd1cc789f851f7f454a5041c8ce2479e13. Logs: CNA cmake-build-debug/
service-online-operation-{build,final-build,bounded-build,focused,net,private}.log and server
build/service-online-operation-e2e.log. No original XNA sample or avatar migration newly unblocked.

- [x] GS-007e2c3a: owned pending preparation/ENet coordinator and exact readiness/consume lifetime.
- [x] GS-007e2c3b: standard public online Create/Join result/session adapter, owned initial gamer
  projection, Update join/leave/data/failure conversion, proper primary host/machine grouping and
  standard-API real-server acceptance. Then invited joins/Guide and lifecycle/Ranked epochs.

Milestone next inspection found committed CNA DX-271 d5cf852212fb2d3a6930dfb05d95ee96be5aaa24
on next, branched from b1ea16a. It touches Windows D3D compile/IID handling, not service/Net.
After committing this coordinator, merge only that committed history into this dedicated branch,
retain both AUDIT/NEXT notes, regenerate relevant inventory and rerun native Net/private gates.
Do not modify the shared samples-agent worktree or copy uncommitted changes.

### GS-001h complete: integrate committed DX-271 next advancement

Merged only committed next d5cf852212fb2d3a6930dfb05d95ee96be5aaa24. NEXT.md had a top-note
conflict; retain GS-007e2c3a/GS-008c3c2 and DX-271 notes. All service and SystemLink source remains
unchanged by the integration. Windows D3D files are renderer-local; their consumer evidence belongs
to DX-271, not this service task. Reuse HEADLESS build, max two jobs; regenerate inventory for
one extra private D3D header and rerun native Net/C API/protocol checks before merge commit.

GS-007e2c3a exact tested integration set:
- CNA 90f8300c5150f1a744ab03ead71321e48e856128.
- sharp-runtime 007280bd1cc789f851f7f454a5041c8ce2479e13 (unchanged).
- server d764f4d0f378429f037d74619388518a457897d5.
Net 443/443, private gates 5/5, real raw/owned native/NAT 4/4 as above.

GS-001h validated: incremental HEADLESS affected build reports no work (renderer-local Windows
changes do not rebuild native service code). Final native Net **443/443 in 17.649s** and private
C API/header/export/canonical protocol **5/5 in 1.35s** pass. Inventory remains 472 public headers /
8134 symbols; excludes 473 internal headers after the new D3D header. No public binding expansion
or service drift. Logs cmake-build-debug/service-next-dx-{build,net,private,inventory}.log.
No Window/Wine/DirectX behavior tests rerun by this task; DX-271's committed evidence is separate.

- [x] GS-001h: integrate committed DX-271 next advancement, resolve only note conflict, refresh
  exclusion inventory, preserve SystemLink/online regressions.

First unfinished checklist remains GS-007e2c3b: standard online session result/projection. The
private coordinator owns readiness and logical observations; public PlayerMatch/Ranked Create/Join
still call the explicit lifecycle refusal in NetworkSession.cpp. Implement that adapter, matching
complete initial GamerJoined replay, one primary host, multiple local accounts, shared machine
views, retained authority, received packet queues and group leave/failure, before claiming any
public online sample acceptance. Follow with standard invited joins/Guide/InviteAccepted, service
StartGame/EndGame/Ranked leaderboard epochs, migration/reconnect and standard Avatar migration.

### GS-001h1 checkpoint / next-agent handoff

Last known-good source integration (native checks after controlled DX merge):
- CNA **255bd10be49eace0217a548f37a60e2b084de4e6**, feature/gamer-services-server,
  /rv/data/development/github.com/libcna/cnawork/cna-gamer-services.
- sharp-runtime **007280bd1cc789f851f7f454a5041c8ce2479e13**,
  feature/gamer-services-collections, /rv/data/development/github.com/libcna/sharp-runtime.
- server **d764f4d0f378429f037d74619388518a457897d5**, feature/gamer-services-server,
  /rv/data/development/github.com/libcna/cna-gamer-services-server.

All three were clean at inspection. Shared samples worktree /rv/data/development/github.com/libcna/cna
is on next d5cf852212fb2d3a6930dfb05d95ee96be5aaa24 and was never changed here. Server LICENCE
is tracked MIT and .gitignore is tracked; no FNA code copied into server. No pushes performed.
The following checkpoint commit changes this plan only; its native/service sources equal the
known-good source set above. Net 443/443 and private five gates pass after DX integration; secure
raw/owned native/NAT 4/4 passed at 90f8300c5 before the renderer-only merge. Earlier full server
13/13 passed at the engine checkpoint 6a2f6fcf0 / 8d50c8079 / runtime007280bd. Do not collapse
those separate runs into a claim that every later source revision reran the entire server corpus.

Continue from **GS-007e2c3b**, not from a fresh service redesign. Existing reusable prerequisites:
OnlineSessionPreparation (membership/verified WSS ownership), ServiceSessionPump (authority/lease),
ServiceGamePacketPolicy (source/full-group admission), ServiceENetSession (owned realtime and
observations), OnlineSessionOperation (dispatcher-driven readiness/lifetime). Public online
Create/Join still refuse lifecycle; they are not complete. Remaining public adapter design:
1. Freeze/validate one through four actual published local identities at Begin without sending
   public gamer pointers to workers; retain origin outside queued storage and verify lifetime at End.
2. Action owns OnlineSessionOperation, publishes asynchronous metadata/wait/callback once;
   callback must safely call End, recurse, throw or release the result.
3. End takes established engine + initial observations, constructs the standard NetworkSession,
   assigns service ordinals, one primary host and shared per-machine views. Populate initial
   remote collections before handler subscription replay, without duplicate queued joins.
4. Update converts owned connected-group/data/failure observations to existing XNA objects/events;
   Dispose closes the service owner before destroying gamers. Bound unread local packet queues.
   Keep production SystemLink backend and its tests unchanged.
5. Real standard-API two-process acceptance for both categories/titles/four users and NAT,
   then service StartGame/EndGame/Ranked epochs, invited joins/Guide/InviteAccepted, reconnect/
   migration and standard AvatarDescription/Animation/Renderer/assets/C API/sample migration.

No original XNA sample was newly unblocked by the private engine/coordinator. No avatar-specific
EXT API was migrated, removed or deprecated yet; existing working EXT rendering is retained.
Internet evidence is separate rootless outbound NATs on this host, not public Internet deployment,
load/failover or browser/platform qualification. Secure transport remains reviewed OpenSSL/libcurl;
sharp-runtime plaintext HTTP/WebSocket is not used for Internet credentials. No new failing tests,
missing dependencies or public API stubs introduced in these two milestones. Known existing runtime
boundary failures and HEADLESS skips remain documented in earlier checkpoints, not rerun/claimed
fixed here. No genuine blocker prevents GS-007e2c3b; the private-slot XML ambiguity is a narrow
edge requiring further behavioral evidence, not grounds to block ordinary public create/join.

### GS-007e2c3b design (2026-09-28, new session; verified state before coding)

Session start verification: CNA feature/gamer-services-server 3d5742e84 clean (next was
fast-forwarded to the same commit by the owner, nothing to integrate); server d764f4d clean;
sharp-runtime 007280bd clean. Instruction hashes unchanged from the GS-008c3c2 record.

Reference evidence read for this slice (local managed Windows IL, decompiled NetworkSession.cs):
BeginCreate refuses `privateGamerSlots < 0 || privateGamerSlots >= maxGamers` (line 447) — this
settles the earlier private-slot XML ambiguity in favour of the server's reserved public slot, and
CNA's current `> maxGamers` check is corrected for every session type (XNA wins over FNA/XML).
EndCreateOrJoin constructs the session, runs one Update before returning (so events raised before
the caller can subscribe are not re-raised after GamerJoined replay), and fails the End when the
session already Ended or IsHost disagrees. Gamers on one machine share one NetworkMachine instance.
StartGame/EndGame are host commands whose state change is observed at the next Update on every
machine. MaxGamers/PrivateGamerSlots setters validate against occupied public/private slots.
BeginJoin uses the local gamers of the Find that produced the AvailableNetworkSession.

Adapter design:
1. ServiceSessionPump gains host-only `publish(settings)` (latest desired settings, CAS update with
   bounded CONFLICT re-read/retry inside one worker job) and `expedite()` (coalesced immediate read,
   100 ms minimum spacing). ServiceENetSession forwards `publish`; after applying a host snapshot
   whose state/settings changed it sends the existing StateChange/SessionProperties broadcasts as
   pure hints; clients only preflight them and expedite an authenticated directory read. The
   directory remains the sole authority for state/properties/capacity.
2. NetworkSessionAction owns an OnlineSessionOperation outside its queued Storage; completion
   captures only Storage, publishes IsCompleted/wait/callback once at Dispatcher.Update with
   CompletedSynchronously=false. Begin freezes 1..4 published, online-authorized local identities.
3. End takes the established engine and initial observations, constructs the standard session,
   projects the initial roster without queued joins (GamerJoined replay covers it), assigns service
   wire IDs, one host gamer, shared per-machine NetworkMachine views and private-slot flags, then
   runs one Update like XNA and disposes on Ended/host mismatch. Errors map to
   NetworkSessionJoinException (SessionFull/SessionNotFound/SessionNotJoinable) or NetworkException.
4. OnlineSessionBinding (private, friend of NetworkSession) converts later observations at Update:
   Joined → AddRemoteGamer, Left → RemoveGamer, Data → local packet queue with sender, client
   Snapshot → state events/properties/capacity/JIP, Failed → SessionEnded (HostEndedSession,
   ClientSignedOut or Disconnected). Host StartGame/EndGame/property/capacity/JIP changes publish
   desired settings. Dispose closes the engine (ENet disconnect + membership leave) before gamers.
5. Deterministic tests: public host + private engine client, and public client + private engine
   host, over the explicit fake directory with a private process-wide fixture hook (never a public
   API). Then a public-API-only two-process harness against the real TLS/WSS server (+ NAT).
Out of this slice (recorded, not claimed): IsReady propagation (SystemLink also lacks it),
NetworkMachine.RemoveFromSession (needs a server host-kick operation), online host migration,
Ranked/PlayerMatch leaderboard epochs, invited joins/Guide.

### GS-007e2c3b complete: public PlayerMatch/Ranked NetworkSession

Implemented exactly the design recorded above. CNA `114a57ff7` (adapter/tests) plus the harness
commit that follows; server `bf4afe8` (acceptance registration/docs only, no server code change).
- [x] Public BeginCreate/EndCreate, Find, BeginJoin/EndJoin (and sync wrappers) for PlayerMatch and
  Ranked over directory + verified relay + service-bound ENet; pending results, one owner-thread
  callback, CompletedSynchronously=false, End-once, abandonment rollback, callback-driven End.
- [x] Initial roster without duplicate joins, reference-style Update inside End, host/IDs/private
  slots/shared machines, Update-time GamerJoined/GamerLeft/data/state/properties/capacity/JIP,
  SessionEnded reasons, host settings publication (CAS retry) and client hint-expedited reads.
- [x] Engine fix: early connects from a newly joined machine are kept unidentified until authority
  names their route (was: rejected, failing the join whenever it beat the host's next read).
- [x] XNA IL corrections: private slots < maxGamers for every type; MaxGamers/PrivateGamerSlots
  validation; Ranked AllowJoinInProgress NotSupported; online host-only Allow*; synthetic online
  listing ObjectDisposedException. plan_bindings_upstream row added.
- [x] Real acceptance with the public API only: two CNA processes, four Guide-signed-in accounts,
  both categories/titles, localhost and separate rootless NAT namespaces.

Validation: Net **454/454** (18.2s; 7 public online-session + 4 pump publication cases new);
GamerServices **459 pass / 1 known HEADLESS screensaver skip**; fake harness **113 checks**; private
C API/ABI/protocol gates **13/13** (C smoke expectation for synthetic listings: INVALID_STATE, the
C mapping of the reference ObjectDisposedException); C inventory regenerated 472 headers / 8,140
symbols, no new exports. Server full corpus **15/15, zero skips, 176.78s** with all five CNA probes
and the NAT helper; new `service_cna_session` 8.12s, `service_cna_session_nat` 8.53s. Logs:
CNA `cmake-build-debug/public-online-{net,private}.log`; server `build/public-session-full.log`.

Harness lesson recorded: a test process must keep calling Update while it waits at a parent
barrier (games update every frame); the first E2E run blocked in getline, stopping directory
publication and ENet keepalives, and the peer correctly observed HostEndedSession.

Reproduce (CNA, max 4 jobs):
```
CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv cmake --build cmake-build-debug --parallel 4 --target CnaNetTests CnaGamerServicesTests cna_service_client_harness cna_c_api_service_client cna_service_directory_client_harness cna_service_relay_client_harness cna_service_session_client_harness cna_c_api_net_smoke cna_c_api_leaderboards_smoke
env -u DISPLAY WAYLAND_DISPLAY= cmake-build-debug/CnaNetTests
# server repository, after cmake --build build --parallel 4:
B=<CNA>/cmake-build-debug; CNA_SERVICE_CLIENT_HARNESS=$B/cna_service_client_harness CNA_SERVICE_C_API_HARNESS=$B/cna_c_api_service_client CNA_SERVICE_DIRECTORY_CLIENT_HARNESS=$B/cna_service_directory_client_harness CNA_SERVICE_RELAY_CLIENT_HARNESS=$B/cna_service_relay_client_harness CNA_SERVICE_SESSION_CLIENT_HARNESS=$B/cna_service_session_client_harness CNA_SERVICE_SLIRP4NETNS=/tmp/cna-gamer-services-nat-tools/root/usr/bin/slirp4netns CNA_SERVICE_SLIRP_LIBRARY_PATH=/tmp/cna-gamer-services-nat-tools/root/usr/lib/x86_64-linux-gnu env -u DISPLAY WAYLAND_DISPLAY= ctest --test-dir build --output-on-failure
```
(The slirp4netns helper was unpacked to /tmp by an earlier session and is not a repository file; if
it is gone, re-unpack Debian slirp4netns 1.2.1-1.1 / libslirp 4.8.0-1+deb13u1 there, or install one.)

Open follow-ups created or confirmed by this slice (not claimed):
- GS-007h: online host migration (server ends the directory on host leave; AllowHostMigration
  is stored but inert online).
- GS-007i (done, see "GS-007i complete" below): IsReady / per-gamer state propagation (SystemLink
  lacks it too) and reference IsEveryoneReady (all gamers, non-empty).
- GS-007j: NetworkMachine.RemoveFromSession (reference validation order known; needs a server
  host-kick operation) and reference AddLocalGamer validation; online AddLocalGamer currently
  refuses NotSupportedException (directory admits one complete group per machine).
- GS-007k (done, see Priority 10): AvailableNetworkSessionCollection.Dispose should make its
  listings unjoinable (reference ObjectDisposedException); QualityOfService for service listings is
  not measured.
Next: GS-007e3 invited joins/Guide invitation flow/InviteAccepted, then Ranked/PlayerMatch
leaderboard lifecycle (GS-006d), reconnect/failure (GS-008d2), remaining Guide, avatars.

### GS-007e3 complete: Guide invitations, InviteAccepted and JoinInvited

- [x] Recipient inbox polling at the outer GamerServicesDispatcher.Update (5 s, 30 s backoff,
  bounded 16 queued/1,024 remembered), one Guide prompt per pending invitation; Accept ->
  service acceptance -> NetworkSession.InviteAccepted (reference pending-until-first-subscriber);
  Decline dismisses; closing leaves it pending; receipt never accepts.
- [x] JoinInvited (both overloads + async): Guide-accepted invitation only (reference NotInvited/
  InviteeNotSignedIn -> InvalidOperationException), invitee first, consumed once, separate
  JoinInvited End family. Works synchronously inside the InviteAccepted handler (SAMPLE-096 shape).
- [x] Guide.ShowGameInvite(player, recipients): reference list validation, gamertag prompt when
  empty, confirmation, send for the active online session; gamer card "Invite to game".
  ShowGameInvite(sessionId) -> NotSupportedException (reference: Windows Phone only).
- [x] Invitation state holds no backend ownership (identity/weak only).
- [x] Deterministic 5 cases; public two-process E2E with invitations for both categories, localhost
  and separate NAT namespaces.

Validation: Net **459/459**; GamerServices **459 pass / 1 known HEADLESS skip**; fake **113**;
private gates **13/13**; server full corpus **17/17, zero skips, 217.97s** (`build/invites-full.log`).
C inventory 472 headers / 8,141 symbols (one reviewed not-applicable friendship rule CBIND-GS-007e3).

Known-good integration set (GS-007e3):
- CNA 4279fe3e4 (feature/gamer-services-server)
- sharp-runtime 007280bd1cc789f851f7f454a5041c8ce2479e13 (unchanged)
- cna-gamer-services-server bcad5ed (feature/gamer-services-server)

Open from this slice (recorded, not claimed): Guide "Join session in progress" for a friend
(push-mode InviteAccepted, needs friend-session presence from the server); party invites
(LocalNetworkGamer.SendPartyInvites/ShowParty) — no CNA party concept yet; IsCurrentSession=true
path is implemented but not exercised (the directory refuses inviting existing members);
Guide.DelayNotifications is still a no-op (would defer the invitation prompt).
Next: GS-006d online leaderboard lifecycle (per-machine unarbitrated epochs at Playing/Lobby,
leave/dispose), then Ranked arbitration design, GS-008d2 relay reconnect, remaining Guide,
avatars, samples, final audit.

### GS-006d/GS-006e complete: online leaderboard lifecycle and Ranked arbitration

Evidence: Microsoft Learn LeaderboardWriter/WriteTrueSkill pages (licensed XDK titles only;
Indie titles got NotSupportedException): writes flush at host EndGame; nonarbitrated rows are
written by their own machine; Ranked machines write arbitrated statistics and TrueSkill for all
gamers, otherwise only the host reports TrueSkill; leaving Ranked players get "bad statistics".
- [x] GS-006d: per-machine local epochs opened at Playing on every online machine, committed at
  Lobby after WriteUnarbitratedLeaderboard; host publishes Playing/Lobby only after its own
  transition (failed EndGame stays retryable); client commit failure abandons; leave/Dispose/
  session loss offer IsLeaving writes. State is applied before GameStarted/GameEnded/SessionEnded
  (reference ProcessStateChanged order).
- [x] GS-006e: server schema 10 arbitration rounds; commit arbitration context; strict majority of
  the machines that reported each row; ranked-arbitration capability. Client: Ranked raises
  WriteArbitratedLeaderboard + WriteTrueSkill for every gamer on every machine, remote departure
  during Ranked play raises IsLeaving events and the departed gamer stays in the report, a leaving
  machine reports only its own gamers; outside Ranked only the host raises WriteTrueSkill and rows
  about remote gamers are not submitted (no storage authority).
- [x] Fix: remote gamers now carry their service identity (their writers used the offline store).
- [x] Fix: Gamer copy/move (now protected) bind a fresh LeaderboardWriter to the new object; a moved
  gamer's writer used to point at the destroyed temporary (make_unique(CreateInternal) exposed it).
- [ ] CNA computes no TrueSkill (skill boards are ordinary arbitrated boards) — documented gap.

Validation: Net **465/465**; GamerServices **459 pass / 1 known skip**; fake **113**; private gates
**13/13**; server **18/18, zero skips, 213.91s** (`build/ranked-full.log`) incl. 43 arbitration
assertions and the public two-process Ranked arbitration read-back. C inventory 8,145 symbols
(reviewed not-applicable rules for Gamer's protected copy/move members).
Server 57aeb0f; runtime 007280bd unchanged; CNA commit follows this entry.

### GS-008d2 complete: relay/directory recovery for live online sessions

- [x] RelayTransport::reconnect(ticket): a failed WSS worker is replaced with fresh one-use
  authority while the loopback route sockets (and so ENet peer addresses) are kept.
- [x] Engine recovery window 15 s: relay failure -> async ticket request on the origin executor
  (no owning capture) -> reconnect, retried each second; transient directory failures
  (SESSION_SERVICE_UNAVAILABLE, RATE_LIMITED) retried via the pump. Authority refusals
  (NOT_AUTHORIZED, UNAUTHENTICATED, NOT_FOUND) end the session immediately, as does exceeding the
  window. Relay ENet peers use 20 s/30 s timeouts so they outlast the window.
- [x] Deterministic: reconnect inside the window keeps peers/data (no new welcome/join); an
  unrecoverable relay fails exactly once after the window.
- [x] Real: live public PlayerMatch and Ranked sessions survive a server restart (same port/DB),
  localhost and separate NAT namespaces; owned-engine probes still observe revocation and final
  server loss (now after the window).
Finding recorded: after an outage ENet's packet throttle drops unreliable sends for a while; the
acceptance harness treats that one packet as best-effort after a restart and strict otherwise.

Validation: Net **466/466**; server **20/20, zero skips, 340.73s** (`build/recovery-full.log`).
Known-good set: CNA (this commit), server (restart acceptance commit), runtime 007280bd.
Open: reconnect across a *client* network change is the same path but only server restart is
measured; host migration is still absent online (GS-007h).

### GS-005c complete: remaining Guide panes (reference validation, service-backed where possible)

Reference (managed Guide.cs): ShowComposeMessage text <256 chars then Gamer.ValidateGamerList
(<=100, no null/disposed); ExecuteKernelCall panes throw ArgumentNullException/ObjectDisposed for
the gamer before service work; ShowMarketplace requires a signed-in LIVE profile with the purchase
privilege (GamerPrivilegeException); ShowGameInvite(sessionId) is Windows-Phone-only; the Windows
`player != One` check is a Windows restriction (Xbox 1..4), so CNA keeps 0..3.
- [x] Server schema 11: account messages (bounded inbox, sender rate) and prefer/avoid reviews;
  avoided hosts are not offered by sessions.find (31 assertions).
- [x] ShowComposeMessage (keyboard compose, gamertag prompt when no recipients), ShowMessages
  (inbox: next/reply/delete, marks read), ShowPlayerReview (prefer/avoid/clear), ShowPlayers
  (recently met online players -> gamer card), ShowAchievementsEXT (earned list),
  DelayNotifications (defers invitation prompts, <=120 s, active delay kept).
- [x] ShowMarketplace/ShowParty/ShowPartySessions open an explanatory Guide pane: CNA has no
  store/payment or party service (titles are fully licensed; IsTrialMode stays false).
  LocalNetworkGamer.SendPartyInvites remains a documented no-op for the same reason.
- [x] Null gamer for ShowFriendRequest/ShowGamerCard/ShowPlayerReview -> ArgumentNullException.
Validation: GamerServices 465 pass / 1 known skip (7 new pane cases); Net 466/466; fake 113;
private gates 13/13; server 21/21 (354.87s). Messages/reviews are verified by server unit tests
and the deterministic Guide panes; not yet exercised by a two-process E2E.

### GS-009 design (2026-09-28): standard XNA avatars on original CNA assets

Reference facts (Windows IL + spec XML): 71 bones with the parent table already in
AvatarRenderer.cpp; BindPose, AvatarAnimation.BoneTransforms and Draw's bones are local
transforms relative to the parent; every Draw bone must be decomposable (else
InvalidOperationException); Draw during loading shows the standard loading effect when the
constructor asked for it; IsValid is `Length == 1021 && description[0] != 0`; Height is in
meters, feet to top of head; a signed-in gamer's description is cached per player index and
Changed fires (and the cache entry drops) when that avatar changes; the renderer obtains the
device through `GamerServicesDispatcher`'s IGraphicsDeviceService.

Decisions:
- Encoding (CNA v1): byte 0 = format version (1, keeps the IL IsValid rule), bytes 1-3 "CNA",
  body type, height in mm, build (weight), catalog version, RGB colors (skin, hair, eyes, top,
  bottom, shoes, accessory), catalog item ids (hair, top, bottom, shoes, glasses, hat), zeroed
  reserved bytes, CRC-32 over bytes 0..1016 in 1017..1020. A foreign or damaged buffer keeps the
  IL IsValid answer, but reads as Height 0 / Female and renders Unavailable.
- Rig: one canonical CNA 71-slot rig per body type in exactly the XNA parent topology, Y up,
  facing +Z, feet at y = 0, meters, identity bind rotations (bind pose = local translations).
  No reduced internal skeleton exists, so no private mapping is needed.
- Draw uses each supplied bone's rotation and scale; translations come from the avatar's own
  bind pose (the root keeps the supplied translation), so preset animations authored once fit
  every height and build.
- Assets: original, procedural GLB files generated deterministically by
  tools/avatar_builder (pure Python, no Blender or third-party input): per-body-type bodies,
  face-feature atlas (13 eyes, 4 eyebrows, 13 mouths), hair/top/bottom/shoes/glasses/hat items,
  and one animation GLB holding the 30 presets (CUBICSPLINE rotations, expression keys in
  animation extras). A manifest lists every item by id, body type, SHA-256 and size.
- Distribution: the v1 base catalog is embedded in the library (offline CreateRandom works);
  the service stores user -> description, serves its catalog manifest and asset blobs by
  SHA-256; the client caches downloaded blobs by hash in the user cache directory, verifies the
  hash on every read, and substitutes the embedded default for an item it cannot resolve.
- Renderer: an invalid description is Unavailable; a valid one starts Loading, resolves and
  assembles CPU data on a worker, becomes Ready (bind pose available), uploads GPU resources on
  the owner thread at the first Draw, and renders with SkinnedEffect lit by exactly
  LightDirection/LightColor/AmbientLightColor, plus face-feature decals selected by the
  expression.
- EXT retirement happens only after demos/tests run through the standard API.

### GS-009a..d complete: encoding, original 71-slot catalog, standard AvatarAnimation/AvatarRenderer

- [x] GS-009a (3ff1b02c1): CNA v1 description encoding, real CreateRandom, Height/BodyType decode.
- [x] GS-009b: `tools/avatar_builder/generate_avatar_catalog.py` (+ `cna_avatar/` package) generates
  the original catalog deterministically: canonical 71-slot rig in XNA topology with identity
  bind rotations; toy-like bodies with jointed fingers, face-feature decals and a cheek texture;
  18 wardrobe items fitted to both bodies; a 1024x320 expression atlas (14 eyes with iris layers,
  5 eyebrows, 14 mouths); 31 presets authored with FK/two-bone IK key poses as CUBICSPLINE
  curves with expression keys. 41 files, 2.68 MB (0.8 MB compressed), embedded by
  `cmake/EmbedBinaryFiles.cmake` (CMake-only generator, ~20 s compile, 360 MB peak).
- [x] GS-009c: strict GLB reader (cgltf, embedded buffers only, exact rig/topology, bounded
  vertices/indices/images/keys), manifest parser, SHA-256-verified embedded resolution; standard
  AvatarAnimation with real lengths, 71 local transforms, keyed expressions and the reference
  Update wrap/clamp/overflow rules (modulo of ticks, position==Length kept).
- [x] GS-009d: AvatarRenderer Loading->Ready on one shared loader thread (model cache per
  description), BindPose scaled to height, Draw with rotation/scale from the supplied bones and
  the avatar's own bind offsets, decomposability check, expression decals, exact
  LightDirection/LightColor/Ambient lighting, animated loading silhouette, device from the
  dispatcher's IGraphicsDeviceService (XNA AvatarHelpers rule), state save/restore, dispose while
  loading. `CNA_EASYGL_ROOT` added so a nested worktree can build GL renderers.
- EXT bridge: the Avatar EXT demos/tests pass an explicit no-avatar description (null is now the
  reference ArgumentNullException); retirement is GS-009f.

Validation: GamerServices **489 pass / 1 known HEADLESS skip** (avatar suites 132); Net
**466/466**; EasyGL private runner `EasyGL_AvatarRenderer_*` **4/4** incl. new
`EasyGL_AvatarRenderer_Standard` (standard API only: loading effect, colors, expression-only face
change, Wave arm, background untouched); private gates **15/15** (13 recorded + CApi_AvatarsSmoke
+ GamerServices_AvatarCatalogUpToDate). Build trees: `cmake-build-debug` (HEADLESS),
`cmake-build-opengl33` (OPENGL33, system SDL3, `-DCNA_EASYGL_ROOT=<libcna>/easy-gl
-DCNA_SHARP_RUNTIME_ROOT=<libcna>/sharp-runtime -DCNA_ENABLE_DRACO=OFF`). Observed, not caused
here: `tools/platform/sdl_classify.py --check` fails on `SDL_TOUCH_MOUSEID` introduced by
committed c74569ae5 (SAMPLE-046).
Known limits: `AvatarDescription.Changed` is still never raised (value-type C++ mapping; see
GS-009e); the Blender pipeline/EXT content remains until GS-009f.

### GS-009e complete: service-backed avatar descriptions and catalog distribution

- [x] Server schema 12 (`avatars`, `avatar_catalogs`, `avatar_catalog_items`,
  `avatar_catalog_assets`), capability `avatars`: `avatars.get` (1..16 ids, hex or null),
  `avatars.set` (own avatar; v1 layout/CRC/reserved/ranges and catalog items/slots validated;
  2 s spacing), `avatars.catalog` (newest or requested manifest). Catalog files live in the
  immutable asset store and `assets.read` authorizes them for every title. Admin:
  `avatar-catalog <dir>` (strict manifest, per-file hash/size/type, versions immutable, items
  never disappear or change slot) and `avatar <user> random [female|male] | set | clear`.
- [x] Client: `IGamerServicesBackend::avatars/avatarCatalog` (online with strict response
  validation; fake fixture avatars + `setFakeAvatarCatalog`). `BeginGetFromGamer` reads a
  service-identity gamer's avatar through the executor (Update-boundary completion);
  otherwise the synchronous all-zero path. Unreachable service -> invalid description.
- [x] Newer catalogs: the loader thread asks the service for the manifest (cached per process)
  and resolves each file by hash: embedded when the library has the same name+SHA-256, else the
  backend's verified disk cache/`assets.read`, re-verified against the manifest; unresolvable
  items substitute the slot default. Face atlases cached per atlas hash.

Validation: GamerServices **496 pass / 1 known skip** (ServiceAvatarTests 7, named Service* so
`IsInitializedDefaultsFalse` keeps running first); server `service_avatars` **66 assertions**;
real E2E `service_cna_avatars`: Guide sign-in, own/lookup/absent descriptions, catalog v2 hat
fetched by hash + cached (only that file), renderer Ready with 0 substitutions, twice (first/
cached); server full corpus **23/23, zero skips, 340.19s** (`build/avatars-full.log`) with every
CNA probe and the NAT helper.
Known gap: `AvatarDescription.Changed` is not raised (documented in docs/avatars.md).

### GS-009f complete: demos on the standard API, Avatar EXT retired

- [x] Demos rewritten on the standard API only (`GraphicsDeviceManager` + `GamerServicesComponent`,
  `AvatarDescription::CreateRandom`, `AvatarAnimation`, `AvatarRenderer`, HiDef so `--screenshot`
  can read the back buffer): `demo_avatar` (presets, new avatar, body type, expression override,
  orbit), `demo_avatar_animation_gallery` (8 avatars, 31 presets paged),
  `demo_avatar_dual_compare` (female/male same preset), `demo_avatar_bone_state_boundary`
  (Unavailable/Loading/Ready/BindPose/ParentBones printed, then custom bone transforms), and
  `demo_net_avatar_sync` (description bytes sent ReliableInOrder, then position/yaw/preset).
  `demo_avatar_appearance_tint_studio`, `demo_avatar_wardrobe_hotswap`,
  `demo_avatar_multi_attach_stress` and the EXT content directory are removed (nothing in them
  is expressible through XNA). Demo link order fixed to `PRIVATE CNA CNA_GamerServices`
  (static module archives ahead of libcna.so duplicated the runtime and the GL loader; the
  untouched `cna_demo_gamer_roster_hud` aborted the same way before).
- [x] Avatar EXT surface removed: `AvatarAppearanceEXT`, preset/body-type name helpers,
  `AvatarRenderer::EnableRealRenderingEXT`/`DrawRealEXT`/`SetAppearanceEXT`/`PartTintEXT`,
  `AvatarAnimation::Set/GetRealClipNameEXT`, their tests and the three EXT GPU tests.
  Generic `SkinnedModelEXT` stays: its attach/remove test moved to
  `modules/graphics/examples/skinned_model_attach_part_integration_test.cpp`
  (`EasyGL_SkinnedModel_AttachPart`, `Vulkan_SkinnedModel_AttachPart`, OpenGL4 corpus);
  `Vulkan_AvatarRenderer_Standard` and the OpenGL4 corpus entry reuse
  `avatar_standard_render_test.cpp`.
- [x] C API 0.33.0: eleven avatar EXT routes and `CNA_AvatarAppearanceEXT` removed (3,204
  exports), `CNA_AvatarRendererInfo` keeps its size with `is_disposed` + reserved bytes; abi
  baseline, bool contract, compatibility matrix, coverage (rules re-approved), limitations,
  release gate, ABI_VERSIONING and GAMER_SERVICES regenerated/updated. `GuideSmoke` expectations
  corrected to GS-005c behavior (panes needing a service player are NOT_SUPPORTED without one,
  marketplace INVALID_STATE).
- [x] Docs/tools: `docs/avatar-real-rendering-ext.md` removed; generic `docs/skinned-model-ext.md`
  added; `docs/avatar-demos.md` rewritten; coverage/model-pipeline/README references updated;
  THIRD_PARTY_NOTICES' MakeHuman/Mixamo section replaced by the original-catalog statement;
  Blender pipeline scripts removed from `tools/avatar_builder/` (README now documents only the
  catalog generator); `tools/avatar_asset_pipeline/` kept as a generic glTF -> SkinnedModelEXT
  converter without MakeHuman/Mixamo instructions.

Validation: GamerServices **463 pass / 1 known skip**; Net **466/466**
(`ENetDiscoveryServiceTest.*` and one host-promotion test fail only when ctest runs them
concurrently with other discovery users on UDP 61190; 9/9 serially); C API 110 ctests with
three environment failures that predate this work (`CApi_AudioSmoke`/`AudioUnavailableSmoke`
expect the NULL audio platform, `CApi_ContentSmoke` the content corpus); all C API gates pass;
EasyGL private runner `EasyGL_AvatarRenderer_Standard` + `EasyGL_SkinnedModel_AttachPart` 2/2;
new `cmake-build-vulkan` (only these targets built, 89% ccache hits) `Vulkan_AvatarRenderer_Standard`
+ `Vulkan_SkinnedModel_AttachPart` 2/2 on the private runner; OpenGL4 parity corpus regenerated;
the four standard demos ran `--smoke` with screenshots inspected.
Open: `demo_net_avatar_sync` aborts without a signed-in gamer because GS-004 removed fabricated
profiles and no offline local profile exists yet -> GS-004l (offline local profiles).
The stale avatar-EXT comments in `VertexPositionNormalTextureSkinned.hpp`, `SkinnedModelEXT.hpp`
(widely included) and `ContentManager.cpp` were fixed together with GS-001i so one rebuild covered
both.

### GS-001i complete: integrate committed next (FULLSCREEN-001/002)

Merged committed `next` 9bbba48d8 (the GS-007e2c3b commits were already on this branch; new:
FULLSCREEN-001/002 renderer presentation/recovery fixes). Those commits change
`GraphicsDevice.hpp` without refreshing the C API coverage inventory, so `CApiCoverageMatrix`
failed after the merge; `docs/c-api/COVERAGE.md` is regenerated here (hash only, 8,126 symbols
unchanged). Post-merge debug rebuild (95% ccache misses from the changed renderer headers):
GamerServices 463/1 skip, Net 466/466, SkinnedModel/skinned-vertex/AnimationPlayer/containment
94/94, C API gates pass (the three environment smoke failures above remain).
Known-good set: CNA feature/gamer-services-server (this checkpoint), server bbfe2d5,
sharp-runtime 007280bd.

### GS-004l complete: offline local profiles (SystemLink without a service)

GS-004 removed the fabricated Stub gamers, which left every SystemLink demo (and any offline XNA
game) without a signed-in gamer. Reference facts (Windows IL, `GamerServicesDispatcher.Update` /
`SignedInGamer.HandlePlayerSignInChanged`): gamers signed in before the game starts become
`SignedInGamers` entries, raising `SignedIn`, at the first `Update`; a local profile is a gamer with
`IsSignedInToLive` false.

- [x] `LocalProfiles` store (`CNA_GAMER_SERVICES_PROFILES_DIR`, else XDG data / `~/.local/share`,
  else `%LOCALAPPDATA%`): version-1 JSON, at most 32 profiles, 256 KiB bound, names 1-15 ASCII
  letters/digits/single spaces starting with a letter, unique ignoring case; each profile keeps a
  random CNA v1 avatar. Atomic writes, a `flock` between processes (the directory is created
  before the lock -- the concurrency test found the first writers racing without it), an
  unreadable store is never overwritten, malformed entries are skipped, a lost avatar is replaced.
- [x] `IGamerServicesBackend::signInLocal` + `BackendEvent::signedInToLive`; the dispatcher builds
  local gamers with `IsSignedInToLive` false, no online-session or purchase privilege, and skips
  presence publication for gamers without a service identity.
- [x] `Guide.ShowSignIn(n, false)` without a service: per pane a keyboard prompt listing stored
  profiles (first unused suggested), creating new ones; invalid or already-signed-in names end
  with a message; cancel stops. `ShowSignIn(n, true)` without a service throws
  GamerServicesNotAvailableException. With a service configured, accounts only (unchanged).
- [x] Automatic sign-in at `Dispatcher.Initialize` without a service: `CNA_GAMER_SERVICES_AUTO_SIGN_IN`
  (up to four names, created when missing; invalid/duplicate/fifth -> INVALID_CONFIGURATION) or
  stored `"autoSignIn": true` profiles, delivered at the first Update.
- [x] `AvatarDescription.BeginGetFromGamer` for a signed-in local profile returns its stored
  avatar synchronously.
- [x] SystemLink demos follow the XNA pattern: `GamerServicesComponent` + `GraphicsDeviceManager`
  (so the Guide prompt draws), `Guide.ShowSignIn` from Update while nobody is signed in, session
  started once a gamer exists; console demos wait for automatic sign-in and explain otherwise.
  `demo_net_avatar_sync` shows each profile's own avatar; its last smoke frame now waits for the
  screenshot Draw (fixed-timestep catch-up skipped it). C API `cna_guide_show_sign_in` docs and
  `GuideSmoke` updated (local sign-in opens and cancels; online-only NOT_SUPPORTED).

Validation: GamerServices **477 pass / 1 known skip** (LocalProfileTests 14: store rules, reuse,
unreadable store, malformed entries, env/flag auto sign-in, six concurrent processes, Guide create/
offer/second pane/invalid/duplicate/cancel/online-only, profile avatar, and startup sign-in in a
re-executed fresh process); Net **466/466**; C API ctests pass except the three pre-existing
environment smokes. Private runner, OPENGL33: all seven networking demos with
`CNA_GAMER_SERVICES_AUTO_SIGN_IN` exit 0 -- avatar sync (haveRemote=true both sides, screenshots
inspected: both profile avatars in both processes, persisted across runs), client/server arena,
simulated conditions, roster HUD, session browser, QoS probe pair, session lifecycle; the QoS probe
without a signed-in gamer prints the guidance and exits 1.
Not changed: test runs read the user's profile store like they read the user's service settings;
a developer who marks profiles `autoSignIn` should point `CNA_GAMER_SERVICES_PROFILES_DIR`
elsewhere for test runs. Guest sign-in remains unimplemented.

### GS-005d complete: system Guide (Home / Guide button) and keyboard/controller message boxes

Found while porting SAMPLE-096 Invites: the unchanged game never calls `Guide.ShowGameInvite`; on
Xbox (Guide button) and Games for Windows LIVE (Home) the player opens the system Guide to invite.
CNA had no such entry point, and Guide message boxes accepted only mouse clicks, so keyboard and
controller players could not answer the invitation prompt at all.

- [x] `openSystemGuide(player)` / `pollSystemGuideButton()` (outer `Dispatcher.Update`, only when the
  Guide draws in a game): nobody signed in -> Sign in (1, 2 or 4 panes covering the player);
  account -> Friends / Invite to game / Messages / Sign out; local profile -> Sign out. Failures
  (e.g. no online session to invite to) show as a Guide message. `CNA_GAMER_SERVICES_GUIDE_BUTTON=0`
  disables the shortcut.
- [x] Message boxes: arrows/Tab, D-pad or left stick move the focus; Enter/Space/A choose;
  Escape/B/Back cancel (no button). Navigation edges start as held, so the key that opened a box
  cannot answer it. The box widens to fit its button row (four buttons overflowed).
- [x] `docs/c-api/COVERAGE.md` hash refreshed: GS-004l added a tracked internal header, which the
  inventory counts only once committed (the gate therefore passed before that commit).

Validation: GamerServices **483 pass / 1 known skip** (SystemGuideTests 5; keyboard message-box
navigation through a canned platform keyboard); Net 466/466; C API gates pass (three environment
smokes as before). SAMPLE-096 acceptance below exercises the real Home key and message boxes.

### Priority 9: formerly blocked XNA samples (complete)

Ports live on cna-samples branch `feature/gamer-services-samples` (worktree
`/rv/data/development/github.com/libcna/cna-samples-gamer-services`, from committed `develop`
053ab42; not merged), built in its persistent `cmake-build-release` (OPENGLES3, Release, pointed at
this CNA worktree through `CNA_SAMPLES_CNA_ROOT`). Acceptance scripts/evidence go in new files under
the retained audit roots; nothing existing there is modified.

- [x] SAMPLE-096 Invites (cna-samples 3fb4799): the PeerToPeer port plus the upstream differences
  (PlayerMatch, `InviteAccepted` -> `JoinInvited`). `scripts/capture-cna-invites-gs.py`: own
  verified-TLS service, two accounts, two Release processes on private Xvfb displays; Guide sign-in
  via the unchanged `ShowSignIn(4, false)`, PlayerMatch create, Home -> "Invite to game" -> gamertag
  -> send, guest Guide invitation -> Accept -> sample handler `JoinInvited`, both tanks and labels on
  both screens, guest driving reaches the host, both exit 0 (`evidence/gs-invites-20260928/`). The
  port found GS-005d (no system Guide; mouse-only message boxes).
- [x] SAMPLE-087 AvatarShadows (cna-samples 03d9bfe): four source units line by line; content built
  for Windows/HiDef from the unchanged sources with the official pipeline
  (`scripts/build-windows-hidef-content.sh`; upstream is Xbox 360-only). 16 random standard-API
  avatars with `CreateShadow` shadows via the compiled `GroundEffect.fx` and a `DepthFormat.None`
  target (`evidence/gs-avatarshadows-20260928/`).
- [x] SAMPLE-075 NetworkStateManagement (cna-samples 844b717): all 26 compile units, the official
  Windows/Reach XNBs. `scripts/probe-cna-ngsm-single.sh` (Single Player through the loading worker)
  and `scripts/capture-cna-ngsm-gs.py --mode systemlink|live` (evidence `evidence/gs-ngsm-20260928/`):
  Guide sign-in (local profile / account), create/find/join, both ready -> host lobby starts ->
  gameplay with two players -> host "Return to Lobby" (nobody ready) -> guest leaves, host told;
  LIVE adds the system-Guide invitation accepted through the sample's `InviteAccepted` ->
  `BeginJoinInvited`; both processes exit 0 in every mode. The port found GS-007i (readiness) and
  GS-007l (gamer order); Invites was rerun after GS-007l (`evidence/gs-invites-20260929-order/`).
- [x] Minimal XNA-shaped achievements/leaderboards compatibility sample (cna-samples 378483b,
  `samples/AchievementsLeaderboards/`): no original exists, so a C# reference in the collection's
  style (`reference/`) compiled with warnings as errors against the shipped XNA 4.0 Windows
  assemblies (`/rv/tmp/samples/GS-AchievementsLeaderboards/scripts/build-reference.sh`), and its
  line-by-line C++ port. `scripts/capture-cna-achievements-leaderboards.py` (evidence
  `evidence/gs-achievements-leaderboards-20260929/`): own TLS service, three achievements with
  original PNG pictures (one secret), a BestScoreLifeTime board with an int32 column and nine seeded
  rows, two accounts; three sequential processes: Guide sign-in, PlayerMatch rounds written in
  `WriteUnarbitratedLeaderboard`, awards, pictures, best-row aggregation, page two via
  `BeginPageDown`/`BeginPageUp`, persistence into a new process; all exit 0. Found GS-004m and GS-005e.

Net fidelity item found by SAMPLE-096, fixed as GS-007l (below): on a joining machine CNA's
`AllGamers` held the local gamer before the existing ones, so `GamerJoined` (replayed at subscription)
gave the joiner index 0 and SAMPLE-075's lobby listed the joiner above the host.

Known-good cross-repo set: CNA 45c17d1d8 (+ this plan commit), cna-samples feature/gamer-services-samples
378483b, server bbfe2d5, sharp-runtime 007280bd. (Previous: CNA 0a078200d / cna-samples 844b717;
CNA 45c23451b / cna-samples 03d9bfe.)

### GS-007i complete: lobby readiness on every transport

Found by the SAMPLE-075 port: its lobby starts the game when `IsEveryoneReady`, and "Return to Lobby"
went straight back into gameplay because readiness never crossed machines and never cleared.
Reference (IL): the `IsReady` setter refuses a gamer that has left, a remote gamer, and a session
outside `Lobby` (InvalidOperationException), and an unchanged value sends nothing; `IsEveryoneReady`
is false for an empty session and otherwise asks every gamer; `ResetReady` is host-only in `Lobby`;
every gamer's readiness is cleared when the game ends.

- Wire: `GamerReadyBroadcast` (0x08) carries 1..31 (id, ready) pairs; bounded by
  `validateServiceControlPacket`.
- SystemLink: a client reports its own gamers to the host; the host accepts only the reporting peer's
  own ids, applies and relays; clients accept reports only from the host; a joining machine receives
  the ready gamers after its welcome.
- Online: the same rules through `ServiceGamePacketPolicy` (a host accepts only the source machine's
  gamers, a client only the host); the engine replays readiness after every welcome, including a
  client's re-hello after a directory revision, so a report dropped while that client's directory view
  lagged behind a new gamer still converges. The recipient's own gamers are never echoed back, so a
  change in flight is not undone. The XNA binding applies reports only in its own `Lobby` state; the
  engine does not gate on the directory's state (a host reaches `Lobby` at `EndGame` before the
  directory records it).
- The transport's setter is private (`NetworkSession::ApplyGamerReadyInternal`, reached through
  `ENetBackend`/`OnlineSessionBinding`); no public C++ symbol was added. C API:
  `cna_network_gamer_set_is_ready` documents `CNA_RESULT_INVALID_STATE`; `NetSmoke` asserts it for a
  detached gamer.

Tests: session/gamer reference checks, three ENet tests (host<->client, late joiner, client authority),
`ServiceENetSessionTest.LobbyReadinessIsRelayedOwnedAndReplayedToALateJoiner` (three machines, NOT_AUTHORIZED,
replay, no echo) and `OnlineNetworkSessionTest.LobbyReadinessCrossesTheServiceSessionAndClearsWhenTheGameEnds`
(XNA API over the service session, EndGame clear, host ResetReady). CnaNetTests 472/472,
CnaGamerServicesTests 483 + 1 skip, C API gates pass (the three environment smokes
ContentSmoke/AudioSmoke/AudioUnavailableSmoke fail as before).

### GS-007l complete: canonical gamer order on every machine

Reference `GamerCollection.Insert` binary-searches by the gamer's session index, so `AllGamers`,
`LocalGamers`, `RemoteGamers` (and the `GamerJoined` replay that walks `AllGamers`) are in the same
order everywhere, host first; the kernel may insert in the middle (every index at or above the new
one is incremented). CNA's cross-machine `Id` plays that index: both transports assign it (online the
directory ordinal + 1, lowest free; SystemLink the host's free-list), so `NetworkSession` keeps each
collection sorted by `Id` (stable; nothing is re-sorted when already in order) after a remote gamer is
added, after a joiner's welcome assigns its own ids, and after a SystemLink host first numbers its
locals. Seen in SAMPLE-075 (the joiner's lobby now lists the host first) and SAMPLE-096 (tank start
positions). Tests: `ENetBackendTest.ClientSendsClientHelloAndProcessesServerWelcome` and
`OnlineNetworkSessionTest.PublicJoinUsesTheFindGroupAndFollowsDirectoryAuthority` assert host-first
`AllGamers` and replay order. CnaNetTests 472/472, CnaGamerServicesTests 483 + 1 skip, C API gates
unchanged (inventory current).

### GS-004m complete: `SignedInGamers[PlayerIndex]` finds the player

Found while porting the achievements/leaderboards sample (`Gamer.SignedInGamers[PlayerIndex.One]`).
XNA IL `SignedInGamerCollection.get_Item(PlayerIndex)` walks the collection and returns the gamer
whose `PlayerIndex` matches, else null; CNA indexed by the enum's ordinal (the FNA-era port), so
with only player two signed in, `[PlayerIndex.One]` named player two. Fixed at the one operator, which
`cna_gamer_get_signed_in_gamer_at_player_index` also uses; the C header/doc text that described the
positional lookup as canonical was corrected, and the divergence has its row in
`plans/plan_bindings_upstream.md`. The dispatcher already publishes gamers in slot order. Tests:
`SignedInGamerCollectionTest.PlayerIndexOperatorFindsThePlayerNotThePosition`, `GamersSmoke` step 10-12
rewritten. CnaGamerServicesTests 484 + 1 skip; C API gates unchanged apart from the three environment
smokes.

### GS-005e complete: `Achievement.EarnedDateTime` is local time

Seen in the achievements/leaderboards sample: an achievement earned at 00:40 local time showed the
previous day. The service records UTC ticks and CNA wrapped them as an unspecified-kind `DateTime`;
the offline store records `DateTime.Now`. Windows XNA throws for this pro-feature property, so the IL
does not settle the zone; the Xbox achievement time is a UTC `FILETIME` that .NET's `FromFileTime`
presents as local, which is what a player reads. The service path now converts with
`ToLocalTime(TimeZone::CurrentTimeZone())` (kind `Local`); gamer-services links the sharp-runtime
`TimeZone` component. Test: `ServiceReadLifetimeTest.EarnedDateTimeIsTheServiceTimeInLocalTime`;
the C struct field documents local ticks. CnaGamerServicesTests 485 + 1 skip.

### Priority 10: final GamerServices/Net behavioral audit (in progress)

- [x] GS-005f `Guide.BeginShowMessageBox` argument rules. Reference `ValidateShowMessageBoxArgs`:
  title and text non-empty and under 256 characters (UTF-16 units), one to three buttons each
  non-empty and under 256, focus within them (`ArgumentException`/`ArgumentOutOfRangeException`),
  then the kernel's refusal while the Guide is visible (`GuideAlreadyVisibleException`, previously
  `InvalidOperationException`); the overload without a player is player one. The Windows-only
  "player must be One" rule is not taken (Xbox shows a box for any player). The Guide's own panes
  (four-entry menus, long friend/message lists) go through the internal
  `CNA::Internal::GamerServices::showGuideMessageBox`, which the public rules do not bind. C header
  documents the same results. Test `GuideTest.BeginShowMessageBoxValidatesArgumentsLikeTheReference`;
  CnaGamerServicesTests 486 + 1 skip, C API gates unchanged.
- [x] GS-007k disposed search results cannot be joined. Reference `BeginJoin` throws
  `ObjectDisposedException` when the listing's parent collection is disposed; CNA's `Dispose` only
  set a flag. Each listing now shares its collection's disposal token (copies included), and
  `Join`/`BeginJoin` check it right after the null check. Listings a binding constructs directly have
  no collection and stay joinable (a CNA extension the C API and SystemLink tests use; XNA has no
  public constructor). Test `AvailableNetworkSessionCollectionTest.DisposingTheCollectionMakesItsListingsUnjoinable`;
  CnaNetTests 473/473.
- [x] GS-005g `Guide.IsVisible` getter. Reference: it throws `InvalidOperationException` before
  gamer services are initialized, and is true while any Guide screen is up. CNA answered false
  instead of throwing. The public getter now checks initialization; the Guide's own checks (message
  box/pane refusals, touch suppression, the overlay's modal flag, invitation prompts) use the
  internal `guideIsVisible()` (the reference's `IsVisibleNoThrow`), so a message box still opens
  without initialized gamer services where it did before. Tests that relied on another test having
  initialized the dispatcher were made self-contained; `SystemGuideTest.IsVisibleIsThePublicViewOfTheGuidePanes`
  and the fresh-process `GuideVisibilityTest.IsVisibleRequiresInitializedGamerServices`;
  `cna_guide_get_is_visible` reports `CNA_RESULT_INVALID_STATE` before initialization (GuideSmoke).
  CnaGamerServicesTests 488 + 1 skip. The reference setter is internal; CNA's public no-op setter
  is left for the ABI removal batch.
- [x] GS-007j (first half) `NetworkMachine.RemoveFromSession`, which threw `NotImplementedException`
  (the final audit's OPEN list). Reference checks in order: no gamers -> `ObjectDisposedException`,
  gamer left / local machine / caller not host -> `InvalidOperationException`; the method is `const`
  so `gamer->getMachineProperty().RemoveFromSession()` compiles. SystemLink: the host removes the
  peer's gamers at once (GamerLeave to the others) and disconnects it with ENet data
  `DisconnectRemovedByHost`; that client ends with `RemovedByHost` and does not attempt migration.
  The host now groups each connecting peer's gamers into one `NetworkMachine` (they had none).
  Online: server `sessions.remove` (capability `session-removal`, schema 13 `directory_removals`),
  host-only, never its own machine; removed users get `REMOVED_BY_HOST` from get/touch/leave and
  relay-ticket issuance. The session pump queues removals (a machine that already left is read
  over); the host disconnects a machine it removed with the same ENet code, so the removed client
  ends with `RemovedByHost` whichever it hears first. C: `cna_network_machine_remove_from_session`
  documented and `NetSmoke` updated; coverage rule re-approved for the `const` signature.
  Tests: `NetworkMachineTest.RemoveFromSessionFollowsTheReferenceChecks`, two ENet tests, the
  engine test `TheHostRemovesAMachineWhichFailsWithRemovedByHost`, two XNA-level online tests,
  directory fake/typed-client tests, server DirectoryTests (130 assertions), server f23f7c5 (full suite 23/23). Still open in GS-007j:
  online `AddLocalGamer`, and SystemLink clients do not learn the host's machine grouping (their
  remote gamers have no shared `Machine`).
- [x] GS-004n signed-in gamer lifecycle and local-gamer validation (audit OPEN 1, 2, 4 and the
  reference half of GS-007j's AddLocalGamer). Reference `HandlePlayerSignInChanged` disposes the old
  gamer before `SignedOut`; CNA instead cleared `IsSignedInToLive`. It now disposes it and leaves the
  flag. `SignedIn`'s add accessor replays every gamer already signed in (sender null), as XNA's does.
  `IsFriend`/`GetFriends` follow the reference order: disposed, then not signed in to an online
  account (`GamerPrivilegeException`), then a null (`ArgumentNullException`) or disposed argument.
  `BeginGetProfile` refuses a disposed gamer. `AddLocalGamer` checks null, disposed gamer, disposed
  session, already in the session, Playing without join-in-progress, Ended, and no open public slot.
  C: `cna_network_session_add_local_gamer` now takes a real `CNA_SignedInGamerHandle` (it refused
  every handle, "no C representation yet"); `CNA_INVALID_HANDLE` is the null refusal. Tests:
  `SystemGuideTest.SignedInReplaysOnSubscribeAndSigningOutDisposesTheGamer`, `SignedInGamerTest`
  friend checks, `NetworkSessionTest.AddLocalGamerFollowsTheReferenceChecks`, `NetSmoke`.
  CnaGamerServicesTests 490 + 1 skip, CnaNetTests 479/479.
- [x] GS-005h Guide keyboard input, trial mode and notification position (audit OPEN 10-12).
  `BeginShowKeyboardInput` follows the reference: title, description and default text each under 256
  UTF-16 units (`ArgumentException`), a defined player (the Windows "One only" rule not taken), then
  `GuideAlreadyVisibleException` while the Guide is visible (was `InvalidOperationException` only for
  a pending keyboard). The Guide's own sign-in/compose/gamer-card/invite/recent-players panes open
  through the internal `showGuideKeyboardInput`. `IsTrialMode` reports true while
  `SimulateTrialMode` is set (it was stored and ignored); `NotificationPosition` defaults to the
  reference `BottomCenter` (CNA draws no toasts, so it is stored only). C doc follows. Tests:
  `GuideTest.BeginShowKeyboardInputValidatesArgumentsLikeTheReference` and updated pending/position/
  trial tests; CnaGamerServicesTests 491 + 1 skip.
- [x] GS-007m Net reference behaviour (audit OPEN 3, 16, 17's validation, QoS and SendPartyInvites
  notes). `GameStarted`'s add accessor tells a handler added while Playing at once. `ReceiveData`:
  the offset is checked first (an empty array is refused), a packet that does not fit is refused
  and stays queued (it used to be consumed and then refused), and both overloads return the packet
  size; the `PacketReader` overload sizes the reader to the packet (it returned 0 and never
  truncated the reader). One divergence kept: with nothing queued the reference then reads through a
  reader that has never held data and throws; CNA's reader has no separate capacity, so it returns 0.
  A sender that already left is found among `PreviousGamers`. `EnableSendVoice` runs the reference
  checks (CNA carries no voice); `SendPartyInvites` refuses a profile alone in its party, which with
  no party service is every profile. The unmeasured `QualityOfService` (service listings) reports
  `IsAvailable` false, as the reference's internal constructor leaves it. C docs, `NET.md`,
  `NetSmoke` and the coverage rule follow. Tests: `NetworkSessionTest.GameStartedReplaysForAHandlerAddedWhilePlaying`,
  `LocalNetworkGamerTest.ReceiveDataReturnsThePacketSizeAndRefusesWithoutDequeuing`, voice/party
  checks, QoS defaults; CnaNetTests 481/481.
- [x] GS-004o offline async results and `PropertyDictionary.CopyTo` (audit OPEN 6, 15). An offline
  `BeginGetProfile`/`BeginAwardAchievement`/`BeginGetAchievements` completes inside Begin (the local
  store has no deferred work) and now says so through `CompletedSynchronously` (it reported false).
  `PropertyDictionary.CopyTo` copied nothing and threw `NotImplementedException`; it now copies every
  pair from the index with the .NET collection checks. (`Add`/`Remove`/`Clear` stay working mutators,
  the documented Task 8.1 choice; the reference's explicit interface members refuse them.)
- [x] GS-007n SystemLink session settings, machine grouping and Find filters (audit OPEN 20, 21 and
  the grouping gap GS-007j left). A joiner reported MaxGamers 31, PrivateGamerSlots 4 and its own
  AllowJoinInProgress/AllowHostMigration; its remote gamers had no shared `Machine`. Two SystemLink-only
  control messages (`SessionSettingsBroadcast` 0x09, `MachineRosterBroadcast` 0x0A; the online path
  takes both from the directory and its validator refuses them): the host sends its settings with
  each welcome and republishes settings and its wire-id -> machine map whenever they change; clients
  apply them (remote gamers regroup into shared machines). `AllowHostMigration`/`AllowJoinInProgress`
  are host-only on SystemLink too (reference `SendAllowCommand`), which is what makes a host's
  migration choice reach its clients: the two-process migration harness now sets it on the host, and
  the roster demo moved the call from its client to its host. `Find` keeps only sessions whose
  non-null search properties match and whose public slots fit the searchers
  (`ENetDiscoveryService::Matching`). Tests: `HostSendsItsSettingsAndMachineGroupingAndRepublishesChanges`,
  `AClientReportsTheHostsSettingsAndMachines`, `MatchingKeepsSessionsWithTheSearchedPropertiesAndRoom`,
  two-process migration; CnaNetTests 484/484. Open: SystemLink `AddLocalGamer` after a client joined
  is not announced to the other machines (wire ids are assigned only during a ClientHello).
- [x] GS-007o SystemLink `AddLocalGamer` after the session has other machines (the gap GS-007n left
  open). Before, a gamer added mid-session got only a local placeholder id; the other machines never
  heard of it, and its sends waited until an unrelated ClientHello happened to number it. Now a
  host numbers its added gamer at once and broadcasts the join; later joiners find it in their
  welcome. A client sends `AddLocalGamerRequest` (0x0B, SystemLink only; the online validator
  refuses it) once welcomed, including for a gamer added while it was still joining. The host
  admits the gamer onto the requesting peer's machine and broadcasts the join to every peer; the
  requester binds the returned id to its own gamer by gamertag rather than adding a remote copy.
  Sends made before then stay held and are delivered in order. A migration clears outstanding
  requests, because the next hello names every local gamer. Tests:
  `HostLocalGamerAddedMidSessionIsAnnouncedAndSendsAtOnce`,
  `ClientLocalGamerIsNumberedByTheHostAndItsEarlierSendsArriveInOrder`,
  `ClientLocalGamerAddedWhileJoiningIsRequestedOnceWelcomed`,
  `HostAdmitsAClientsAddedGamerOntoThatClientsMachine`, and the two-process
  `ClientAddLocalGamerJoinsTheClientsMachineAcrossRealProcesses` (15 repeats clean). Three older
  tests that relied on the gap (queue-until-a-second-hello, its ordering twin and the
  host-disconnect purge) became unreachable and were replaced; the host-side purge stays as
  defensive code. CnaNetTests 486/486, CApi_NetSmoke, protocol drift check clean. Online
  `AddLocalGamer` is still refused (the directory admits one local group per machine).
- [x] GS-007p Explicit local-gamer lists and the Find -> Join local limit (XNA over FNA; row in
  `plans/plan_bindings_upstream.md`). The explicit-list `Create`/`JoinInvited` constructor sized the
  local-gamer limit to the list, so a session created for one gamer refused `AddLocalGamer`; a gamer
  listed twice joined twice. `BeginCreate`/`BeginFind`/`BeginJoinInvited` with a list skipped
  reference `GetLocalGamers`. A SystemLink `Join` always allowed 4 local gamers, and ignored the list
  a `Find` was given. Now explicit lists are checked first (null entry or empty list
  `ArgumentException`; disposed gamer `ObjectDisposedException`, offline only, because an online
  request dereferences a gamer only after finding it among the signed-in gamers). Such sessions have
  the reference limit of 4 and fold duplicates. A SystemLink listing carries its search's limit, or
  its list, into `Join`. Tests: `AddLocalGamerThrowsAtMaxLimit` (rewritten to the limit of 4),
  `ExplicitLocalGamerListsAreCheckedFirst`, and two-process
  `JoinKeepsTheLocalGamerLimitOfTheFindAcrossRealProcesses` /
  `JoinUsesTheGamersTheFindWasGivenAcrossRealProcesses` over real LAN discovery; the client joins
  the listing on its host's port, because other SystemLink hosts on the machine answer discovery
  too (10 repeats clean). CnaNetTests 489/489, CApi_NetSmoke.
- [x] GS-007q `NetworkSession.BytesPerSecondSent`/`Received` and `NetworkGamer.RoundtripTime` (audit
  OPEN 18, 19). XNA fills all three from the kernel's periodic network stats at Update
  (`ProcessUpdateNetworkStats`). CNA reported 0 for both rates on every session type. Round trips
  were measured only on a SystemLink host and never online. Now both transports feed a
  `TrafficRate` from their ENet host's wire totals (`totalSentData`/`totalReceivedData`, protocol
  overhead included; unsigned deltas survive the 2^32 wrap) and publish per-second rates. Both are
  stars, so a gamer's round trip is the direct peer's where there is one: on the host, each
  client's; on a client, the host's. For a gamer on another client, relayed through the host, the
  host's own round trip is added: the host sends `NetworkStatsBroadcast` (0x0C) about once a second
  (SystemLink control channel; online, validated by `validateServiceControlPacket` and accepted only
  from the host machine once welcomed). Local gamers stay at zero. Tests:
  `TrafficRateTest` (2), `HostMeasuresTrafficAndPublishesItsRoundTripsToClientGamers`,
  `ClientAddsTheHostsRoundTripForAGamerOnAnotherClient`,
  `RoundTripsCoverEveryRemoteGamerAndARelayedOneAddsTheHosts` (three-machine engine),
  `OnlineNetworkSessionTest.TrafficAndRoundTripsReachTheSession`, and validator preflight/round-trip
  cases. CnaNetTests 495/495, CApi_NetSmoke, protocol drift check clean.
