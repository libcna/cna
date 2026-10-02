# CNA GamerServices Phase 3 handoff — GS-AUDIT-P3

2026-10-02. Continued the completed Phase 2 audit; did not restart the inventory, add major features, redesign GamerServices, rewrite persistence, change language bindings, or push.

## Repositories, starting and ending commits

| Repository / branch | Phase 2 starting references | Phase 3 ending production commit |
|---|---|---|
| `/rv/data/development/github.com/libcna/cnawork`, `work` | 33e08899f, 38287637d, **cf9a38b1f3d632961fce6cdcbba8406cd319e965** | **2f95d29d71eb9b05590ee5ed53242483025827e4** |
| `/rv/data/development/github.com/libcna/cna-gamer-services-server`, `feature/gamer-services-server` | f8c1491, d8e4fea, **477fe6926026c0afa067bacbd82724a2b583bee6** | **f627536ff6eb11f6d9232983b3b235665fb4dba4** |

The final native documentation commit contains this handoff. Its exact hash can be obtained without a self-referential hash: `git log -1 --format='%H %s' -- audit/gamer-services/gamer-services-handoff-phase3.md`. The final response reports it. [Repository/file evidence](evidence/phase3/repositories-and-files.json) records the production boundary, not the subsequent handoff commit.

Native commits:

1. **3314ffb05cc2926b283e4b946e85ef95ddd039cd** — serialize offline read-modify-replace and refuse unlocked profile writes.
2. **b05851a47bbce979581d000d66ee38502c72ab6e** — revalidate authorization before serving cached assets.
3. **2f95d29d71eb9b05590ee5ed53242483025827e4** — refuse avatar pack installation when storage locking fails; retain per-file native locking without a global catalog-fetch mutex.
4. Handoff-bearing `docs(GS-AUDIT-P3)` commit — audit, evidence and remaining qualification instructions.

Server commits:

1. **d5eeba01f7b653e5efa4589e704e554157fe5a45** — bound preauthentication address history and avoid refused allocations.
2. **a5252756ff1429328163c1210a9aebe0ddcb3257** — forbid HTTP grant reuse and qualify live privacy transitions.
3. **f627536ff6eb11f6d9232983b3b235665fb4dba4** — progression invariants, concurrent session lifetime and permanent loopback recovery qualification.

Both trees were initially clean. Commits contain only task files. No stubs, missing dependencies, database migrations, public XNA API additions, protocol changes or intentional XNA behavior changes were introduced. Internal locking failure now respects existing failure/fallback semantics; cached access may make a current authorization request and fail after access is revoked, as intended. Callbacks still publish through the existing completion path.

## Runtime and exact final totals

| Suite | Phase 2 | Final Phase 3 | New tests | Remaining skips |
|---|---|---|---|---|
| Native GamerServices | 633 passed | **642 passed, 0 failed** (81 suites, 70.396 s) | **9** | **0** |
| Native Net | 523 passed | **523 passed, 0 failed** (48 suites, 44.792 s) | **0** | **0** |
| Full server CTest | 27 passed / 0 failed / 7 skipped | **33 passed, 0 failed, 7 skipped** (40 registered, 307.23 s) | **6 CTest cases** | **7** |

Builds completed for affected native targets/four real service harnesses and the full server Debug configuration. Native configuration is HEADLESS; every display-capable launch used private Weston/Xwayland. Real loopback TLS, SQLite, WSS relay, two CNA processes/multiple accounts, avatars, invitation, directory/session/game/boards, restart, graceful/crash migration and member addition worked. These are not GPU visual, physical device, LAN or Internet results.

[Machine-readable final counts](evidence/phase3/final-results.json), [full server result](evidence/phase3/server-final.log), [focused server counts](evidence/phase3/server-focused.log). Four new offline race cases repeated ten times: **40 passed, 0 failed** ([output](evidence/phase3/offline-repeat.log)).

**Flaky behavior:** none observed after completed builds. The first full GamerServices run returned 641 pass/1 fail because it overlapped relinking the self-executing avatar process-test binary; child paths could not execute the replaced parent. The entire stable-build rerun passed 642/642 without weakening any test or changing production code for that failure. [Exact orchestration artifact](evidence/phase3/harness-relink-artifact.md). Coordinate build completion before test launches.

## Confirmed defects and minimal fixes

| Finding / status | Reproducer before fix | Root fix and qualification |
|---|---|---|
| GS-AUDIT-001 offline concurrent loss — **FIXED** on tested Linux | four coordinated real-file thread/process cases lose distinct keys/ticks/columns and collide on `.tmp` | scoped stable sibling lock covers full read/modify/checked write/replace; all four pass and 40/40 repeats |
| GS-AUDIT-017 local profile lock fail-open — **FIXED** | lock path directory; old implementation persists unlocked data | shared checked lock; no persisted mutation; existing best-effort fallback retained |
| GS-AUDIT-018 avatar installer lock fail-open — **FIXED** | version lock directory; old implementation downloads/activates despite no lock | fail before fetch/staging; return CatalogInstall::Failed; existing staging survives |
| GS-AUDIT-007 native cached pictures — **FIXED** | cached access survives denial, other backend account and revoked token in three tests | validated one-byte assets.read using current authority before cache return; no denial cache |
| GS-AUDIT-007 reusable HTTP grants — **FIXED** | real TLS binary route returns year-long immutable private caching | retain no-store; fresh profile/picture policy checked for both data routes |
| GS-AUDIT-008 admission history — **FIXED** | direct deterministic insertion exceeds nominal 4,096 cap | global/per-peer refusal precedes allocation; prune expired minute buckets; refuse new keys at cap; 17,027 checks |

Small red/green logs: [offline red](evidence/phase3/offline-before.log), [green](evidence/phase3/offline-after.log); [native cache red](evidence/phase3/cache-before.log), [green](evidence/phase3/cache-after.log); [avatar lock red](evidence/phase3/avatar-lock-before.log), [green](evidence/phase3/avatar-lock-after.log); [admission red](evidence/phase3/admission-before.log); [HTTP red](evidence/phase3/http-cache-before.log), [green](evidence/phase3/http-cache-after.log). The profile lock regression is included in offline logs. Nearby native transport/storage/avatar and server privacy/TLS/relay/service tests passed before full suites.

### New regression/qualification tests

Native tests (exact Google Test names):

- `OfflineConcurrencyTest.AchievementThreadUpdatesKeepEveryKeyAndTimestamp`
- `OfflineConcurrencyTest.LeaderboardThreadUpdatesKeepEveryGamerAndColumns`
- `OfflineConcurrencyTest.AchievementProcessUpdatesKeepEveryKeyAndTimestamp`
- `OfflineConcurrencyTest.LeaderboardProcessUpdatesKeepEveryGamerAndColumns`
- `LocalProfileStoreTest.FailedLockNeverWritesAnUnlockedProfileStore`
- `ServiceRequestRetryTest.CachedPictureRechecksPolicyAndDoesNotCacheDenial`
- `ServiceRequestRetryTest.AnotherBackendCannotUseCachedPictureWithoutCurrentAuthorization`
- `ServiceRequestRetryTest.InvalidatedTokenCannotReadCachedPicture`
- `AvatarServiceTest.AFailedInstallLockCannotActivateOrDeleteStaging`

New server CTest cases:

- `service_admission` — 17,027 checks, exact 59/60-second expiry, eight concurrent inserters, rate/global/per-peer limits, reconnect and balanced release.
- `service_cache_authorization` — real TLS/SQLite/operator transitions, both asset paths, repeated read ID, friends/unfriend/either block, current presence, public avatars and logout; no-store responses.
- `service_progress_trust` — 54 checks: owner/title/key, server timestamps, ignored foreign user/client ticks, replay/duplicates, malformed/extreme score/columns, mixed batch rollback, epoch and unsupported mutation routes.
- `service_concurrency_lifetime` — 22 checks: competing last-slot joins, snapshot/deletion order, nonmigrating host destruction, retained grant invalidation, repeated release, dead session reconnect/leave/invite and dependent row cleanup.
- `service_cna_session_host_crash_loopback` — original public session script `--crash` without namespace isolation, PlayerMatch/Ranked.
- `service_cna_session_add_gamer_loopback` — original script `--add` without isolation; actual additional signed-in member and packets.

Concurrency tests use barriers/start pipes, distinct keys and substantial seeded histories, not sleeps to manufacture races. The offline races are strongly reproducible but do not force a specific internal read boundary; no production test hook was added. Admission uses an ordinary optional monotonic-time input for deterministic expiry. Session mutations retain the existing supported single-Service mutex/SQLite transaction architecture.

## Persistence and lifetime conclusions

Achievement and board stores now lock before read, through modify/serialize/temp flush/close/rename. Acknowledgement follows checked completion. Fixed temp names are safe for cooperating locked writers. Profile/avatar/default metadata uses the same checked mechanism; catalog installs use version-specific locks. Lock files persist; close/process death releases authority. There is no production offline progress delete API; test reset is not a live cross-process deletion protocol.

No additional offline progression read/modify/write file was found in gamer state/presence, credentials or immutable asset caching. Credential storage has exclusive endpoint/title ownership, serialized writes and unique checked replacement; avatar/profile revisions live in their existing store. Asset cache hashes validate immutable content, with opportunistic eviction. [Architecture overlay](gamer-services-architecture.md) records these boundaries and resource structures.

**DEFERRED:** no fsync/power-loss promise; unreadable offline progress still becomes empty and can be replaced by the next save; corrupt profile files are preserved. Windows existing-file replacement, network filesystem locks, external/legacy unlocked writers and externally deleted lock files were not qualified. Offline stream columns/column-only durability are inherited limitations, not repaired by concurrency locking.

Session deletion and joins serialize under Service. Tests disprove overfilling the last slot and continued retained-grant validity after tested destruction; stale identity/machine/session operations fail and dependent tickets/invites/participants disappear. Repeated release is safe. Multiple independent server processes on the same DB are prohibited, not qualified. Exhaustive conflicting social/admin writes and long churn remain outside this bounded phase.

## Trust classification and authorization caches

| Input / derived state | Classification | Assumption / checked boundary |
|---|---|---|
| Own achievement award | SERVER VALIDATED BUT CLIENT AUTHORED | configured title/key and token owner; idempotent; no gameplay evidence |
| Leaderboard score/columns/ranked report | SERVER VALIDATED BUT CLIENT AUTHORED | schema/ranges/participants/epoch/replay; ranked agreement is not gameplay verification |
| Completion time/rank/achievement totals | SERVER VERIFIED relative to stored claims | server clock/sort/aggregate, not earning proof |
| Operator definitions/catalog metadata | SERVER VERIFIED within operator boundary | admin provisioning; player cannot rewrite definitions |
| Profile defaults/zone, reputation reviews | SERVER VALIDATED BUT CLIENT AUTHORED | own preferences or authenticated bounded review; aggregates derived |
| Avatar layout | SERVER VALIDATED BUT CLIENT AUTHORED | own account, IDs/format/revision; public cosmetics, no entitlement/unlock system |
| Incremental progress/reward/challenge/session-result/skill mutation | UNSUPPORTED | no such dispatch endpoint; TrueSkill event is not calculation |

No implemented inspected progression family remained UNKNOWN or entirely unchecked CLIENT TRUSTED. Plausible false gameplay outcomes remain possible. **GS-AUDIT-002 ACCEPTED DESIGN** is justified by the title-agnostic XNA-style API and absence of game logic; it is not a claim that untrusted competitive clients are safe. A title requiring authoritative results needs an explicitly chosen title-specific boundary. Full signed int64 scores, including negatives/extrema, remain valid compatibility inputs; malformed numeric types, overflow and invalid replay/batches are rejected.

Fresh server profile/picture/friends/block/presence authorization does not use an old read result on request-ID reuse. Tests change the **viewer's profileViewing privilege** (everyone → friends → blocked); the server has no owner public/private toggle. Accepted friendship, unfriend, either-direction block, restoration and logout are exercised. Avatar/catalog visibility is deliberately public to authenticated title clients. Hash sharing can give independent public or multiple-owner grants.

Native bytes may remain cached, but current authorization is required to return them. HTTP token-gated responses no longer advertise reusable grants. Denials are not cached; restoration works. Already returned streams and copied GamerProfile/Friend values remain snapshots; OS-readable cached data cannot be retroactively erased. Per-request authorization does not imply revocation of an in-flight response after a later policy change.

**GS-AUDIT-019 DEFERRED MEDIUM, source-qualified:** voice reads a published gamer privilege snapshot and initial/local Guide mute state; renewal refreshes backend slots without republishing privileges to existing gamers. Cross-device/operator block/communication changes have no proven prompt effect on ongoing voice. No end-to-end disclosure was reproduced. Fresh server request authorization is fixed/qualified; synthetic two-client plus third-controller voice qualification is the next focused step.

## Admission/resource bounds

The actual preauthentication structure was Listener Admission's `rates_`. Socket-derived addresses allocate it; history could exceed 4,096 even on refused connections. Extraction into private Admission source/header preserved behavior for the red test; the minimal fix changes insertion/pruning order only. It now refuses unknown keys at the live cap, expires one-minute buckets, lets existing keys reconnect within existing 600/minute rate, and releases active leases correctly. It needs no authentication by design, so this was a confirmed resource defect.

Nearby relay/event queues, channels, ticket/machine/session/invitation/auth maps already have limits, expiry and deletion cleanup; details are in the architecture overlay. No STUN/ICE/NAT map exists. Authenticated download/request-budget cardinality uses provisioned accounts/titles and opportunistic cleanup rather than a proved global cap: **DEFERRED authenticated-load qualification**, without a speculative preauthentication DoS claim or new global rate limiter.

## Seven skips and NAT/device qualification

All seven final skips are **ENVIRONMENT REQUIRED**, still relevant, not obsolete or proven bug-masking. Missing Linux `unshare`/`ip`/`slirp4netns` gate (slirp4netns absent here); namespaces must also be permitted. Exact names:

`service_cna_relay_nat`, `service_cna_owned_enet_nat`, `service_cna_session_nat`, `service_cna_invite_nat`, `service_cna_session_restart_nat`, `service_cna_session_host_crash`, `service_cna_session_add_gamer`.

The [manual qualification file](evidence/phase3/manual-qualification.md) records each name/subsystem/skip condition/reason/relevance/unqualified behavior, commands and assertions. No skipped test was weakened to remove isolation. Crash/add now have additional permanent passing loopback gates.

**TESTED:** local loopback, IPv4 loopback TLS/WSS relay, reconnect/server restart, graceful and abrupt host migration. **IMPLEMENTED BUT UNQUALIFIED:** physical LAN, public Internet relay, separate outbound-only namespace NAT, symmetric NAT relay connectivity. **PARTIAL:** IPv6 service address paths; ENet carrier/LAN paths use IPv4. **UNSUPPORTED:** direct peer Internet session path and STUN/ICE/hole punching. Online relay is the initial path, not fallback from a failed direct negotiation. Code existence never implies Internet qualification.

Client bounded connect/upgrade/hello/frame, recovery and migration timeouts and server shortened disconnect/ordinary leases are documented with source files in manual qualification. Still required: tools-enabled seven NAT gates; two physical LAN hosts with remote-address adapter; two routers/WAN relay including symmetric NAT; IPv6-only endpoint; disruption/reconnect/migration; synthetic cross-device voice policy; consented physical microphone/speaker; Windows/macOS locking/replacement/failure tests. None was run against the owner's live desktop.

## Exact reproduction commands

Run from cnawork. Reuse the Phase 2 Debug HEADLESS configuration and available dependencies. Finish both builds before starting tests; ccache uses writable temporary storage.

```sh
CCACHE_DIR=/tmp/gs-phase3-ccache cmake --build cmake-build-debug --target \
  CnaGamerServicesTests CnaNetTests cna_service_session_client_harness \
  cna_service_directory_client_harness cna_service_relay_client_harness \
  cna_service_avatar_client_harness -j4
CCACHE_DIR=/tmp/gs-phase3-ccache cmake --build ../cna-gamer-services-server/build -j2

tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-phase3-gamer-state XDG_CONFIG_HOME=/tmp/gs-phase3-gamer-state \
  XDG_CACHE_HOME=/tmp/gs-phase3-gamer-state CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaGamerServicesTests --gtest_output=xml:/tmp/gs-phase3-gamer.xml

tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-phase3-net-state XDG_CONFIG_HOME=/tmp/gs-phase3-net-state \
  XDG_CACHE_HOME=/tmp/gs-phase3-net-state CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaNetTests --gtest_output=xml:/tmp/gs-phase3-net.xml

export CNA_SERVICE_SESSION_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_session_client_harness"
export CNA_SERVICE_DIRECTORY_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_directory_client_harness"
export CNA_SERVICE_RELAY_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_relay_client_harness"
export CNA_SERVICE_AVATAR_CLIENT_HARNESS="$PWD/cmake-build-debug/cna_service_avatar_client_harness"
export CNA_AVATAR_CATALOGS="$PWD/modules/gamer-services/assets/avatars"
tools/platform/run_gpu_tests_private.sh --exec \
  ctest --test-dir ../cna-gamer-services-server/build --output-on-failure -j2
```

Focused reproductions on the final tree (all pass):

```sh
tools/platform/run_gpu_tests_private.sh --exec env \
  XDG_DATA_HOME=/tmp/gs-phase3-focus XDG_CONFIG_HOME=/tmp/gs-phase3-focus \
  XDG_CACHE_HOME=/tmp/gs-phase3-focus CNA_GAMER_SERVICES_KEYRING=0 \
  cmake-build-debug/CnaGamerServicesTests \
  '--gtest_filter=OfflineConcurrencyTest.*:LocalProfileStoreTest.FailedLockNeverWritesAnUnlockedProfileStore:ServiceRequestRetryTest.*:AvatarServiceTest.AFailedInstallLockCannotActivateOrDeleteStaging'
# Same launch/env; use these flags for the repeated race qualification:
# --gtest_filter=OfflineConcurrencyTest.* --gtest_repeat=10

tools/platform/run_gpu_tests_private.sh --exec \
  ctest --test-dir ../cna-gamer-services-server/build --output-on-failure \
  -R '^service_(admission|cache_authorization|progress_trust|concurrency_lifetime|cna_session_host_crash_loopback|cna_session_add_gamer_loopback)$' -j2
```

For historical red results, use disposable checkouts: first native concurrency/profile tests added to cf9a38b1f before 3314ffb05; cache tests before b05851a4; installer test before 2f95d29d. Server Admission extraction/test before cap fix; cache e2e before a5252756 header removal. Keep qualified evidence logs instead of rewriting production backwards in the working checkouts.

## Files changed

Native production: `modules/gamer-services/include/CNA/Internal/GamerServices/LocalStoreLock.hpp`; `modules/gamer-services/src/Internal/{LocalStoreLock,LocalGamerServicesStore,LocalProfiles,GamerServicesBackend}.cpp`; `modules/gamer-services/src/Internal/Avatars/AvatarCatalogStore.cpp`.

Native tests: `modules/gamer-services/tests/Microsoft/Xna/Framework/GamerServices/{GamerServicesGamerTests,LocalProfileTests,ServiceRequestRetryTests,ServiceAvatarTests}.cpp`.

Server: `CMakeLists.txt`; `src/{Admission.hpp,Admission.cpp,Listener.cpp}`; `tests/{AdmissionTests.cpp,ProgressTrustTests.cpp,ConcurrencyLifetimeTests.cpp,cache_authorization_e2e.py}`.

Audit: existing `gamer-services-{audit,issues,compatibility-matrix,architecture}.md`; this Phase 3 handoff; focused small logs/JSON/manual instructions under `audit/gamer-services/evidence/phase3/`. No binaries/build trees or read-only binding-analysis documents changed. [Exact production file list](evidence/phase3/repositories-and-files.json).

## Remaining risk / recommended Phase 4

No newly confirmed HIGH runtime defect remains open. Inherited HIGH compatibility limitations (browser transport, partner/commerce/TrueSkill and Recent semantics) remain, outside this phase. The highest unresolved integrity boundary is title-managed progress if an owner intends untrusted competitive authority; this is a product/trust decision, not an assertion that every XNA title needs anti-cheat.

| Priority | Area | Risk | Evidence | Next action |
|---|---|---|---|---|
| HIGH, conditional on competitive deployment | Progress authority | authenticated client can assert plausible unearned outcome | trust table / 54 service checks; no game-specific logic | choose/document title authority; authorize title-specific verification only if needed |
| MEDIUM | Voice policy freshness | external block/privilege change may leave an ongoing snapshot stale | renewal/VoiceMutes/VoiceChat source trace; no E2E disclosure reproduced | synthetic two clients + third controller, then minimal propagation fix if confirmed |
| MEDIUM | Offline persistence | corrupt-read-as-empty, no fsync, Windows replacement and inherited stream/column durability | write source/tests; concurrency fixed, platforms unrun | decide corruption/durability contract; qualify Windows/macOS and failure semantics |
| MEDIUM / environment | NAT/device compatibility | LAN/WAN/symmetric NAT/IPv6/audio not certified | seven explicit tool skips; loopback passing only | execute manual qualification on suitable hosts/devices |
| MEDIUM, inherited | SendDataOptions Chat flags | combined flag/separate ordering semantics not established | Phase 2 reference/source evidence; basic options pass | focused differential reference test before any codec change |

Additional authenticated cardinality and long session/social churn qualification is useful if deployment scale requires it; no new broad refactor or speculative limiter is recommended. Finish the concrete voice/storage/network qualifications before expanding features. Preserve the exact native/server baseline and per-fix red→green policy.
