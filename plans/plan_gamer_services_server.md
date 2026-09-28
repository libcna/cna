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
